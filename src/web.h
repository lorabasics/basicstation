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
