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
#include "web.h"
#include "sys.h"
#include "uj.h"
#include "kwcrc.h"

static web_t* WEB;

static const web_handler_t HANDLERS[];     // fwdecl
extern const web_handler_t SYS_HANDLERS[];

static void web_done (web_t* web, s1_t wstate) {
    // web->wstate = wstate;
    // http_free(&web->hd);
    sys_stopWeb();
    // rt_yieldTo(&web->timeout, cups_ondone);
}

static void web_timeout (tmr_t* tmr) {
    web_t* web = timeout2web(tmr);
    LOG(MOD_WEB|ERROR, "WEB timed out");
    web_done(web, WEB_ERR_TIMEOUT);
}


web_t* web_ini () {
    web_t* web = rt_malloc(web_t);
    if ( web == NULL ) {
        LOG(MOD_WEB|ERROR, "Not enough space to initialize WEB.");
        return NULL;
    }
    httpd_ini(&web->hd, CUPS_BUFSZ); //XX define WEB_BUFSZ
    rt_iniTimer(&web->timeout, web_timeout);
    web->wstate = WEB_INI;
    return web;
}

void web_free (web_t* web) {
    if( web == NULL )
        return;
    httpd_stop(&web->hd);
    httpd_free(&web->hd);
    rt_clrTimer(&web->timeout);
    web->wstate = WEB_ERR_CLOSED;
    rt_free(web);
}

static int web_route(httpd_pstate_t* pstate, httpd_t* hd, dbuf_t* buf) {
    char* path = pstate->path;
    LOG(MOD_WEB|VERBOSE, "Requested Path: %s (crc=0x%08x) [%s]",
        path, pstate->pathcrc, pstate->meth);
    // Handle empty path
    if ( path[0] == 0 ) {
        path = "index.html";
        pstate->contentType = "text/html";
    }
    *buf = sys_webFile(path);

    if ( buf->buf != NULL) {
        if( buf->pos >= 4 && (rt_rlsbf4((u1_t*)buf->buf) & 0x00ffffff) == 0x088b1f ) {
            pstate->contentEnc = "gzip";
        }
        return 200;
    }

    const web_handler_t * const handlers[] = {
        SYS_HANDLERS, HANDLERS, AUTH_HANDLERS, NULL
    };

    for( int k=0; k<sizeof(handlers)/sizeof(*handlers) && handlers[k]; k++ )
        for( const web_handler_t* hdlr=handlers[k]; hdlr && hdlr->pathcrc; hdlr++ )
            if( hdlr->pathcrc == pstate->pathcrc )
                return hdlr->f(pstate,hd,buf);

    return 404;
}

static void web_onev (conn_t* _conn, int ev) {
    web_t* web = conn2web(_conn);
    httpd_t* hd = &web->hd;
    LOG(MOD_WEB|XDEBUG, "Web Event: %d", ev);
    switch(ev) {
    
    
    
    
    case HTTPDEV_REQUEST: {
        dbuf_t hdr = httpd_getHdr(hd);
        LOG(MOD_WEB|XDEBUG, "Client request: content-length=%d\n%.*s", hd->extra.clen, hdr.bufsize, hdr.buf);
        httpd_pstate_t pstate;
        int r = 500;
        // Note: writing to respbuf overwrites hdr!
        dbuf_t respbuf = httpd_getRespbuf(hd);
        dbuf_t fbuf = {0};
        if( !httpd_parseReqLine(&pstate, &hdr) ) {
            LOG(MOD_WEB|ERROR, "Failed to parse request header");
            r = 400;
        } else {
            r = web_route(&pstate, hd, &fbuf);
        }
        char* path = rt_strdup(pstate.path);
        switch(r) {
        case 200:
            xprintf(&respbuf,
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Encoding: %s\r\n"
                    "\r\n", pstate.contentType, (pstate.contentEnc && pstate.contentEnc[0] != 0) ? pstate.contentEnc : "identity" );
            if( respbuf.bufsize - respbuf.pos < fbuf.pos ) {
                LOG(MOD_WEB|ERROR, "Too big: %s (size=%d, bufsize=%d)", path, fbuf.pos, respbuf.bufsize - respbuf.pos);
                respbuf.pos = 0;
                xprintf(&respbuf,
                        "HTTP/1.1 507 Insufficient Storage\r\n\r\n"
                        "Resource too big!\r\n");
            } else {
                LOG(MOD_WEB|VERBOSE, "Sending response: %s (%d bytes)", path, fbuf.pos);
                memcpy(respbuf.buf + respbuf.pos, fbuf.buf, fbuf.pos);
                respbuf.pos += fbuf.pos;
            }
            rt_free((void*)fbuf.buf);
            break;
        case 400:
            xprintf(&respbuf, "HTTP/1.1 400 Bad Request\r\n\r\n");
            break;
        case 401:
            xprintf(&respbuf, "HTTP/1.1 401 Unauthorized\r\n\r\n");
            break;
        case 404:
            xprintf(&respbuf,
                        "HTTP/1.1 404 Not Found\r\n\r\n"
                        "Resource not found!\r\n");
            break;
        case 405:
            xprintf(&respbuf, "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
            break;
        case 500:
            xprintf(&respbuf, "HTTP/1.1 500 Internal Server Error\r\n\r\n");
            break;
        }
        free(path);
        httpd_response(hd, &respbuf);
        break;
    }
    case HTTPDEV_DEAD: {
        LOG(MOD_WEB|INFO, "Web client dead");
        httpd_close(hd);
        break;
    }
    case HTTPDEV_CLOSED: {
        LOG(MOD_WEB|DEBUG, "Web client closed");
        web->hd.c.evcb = (evcb_t)web_onev; // http_close sets ecvb to default nil-cb
        break;
    }
    default: {
        LOG(MOD_WEB|ERROR, "Web - unknown event: %d", ev);
        break;
    }
    }
}


static void web_start (web_t* web) {
    assert(web->wstate == WEB_INI);
    char port[10];
    snprintf(port, sizeof(port), "%d", sys_webPort);

    if( !httpd_listen(&web->hd, port) ) {
        LOG(MOD_WEB|ERROR, "Web listen failed on port %d", sys_webPort);
        goto errexit;
    }
    // rt_setTimerCb(&web->timeout, rt_micros_ahead(WEB_CONN_TIMEOUT), web_timeout);
    web->hd.c.evcb = (evcb_t)web_onev;

    LOG(MOD_WEB|INFO, "Web server listening on port %d (fd=%d)...", sys_webPort, web->hd.listen.netctx.fd);
    return;

 errexit:
    web_done(web, WEB_ERR_FAILED);
    return;
}


void sys_iniWeb () {
    if( !sys_webPort )
        return;
    if ( (WEB = web_ini()) )
        web_start(WEB);
    web_authini();
}

void sys_stopWeb () {
    web_free(WEB);
    WEB = NULL;
}

/* ------------------------------------------------------------------------------
 *  HTTP request handlers
 *   - return HTTP response code (OK->200, ERROR->500)
 *   - dbuf_t *b not initialized
 * ------------------------------------------------------------------------------ */

int handle_api(httpd_pstate_t* pstate, httpd_t* hd, dbuf_t* b) {
    return 200;
}

int handle_version(httpd_pstate_t* pstate, httpd_t* hd, dbuf_t* b) {
    if ( pstate->method != HTTP_GET )
        return 405; // Method not allowed

    b->buf = _rt_malloc(200,0);
    b->bufsize = 200;
    uj_encOpen(b, '{');
        uj_encKV(b, "msgtype",  's', "version");
        uj_encKV(b, "firmware", 's', sys_version());
        uj_encKV(b, "station",  's', CFG_version);
        uj_encKV(b, "protocol", 'i', MUXS_PROTOCOL_VERSION);
        uj_encKV(b, "features", 's', rt_features());
        uj_encClose(b, '}');
    pstate->contentType = "application/json";
    return 200;
}

static const web_handler_t HANDLERS[] = {
    { J_api,     handle_api     },
    { J_version, handle_version },
    { 0,         NULL           },
};
