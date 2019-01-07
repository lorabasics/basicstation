// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _tc_h_
#define _tc_h_

#include "ws.h"
#include "s2e.h"


enum {
    TC_INI            = 0,
    TC_INFOS_REQ_PEND    ,
    TC_INFOS_GOT_URI     ,
    TC_MUXS_REQ_PEND     ,
    TC_MUXS_CONNECTED    ,
    TC_MUXS_BACKOFF      ,
    TC_INFOS_BACKOFF     ,

    TC_ERR_FAILED        = -1,
    TC_ERR_NOURI         = -2,
    TC_ERR_TIMEOUT       = -3,
    TC_ERR_REJECTED      = -4,  // infos/muxs send back an error
    TC_ERR_CLOSED        = -5,
    TC_ERR_DEAD          = -6,
};

typedef struct tc {
    ws_t     ws;          // WS connection state
    tmr_t    timeout;
    s1_t     tstate;      // state of TC engine
    u1_t     credset;     // connect via this credential set
    u1_t     retries;
    char     muxsuri[MAX_URI_LEN+3];
    tmrcb_t  ondone;
    s2ctx_t  s2ctx;
} tc_t;


#define timeout2tc(p) memberof(tc_t, p, timeout)
#define conn2tc(p)    memberof(tc_t, p, ws)
#define s2ctx2tc(p)   memberof(tc_t, p, s2ctx)

extern tc_t* TC;

tc_t*  tc_ini   (tmrcb_t ondone);
void   tc_free  (tc_t* tc);
void   tc_start (tc_t* tc);
void   tc_ondone_default (tmr_t* timeout);
void   tc_continue (tc_t* tc);

#endif // _tc_h_
