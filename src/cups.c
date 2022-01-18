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
#include "cups.h"
#include "tc.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/error.h"
#include "mbedtls/sha512.h"
#include "mbedtls/bignum.h"


#define FAIL_CNT_THRES 6
#define SIGCRC_LEN 4

static tmr_t   cups_sync_tmr;
static cups_t* CUPS;
static int     cups_credset = SYS_CRED_REG;
static int     cups_failCnt;
static s1_t    cstateLast;

struct cups_sig {
    mbedtls_sha512_context sha;
    u1_t     signature[128];
    u1_t     hash[64];
    u1_t     len;
    union {
        u4_t keycrc;
        u1_t keycrcb[4];
    };
};

static int cups_verifySig (cups_sig_t* sig) {
    int verified = 0;
    dbuf_t key;
    int keyid = -1;
    while ( (key = sys_sigKey(++keyid)).buf != NULL && !verified ) {
        if ( key.bufsize != 64 )
            continue;

        mbedtls_ecp_keypair k;
        mbedtls_ecp_keypair_init(&k);
        mbedtls_ecdsa_context ecdsa;
        mbedtls_ecdsa_init(&ecdsa);
        int ret;
        if ((ret = mbedtls_ecp_group_load        (&k.grp, MBEDTLS_ECP_DP_SECP256R1) ) ||
            (ret = mbedtls_mpi_read_binary       (&k.Q.X, (u1_t*)key.buf, 32)       ) ||
            (ret = mbedtls_mpi_read_binary       (&k.Q.Y, (u1_t*)key.buf+32, 32)    ) ||
            (ret = mbedtls_mpi_lset              (&k.Q.Z, 1)                        ) ||
            (ret = mbedtls_ecp_check_pubkey      (&k.grp, &k.Q)                     ) ||
            (ret = mbedtls_ecdsa_from_keypair    (&ecdsa, &k)                       ) ||
            (ret = mbedtls_ecdsa_read_signature  (&ecdsa, sig->hash, sizeof(sig->hash), sig->signature, sig->len ))
         ) {
            verified = 0;
        } else {
            verified = 1;
        }
        mbedtls_ecp_keypair_free(&k);
        mbedtls_ecdsa_free(&ecdsa);

        LOG(MOD_CUP|INFO, "ECDSA key#%d -> %s", keyid, verified? "VERIFIED" : "NOT verified");
    }
    sys_sigKey(-1); // Release memory
    if (!verified) {
        LOG(MOD_CUP|WARNING, "No key could verify signature. Tried %d keys", keyid);
    }
    return verified;
}

static void cups_ondone (tmr_t* tmr) {
    if( CUPS == NULL ) {
        sys_triggerCUPS(0);
        return;
    }

    str_t msg="", detail="";
    ustime_t ahead = CUPS_RESYNC_INTV;
    u1_t log = 1;

    if( CUPS->cstate != CUPS_DONE ) {
        // Someting went wrong - retry again more quickly
        msg = "Interaction with CUPS failed%s - retrying in %~T";
        if( cups_failCnt > FAIL_CNT_THRES ||
            CUPS->cstate == CUPS_ERR_REJECTED ||
            CUPS->cstate == CUPS_ERR_NOURI ) {
            // Rotate and try REG/BAK/BOOT sets of config files
            cups_credset = (cups_credset+1) % (SYS_CRED_BOOT+1);
        }
        cups_failCnt += 1;
        if (CUPS->cstate == CUPS_ERR_NOURI)
            log = 0; // already logged
    } else {
        // Successful interaction with CUPS
        u1_t uflags = CUPS->uflags;
        if( uflags & UPDATE_FLAG(UPDATE) ) {
            LOG(MOD_CUP|INFO, "CUPS provided update.bin");
            u1_t run_update = 0;
            if( (uflags & UPDATE_FLAG(SIGNATURE)) ) {
                LOG(MOD_CUP|INFO, "CUPS provided signature len=%d keycrc=%08X", CUPS->sig->len, CUPS->sig->keycrc);
                assert( CUPS->sig );
                mbedtls_sha512_finish( &CUPS->sig->sha, CUPS->sig->hash );
                mbedtls_sha512_free  ( &CUPS->sig->sha );
                run_update = cups_verifySig(CUPS->sig);
            } else {
                dbuf_t key = sys_sigKey(0);
                if ( key.buf == NULL ) {
                    LOG(MOD_CUP|INFO, "No Key. No Sig. UPDATE.");
                    run_update = 1;
                } else {
                    LOG(MOD_CUP|ERROR, "Keyfile present, but no signature provided. Aborting update.");
                    sys_sigKey(-1);
                }
            }
            if (run_update) {
                LOG(MOD_CUP|INFO, "Running update.bin as background process");
                sys_runUpdate();
            } else {
                LOG(MOD_CUP|INFO, "Aborting update.");
                sys_abortUpdate();
            }
        }
        if( uflags & (UPDATE_FLAG(TC_URI)|UPDATE_FLAG(TC_CRED)) ) {
            // Any change here - restart TC connection
            str_t s = ((uflags & UPDATE_FLAG(TC_URI )) == UPDATE_FLAG(TC_URI ) ? "uri" :
                       (uflags & UPDATE_FLAG(TC_CRED)) == UPDATE_FLAG(TC_CRED) ? "credentials" :
                       "uri/credentials");
            LOG(MOD_CUP|INFO, "CUPS provided TC updates (%s) %s", s, sys_noTC ? "" : "- restarting TC engine");
            sys_stopTC();
        }
        if( uflags & (UPDATE_FLAG(CUPS_URI)|UPDATE_FLAG(CUPS_CRED)) ) {
            // Any change here - contact new CUPS immediately
            detail = ((uflags & UPDATE_FLAG(CUPS_URI )) == UPDATE_FLAG(CUPS_URI ) ? "uri" :
                      (uflags & UPDATE_FLAG(CUPS_CRED)) == UPDATE_FLAG(CUPS_CRED) ? "credentials" :
                      "uri/credentials");
            msg = "CUPS provided CUPS updates (%s) - reconnecting in %~T";
            // Reconnect right away - using new settings
        } else {
            detail = uflags ? "" : " (no updates)";
            msg = "Interaction with CUPS done%s - next regular check in %~T";
            ahead = CUPS_OKSYNC_INTV;
        }
        cups_credset = SYS_CRED_REG;
        cups_failCnt = 0;
    }
    if( TC && sys_statusTC() == TC_MUXS_CONNECTED )
        ahead = CUPS_OKSYNC_INTV;
    cups_free(CUPS);
    CUPS = NULL;
    if (log)
        LOG(MOD_CUP|INFO, msg, detail, ahead);
    sys_startTC();
    rt_setTimer(&cups_sync_tmr, rt_micros_ahead(ahead));
}


static void cups_done (cups_t* cups, s1_t cstate) {
    cups->cstate = cstate;
    http_free(&cups->hc);
    rt_yieldTo(&cups->timeout, cups_ondone);
    sys_inState(SYSIS_CUPS_DONE);
}


static void cups_timeout (tmr_t* tmr) {
    cups_t* cups = timeout2cups(tmr);
    LOG(MOD_CUP|ERROR, "CUPS timed out");
    cups_done(cups, CUPS_ERR_TIMEOUT);
}


static int sizelen (int cstate) {
    assert(cstate >= CUPS_FEED_CUPS_URI && cstate <= CUPS_FEED_UPDATE);
    return 1<<((cstate - CUPS_FEED_CUPS_URI)>>1);  // 1,2,4 for URI,CRED,SIG/UPDATE
}


static void cups_update_info (conn_t* _conn, int ev) {
    cups_t* cups = conn2cups(_conn);

    if( ev == HTTPEV_CONNECTED ) {
        dbuf_t cupsuri;
        dbuf_str(cupsuri, sys_uri(SYS_CRED_CUPS, cups_credset));
        LOG(MOD_CUP|VERBOSE, "Retrieving update-info from CUPS%s %s...", sys_credset2str(cups_credset), cupsuri.buf);
        struct uri_info cui;
        uri_parse(&cupsuri, &cui, 0); // Does not write
        dbuf_t b = http_getReqbuf(&cups->hc);
        xprintf(&b,
                "POST /update-info HTTP/1.1\r\n"
                "Host: %*s\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: 00000\r\n"
                "%s\r\n",
                cui.hostportEnd-cui.hostportBeg, cupsuri.buf+cui.hostportBeg,
                cups->hc.c.authtoken ? cups->hc.c.authtoken : "");
        doff_t bodybeg = b.pos;
        xputs(&b, "{", -1);  // note: uj_encOpen would create a comma since b.pos>0
        str_t version = sys_version();
        uj_encKVn(&b,
                  "router",     '6', sys_eui(),
                  "cupsUri",    's', sys_uri(SYS_CRED_CUPS, SYS_CRED_REG),
                  "tcUri",      's', sys_uri(SYS_CRED_TC, SYS_CRED_REG),
                  "cupsCredCrc",'u', sys_crcCred(SYS_CRED_CUPS, SYS_CRED_REG),
                  "tcCredCrc",  'u', sys_crcCred(SYS_CRED_TC, SYS_CRED_REG),
                  "station",    's', CFG_version " " CFG_bdate,
                  "model",      's', CFG_platform,
                  "package",    's', version,
                  // "os",         's', sys_osversion(), 
                  NULL);
        uj_encKey  (&b, "keys");
        uj_encOpen (&b, '[');
        int keyid = -1;
        u4_t crc = 0;
        while( (crc = sys_crcSigkey(++keyid)) > 0 ) {
            uj_encUint(&b, (uL_t)crc);
        }
        uj_encClose(&b, ']');
        uj_encClose(&b, '}');
        http_setContentLength(b.buf, b.pos-bodybeg);
        LOG(MOD_CUP|DEBUG, "CUPS Request: %.*s", b.pos-bodybeg, b.buf+bodybeg);
        http_request(&cups->hc, &b);
        return;
    }
    if( ev == HTTPEV_RESPONSE ) {
        dbuf_t body = http_getBody(&cups->hc);
        s1_t cstate = cups->cstate;

        if( cups->cstate == CUPS_HTTP_REQ_PEND ) {
            // Hdr is only present in very first chunk
            dbuf_t hdr = http_getHdr(&cups->hc);
            int status = http_getStatus(&cups->hc);
            if( status != 200 ) {
                dbuf_t msg = http_statusText(&hdr);
                LOG(MOD_CUP|VERBOSE, "Failed to retrieve TCURI from CUPS: (%d) %.*s", status, msg.bufsize, msg.buf);
                cups->cstate = CUPS_ERR_REJECTED;
                http_close(&cups->hc);
                return;
            }
            if( cups_credset == SYS_CRED_REG )
                sys_backupConfig(SYS_CRED_CUPS);

            // We expect both CUPS/TC URI segments to fit into HTTP buffer
            u1_t cupsuri_len = body.buf[0];
            u1_t tcuri_len = body.buf[1+cupsuri_len];
            body.pos = 2+cupsuri_len+tcuri_len;  // after both URI segments
            if( body.bufsize < 2 || 1+body.pos > body.bufsize ) { // need one more byte for \0
                LOG(MOD_CUP|ERROR, "Malformed CUPS response: URI segments lengths (%u) exceed available data (%u)", body.pos, body.bufsize);
                goto proto_err;
            }
            sys_resetConfigUpdate();
            if( cupsuri_len ) {
                str_t uri = (str_t)body.buf+1;
                body.buf[1+cupsuri_len] = 0;
                sys_saveUri(SYS_CRED_CUPS, uri);
                LOG(MOD_CUP|INFO, "[Segment] CUPS URI: %s", uri);
                cups->uflags |= UPDATE_FLAG(CUPS_URI);
            }
            if( tcuri_len ) {
                char* uri = (char*)body.buf+2+cupsuri_len;
                char save = uri[tcuri_len];
                uri[tcuri_len] = 0;
                sys_saveUri(SYS_CRED_TC, uri);
                LOG(MOD_CUP|INFO, "[Segment] TC URI: %s", uri);
                uri[tcuri_len] = save;
                cups->uflags |= UPDATE_FLAG(TC_URI);
            }
            cups->cstate = cstate = CUPS_FEED_CUPS_CRED;
            cups->temp_n = 0;
        }
        assert(cstate > CUPS_HTTP_REQ_PEND || cstate < CUPS_DONE);
        // Rewind timeout every time we get some data
        rt_setTimer(&cups->timeout, rt_micros_ahead(CUPS_CONN_TIMEOUT));
        int segm_len = cups->segm_len;

        while( cups->temp_n < 4 ) {
            // Assemble length of a section in temp
            if( body.pos >= body.bufsize ) {
                // Not enough data in body buffer
              get_more:
                if( !http_getMore(&cups->hc) ) {
                    LOG(MOD_CUP|ERROR, "Unexpected end of data");
                  proto_err:
                    LOG(MOD_CUP|ERROR, "CUPS Protocol error. Closing connection.");
                    cups_done(cups, CUPS_ERR_FAILED);
                }
                return; // waiting for next chunk
            }
            cups->temp[cups->temp_n++] = body.buf[body.pos++];
            if( cups->temp_n == sizelen(cstate) ) {
                if( (segm_len = rt_rlsbf4(cups->temp)) == 0 ) {
                  next_cstate:
                    if( (cups->cstate = cstate += 1) == CUPS_DONE ) {
                        sys_commitConfigUpdate();
                        http_close(&cups->hc);
                        return;
                    }
                    cups->temp_n = 0;
                    continue;
                }
                if( segm_len < 0 ) {
                    LOG(MOD_CUP|ERROR, "Segment %d length not allowed (must be <2GB): 0x%08x bytes", cstate-CUPS_FEED_CUPS_URI, segm_len);
                    goto proto_err;
                }
                cups->segm_off = 0;
                cups->segm_len = segm_len;
                cups->temp_n = 4;
                memset(&cups->temp, 0, sizeof(cups->temp));
                if( cstate == CUPS_FEED_CUPS_CRED ) {
                    sys_credStart(SYS_CRED_CUPS, segm_len);
                    cups->uflags |= UPDATE_FLAG(CUPS_CRED);
                    LOG(MOD_CUP|INFO, "[Segment] CUPS Credentials (%d bytes)", segm_len);
                }
                else if( cstate == CUPS_FEED_TC_CRED ) {
                    sys_credStart(SYS_CRED_TC, segm_len);
                    cups->uflags |= UPDATE_FLAG(TC_CRED);
                    LOG(MOD_CUP|INFO, "[Segment] TC Credentials (%d bytes)", segm_len);
                } else if( cstate == CUPS_FEED_SIGNATURE ) {
                    LOG(MOD_CUP|INFO, "[Segment] FW Signature (%d bytes)", segm_len);
                    rt_free(cups->sig);
                    if( segm_len < 8 || segm_len > sizeof(cups->sig->signature) + SIGCRC_LEN ) {
                        LOG(MOD_CUP|ERROR, "Illegal signature segment length (must be 8-%d bytes): %d", sizeof(cups->sig->signature) + SIGCRC_LEN, segm_len);
                        goto proto_err;
                    }
                    cups->sig = rt_malloc(cups_sig_t);
                } else { // cstate == CUPS_FEED_UPDATE
                    assert(cstate == CUPS_FEED_UPDATE);
                    sys_commitConfigUpdate(); 
                    sys_updateStart(segm_len);
                    LOG(MOD_CUP|INFO, "[Segment] FW Update (%d bytes)", segm_len);
                }
            }
        }
      check_segm:
        if( cups->segm_off >= segm_len ) {
            // Segment finished
            if( cstate == CUPS_FEED_CUPS_CRED ) {
                sys_credComplete(SYS_CRED_CUPS, cups->segm_len);
                LOG(MOD_CUP|INFO, "[Segment] CUPS Credentials update completed (%d bytes)", cups->segm_len);
            }
            else if( cstate == CUPS_FEED_TC_CRED ) {
                sys_credComplete(SYS_CRED_TC, cups->segm_len);
                LOG(MOD_CUP|INFO, "[Segment] TC Credentials update completed (%d bytes)", cups->segm_len);
            } else if( cstate == CUPS_FEED_SIGNATURE ) {
                cups->uflags |= UPDATE_FLAG(SIGNATURE);
                cups->sig->len = cups->segm_len - SIGCRC_LEN;
                mbedtls_sha512_init(&cups->sig->sha);
                mbedtls_sha512_starts(&cups->sig->sha, 0);
            }
            else { // cstate == CUPS_FEED_UPDATE
                if( sys_updateCommit(cups->segm_len) ) {
                    cups->uflags |= UPDATE_FLAG(UPDATE);
                    LOG(MOD_CUP|INFO, "[Segment] Update committed (%d bytes)", cups->segm_len);
                } else {
                    LOG(MOD_CUP|ERROR, "[Segment] Update received (%d bytes) but failed to write (ignored)", cups->segm_len);
                }
            }
            goto next_cstate;
        }
        if( body.pos >= body.bufsize )
            goto get_more;

        int segm_off = cups->segm_off;
        int dlen = min(segm_len - segm_off, body.bufsize - body.pos);
        u1_t* data = (u1_t*)&body.buf[body.pos];
        if( cstate == CUPS_FEED_CUPS_CRED ) {
            sys_credWrite(SYS_CRED_CUPS, data, segm_off, dlen);
        }
        else if( cstate == CUPS_FEED_TC_CRED ) {
            sys_credWrite(SYS_CRED_TC, data, segm_off, dlen);
        }
        else if( cstate == CUPS_FEED_SIGNATURE ) {
            // CRC of signature comes just after length
            int siglen = dlen;
            if( segm_off < SIGCRC_LEN ) { // CRC before signature
                int d = min(SIGCRC_LEN-segm_off, siglen);
                memcpy(cups->sig->keycrcb + segm_off, data, d);
                segm_off += d;
                data     += d;
                siglen   -= d;
            }
            if (siglen > 0 && segm_off + siglen - SIGCRC_LEN <= sizeof(cups->sig->signature)) {
                memcpy(cups->sig->signature+segm_off-SIGCRC_LEN, data, siglen);
            }
        } else {
            assert(cstate == CUPS_FEED_UPDATE);
            if( cups->sig != NULL ) {
                mbedtls_sha512_update(&cups->sig->sha, data, dlen);
            }
            sys_updateWrite(data, segm_off, dlen);
        }
        body.pos += dlen;
        cups->segm_off += dlen;
        goto check_segm;
    }
    if( ev == HTTPEV_CLOSED ) {
        if( cups->cstate >= CUPS_INI && cups->cstate < CUPS_DONE )
            cups->cstate = CUPS_ERR_CLOSED;  // unexpected close
        cups_done(cups, cups->cstate);
        return;
    }
    LOG(MOD_CUP|INFO, "cups_update_info - Unknown event: %d", ev);
}


cups_t* cups_ini () {
    assert(CUPS_BUFSZ > MAX_HOSTNAME_LEN + MAX_PORT_LEN + 2);
    cups_t* cups = rt_malloc(cups_t);
    http_ini(&cups->hc, CUPS_BUFSZ);
    rt_iniTimer(&cups->timeout, cups_timeout);
    cups->cstate = CUPS_INI;
    return cups;
}


void cups_free (cups_t* cups) {
    if( cups == NULL )
        return;
    http_free(&cups->hc);
    rt_clrTimer(&cups->timeout);
    cstateLast = cups->cstate;
    cups->cstate = CUPS_ERR_DEAD;
    if (cups->sig) {
        mbedtls_sha512_free(&cups->sig->sha);
        rt_free(cups->sig);
    }
    rt_free(cups);
}


void cups_start (cups_t* cups) {
    assert(cups->cstate == CUPS_INI);

    str_t cupsuri = sys_uri(SYS_CRED_CUPS, cups_credset);
    if( cupsuri == NULL ) {
        LOG(MOD_CUP|ERROR, "No CUPS%s URI configured", sys_credset2str(cups_credset));
        cups_done(cups, CUPS_ERR_NOURI);
        return;
    }
    LOG(MOD_CUP|INFO, "Connecting to CUPS%s ... %s (try #%d)",
        sys_credset2str(cups_credset), cupsuri, cups_failCnt+1);
    log_flushIO();
    // Use HTTP buffer as temp place for host/port strings
    // Gets destroyed when reading response.
    char* hostname = (char*)cups->hc.c.rbuf;
    char* port     = (char*)cups->hc.c.rbuf + MAX_HOSTNAME_LEN + 1;
    int   ok;
    if( (ok = uri_checkHostPortUri(cupsuri, "http", hostname, MAX_HOSTNAME_LEN, port, MAX_PORT_LEN)) == URI_BAD ) {
        LOG(MOD_CUP|ERROR,"Bad CUPS URI: %s", cupsuri);
        goto errexit;
    }
    if( ok == URI_TLS && !conn_setup_tls(&cups->hc.c, SYS_CRED_CUPS, cups_credset, hostname) ) {
        goto errexit;
    }
    if( !http_connect(&cups->hc, hostname, port) ) {
        LOG(MOD_CUP|ERROR, "CUPS connect failed - URI: %s", cupsuri);
        goto errexit;
    }
    rt_setTimerCb(&cups->timeout, rt_micros_ahead(CUPS_CONN_TIMEOUT), cups_timeout);
    cups->hc.c.evcb = (evcb_t)cups_update_info;
    cups->cstate = CUPS_HTTP_REQ_PEND;
    return;

 errexit:
    cups_done(cups, CUPS_ERR_FAILED);
    return;
}

static void delayedCUPSstart(tmr_t* tmr) {
    LOG(MOD_CUP|INFO, "Starting a CUPS session now.");
    cups_start(CUPS);
}

void sys_triggerCUPS (int delay) {
    if( CUPS != NULL || sys_noCUPS )
        return;  // interaction pending
#if defined(CFG_cups_exclusive)
    if( !sys_noTC ) {
        LOG(MOD_CUP|INFO, "Stopping TC in favor of CUPS");
        sys_stopTC();
    }
#endif // defined(CFG_cups_exclusive)
    if( delay < 0 ) {
        delay = CUPS_RESYNC_INTV/1000000;
    }
    LOG(MOD_CUP|INFO, "Starting a CUPS session in %d seconds.", delay);
    sys_inState(SYSIS_CUPS_INTERACT);
    CUPS = cups_ini();
    rt_clrTimer(&cups_sync_tmr);
    rt_setTimerCb(&CUPS->timeout, rt_seconds_ahead(delay), delayedCUPSstart);
}


void sys_iniCUPS () {
    rt_iniTimer(&cups_sync_tmr, cups_ondone);
}

void sys_clearCUPS () {
    rt_clrTimer(&cups_sync_tmr);
}

void sys_delayCUPS () {
    if( sys_statusCUPS() < 0 ) {
        LOG(MOD_CUP|INFO, "Next CUPS interaction delayed by %~T.", CUPS_OKSYNC_INTV);
        rt_setTimer(&cups_sync_tmr, rt_micros_ahead(CUPS_OKSYNC_INTV));
    }
}

s1_t sys_statusCUPS () {
    return CUPS ? CUPS->cstate : cstateLast;
}
