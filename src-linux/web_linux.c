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

#include "sys.h"
#include "web.h"
#include "uj.h"
#include "kwcrc.h"
#include "s2conf.h"

static int handle_config_GET(httpd_pstate_t* pstate, httpd_t* hd, dbuf_t* b) {
    b->buf = _rt_malloc(2048,0);
    b->bufsize = 2048;
    uj_encOpen(b, '{');
    uj_encKey(b, "config");
    uj_encOpen(b, '[');

    for( struct conf_param* p = conf_params; p->name; p++ ) {
        uj_encOpen(b, '{');
        uj_encKV(b, "type",  's', p->type);
        uj_encKV(b, "name",  's', p->name);
        uj_encKV(b, "value", 's', p->value);
        uj_encKV(b, "src",   's', p->src);
        uj_encClose(b, '}');
    }
    uj_encClose(b, ']');
    uj_encClose(b, '}');

    pstate->contentType = "application/json";
    b->bufsize = b->pos;
    return 200;
}


static int handle_config(httpd_pstate_t* pstate, httpd_t* hd, dbuf_t* b) {
    if ( pstate->method == HTTP_GET )
        return handle_config_GET(pstate,hd,b);

    return 405; // Method not allowed
}

const web_handler_t SYS_HANDLERS[] = {
    { J_config,  handle_config  },
    { 0,         NULL           },
};

void web_authini() {}

web_handler_t AUTH_HANDLERS[] = {{ 0, NULL }};
