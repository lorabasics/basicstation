// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _web_h_
#define _web_h_

#include "httpd.h"

#define WEB_PORT "8080"

enum {
    WEB_INI            = 0,

    WEB_ERR_FAILED        = -1,
    WEB_ERR_TIMEOUT       = -3,
    WEB_ERR_REJECTED      = -4,
    WEB_ERR_CLOSED        = -5,
    WEB_ERR_DEAD          = -6,
};


typedef struct web {
    httpd_t   hd;          // HTTPD connection state
    tmr_t     timeout;
    s1_t      wstate;      // state of web
} web_t;

typedef struct {
    ujcrc_t pathcrc;
    int (*f)(httpd_pstate_t* pstate, httpd_t* hd, dbuf_t* buf);
} web_handler_t;

extern const web_handler_t SYS_HANDLERS[];
extern web_handler_t AUTH_HANDLERS[];

void web_authini();

dbuf_t sys_webFile (str_t filename);

#define timeout2web(p) memberof(web_t, p, timeout)
#define conn2web(p)    memberof(web_t, p, hd.c)

#endif // _web_h_
