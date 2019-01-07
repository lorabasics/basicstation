// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

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
