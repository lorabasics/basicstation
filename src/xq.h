// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _xq_h_
#define _xq_h_

#include "rt.h"
#include "s2conf.h"

typedef u2_t txoff_t;
typedef u1_t txidx_t;

enum { TXIDX_NIL = 255 };
enum { TXIDX_END = 254 };
enum { TXOFF_NIL = 0xFFFF };

typedef struct txjob {
    ustime_t txtime;
    uL_t     deveui;
    sL_t     diid;     // device interaction ID (was 'seqno')
    sL_t     rctx;
    sL_t     gpstime;
    sL_t     xtime;
    u4_t     freq;
    u4_t     rx2freq;
    u4_t     airtime;
    txidx_t  next;     // next index in txjobs or TXIDX_END, if not q'd TXIDX_NIL
    txoff_t  off;      // frame start in txdata or TXOFF_NIL if none
    s2_t     txpow;    // (scaled by TXPOW_SCALE)
    u1_t     txunit;   // currently queued for this TX path
    u1_t     altAnts;  // alternate antennas
    u1_t     txflags;  // see TXFLAGS_* in s2e.h
    u1_t     retries;  // class C: TX attempts
    u1_t     dr;
    u1_t     rx2dr;
    u1_t     rxdelay;
    u1_t     len;     // frame length
    u1_t     prio;    // priority
    u1_t     dnchnl;  // channel number (internal use only - for DC tracking)
    u1_t     dnchnl2; //   -ditto- RX2
} txjob_t;

typedef struct txq {
    txjob_t txjobs[MAX_TXJOBS];  // pool of txjobs
    u1_t    txdata[MAX_TXDATA];  // pool for pending txdata
    txidx_t freeJobs;            // linked list of free txjob elements
    txoff_t txdataInUse;         // free buffer space from here to end of txdata
} txq_t;


void     txq_ini      (txq_t* txq);
txidx_t  txq_job2idx  (txq_t* txq, txjob_t* j);
txjob_t* txq_idx2job  (txq_t* txq, txidx_t  i);
txjob_t* txq_nextJob  (txq_t* txq, txjob_t* j);
txidx_t* txq_nextIdx  (txq_t* txq, txidx_t* pi);
txjob_t* txq_unqJob   (txq_t* txq, txidx_t* pi);
void     txq_insJob   (txq_t* txq, txidx_t* pi, txjob_t* j);
void     txq_freeJob  (txq_t* txq, txjob_t* j);
void     txq_freeData (txq_t* txq, txjob_t* j);
txjob_t* txq_reserveJob  (txq_t* txq);
u1_t*    txq_reserveData (txq_t* txq, txoff_t maxlen);
void     txq_commitJob   (txq_t* txq, txjob_t*j);


typedef u2_t rxoff_t;

typedef struct rxjob {
    sL_t     rctx;
    sL_t     xtime;
    u4_t     freq;
    rxoff_t  off;    // frame start in rxdata
    u1_t     rssi;   // scaled RSSI (*-1)
    s1_t     snr;    // scaled SNR (*8)
    u1_t     dr;
    u1_t     len;    // frame end
} rxjob_t;

typedef struct rxq {
    rxjob_t rxjobs[MAX_RXJOBS];
    u1_t    rxdata[MAX_RXDATA];
    u1_t    first;   // first filled job
    u1_t    next;    // next job to fill
} rxq_t;


void     rxq_ini       (rxq_t* rxq);
rxjob_t* rxq_nextJob   (rxq_t* rxq);
void     rxq_commitJob (rxq_t* rxq, rxjob_t* p);
rxjob_t* rxq_dropJob   (rxq_t* rxq, rxjob_t* p);


#endif // _xq_h_
