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

#ifndef _http_h_
#define _http_h_

#include "net.h"

extern str_t const SUFFIX2CT[];

#define conn2http(p) memberof(http_t, p, c)
#define tmr2http(p) memberof(http_t, p, c.tmr)

typedef struct http {
    conn_t c;
    struct {
        int clen;  // content length
        int coff;  // content offset
    } extra;

    // HTTPD mode only
    struct {
        netctx_t netctx;
        aio_t*   aio;
    } listen;
} http_t;

enum {
    HTTP_DEAD = 0,
    HTTP_CONNECTED,    // just connected or response received
    HTTP_CLOSED,       // not connected to server
    HTTP_SENDING_REQ,
    HTTP_READING_HDR,
    HTTP_READING_BODY,
};

enum {
    HTTPEV_DEAD = 0,
    HTTPEV_CLOSED,        // HTTP connection closed
    HTTPEV_CONNECTED,     // connected to server
    HTTPEV_RESPONSE,      // received a response (hdr + body - maybe partially) - start processing it
    HTTPEV_RESPONSE_MORE, // received more data from body (if large)
};

void   http_ini       (http_t*, int bufsize);
void   http_free      (http_t*);
int    http_connect   (http_t*, char* host, char* port);
void   http_close     (http_t*);
void   http_request   (http_t*, dbuf_t* req);
int    http_getMore   (http_t*);  // read more body data
dbuf_t http_getReqbuf (http_t*);
dbuf_t http_getHdr    (http_t*);
dbuf_t http_getBody   (http_t*);
int    http_getStatus (http_t*);


// Some functions facilitating simple HTTP parsing:
char*  http_skipWsp    (char* p);
int    http_readDec    (char* p);
int    http_statusCode (char* p);
int    http_icaseCmp   (const char* p, const char* what);
char*  http_findHeader (char* p, const char* field);
int    http_findContentLength (char* p);
int    http_setContentLength  (char* p, int clen);
dbuf_t http_statusText (dbuf_t* hdr);
int    http_unquote    (char** p);


#endif // _http_h_
