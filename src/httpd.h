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

#ifndef _httpd_h_
#define _httpd_h_

#include "net.h"
#include "http.h"
#include "uj.h"

#define conn2httpd(p) memberof(httpd_t, p, c)
#define tmr2httpd(p) memberof(httpd_t, p, c.tmr)

typedef http_t httpd_t;

enum {
    // keep in sync with http - we can share code
    HTTPD_DEAD         = HTTP_DEAD,
    HTTPD_CONNECTED    = HTTP_CONNECTED,   // just connected / or request is in - answer it
    HTTPD_CLOSED       = HTTP_CLOSED,      // no client connected
    HTTPD_SENDING_RESP = HTTP_SENDING_REQ,
    HTTPD_READING_HDR  = HTTP_READING_HDR,
    HTTPD_READING_BODY = HTTP_READING_BODY,
};

enum {
    // keep in sync with http - we can share code
    HTTPDEV_DEAD      = HTTPEV_DEAD,
    HTTPDEV_CLOSED    = HTTPEV_CLOSED,     // HTTP connection is closed
    HTTPDEV_REQUEST   = HTTPEV_RESPONSE,   // received a request - start preparing a response
};

void   httpd_ini        (httpd_t*, int bufsize);
void   httpd_free       (httpd_t*);
int    httpd_listen     (httpd_t*, const char* port);
void   httpd_close      (httpd_t*);
void   httpd_stop       (httpd_t*);
dbuf_t httpd_getRespbuf (httpd_t*);
dbuf_t httpd_getHdr     (httpd_t*);
dbuf_t httpd_getBody    (httpd_t*);
void   httpd_response   (httpd_t*, dbuf_t* resp);

enum {
    HTTPD_PATH_DONE,
    HTTPD_PATH_ROOT,
    HTTPD_PATH_ELEM,
    HTTPD_PATH_LAST,
    HTTPD_PARAM_ELEM,
    HTTPD_QUERY_ELEM,
};

typedef struct httpd_pstate {
    char*  meth;
    char*  path;
    char*  suffix;
    char*  query;
    char*  fragment;
    str_t  contentType;
    str_t  contentEnc;
    int    httpVersion;
    int    method;
    ujcrc_t  pathcrc;
} httpd_pstate_t;

// methods returned by httpd_iniParseReqLine
enum { HTTP_OTHER_METHOD, HTTP_GET, HTTP_POST };
// versions are returned by httpd_iniParseReqLine
enum { HTTP_x_x = 0 , HTTP_1_0 = 1000, HTTT_1_1 = 1001 };

int httpd_parseReqLine (httpd_pstate_t* pstate, dbuf_t* hdr);

#endif // _httpd_h_
