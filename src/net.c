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
#include "ws.h"
#include "http.h"
#include "httpd.h"
#include "tls.h"
#include "kwcrc.h"

str_t const SUFFIX2CT[] = {
    "txt",  "text/plain",
    "htm",  "text/html",
    "html", "text/html",
    "css",  "text/css",
    "png",  "image/png",
    "js",   "application/javascript",
    "json", "application/json",
    NULL, NULL
};


// --------------------------------------------------------------------------------
//
// HTTP parsing stuff
//
// --------------------------------------------------------------------------------

dbuf_t http_statusText (dbuf_t* hdr) {
    char* s1 = strchr(hdr->buf, ' ');
    s1 = strchr(s1?s1+1:hdr->buf, ' ');
    char* s2 = strchr(hdr->buf, '\r');
    dbuf_t msg = {.buf=hdr->buf, .bufsize=0, .pos=0 };
    if( s1 && s2 ) {
        msg.buf = s1+1;
        msg.bufsize = s2-s1-1;
    }
    return msg;
}

int uri_isScheme(const char* uri, const char* scheme) {
    int n = http_icaseCmp(uri, scheme);
    return n && uri[n] == ':' ? n : 0;
}

static int nextChar (dbuf_t* b) {
    return b->pos >= b->bufsize ? 0 : b->buf[b->pos++];
}

int uri_parse (dbuf_t* b, struct uri_info* u, int skipSchema) {
    int c;
    if( !skipSchema ) {
        do {
            if( (c = nextChar(b)) == 0 )
                return 0;
            if( c ==':' ) {
                u->schemeEnd = b->pos-1;
                if( nextChar(b) != '/' || nextChar(b) != '/' )
                    return 0;
                break;
            }
        } while(1);
    } else {
        u->schemeEnd = 0;
    }
    u->hostportBeg = b->pos;
    c = nextChar(b);
    if( c=='[' ) {
        // IPv6 hostname [200::1]:port
        u->hostBeg = b->pos;
        do {
            c = nextChar(b);
            if( c==0 ) return 0;
            if( c==']' ) {
                u->hostEnd = (u->hostportEnd = b->pos-1)-1;
                break;
            }
        } while(1);
        c = nextChar(b);
    } else {
        u->hostBeg = u->hostportBeg;
        do {
            if( c==0 ) {
                u->hostEnd = b->pos;
                break;
            }
            if( c==':' || c=='/' ) {
                u->hostEnd = b->pos-1;
                break;
            }
            c = nextChar(b);
        } while(1);
        u->hostportEnd = u->hostEnd;
    }
    if( u->hostBeg == u->hostEnd )
        return 0;  // hostname is empty
    if( c==':' ) {
        u->portBeg = b->pos;
        do {
            c = nextChar(b);
            if( c==0 ) {
                u->portEnd = b->pos;
                break;
            }
            if( c=='/' ) {
                u->portEnd = b->pos-1;
                break;
            }
        } while(1);
        if( u->portBeg == u->portEnd )
            return 0;  // port is empty although : is present
        u->hostportEnd = u->portEnd;
    } else {
        u->portBeg = u->portEnd = 0;
    }

    if( c=='/' ) {
        u->pathBeg = b->pos-1;
        do {
            c = nextChar(b);
            if( c==0 ) {
                u->pathEnd = b->pos;
                break;
            }
            if( c==' ' || c=='\t' || c=='\r' || c=='\n' ) {
                u->pathEnd = b->pos-1;
                break;
            }
        } while(1);
    } else {
        u->pathBeg = u->pathEnd = 0;
    }
    return 1;
}

// Commonly used URI parsing - no path
int uri_checkHostPortUri (const char* uri,
                          const char* scheme,
                          char* host, int hostlen,
                          char* port, int portlen) {
    assert(hostlen > 0 && portlen > 0);
    host[0] = port[0] = 0;
    int n = http_icaseCmp(uri, scheme);
    int tls = (n && uri[n]=='s');
    if( n==0 || uri[n+tls] != ':' ) {
        LOG(MOD_AIO|ERROR, "Malformed URI - expecting %s://.. or %ss://.. but found: %s", scheme, scheme, uri);
        return URI_BAD;
    }
    struct uri_info u;
    dbuf_t b;
    dbuf_str(b, uri);
    int ok = uri_parse(&b, &u, 0);
    if( !ok || u.pathBeg != 0 || u.portBeg==0 ) {
        LOG(MOD_AIO|ERROR, "Malformed URI - expecting %s(s)://host:port (no path, port mandatory) but found: %s", scheme, uri);
        return URI_BAD;
    }
    int hlen = u.hostEnd-u.hostBeg;
    int plen = u.portEnd-u.portBeg;
    if( hostlen <= hlen || portlen <= plen ) {
        LOG(MOD_AIO|ERROR, "Malformed URI - host/port too big (max %d/%d): %s", hostlen, portlen, uri);
        return URI_BAD;
    }
    memcpy(host, uri+u.hostBeg, hlen); host[hlen] = 0;
    memcpy(port, uri+u.portBeg, plen); port[plen] = 0;
    return tls ? URI_TLS : URI_TCP;
}


char* http_skipWsp (char* p) {
    while(1) {
        int c = p[0];
        if( c==' ' || c=='\t' ) {
            p += 1;
            continue;
        }
        if( c=='\r' && p[1]=='\n' && (p[2]==' ' || p[2]=='\t') ) {  // continuation line
            p += 3;
            continue;
        }
        return p;
    }
}


int http_unquote (char** p) {
    char* s = *p;
    int c = *s;
    if( c == '%' ) {
        int v = rt_hexDigit(s[1]) | rt_hexDigit(s[2]);
        if( v >= 0 ) {
            *p = s+3;
            return v;
        }
        // Bad HEX - assume it's a literal %
    }
    *p = s+1;
    return c;
}


int http_readDec (char* p) {
    return rt_readDec((str_t*)&p);
}


int http_statusCode (char* p) {
    // Status line starts with "HTTP/1.x NNN"
    if( p[4] != '/' || p[5] != '1' || p[6] != '.' || p[8] != ' ')
        return -1;
    return http_readDec(p+9);
}

// Check if what is found at p ignoring case (what must be lower case)
// Return the number matching char (aka length of what, assuming what=="" does not make sense)
//  0 if what is not at p.
int http_icaseCmp (const char* p, const char* what) {
    int n = -1;
    do {
        int d = what[++n];
        if( d == 0 )
            return n;
        int c = p[n];
        if( c >='A' && c <='Z' )
            c += 'a'-'A';
        if( c != d )
            return 0;
    } while(1);
}

// field MUST be lower case!
char* http_findHeader (char* p, const char* field) {
    u4_t v = 0;
    do {
        char c = *p++;
        v = (v << 8) | (u1_t)c;
        if( v == 0x0d0a0d0a )
            return NULL;
        if( c == '\n' ) {
            int n = http_icaseCmp(p, field);
            if( n && p[n] == ':' )
                return http_skipWsp(&p[n+1]);
        }
    } while(1);
}

int http_findContentLength (char* p) {
    return http_readDec(http_findHeader(p, "content-length"));
}

// Find content-length header and replace subsequent stretch of 00000
// with actual length clearing excess zeros with blanks. You must provide
// enough 0s so that the actual length will fit in.
// Return: 0 - failed to encode length in stretch of zeros
//         1 - ok
int http_setContentLength (char* p, int clen) {
    char* cp = http_findHeader(p, "content-length");
    if( cp == NULL )
        return 0;
    char* beg = cp;
    while( beg[0] == cp[1] ) cp++;
    do {
        *cp-- = clen % 10 + '0';
        clen /= 10;
    } while( clen && cp >= beg );
    while( cp >= beg ) *cp-- = ' ';
    if( clen )
        return 0;
    return 1;
}



// --------------------------------------------------------------------------------
//
// WS stuff
//
// --------------------------------------------------------------------------------


enum {
    IO_ERROR,
    IO_WRPEND,
    IO_RDPEND,
    IO_WRDONE,
    IO_RDDONE,
};

enum { WSHDR_INTRA  = 3 }; // frame header internal to wbuf (16bit LSB length of data)
enum { WSHDR_RESV_W = 8 }; // reserve at start of wbuf
enum { WSHDR_RESV_R = 1 }; // reserve at start of rbuf
enum { WSHDR_MASK   = 0x80,
       WSHDR_LEN2   = 0x7E,  // 16 bit length
       WSHDR_LEN4   = 0x7F,  // 64 bit length - not used in this code
};
enum { WSHDR_FIN    = 0x80,
       WSHDR_CONT   = 0x00,
       WSHDR_TEXT   = 0x01,
       WSHDR_BINARY = 0x02,
       WSHDR_CLOSE  = 0x08,
       WSHDR_PING   = 0x09,
       WSHDR_PONG   = 0x0A,
};


// Write data between wpos..wend
static int writeData (conn_t* conn) {
    int ret;
    while( conn->wpos < conn->wend ) {
        if( (ret = tls_write(&conn->netctx, conn->tlsctx, conn->wbuf + conn->wpos, conn->wend - conn->wpos) ) <= 0 ) {
            if( ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE ) {
                log_mbedError(MOD_AIO|ERROR, ret, "[%d] Send failed", conn->netctx.fd);
                return IO_ERROR;
            }
            return IO_WRPEND;
        }
        LOG(MOD_AIO|XDEBUG, "[%d] socket write bytes=%d", conn->netctx.fd, ret);
        conn->wpos += ret;
    }
    return IO_WRDONE;
}


enum { WS_FRAME, HTTP_HDR, HTTP_BODY };
// Fill in data from rpos..rbufsize
static int readData (conn_t* conn, int mode) {
    int r;
    while(1) {
        if( mode == WS_FRAME ) {
            // Do we have frame frame header and all data?
            int b = conn->rbeg;
            u1_t* r = &conn->rbuf[b];
            int n = conn->rpos - b;
            if( n >= 2 ) {
                u1_t opcode = r[0] & 0xF;
                u2_t len = r[1] & 0x7F;
                // ensure: FIN=1 RSV1/2/3=0, no masking (0x80) and no 64bit length
                if( (r[0] & 0xF0) != 0x80 || (r[1]&0x80) || len == 0x7F ) {
                    LOG(MOD_AIO|ERROR, "[%d] Illegal WS frame: %02X:%02X", conn->netctx.fd, r[0], r[1]);
                    return IO_ERROR;
                }
                if( len < 0x7E ) {
                    if( len+2 <= n ) {
                        conn->rbeg = b + 2;
                        conn->rend = b + 2 + len;
                        r[1] = opcode;
                        return IO_RDDONE;
                    }
                } else if( n >= 4 ) {
                    len = rt_rmsbf2(&r[2]);
                    if( len+4 <= n ) {
                        conn->rbeg = b + 4;
                        conn->rend = b + 4 + len;
                        r[3] = opcode;
                        return IO_RDDONE;
                    }
                }
                if( b+len+2 > conn->rbufsize )
                    goto compact;
            }
            if( conn->rpos >= conn->rbufsize )
                goto compact;
        }
        else if( mode == HTTP_HDR ) {
            // Did we see end of HTTP header "\r\n\r\n" ?
            u4_t v = 0;
            for( int i=conn->rbeg; i<conn->rpos; i++ ) {
                v = (v<<8) | conn->rbuf[i];
                if( v == 0x0d0a0d0a ) {
                    conn->rend = i+1;
                    return IO_RDDONE;
                }
            }
        }
        else {
            assert(mode==HTTP_BODY);
            if( conn->rpos >= conn->rend ) {
                if( conn->rpos > conn->rend ) {
                    LOG(MOD_AIO|ERROR, "[%d] Received more data than expected HTTP content size: %d extra bytes", conn->netctx.fd, conn->rpos - conn->rend);
                    return IO_ERROR;
                }
                return IO_RDDONE;
            }
        }
    readagain:
        if( (r = tls_read(&conn->netctx, conn->tlsctx, conn->rbuf + conn->rpos, conn->rbufsize - conn->rpos) ) <= 0 ) {
            if( r == 0 ) {
                LOG(MOD_AIO|DEBUG, "[%d] Connection closed unexpectedly", conn->netctx.fd);
                return IO_ERROR;
            }
            if( r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE ) {
                log_mbedError(MOD_AIO|ERROR, r, "Recv failed");
                return IO_ERROR;
            }
            return IO_RDPEND;
        }
        LOG(MOD_AIO|XDEBUG, "[%d] socket read  bytes=%d", conn->netctx.fd, r);
        conn->rpos += r;
    }
  compact:
    // No enough remaining buffer space
    LOG(MOD_AIO|INFO, "[%d] COMPACTING Recv buffer", conn->netctx.fd);
    r = conn->rbeg-WSHDR_RESV_R;
    if( r > 0 ) {
        memmove(&conn->rbuf[WSHDR_RESV_R], &conn->rbuf[r+WSHDR_RESV_R], conn->rbufsize-r);
        conn->rbeg -= r;
        conn->rend -= r;
        conn->rpos -= r;
        goto readagain;
    }
    LOG(MOD_AIO|ERROR, "[%d] Recv buffer too small", conn->netctx.fd);
    return IO_ERROR;
}


void conn_evcb_nil (conn_t* conn, int ev) {
    LOG(MOD_AIO|VERBOSE, "Connection event %d ignored (conn=%p)", ev, conn)
}


static void triggerWsClosedEv(tmr_t* tmr) {
    ws_t* conn = tmr2ws(tmr);
    evcb_t evcb = conn->evcb;
    conn->evcb = conn_evcb_nil;
    evcb(conn, WSEV_CLOSED);
}


void ws_shutdown (ws_t* conn) {
    LOG(MOD_AIO|DEBUG, "[%d] WS connection shutdown...", conn->netctx.fd);
    mbedtls_net_free(&conn->netctx);
    rt_free(conn->rbuf);
    rt_free(conn->wbuf);
    conn->rbuf = NULL;
    conn->wbuf = NULL;
    rt_free((void*)conn->authtoken);
    conn->authtoken = NULL;
    tls_freeSession(conn->tlsctx); conn->tlsctx = NULL;
    tls_freeConf(conn->tlsconf); conn->tlsconf = NULL;
    aio_close(conn->aio);
    rt_clrTimer(&conn->tmr);
    conn->aio = NULL;
    conn->state = WS_CLOSED;
    rt_yieldTo(&conn->tmr, triggerWsClosedEv);
}


static void ws_closing_w (aio_t* aio) {
    ws_t* conn = (ws_t*)aio->ctx;
    assert(conn->state >= WS_CLOSING_DRAINC);
    LOG(MOD_AIO|XDEBUG, "[%d] ws_closing_w state=%d", conn->netctx.fd, conn->state);
    int e;
  again:
    if( (e = writeData(conn)) == IO_ERROR ) {
        ws_shutdown(conn);
        return;
    }
    if( e == IO_WRPEND )
        return;
    assert(e==IO_WRDONE);
    if( conn->state == WS_CLOSING_DRAINC || conn->state == WS_CLOSING_DRAINS ) {
        conn->wpos = 0;
        conn->wend = conn->wfill = 8;
        u1_t* p = conn->wbuf;
        p[0] = WSHDR_FIN | WSHDR_CLOSE;
        p[1] = 2 | WSHDR_MASK;
        p[2] = p[3] = p[4] = p[5] = 0;
        p[6] = conn->creason>>8;
        p[7] = conn->creason;
        conn->state += WS_CLOSING_SENDCLOSE - WS_CLOSING_DRAINC;
        LOG(MOD_AIO|DEBUG, "%s close - reason=%d",
            conn->state == WS_CLOSING_DRAINC ? "Initiating" : "Echoing", conn->creason);
        goto again;
    }
    if( conn->state == WS_CLOSING_ECHOCLOSE ) {
        ws_shutdown(conn);
        return;
    }
    conn->state = WS_CLOSING_SENTCLOSE;
    aio_set_wrfn(conn->aio, NULL);
}


static void ws_connected_w (aio_t* aio) {
    ws_t* conn = (ws_t*)aio->ctx;
    assert(conn->state == WS_CONNECTED);
    // LOG(MOD_AIO|XDEBUG, "[%d] ws_connected_w state=%d", conn->netctx.fd, conn->state);
    int e;
  again:
    if( conn->wpos < conn->wend ) {
        if( (e = writeData(conn)) == IO_ERROR ) {
            ws_shutdown(conn);
            return;
        }
        if( e == IO_WRPEND )
            return;
        assert(e==IO_WRDONE);
        conn->evcb(conn, WSEV_DATASENT);
    }
    // Do we have more data pending?
    doff_t wend = conn->wend;
    if( wend == conn->wfill ) {
        // No more data to send
        aio_set_wrfn(conn->aio, NULL);
        return;
    }
    // Setup next frame
    u1_t* wbuf = conn->wbuf;
    u2_t dlen = rt_rmsbf2(wbuf + wend);
    u1_t ftype = wbuf[wend+2];  // WSHDR_TEXT | WSHDR_BINARY
    wend += WSHDR_INTRA;
    if( dlen < WSHDR_LEN2 ) {
        // short WS header
        wbuf[wend-6] = WSHDR_FIN|ftype;
        wbuf[wend-5] = dlen | WSHDR_MASK;
        conn->wpos = wend-6;
    } else {
        // medium WS head (note we have WSHDR_WRESV reserve at the start of wbuf)
        wbuf[wend-8] = WSHDR_FIN|ftype;
        wbuf[wend-7] = WSHDR_LEN2 | WSHDR_MASK;
        wbuf[wend-6] = dlen>>8;
        wbuf[wend-5] = dlen;
        conn->wpos = wend-8;
    }
    conn->wend = wend + dlen;
    // Masking value - 0
    wbuf[wend-4] = wbuf[wend-3] = wbuf[wend-2] = wbuf[wend-1] = 1;
    for( int i=0; i<dlen; i++ )
        wbuf[wend+i] ^= 1;
    goto again;
}


static void ws_connected_r (aio_t* aio) {
    ws_t* conn = (ws_t*)aio->ctx;
    assert(conn->state >= WS_CONNECTED);  // also called during close
    // LOG(MOD_AIO|XDEBUG, "[%d|WS] ws_connected_r state=%d", conn->netctx.fd, conn->state);
    int e;
  again:
    if( (e = readData(conn, WS_FRAME)) == IO_ERROR ) {
        ws_shutdown(conn);
        return;
    }
    if( e == IO_RDPEND )
        return;
    assert(e==IO_RDDONE);
    u1_t* p = &conn->rbuf[conn->rbeg];
    u1_t opcode = p[-1];
    switch(opcode) {
    case WSHDR_PING: {
        int plen = conn->rend-conn->rbeg;
        LOG(MOD_AIO|XDEBUG, "[%d|WS] < PING (%H)", conn->netctx.fd, plen, p);
        dbuf_t wbuf = ws_getSendbuf(conn, plen);
        if( wbuf.buf == NULL ) {
            LOG(MOD_AIO|WARNING, "[%d] Cannot respond to PING message of length %d", conn->netctx.fd, plen);
            break;
        }
        wbuf.buf[0-WSHDR_INTRA] = plen>>8;
        wbuf.buf[1-WSHDR_INTRA] = plen;
        wbuf.buf[2-WSHDR_INTRA] = WSHDR_PONG;
        conn->wfill += plen+WSHDR_INTRA;
        memcpy(wbuf.buf, p, plen);
        aio_set_wrfn(conn->aio, ws_connected_w);
        LOG(MOD_AIO|XDEBUG, "[%d|WS] > PONG", conn->netctx.fd);
        break;
    }
    case WSHDR_PONG: {
        LOG(MOD_AIO|XDEBUG, "[%d|WS] Ignoring incoming WS PONG", conn->netctx.fd);
        break;
    }
    case WSHDR_TEXT: {
        int offset = 0;
        int plen = conn->rend - conn->rbeg;
        while( offset < plen ) {
            LOG(MOD_AIO|XDEBUG, "[%d|WS] %c %.*s", conn->netctx.fd, offset ? '.' : '<', min((LOGLINE_LEN-50),plen-offset), p+offset);
            offset += (LOGLINE_LEN-50);
        }
        conn->evcb(conn, WSEV_TEXTRCVD);
        if( conn->aio == NULL )
            return;
        break;
    }
    case WSHDR_BINARY: {
        conn->evcb(conn, WSEV_BINARYRCVD);
        if( conn->aio == NULL )
            return;
        break;
    }
    case WSHDR_CLOSE: {
        u2_t reason = rt_rmsbf2(p);
        LOG(MOD_AIO|DEBUG, "[%d|WS] Server sent close: reason=%d", conn->netctx.fd, reason);
        if( conn->state > WS_CONNECTED ) {
            ws_shutdown(conn);
            return;
        }
        ws_close(conn, reason);
        conn->state = WS_CLOSING_DRAINS;
        break;
    }
    default: {
        LOG(MOD_AIO|WARNING, "[%d|WS] Unsupported WS opcode: %d", conn->netctx.fd, opcode);
        break;
    }
    }
    conn->rbeg = conn->rend;
    if( conn->rend == conn->rpos )
        conn->rbeg = conn->rend = conn->rpos = WSHDR_RESV_R;
    goto again;
}


void ws_close (ws_t* conn, int reason) {
    if( conn->state < WS_CONNECTED ) {
        ws_shutdown(conn);
        return;
    }
    if( conn->state >= WS_CLOSING_DRAINC )
        return;
    LOG(MOD_AIO|DEBUG, "[%d] ws_close reason=%d", conn->netctx.fd, reason);
    conn->creason = reason==0 ? 1000 : reason;  // 1000=normal close
    conn->state = WS_CLOSING_DRAINC;
    aio_set_rdfn(conn->aio, ws_connected_r);
    aio_set_wrfn(conn->aio, ws_closing_w);
}


static void ws_connecting (aio_t* aio);

static void ws_handshaking (aio_t* aio) {
    ws_t* conn = (ws_t*)aio->ctx;
    LOG(MOD_AIO|XDEBUG, "[%d] ws_connecting state=%d", conn->netctx.fd, conn->state);
    switch( conn->state ) {
    case WS_TLS_HANDSHAKE: {
        int err = 0;
        if( conn->tlsctx )
            err = mbedtls_ssl_handshake(conn->tlsctx);
        if( err == 0 ) {
            // Ready to run websocket protocol
            assert(conn->rbuf == NULL && conn->wbuf == NULL);
            conn->rbuf = rt_mallocN(u1_t, conn->rbufsize);
            conn->wbuf = rt_mallocN(u1_t, conn->wbufsize);

            conn->wpos = 0;
            conn->wend = snprintf
                ((char*)conn->wbuf, conn->wbufsize,
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s:%s\r\n"
                 "Upgrade: websocket\r\n"
                 "Connection: upgrade\r\n"
                 "Sec-WebSocket-Key: %s\r\n"
                 "Sec-WebSocket-Version: 13\r\n"
                 //"Sec-WebSocket-Protocol: ...\r\n"
                 //"Sec-WebSocket-Extensions: ...\r\n"
                 //"Origin: http://www.example.com\r\n"  // if browser request or similar
                 //and other header fields if required e.g. cookies
                 "%s\r\n",
                 conn->uripath, conn->host, conn->port,
                 // Well let's say we chose the same random over and over again...
                 // then we get the same key. If you think variation is important below are
                 // a few more to choose from:
                 //  fKlZ7KnycyYQddIrRwvjXg==
                 //  aPnEh0f0Q/DcX6MmuFsHYw==
                 //  OMQ7ar+ghnUHbT8lsfjziA==
                 "bpse8nVmEl6ZlX4lSb6RMw==",
                 conn->authtoken ? conn->authtoken : "");
            assert(conn->wend < conn->wbufsize-1);
            conn->state = WS_CLIENT_REQ;
            aio_set_rdfn(conn->aio, ws_connecting);
            aio_set_wrfn(conn->aio, ws_connecting);
            ws_connecting(conn->aio);
            return;
        }
        if( err == MBEDTLS_ERR_SSL_WANT_READ ) {
            aio_set_rdfn(conn->aio, ws_handshaking);
            aio_set_wrfn(conn->aio, NULL);
            return;
        }
        if( err == MBEDTLS_ERR_SSL_WANT_WRITE ) {
            aio_set_rdfn(conn->aio, NULL);
            aio_set_wrfn(conn->aio, ws_handshaking);
            return;
        }
	if( err == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ) {
	    char errmsg[128] = {0};
	    u4_t flags = mbedtls_ssl_get_verify_result(conn->tlsctx);
	    mbedtls_x509_crt_verify_info(errmsg, sizeof(errmsg), "", flags);
	    LOG(MOD_AIO|INFO, "TLS server certificate verification failed: %.*s", sizeof(errmsg), errmsg)
	}

        ws_shutdown(conn);
        return;
    }
    default: {
        assert(0);
    }
    }
}


static void ws_connecting (aio_t* aio) {
    ws_t* conn = (ws_t*)aio->ctx;
    LOG(MOD_AIO|XDEBUG, "[%d] ws_connecting state=%d", conn->netctx.fd, conn->state);
    switch( conn->state ) {
    case WS_CLIENT_REQ: {
        int e = writeData(conn);
        if( e == IO_ERROR ) {
            ws_shutdown(conn);
            return;
        }
        if( e == IO_WRPEND )
            return;
        // IO_WRDONE
        aio->wrfn = NULL;
        conn->state = WS_SERVER_RESP;
        return;
    }
    case WS_SERVER_RESP: {
        int e = readData(conn, HTTP_HDR);
        if( e == IO_ERROR ) {
            ws_shutdown(conn);
            return;
        }
        if( e == IO_RDPEND )
            return;
        // IO_RDDONE - check header
        int scode = http_statusCode((char*)conn->rbuf);
        if( scode != 101 ) {
            LOG(MOD_AIO|ERROR, "[%d] WS upgrade failed with HTTP status code: %d", conn->netctx.fd, scode);
            ws_shutdown(conn);
            return;
        }
        conn->wpos = conn->wend = conn->wfill = WSHDR_RESV_W;
        aio_set_rdfn(conn->aio, ws_connected_r);
        aio_set_wrfn(conn->aio, NULL);
        conn->state = WS_CONNECTED;
        conn->evcb(conn, WSEV_CONNECTED);
        conn->rbeg = conn->rend; // signal lower level that we consumed this frame
        if( conn->aio )
            ws_connected_r(conn->aio);
        return;
    }
    default: {
        assert(0);
    }
    }
}


// Request some space for filling in frame data
// Space defined in b is ONLY valid until CPU is yielded!
// If space is not used caller can just walk away - no clean/free required.
//
dbuf_t ws_getSendbuf (ws_t* conn, int minsize) {
    if( conn->state != WS_CONNECTED )
        goto errexit;  // nope - come back later
    if( conn->wpos == conn->wfill )
        conn->wpos = conn->wend = conn->wfill = WSHDR_RESV_W;
    if( WSHDR_RESV_W + WSHDR_INTRA + minsize > conn->wbufsize ) {
        LOG(MOD_AIO|CRITICAL, "[%d] Requested send buffer size exceeds available space: %d > %d bytes",
            conn->netctx.fd, minsize, conn->wbufsize - WSHDR_RESV_W - WSHDR_INTRA);
        goto errexit;  // nope - come back never...
    }
    int n = conn->wfill + minsize - conn->wbufsize;
    if( n > 0 ) {
        int m = conn->wpos - WSHDR_RESV_W;
        if( n > m - WSHDR_INTRA )
            goto errexit; // nope - even compaction would not make enough space - come back later
        // Compaction
        memmove(&conn->wbuf[WSHDR_RESV_W], &conn->wbuf[conn->wpos], conn->wfill - conn->wpos);
        conn->wpos  -= m;
        conn->wend  -= m;
        conn->wfill -= m;
    }
    dbuf_t b = {
        .buf    =(char*)(conn->wbuf + conn->wfill + WSHDR_INTRA),
        .bufsize=conn->wbufsize - conn->wfill - WSHDR_INTRA,
        .pos    =0 };
    return b;

 errexit: {
        dbuf_t b = { .buf=NULL, .bufsize=0, .pos=0 };
        return b;
    }
}

// Commit frame data to send buffer
// b was obtained from ws_getSendbuf
// b->pos should be the length of the filled in data
//
void ws_sendData (ws_t* conn, dbuf_t* b, int binaryData) {
    if( conn->state != WS_CONNECTED )
        return;
    int n = b->pos;
    b->buf[0-WSHDR_INTRA] = n>>8;
    b->buf[1-WSHDR_INTRA] = n;
    b->buf[2-WSHDR_INTRA] = binaryData ? WSHDR_BINARY : WSHDR_TEXT;
    conn->wfill += n+WSHDR_INTRA;
    b->buf = NULL;
    b->pos = b->bufsize = 0;
    aio_set_wrfn(conn->aio, ws_connected_w);
}


void ws_sendText (ws_t* conn, dbuf_t* b) {
    int offset = 0;
    int plen = b->pos;
    while( offset < plen ) {
        LOG(MOD_AIO|XDEBUG, "[%d|WS] %c %.*s", conn->netctx.fd, offset ? '.' : '>', min((LOGLINE_LEN-50),plen-offset), b->buf+offset);
        offset += (LOGLINE_LEN-50);
    }

    ws_sendData(conn, b, 0);
}


void ws_sendBinary (ws_t* conn, dbuf_t* b) {
    ws_sendData(conn, b, 1);
}


dbuf_t ws_getRecvbuf (ws_t* conn) {
    if( conn->state != WS_CONNECTED ||
        conn->rbeg == conn->rend ) {
        dbuf_t b = {
            .buf = NULL,
            .bufsize = 0,
            .pos = 0 };
        return b;
    }
    dbuf_t b = {
        .buf = (char*)(conn->rbuf + conn->rbeg),
        .bufsize = conn->rend - conn->rbeg,
        .pos = 0 };
    return b;
}


void ws_ini (ws_t* conn, int rbufsize, int wbufsize) {
    memset(conn, 0, sizeof(*conn));
    mbedtls_net_init(&conn->netctx);
    rt_iniTimer(&conn->tmr, NULL);
    conn->state = WS_CLOSED;
    conn->evcb = conn_evcb_nil;
    conn->rbufsize = rbufsize;
    conn->wbufsize = wbufsize;
}


void ws_free (ws_t* conn) {
    rt_free(conn->rbuf);
    rt_free(conn->wbuf);
    conn->rbuf = NULL;
    conn->wbuf = NULL;
    rt_free(conn->host);
    rt_free(conn->port);
    rt_free(conn->uripath);
    conn->host = NULL;
    conn->port = NULL;
    conn->uripath = NULL;
    rt_free((void*)conn->authtoken);
    conn->authtoken = NULL;
    rt_clrTimer(&conn->tmr);
    aio_close(conn->aio);
    conn->aio = NULL;
    mbedtls_net_free(&conn->netctx);
    tls_freeSession(conn->tlsctx); conn->tlsctx = NULL;
    tls_freeConf(conn->tlsconf); conn->tlsconf = NULL;
}


int ws_connect (ws_t* conn, char* host, char* port, char* uripath) {
    if( conn->state != WS_CLOSED )
        return 0;  // forgot to ws_close?
    rt_clrTimer(&conn->tmr);
    mbedtls_net_free(&conn->netctx);
    mbedtls_net_init(&conn->netctx);

    int ret;
    if( (ret = mbedtls_net_connect(&conn->netctx, host, port, MBEDTLS_NET_PROTO_TCP)) != 0 ) {
        log_mbedError(MOD_AIO|ERROR, ret, "[%d] WS connect failed", conn->netctx.fd);
        ws_shutdown(conn);
        return 0;
    }
    if( (ret = mbedtls_net_set_nonblock(&conn->netctx)) != 0 ) {
        log_mbedError(MOD_AIO|ERROR, ret, "[%d] Non blocking failed", conn->netctx.fd);
        ws_shutdown(conn);
        return 0;
    }
    sys_keepAlive(conn->netctx.fd);
    if( conn->tlsctx )
        mbedtls_ssl_set_bio(conn->tlsctx, &conn->netctx, mbedtls_net_send, mbedtls_net_recv, NULL);

    conn->host = rt_strdup(host);
    conn->port = rt_strdup(port);
    conn->uripath = rt_strdup(uripath);
    conn->aio = aio_open(conn, conn->netctx.fd, NULL, NULL);
    conn->state = WS_TLS_HANDSHAKE;
    ws_handshaking(conn->aio);
    return 1;
}


int ws_getRtt (ws_t* conn, u2_t* q_80_90_95) {
    q_80_90_95[0] = q_80_90_95[1] = q_80_90_95[2] = 0;
    return 0; // no data XXX:TBD
}


// --------------------------------------------------------------------------------
//
// HTTP stuff
//
// --------------------------------------------------------------------------------


static void triggerHttpRead (tmr_t* tmr) {
    http_t* conn = tmr2http(tmr);
    conn->c.aio->rdfn(conn->c.aio);
}

static void triggerHttpConnectedEv (tmr_t* tmr) {
    http_t* conn = tmr2http(tmr);
    conn->c.evcb(&conn->c, HTTPEV_CONNECTED);
}

static void triggerHttpClosedEv (tmr_t* tmr) {
    http_t* conn = tmr2http(tmr);
    evcb_t evcb = conn->c.evcb;
    conn->c.evcb = conn_evcb_nil;
    evcb(&conn->c, HTTPEV_CLOSED);
}


static void http_read (aio_t* aio) {
    http_t* conn = (http_t*)aio->ctx;
    LOG(MOD_AIO|XDEBUG, "[%d] http_read state=%d", conn->c.netctx.fd, conn->c.state);
    if( conn->c.state == HTTP_READING_HDR ) {
        assert(conn->extra.coff == -1);
        // Read upto \r\n\r\n
        int e = readData(&conn->c, HTTP_HDR);
        if( e == IO_ERROR ) {
            LOG(MOD_AIO|ERROR, "[%d] Error reading HTTP Header", conn->c.netctx.fd);
            http_close(conn);
            return;
        }
        if( e == IO_RDPEND )
            return;
        assert(e==IO_RDDONE);
        char* hdr = (char*)&conn->c.rbuf[conn->c.wfill];
        int clen = http_findContentLength(hdr);
        if( clen >= 0 ) {
            conn->extra.coff = 0;
            conn->extra.clen = clen;
            clen = min(clen, conn->c.rbufsize - conn->c.rend);
        } else {
            // No content-length, assume no body
            conn->extra.coff = conn->extra.clen = clen = 0;
        }
        conn->c.creason = http_statusCode(hdr);
        conn->c.rbeg = conn->c.rend;  // remember end header / start of body
        conn->c.rend += clen;
        conn->c.state = HTTP_READING_BODY;
    }
    assert(conn->c.state == HTTP_READING_BODY && conn->extra.coff >= 0 && conn->extra.coff <= conn->extra.clen);
    int e = readData(&conn->c, HTTP_BODY);
    if( e == IO_ERROR ) {
        LOG(MOD_AIO|ERROR, "[%d] Error reading HTTP Body", conn->c.netctx.fd);
        http_close(conn);
        return;
    }
    if( e == IO_RDPEND )
        return;
    assert(e==IO_RDDONE);
    aio_set_rdfn(aio, NULL);
    int r = conn->c.rend - conn->c.rbeg;
    if( conn->extra.coff + r >= conn->extra.clen )
        conn->c.state = HTTP_CONNECTED;
    conn->c.evcb(&conn->c, HTTPEV_RESPONSE);
}


static void http_write (aio_t* aio) {
    http_t* conn = (http_t*)aio->ctx;
    assert(conn->c.state == HTTP_SENDING_REQ);
    int e = writeData(&conn->c);
    if( e == IO_ERROR ) {
        http_close(conn);
        return;
    }
    if( e == IO_WRPEND )
        return;
    assert(e==IO_WRDONE);
    conn->c.rpos = conn->c.rbeg = conn->c.wfill;
    conn->c.state = HTTP_READING_HDR;
    conn->c.creason = 0;
    aio_set_wrfn(aio, NULL);
    aio_set_rdfn(aio, http_read);
    rt_yieldTo(&conn->c.tmr, triggerHttpRead);
}


void http_ini (http_t* conn, int bufsize) {
    memset(conn, 0, sizeof(*conn));
    mbedtls_net_init(&conn->c.netctx);
    mbedtls_net_init(&conn->listen.netctx);
    rt_iniTimer(&conn->c.tmr, NULL);
    conn->c.state = HTTP_CLOSED;
    conn->c.evcb = conn_evcb_nil;
    conn->c.rbufsize = bufsize;
    conn->c.wbufsize = bufsize;
    conn->c.rbuf = conn->c.wbuf = rt_mallocN(u1_t,bufsize);
}


void http_free (http_t* conn) {
    rt_free(conn->c.rbuf);
    conn->c.rbuf = conn->c.wbuf = NULL;
    ws_free(&conn->c);
    aio_close(conn->listen.aio);
    conn->listen.aio = NULL;
    conn->c.state = HTTP_DEAD;
}


static void _http_close (http_t* conn, tmrcb_t trigCloseEv) {
    rt_clrTimer(&conn->c.tmr);
    LOG(MOD_AIO|DEBUG, "[%d] HTTP connection shutdown...", conn->c.netctx.fd);
    mbedtls_net_free(&conn->c.netctx);
    tls_freeSession(conn->c.tlsctx); conn->c.tlsctx = NULL;
    tls_freeConf(conn->c.tlsconf); conn->c.tlsconf = NULL;
    aio_close(conn->c.aio);
    rt_free((void*)conn->c.authtoken);
    conn->c.authtoken = NULL;
    conn->c.rpos = conn->c.rbeg = conn->c.rend = 0;
    conn->c.wfill = conn->c.wpos = conn->c.wend = 0;
    conn->c.aio = NULL;
    conn->c.state = HTTP_CLOSED;
    if( conn->c.evcb != conn_evcb_nil )
        rt_yieldTo(&conn->c.tmr, trigCloseEv);
}

void http_close (http_t* conn) {
    _http_close(conn, triggerHttpClosedEv);
}


int http_connect (http_t* conn, char* host, char* port) {
    if( conn->c.state != HTTP_CLOSED )
        return 0;  // forgot to http_close?
    rt_clrTimer(&conn->c.tmr);
    mbedtls_net_free(&conn->c.netctx);
    mbedtls_net_init(&conn->c.netctx);

    int ret;
    if( (ret = mbedtls_net_connect(&conn->c.netctx, host, port, MBEDTLS_NET_PROTO_TCP)) != 0 ) {
        log_mbedError(MOD_AIO|ERROR, ret, "[%d] HTTP connect failed", conn->c.netctx.fd);
        http_close(conn);
        return 0;
    }
    if( (ret = mbedtls_net_set_nonblock(&conn->c.netctx)) != 0 ) {
        log_mbedError(MOD_AIO|ERROR, ret, "[%d] Non blocking failed", conn->c.netctx.fd);
        http_close(conn);
        return 0;
    }
    sys_keepAlive(conn->c.netctx.fd);
    if( conn->c.tlsctx )
        mbedtls_ssl_set_bio(conn->c.tlsctx, &conn->c.netctx, mbedtls_net_send, mbedtls_net_recv, NULL);

    conn->c.aio = aio_open(conn, conn->c.netctx.fd, NULL, NULL);
    // NOTE: the first wfill bytes are reserved for host:port
    // We might need this to build Host header line.
    int n = snprintf((char*)conn->c.wbuf, conn->c.wbufsize, "%s:%s", host, port);
    conn->c.wfill = conn->c.rbeg = conn->c.rend = n+1;
    conn->c.state = HTTP_CONNECTED;

    rt_yieldTo(&conn->c.tmr, triggerHttpConnectedEv);
    return 1;
}


dbuf_t http_getReqbuf(http_t* conn) {
    if( conn->c.state != HTTP_CONNECTED ) {
        dbuf_t b = {
            .buf = NULL,
            .bufsize = 0,
            .pos = 0 };
        return b;
    }
    dbuf_t b = {
        .buf = (char*)conn->c.wbuf + conn->c.wfill,
        .bufsize = conn->c.wbufsize - conn->c.wfill,
        .pos = 0 };
    return b;
}

dbuf_t http_getHdr(http_t* conn) {
    if( conn->c.state != HTTP_CONNECTED ||
        (conn->c.state == HTTP_READING_BODY && conn->extra.coff < conn->extra.clen)) {
        dbuf_t b = {
            .buf = NULL,
            .bufsize = 0,
            .pos = 0 };
        return b;
    }
    dbuf_t b = {
        .buf = (char*)conn->c.wbuf + conn->c.wfill,
        .bufsize = conn->c.rbeg - conn->c.wfill,
        .pos = 0 };
    return b;
}

dbuf_t http_getBody(http_t* conn) {
    if( conn->c.state == HTTP_CONNECTED ||
        (conn->c.state == HTTP_READING_BODY && conn->extra.coff < conn->extra.clen)) {
        dbuf_t b = {
            .buf = (char*)conn->c.wbuf + conn->c.rbeg,
            .bufsize = conn->c.rend - conn->c.rbeg,
            .pos = 0 };
        return b;
    }
    dbuf_t b = {
        .buf = NULL,
        .bufsize = 0,
        .pos = 0 };
    return b;
}


int http_getStatus (http_t* conn) {
    return conn->c.creason;
}


int http_getMore (http_t* conn) {  // read more body data
    conn->extra.coff += conn->c.rend - conn->c.rbeg;
    if( conn->extra.coff >= conn->extra.clen )
        return 0; // no more data

    conn->c.rbeg = conn->c.rpos = conn->c.wfill;
    conn->c.rend = conn->c.rbeg + min(conn->extra.clen - conn->extra.coff,
                                      conn->c.rbufsize - conn->c.rbeg);
    aio_set_rdfn(conn->c.aio, http_read);
    rt_yieldTo(&conn->c.tmr, triggerHttpRead);
    return 1;
}


void http_request (http_t* conn, dbuf_t* req) {
    assert(req->pos > 0 && (u1_t*)req->buf == &conn->c.wbuf[conn->c.wfill]);
    conn->c.wend = req->pos + conn->c.wfill;
    conn->c.wpos = conn->c.wfill;
    conn->c.state = HTTP_SENDING_REQ;
    conn->extra.coff = conn->extra.clen = -1;
    aio_set_wrfn(conn->c.aio, http_write);
    http_write(conn->c.aio);
}



// --------------------------------------------------------------------------------
//
// HTTPD stuff
//
// --------------------------------------------------------------------------------









static void httpd_write (aio_t* aio) {
    httpd_t* conn = (httpd_t*)aio->ctx;
    assert(conn->c.state == HTTPD_SENDING_RESP);
    int e = writeData(&conn->c);
    if( e == IO_ERROR ) {
        httpd_close(conn);
        return;
    }
    if( e == IO_WRPEND )
        return;
    assert(e==IO_WRDONE);
    conn->c.wpos = conn->c.wend = conn->c.wfill;
    conn->c.state = HTTPD_CLOSED;
    httpd_close(conn);
}


static void httpd_accept (aio_t* aio) {
    httpd_t* conn = (httpd_t*)aio->ctx;
    netctx_t client_netctx;
    int ret;
    mbedtls_net_init(&client_netctx);
    if( (ret = mbedtls_net_accept( &conn->listen.netctx, &client_netctx, NULL, 0, NULL) ) != 0 ) {
        log_mbedError(MOD_AIO|ERROR, ret, "[%d->%d] Accept failed", conn->listen.netctx.fd, client_netctx.fd);
        return;
    }
    
    if( conn->c.aio != NULL ) {
        LOG(MOD_AIO|WARNING, "[%d->%d] Dropping new connection - busy with [%d]!",
            conn->listen.netctx.fd, client_netctx.fd, conn->c.netctx.fd);
        mbedtls_net_free(&client_netctx);
        return;
    }
    assert(conn->c.state == HTTPD_CLOSED);
    conn->c.netctx = client_netctx;
    conn->c.rpos = conn->c.rbeg = conn->c.rend = 0;
    conn->c.wfill = conn->c.wpos = conn->c.wend = 0;
    conn->extra.coff = conn->extra.clen = -1;
    conn->c.state = HTTPD_READING_HDR;
    conn->c.aio = aio_open(conn, conn->c.netctx.fd, http_read, NULL);
    LOG(MOD_AIO|DEBUG, "[%d->%d] Connection accepted...", conn->listen.netctx.fd, conn->c.netctx.fd);
}


void httpd_response (httpd_t* conn, dbuf_t* req) {
    int wfill = conn->c.wfill;
    assert(req->pos > 0 && (u1_t*)req->buf == &conn->c.wbuf[wfill]);
    conn->c.rpos = conn->c.rbeg = conn->c.rend =
        conn->c.wpos = wfill;
    conn->c.wend = req->pos + wfill;
    conn->c.state = HTTPD_SENDING_RESP;
    aio_set_wrfn(conn->c.aio, httpd_write);
    httpd_write(conn->c.aio);
}


dbuf_t httpd_getRespbuf (httpd_t* conn) {
    return http_getReqbuf(conn);
}

dbuf_t httpd_getHdr (httpd_t* conn) {
    return http_getHdr(conn);
}

dbuf_t httpd_getBody (httpd_t* conn) {
    return http_getBody(conn);
}


void httpd_ini (httpd_t* conn, int bufsize) {
    http_ini(conn, bufsize);
}


void httpd_free (httpd_t* conn) {
    http_free(conn);
}


int httpd_listen (httpd_t* conn, const char* port) {
    if( conn->listen.aio != NULL ||    // forgot to httpd_stop?
        conn->c.tlsctx != NULL )    // TLS not supported for HTTPD
        return 0;
    int ret;
    if( (ret = mbedtls_net_bind(&conn->listen.netctx, NULL, port, MBEDTLS_NET_PROTO_TCP)) != 0 ) {
        log_mbedError(MOD_AIO|ERROR, ret, "[%d] Listen failed", conn->listen.netctx.fd);
      fail:
        mbedtls_net_free(&conn->listen.netctx);
        return 0;
    }
    if( (ret = mbedtls_net_set_nonblock(&conn->listen.netctx)) != 0 ) {
        log_mbedError(MOD_AIO|ERROR, ret, "[%d] Non blocking failed", conn->listen.netctx.fd);
        goto fail;
    }
    conn->listen.aio = aio_open(conn, conn->listen.netctx.fd, httpd_accept, NULL);
    conn->c.wfill = conn->c.rbeg = conn->c.rend = 0;  // getHdr/Body return empty buffers
    conn->c.state = HTTPD_CLOSED;
    LOG(MOD_AIO|DEBUG, "[%d] Connection listening...", conn->listen.netctx.fd);
    return 1;
}

void httpd_stop (httpd_t* conn) {
    aio_close(conn->listen.aio);
    conn->listen.aio = NULL;
    mbedtls_net_free(&conn->listen.netctx);
    httpd_close(conn);
}


static void triggerHttpdClosedEv(tmr_t* tmr) {
    httpd_t* conn = tmr2httpd(tmr);
    conn->c.evcb(&conn->c, HTTPDEV_CLOSED);
}


void httpd_close (httpd_t* conn) {
    _http_close(conn, triggerHttpdClosedEv);
}

int httpd_parseReqLine (httpd_pstate_t* pstate, dbuf_t* hdr) {
    memset(pstate, 0, sizeof(*pstate));
    pstate->method = -1;
    int n = hdr->bufsize;
    for( int i=0; i<n; i++ ) {
        char* hc = &hdr->buf[i];
        int c = *hc;
        if( c == '\n' ) {
            LOG(MOD_AIO|ERROR, "Failed to parse HTTP req line: %.*s", hc-hdr->buf, hdr->buf);
            return 0; // parsing failed - we should not be here
        }
        if( c == ' ' ) {
            *hc = 0;
            if( pstate->method == -1 ) {
                pstate->meth = &hdr->buf[0];
                pstate->path = hc+1;

                if( strcasecmp(pstate->meth, "GET") == 0 ) {
                    pstate->method = HTTP_GET;
                }
                else if( strcasecmp(pstate->meth, "POST") == 0 ) {
                    pstate->method = HTTP_POST;
                }
                else {
                    pstate->method = HTTP_OTHER_METHOD;
                }
                continue;
            }
            // End of path element / start of protocol label
            const char* s = hc+1;
            int major = -1, minor = -1;
            if( strncasecmp(s, "http/", 5) == 0 ) {
                s += 5;
                major = rt_readDec(&s);
                if( s[0] == '.' ) {
                    s += 1;
                    minor = rt_readDec(&s);
                }
            }
            if( major < 0 || minor < 0 || (s[0] != '\r' && s[0] != '\n') ) {
                LOG(MOD_AIO|ERROR, "Failed to parse HTTP version: %.*s", 10, hc+1);
                return 0;  // parsing failed
            }
            pstate->httpVersion = major*1000 + minor;
            break;
        }
        if( pstate->method >= 0 ) {
            if( c == '?' && (!pstate->query && !pstate->fragment) ) {
                pstate->query = hc+1;
            }
            else if( c == '#' && !pstate->fragment ) {
                pstate->fragment = hc+1;
            }
        }
    }
    // Dequote path - kill any /./ and /../
    char *rp, *wp;
    rp = wp = pstate->path;
    if( rp[0] != '/' )
        return 0;
    u4_t c, hist=0;
    while( 1 ) {
        c = *wp++ = http_unquote(&rp);
        hist = (hist << 8) | c;
        if( (hist & 0xFFFF) == 0x2F2F ||   /* // */
            (hist & 0xFFFF) == 0x2F00 ) {  /* /\0 */
            hist >>= 8;
            wp -= 1;
        }
        else if( (hist & 0xFFFFFF) == 0x2F2E2F ||   /* /./ */
                 (hist & 0xFFFFFF) == 0x2F2E00) {   /* /.\0 */
            hist >>= 16;
            wp -= 2;
        }
        else if( hist == 0x2F2E2E2F ||    /* /../ */
                 hist == 0x2F2E2E00 ) {   /* /..\0 */
            wp -= 4;
            while( wp > pstate->path && wp[-1] != '/' )
                wp -= 1;
        }
        else {
            if ( c != 0 && c != '/') {
                pstate->pathcrc = UJ_UPDATE_CRC(pstate->pathcrc,c);
            }
        }
        if( c == 0 )
            break;

    }
    wp[-1] = 0;
    for( rp = wp-2; rp >= pstate->path && rp[0] != '/'; rp-- ) {
        if( rp[0] == '.' ) {
            pstate->suffix = rp+1;
            break;
        }
    }
    pstate->contentType = "application/octet-stream";
    if( pstate->suffix ) {
        for( str_t const *map=SUFFIX2CT; *map; map+=2 ) {
            if( strcmp(pstate->suffix, map[0]) == 0 ) {
                pstate->contentType = map[1];
                break;
            }
        }
    }
    return 1;
}


static str_t validateAuthToken (str_t s) {
    int l = strlen(s);
    // Trim empty lines from the end (either \r\n or \n)
    while( l>0 && s[l-1]=='\n' ) {
        l -= l>1 && s[l-2]=='\r' ? 2 : 1;
    }
    // Count \n without preceeding \r
    int extra = 0;
    for( int i = l-1; i>=0; i-- ) {
        if( s[i] == '\n' && (i==0 || s[i-1]=='\r') )
            extra += 1;
    }
    char* w = rt_mallocN(char, l+extra+2+1);
    if( l==0 ) {
        return w;
    }
    int i=0, j=0;
    while( i<l ) {
        int c, fi=i;
        while( ((c = s[i++]) >= 'a' && c <= 'z') || (c >= 'A' && c<= 'Z') || (c >= '0' && c<='9') || c=='-' || c=='_' ) {
            w[j++] = c;
        }
        if( i==fi+1 || c != ':' || s[i] != ' ' ) {
            rt_free(w);
            return NULL;  // field name *must* be followed by COLON SPACE
        }
        for( i -= 1; i<l && c!='\n'; i++ ) {
            c = s[i];
            if( c == '\n' && s[i-1] != '\r' )
                w[j++] = '\r';
            w[j++] = c;
        }
    }
    w[j++] = '\r';
    w[j++] = '\n';
    return w;
}


int conn_setup_tls (conn_t* conn, int cred_cat, int cred_set, const char* servername) {
    tlsconf_t* tlsconf = tls_makeConf();
    str_t elems[SYS_CRED_NELEMS];
    int elemslen[SYS_CRED_NELEMS];
    int auth = sys_cred(cred_cat, cred_set, elems, elemslen);    // get pointers to files or cert data
    str_t errmsg = "";
    if( auth == SYS_AUTH_NONE ) {
        errmsg = "%s URI requires TLS but no trust configured";
        goto errexit;
    }
    if( !tls_setTrustedCAs(tlsconf, elems[SYS_CRED_TRUST], elemslen[SYS_CRED_TRUST]) ) {
        errmsg = "%s%s trust certificates rejected by MBedTLS";
        goto errexit;
    }
    if( auth == SYS_AUTH_TOKEN ) {
        errmsg = "%s%s has no cert configured - running server auth and client auth with token";
        dbuf_t dbuf = sys_readFile(elems[SYS_CRED_MYKEY]);
        if( dbuf.buf == NULL ) {
            errmsg = "%s%s has unreadable client auth token";
            goto errexit;
        }
        conn->authtoken = validateAuthToken(dbuf.buf);
        rt_free(dbuf.buf);
        if( !conn->authtoken ) {
            errmsg = "%s%s contains malformed auth token - expecting: {header: value{\\r\\n|\\n}}*";
            goto errexit;
        }
    }
    else if( auth == SYS_AUTH_SERVER ) {
        errmsg = "%s%s has no key+cert configured - running server auth only";
        LOG(MOD_AIO|INFO, errmsg, sys_credcat2str(cred_cat), sys_credset2str(cred_set));
    }
    else if( !tls_setMyCert(tlsconf,
                            elems[SYS_CRED_MYCERT], elemslen[SYS_CRED_MYCERT],
                            elems[SYS_CRED_MYKEY ], elemslen[SYS_CRED_MYKEY ], NULL) ) {
        errmsg = "%s%s key/cert rejected by MBedTLS";
        goto errexit;
    }
    assert(conn->tlsconf==NULL && conn->tlsctx==NULL);
    conn->tlsconf = tlsconf;
    conn->tlsctx = tls_makeSession(tlsconf, servername);
    return 1;
 errexit:
    LOG(MOD_AIO|ERROR, errmsg, sys_credcat2str(cred_cat), sys_credset2str(cred_set));
    tls_freeConf(tlsconf);
    return 0;
}

