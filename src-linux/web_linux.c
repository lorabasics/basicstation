// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

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
