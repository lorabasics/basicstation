// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _ws_h_
#define _ws_h_

#include "net.h"

typedef conn_t ws_t;

#define conn2ws(p) (p)
#define tmr2ws(p) memberof(ws_t, p, tmr)

// Websocket states
enum {
    WS_DEAD = 0,
    WS_TLS_HANDSHAKE,
    WS_CLIENT_REQ,
    WS_SERVER_RESP,
    WS_CONNECTED,
    WS_CLOSING_DRAINC,     // client initiated
    WS_CLOSING_DRAINS,     // server initiated
    WS_CLOSING_SENDCLOSE,
    WS_CLOSING_ECHOCLOSE,
    WS_CLOSING_SENTCLOSE,
    WS_CLOSED
};


// Events reported via evcb
enum {
    WSEV_DEAD = 0,
    WSEV_CLOSED,
    WSEV_DATASENT,
    WSEV_BINARYRCVD,
    WSEV_TEXTRCVD,
    WSEV_CONNECTED,
};


void   ws_shutdown   (ws_t*);                   // immediately close (=> ws_connect)
void   ws_close      (ws_t*, int reason);       // initialte close protocol => WSEV_CLOSED
dbuf_t ws_getRecvbuf (ws_t*);
dbuf_t ws_getSendbuf (ws_t*, int minsize);
void   ws_sendData   (ws_t*, dbuf_t* b, int binaryData); // send b obtained from get_sendbuf + b.pos indicates size
void   ws_sendText   (ws_t*, dbuf_t* b);
void   ws_sendBinary (ws_t*, dbuf_t* b);
void   ws_ini        (ws_t*, int rbufsize, int wbufsize);
void   ws_free       (ws_t*);                   // free all resources (=> ws_ini)
int    ws_connect    (ws_t*, char* host, char* port, char* uripath);

int    ws_getRtt     (ws_t*, u2_t* q_80_90_95); // round trip quantiles 80/90/95% in millis

#endif // _ws_h_
