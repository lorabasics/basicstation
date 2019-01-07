// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _net_h_
#define _net_h_

#include "mbedtls/net_sockets.h"
#include "rt.h"
#include "tls.h"

struct conn;
typedef void (*evcb_t)(struct conn*, int ev);
typedef mbedtls_net_context netctx_t;

typedef struct conn {
    aio_t*   aio;
    tmr_t    tmr;
    // Read side
    u1_t*    rbuf;
    doff_t   rbufsize;
    doff_t   rpos;     // socket fills in data here
    doff_t   rbeg;     // oldest frame in recv buffer, rbeg[-1] is OPCODE
    doff_t   rend;     // end of frame, after that starts a WS header
    // Write side
    u1_t*    wbuf;
    doff_t   wbufsize;
    doff_t   wpos;     // socket reads data from here and sends it
    doff_t   wend;     // end of WS frame, after that 2 bytes frame length + frame data
    doff_t   wfill;    // local producers fill in data here

    u1_t     state;
    s1_t     optemp;   // some temp value related to opctx
    u2_t     creason;  // close reason
    evcb_t   evcb;

    netctx_t   netctx;
    tlsctx_p   tlsctx;
    tlsconf_t* tlsconf;   // or NULL if shared and stored someplace else
    str_t      authtoken;

    void*      opctx;     // context managing this connection

    char* host;
    char* port;
    char* uripath;
} conn_t;


// conn_t.evcb must not be NULL - use this
void conn_evcb_nil(conn_t* conn, int wsev);
int  conn_setup_tls (conn_t* conn, int cred_cat, int cred_set);


struct uri_info {
    doff_t schemeEnd;
    doff_t hostportBeg;
    doff_t hostportEnd;
    doff_t hostBeg;
    doff_t hostEnd;
    doff_t portBeg;
    doff_t portEnd;
    doff_t pathBeg;
    doff_t pathEnd;
};

int uri_isScheme (const char* uri, const char* scheme);
int uri_parse    (dbuf_t* b, struct uri_info* u, int skipSchema);
enum { URI_BAD=0, URI_TCP, URI_TLS };
int uri_checkHostPortUri (const char* uri,
                          const char* scheme,
                          char* host, int hostlen,
                          char* port, int portlen);

#endif // _net_h_
