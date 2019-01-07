// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _cups_h_
#define _cups_h_

#include "http.h"

enum {
    CUPS_INI            = 0,
    CUPS_HTTP_REQ_PEND     ,
    CUPS_FEED_CUPS_URI     ,  // never used - both expected to fit into HTTP buffer
    CUPS_FEED_TC_URI       ,  //  ditto       together with HTTP header
    CUPS_FEED_CUPS_CRED    ,
    CUPS_FEED_TC_CRED      ,
    CUPS_FEED_SIGNATURE    ,
    CUPS_FEED_UPDATE       ,
    CUPS_DONE              ,

    CUPS_ERR_FAILED        = -1,
    CUPS_ERR_NOURI         = -2,
    CUPS_ERR_TIMEOUT       = -3,
    CUPS_ERR_REJECTED      = -4,
    CUPS_ERR_CLOSED        = -5,
    CUPS_ERR_DEAD          = -6,
};

#define UPDATE_FLAG(n) (1 << (CUPS_FEED_##n - CUPS_FEED_CUPS_URI))

typedef struct cups_sig cups_sig_t;

typedef struct cups {
    http_t   hc;          // HTTP connection state
    tmr_t    timeout;
    s1_t     cstate;      // state of CUPS engine
    u1_t     uflags;      // update flags - which parts have been updated
    u1_t     temp_n;
    u1_t     temp[4];     // assemble length fields
    int      segm_off;
    int      segm_len;
    tmrcb_t  ondone;
    cups_sig_t* sig;
} cups_t;

#define timeout2cups(p) memberof(cups_t, p, timeout)
#define conn2cups(p)    memberof(cups_t, p, hc.c)

cups_t* cups_ini ();
void    cups_free (cups_t* cups);
void    cups_start (cups_t* cups);


#endif // _cups_h_
