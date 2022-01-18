/*
 * --- Revised 3-Clause BSD License ---
 * Copyright Semtech Corporation 2022. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice,
 *       this list of conditions and the following disclaimer in the documentation
 *       and/or other materials provided with the distribution.
 *     * Neither the name of the Semtech corporation nor the names of its
 *       contributors may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SEMTECH CORPORATION. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "s2conf.h"
#include "sys.h"
#include "uj.h"
#include "kwcrc.h"
#include "s2e.h"
#include "tc.h"


tc_t* TC;
static s1_t tstateLast;


static void tc_done (tc_t* tc, s1_t tstate) {
    tc->tstate = tstate;
    ws_free(&tc->ws);
    rt_yieldTo(&tc->timeout, tc->ondone);
    sys_inState(SYSIS_TC_DISCONNECTED);
}


static void tc_timeout (tmr_t* tmr) {
    tc_t* tc = timeout2tc(tmr);
    LOG(MOD_TCE|ERROR, "TC engine timed out");
    tc_done(tc, TC_ERR_TIMEOUT);
}


static void tc_muxs_connection (conn_t* _conn, int ev) {
    tc_t* tc = conn2tc(_conn);
    if( ev == WSEV_CONNECTED ) {
        rt_clrTimer(&tc->timeout);
        tc->tstate = TC_MUXS_CONNECTED;
        LOG(MOD_TCE|VERBOSE, "Connected to MUXS.");
        dbuf_t b = ws_getSendbuf(&tc->ws, MIN_UPJSON_SIZE);
        assert(b.buf != NULL);   // this should not fail on a fresh connection
        uj_encOpen(&b, '{');
        uj_encKV(&b, "msgtype",  's', "version");
        uj_encKV(&b, "station",  's', CFG_version);
        uj_encKV(&b, "firmware", 's', sys_version());
        uj_encKV(&b, "package",  's', sys_version());
        // uj_encKV(&b, "os",       's', sys_osversion()); 
        uj_encKV(&b, "model",    's', CFG_platform);
        uj_encKV(&b, "protocol", 'i', MUXS_PROTOCOL_VERSION);
        uj_encKV(&b, "features", 's', rt_features());
        uj_encClose(&b, '}');
        ws_sendText(&tc->ws, &b);
        if( tc->credset == SYS_CRED_REG )
            sys_backupConfig(SYS_CRED_TC);
        // Put CUPS to long back-off
        sys_delayCUPS();
        return;
    }
    if( ev == WSEV_DATASENT ) {
        s2e_flushRxjobs(&tc->s2ctx);   // send off more pending rxjobs
        return;
    }
    if( ev == WSEV_TEXTRCVD ) {
        dbuf_t b = ws_getRecvbuf(&tc->ws);
        if( !s2e_onMsg(&tc->s2ctx, b.buf, b.bufsize) ) {
            LOG(ERROR, "Closing connection to muxs - error in s2e_onMsg");
            tc->tstate = TC_ERR_FAILED;
            ws_close(&tc->ws, 1000);
        }
        return;
    }
    if( ev == WSEV_BINARYRCVD ) {
        dbuf_t b = ws_getRecvbuf(&tc->ws);
        if( !s2e_onBinary(&tc->s2ctx, (u1_t*)b.buf, b.bufsize) ) {
            LOG(ERROR, "Closing connection to muxs - error in s2e_onBinary");
            tc->tstate = TC_ERR_FAILED;
            ws_close(&tc->ws, 1000);
        }
        return;
    }
    if( ev == WSEV_CLOSED ) {
        s1_t tstate = tc->tstate;
        LOG(MOD_TCE|VERBOSE, "Connection to MUXS closed in state %d", tstate);
        if( tstate >= 0 ) {
            // Quickly reopen muxs connection if just close else go thru infos
            tstate = tstate == TC_MUXS_CONNECTED ? TC_ERR_CLOSED : TC_ERR_FAILED;
        }
        tc_done(tc, tstate);
        return;
    }
    LOG(MOD_TCE|INFO, "tc_muxs_connection - Unknown event: %d", ev);
}


static void tc_connect_muxs (tc_t* tc) {
    char* u = tc->muxsuri;
    int   tlsmode  = u[0];
    char* hostname = u+3;
    char* port     = &u[(u1_t)u[1]];
    char* path     = &u[(u1_t)u[2]];

    ws_ini(&tc->ws, TC_RECV_BUFFER_SIZE, TC_SEND_BUFFER_SIZE);
    if( tlsmode == URI_TLS && !conn_setup_tls(&tc->ws, SYS_CRED_TC, SYS_CRED_REG, hostname) ) {
        goto errexit;
    }
    LOG(MOD_TCE|VERBOSE, "Connecting to MUXS...");
    log_flushIO();
    if( !ws_connect(&tc->ws, hostname, port, path) ) {
        LOG(MOD_TCE|ERROR, "Muxs connect failed - URI: ws%s://%s:%s%s", tlsmode==URI_TLS?"s":"", hostname, port, path);
        goto errexit;
    }
    rt_setTimerCb(&tc->timeout, rt_micros_ahead(TC_TIMEOUT), tc_timeout);
    tc->ws.evcb = (evcb_t)tc_muxs_connection;
    tc->tstate = TC_MUXS_REQ_PEND;
    return;

 errexit:
    tc_done(tc, TC_ERR_FAILED);
    return;
}


static void tc_info_request (conn_t* _conn, int ev) {
    tc_t* tc = conn2tc(_conn);
    if( ev == WSEV_CONNECTED ) {
        dbuf_t b = ws_getSendbuf(&tc->ws, MIN_UPJSON_SIZE);
        assert(b.buf != NULL);   // this should not fail on a fresh connection
        uj_encOpen(&b, '{');
        uj_encKV(&b, "router", '6', sys_eui());
        uj_encClose(&b, '}');
        ws_sendText(&tc->ws, &b);
        return;
    }
    if( ev == WSEV_DATASENT ) {
        return; // we're not interested in this event
    }
    if( ev == WSEV_BINARYRCVD ) {
        LOG(MOD_TCE|ERROR, "Binary data from 'infos' - ignored");
        return;
    }
    if( ev == WSEV_TEXTRCVD ) {
        s1_t err = TC_ERR_FAILED;
        dbuf_t b = ws_getRecvbuf(&tc->ws);
        ujdec_t D;
        uj_iniDecoder(&D, b.buf, b.bufsize);
        if( uj_decode(&D) ) {
            LOG(MOD_TCE|ERROR, "Parsing of INFOS response failed");
            err = TC_ERR_FAILED;
            goto failed;
        }
        uj_nextValue(&D);
        uj_enterObject(&D);
        ujcrc_t field;
        char* router  = NULL;
        char* muxsid  = NULL;
        char* muxsuri = NULL;
        char* error   = NULL;
        while( (field = uj_nextField(&D)) ) {
            switch(field) {
            case J_router: { router  = uj_str(&D); break; }
            case J_muxs  : { muxsid  = uj_str(&D); break; }
            case J_error : { error   = uj_str(&D); break; }
            case J_uri   : { muxsuri = uj_str(&D);
                if( !uri_isScheme(muxsuri,"ws") && !uri_isScheme(muxsuri,"wss") ) {
                    LOG(MOD_TCE|ERROR, "Muxs URI must be ws://.. or wss://..: %s", muxsuri);
                    goto failed;
                }
                if( D.str.len+1 > MAX_URI_LEN ) {
                    LOG(MOD_TCE|ERROR, "Muxs URI too long (max %d): %s", MAX_URI_LEN, muxsuri);
                    goto failed;
                }
                struct uri_info ui;
                dbuf_t uri = { .buf=D.str.beg, .bufsize=D.str.len, .pos=0 };
                if( !uri_parse(&uri, &ui, 0) || ui.portBeg==ui.portEnd || ui.pathBeg==ui.pathEnd ) {
                    LOG(MOD_TCE|ERROR, "Illegal muxs URI (no port/path etc.): %s", muxsuri);
                    goto failed;
                }
                memset(tc->muxsuri, 0, sizeof(tc->muxsuri));
                u1_t portoff = ui.hostEnd - ui.hostBeg + 4;
                u1_t pathoff = portoff + ui.portEnd - ui.portBeg + 1;
                tc->muxsuri[0] = muxsuri[2]=='s' ? URI_TLS : URI_TCP;
                tc->muxsuri[1] = portoff;
                tc->muxsuri[2] = pathoff;
                memcpy(&tc->muxsuri[3],       &muxsuri[ui.hostBeg], ui.hostEnd - ui.hostBeg);
                memcpy(&tc->muxsuri[portoff], &muxsuri[ui.portBeg], ui.portEnd - ui.portBeg);
                memcpy(&tc->muxsuri[pathoff], &muxsuri[ui.pathBeg], ui.pathEnd - ui.pathBeg);
                break;
            }
            default: {
                LOG(MOD_TCE|WARNING, "Unknown field in infos response - ignored: %s", D.field.name);
                uj_skipValue(&D);
                break;
            }
            }
        }
        uj_exitObject(&D);
        uj_assertEOF(&D);
        if( error || muxsuri == NULL ) {
            LOG(MOD_TCE|ERROR, "Infos error: %s %s", router, error);
            err = TC_ERR_REJECTED;
            goto failed;
        }
        LOG(MOD_TCE|INFO, "Infos: %s %s %s", router, muxsid, muxsuri);
        err = TC_INFOS_GOT_URI;
      failed:
        tc->tstate = err;
        ws_close(&tc->ws, 1000);
        return;
    }
    if( ev == WSEV_CLOSED ) {
        s1_t tstate = tc->tstate;
        if( tstate >= 0 && tstate != TC_INFOS_GOT_URI )
            tstate = TC_ERR_CLOSED; // unexpected close
        if( tstate != TC_INFOS_GOT_URI ) {
            tc_done(tc, tstate);
            return;
        }
        ws_free(&tc->ws);
        tc_connect_muxs(tc);
        return;
    }
    LOG(MOD_TCE|INFO, "tc_info_request - Unknown event: %d", ev);
}


static dbuf_t tc_getSendbuf (s2ctx_t* s2ctx, int minsize) {
    tc_t* tc = s2ctx2tc(s2ctx);
    if( tc->tstate != TC_MUXS_CONNECTED ) {
        // LOG(MOD_TCE|WARNING, "Dropping WS frame - not connected to MUXS");
        dbuf_t b = { .buf=NULL, .bufsize=0, .pos=0 };
        return b;
    }
    return ws_getSendbuf(&tc->ws, minsize);
}


static void tc_sendText (s2ctx_t* s2ctx, dbuf_t* buf) {
    tc_t* tc = s2ctx2tc(s2ctx);
    ws_sendText(&tc->ws, buf);
}


static void tc_sendBinary (s2ctx_t* s2ctx, dbuf_t* buf) {
    tc_t* tc = s2ctx2tc(s2ctx);
    ws_sendBinary(&tc->ws, buf);
}


void tc_ondone_default (tmr_t* timeout) {
    tc_continue(timeout2tc(timeout));
}

tc_t* tc_ini (tmrcb_t ondone) {
    assert(TC_RECV_BUFFER_SIZE > MAX_HOSTNAME_LEN + MAX_PORT_LEN + 2);
    tc_t* tc = rt_malloc(tc_t);
    ws_ini(&tc->ws, TC_RECV_BUFFER_SIZE, TC_SEND_BUFFER_SIZE);
    rt_iniTimer(&tc->timeout, tc_timeout);
    tc->tstate = TC_INI;
    tc->credset = SYS_CRED_REG;
    tc->ondone = ondone==NULL ? tc_ondone_default : ondone;
    tc->muxsuri[0] = URI_BAD;
    s2e_ini(&tc->s2ctx);
    tc->s2ctx.getSendbuf = tc_getSendbuf;
    tc->s2ctx.sendText   = tc_sendText;
    tc->s2ctx.sendBinary = tc_sendBinary;
    return tc;
}


void tc_free (tc_t* tc) {
    if( tc == NULL )
        return;
    ws_free(&tc->ws);
    rt_clrTimer(&tc->timeout);
    tstateLast = tc->tstate;
    tc->tstate = TC_ERR_DEAD;
    s2e_free(&tc->s2ctx);
    rt_free(tc);
}


void tc_start (tc_t* tc) {
    assert(tc->tstate == TC_INI);
    int tstate_err = TC_ERR_NOURI;

    str_t tcuri = sys_uri(SYS_CRED_TC, tc->credset);
    if( tcuri == NULL ) {
        LOG(MOD_TCE|ERROR, "No TC URI configured");
        goto errexit;
    }
    // Use a WS buffer as temp place for host/port strings
    // Gets destroyed while ramping up connection
    char hostname[MAX_HOSTNAME_LEN];
    char port[MAX_PORT_LEN];
    int   ok;
    if( (ok = uri_checkHostPortUri(tcuri, "ws", hostname, MAX_HOSTNAME_LEN, port, MAX_PORT_LEN)) == URI_BAD ) {
        LOG(MOD_TCE|ERROR,"Bad TC URI: %s", tc);
        goto errexit;
    }
    if( ok == URI_TLS && !conn_setup_tls(&tc->ws, SYS_CRED_TC, tc->credset, hostname) ) {
        goto errexit;
    }
    tstate_err = TC_ERR_FAILED;
    LOG(MOD_TCE|INFO, "Connecting to INFOS: %s", tcuri);
    log_flushIO();
    if( !ws_connect(&tc->ws, hostname, port, "/router-info") ) {
        LOG(MOD_TCE|ERROR, "TC connect failed - URI: %s", tcuri);
        goto errexit;
    }
    rt_setTimerCb(&tc->timeout, rt_seconds_ahead(TC_TIMEOUT), tc_timeout);
    tc->ws.evcb = (evcb_t)tc_info_request;
    tc->tstate = TC_INFOS_REQ_PEND;
    return;
 errexit:
    tc_done(tc, tstate_err);
    return;
}


void tc_continue (tc_t* tc) {
    s1_t tstate = tc->tstate;

    if( (tstate == TC_ERR_REJECTED || tstate == TC_ERR_NOURI || tc->retries >= 10) && !sys_noCUPS ) {
        LOG(MOD_TCE|INFO, "Router rejected or retry limit reached. Invoking CUPS.");
        sys_stopTC();
        sys_triggerCUPS(-1);
        return;
    }

    if( tstate == TC_INFOS_BACKOFF ) {
        int retries_old = tc->retries;
        tmrcb_t ondone = tc->ondone;
        assert(TC == tc);
        tc_free(tc);
        TC = tc = tc_ini(ondone);
        tc_start(tc);
        tc->retries = retries_old + 1;
        return;
    }
    if( tstate == TC_MUXS_BACKOFF ) {
        tc->retries += 1;
        tc_connect_muxs(tc);
        return;
    }

    if( tc->muxsuri[0] != URI_BAD ) {
        // We have a muxs uri
        if( tc->retries <= 4 && tstate == TC_ERR_CLOSED ) {
            // Try to reconnect with increasing backoff
            int backoff = 1 << tc->retries;
            tc->tstate = TC_MUXS_BACKOFF;
            rt_setTimerCb(&tc->timeout, rt_seconds_ahead(backoff), tc->ondone);
            LOG(MOD_TCE|INFO, "MUXS reconnect backoff %ds (retry %d)", backoff, tc->retries);
            return;
        }
        tc->muxsuri[0] = URI_BAD;
        tc->retries = 1;
    }

    int backoff = min(tc->retries, 6);
    tc->tstate = TC_INFOS_BACKOFF;
    rt_setTimerCb(&tc->timeout, rt_seconds_ahead(backoff * 10), tc->ondone);
    LOG(MOD_TCE|INFO, "INFOS reconnect backoff %ds (retry %d)", backoff*10, tc->retries);
}


void sys_stopTC () {
    if( TC != NULL ) {
        LOG(MOD_TCE|INFO, "Terminating TC engine");
        tc_free(TC);
        TC = NULL;
        sys_inState(SYSIS_TC_DISCONNECTED);
    }
}


void sys_startTC () {
    if( TC != NULL || sys_noTC )
        return;  // already running
    LOG(MOD_TCE|INFO, "Starting TC engine");
    TC = tc_ini(NULL);
    tc_start(TC);
    sys_inState(SYSIS_TC_DISCONNECTED);
}


void sys_iniTC () {
}

s1_t sys_statusTC () {
    return TC ? TC->tstate : tstateLast;
}
