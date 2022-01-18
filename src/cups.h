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
