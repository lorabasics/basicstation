// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

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
