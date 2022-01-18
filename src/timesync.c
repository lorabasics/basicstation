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

#include <stdlib.h>
#include <math.h>
#include "s2conf.h"
#include "tc.h"
#include "timesync.h"
#include "ral.h"

#if defined(CFG_smtcpico)
#define _MAX_DT 300
#else
#define _MAX_DT 100
#endif

#define SYNC_QUAL_GOOD        100  // values considered good
#define SYNC_QUAL_THRES        90  // cut off quantile - for sync quality
#define N_SYNC_QUAL            30  // size of sync quality table
#define MCU_DRIFT_THRES        90  // cut off quantile - for MCU sync quality
#define PPS_DRIFT_THRES        80  // cut off quantile - for PPS sync quality
#define N_DRIFTS               20  // size of quantile table MCU/PPS
#define QUICK_RETRIES           3
#define PPM       ((sL_t)1000000)  // 1sec in micros
#define iPPM_SCALE             10  // keep drifts in deci ppm as ints
#define fPPM_SCALE           10.0  //   -double-
#define MIN_MCU_DRIFT_THRES         2*iPPM_SCALE   // deviation in deci ppm
#define MAX_MCU_DRIFT_THRES   _MAX_DT*iPPM_SCALE   // deviation in deci ppm
#define MAX_PPS_ERROR         1000  // deviation in micros
#define MAX_PPS_OFFSET_CHANGE  50  // update if more than this
#define NO_PPS_ALARM_INI       10  // seconds
#define NO_PPS_ALARM_RATE     2.0  // growth rate alarm threshold
#define NO_PPS_ALARM_MAX     3600  // seconds
#define XTICKS_DECAY       100000  // max age of xticks from FIFO (us)
#define UTC_GPS_EPOCH_US 315964800 // UTC epoch expressed in s since GPS epoch

#define ustimeRoundSecs(x) (((x) + PPM/2) / PPM * PPM)
#define ustime2xtime(sync, _ustime) ((sync)->xtime + ((_ustime)-(sync)->ustime))
#define xtime2ustime(sync, _xtime)  ((sync)->ustime + ((_xtime)-(sync)->xtime))
#define xtime2xtime(src_sync, dst_sync, _xtime)                 \
    (((dst_sync)->xtime - (src_sync)->xtime) +                  \
     ((src_sync)->ustime - (dst_sync)->ustime) + (_xtime))


struct quants {
    int qmin, q50, q80, qmax;
};

static struct txunit_stats {
    int excessive_drift_cnt;
    int drift_thres;   // drift threshold (MCU_DRIFT_THRES quantile)
    int mcu_drifts[N_DRIFTS];
    int mcu_drifts_widx;
} txunit_stats[MAX_TXUNITS];
static int         sum_mcu_drifts;    // sum of txunit_stats[0].mcu_drifts

static int         pps_drifts[N_DRIFTS];
static int         pps_drifts_widx;
static int         pps_drifts_thres; // drift threshold (PPS_DRIFT_THRES quantile)
static u4_t        no_pps_thres;     // when to issue next error
static ustime_t    ppsOffset;        // denotes where the PPS occurs on ustime_t, -1: unknown, otherwise 0..1e6-1
static sL_t        gpsOffset;        // add to go from gpstime to xtime
static int         syncLnsCnt;       // count timesync tries with LNS, 0=not trying
static tmr_t       syncLnsTmr;       // sync with server time
static ustime_t    lastReport;       // report time sync status regularly
static timesync_t  timesyncs[MAX_TXUNITS];
static timesync_t  ppsSync;          // last good PPS sync
static s1_t        syncWobble;
static u1_t        wsBufFull;
static int         syncQual[N_SYNC_QUAL];
static int         syncQual_widx;
static int         syncQual_thres;  // current threshold

// Fwd decl
static void onTimesyncLns (tmr_t* tmr);


static void timesyncReport (int force) {
    ustime_t now = rt_getTime();
    if( !force && now < lastReport + TIMESYNC_REPORTS )
        return;
    lastReport = now;
    uL_t pps_ustime = timesyncs[0].pps_xtime != 0 ? xtime2ustime(&timesyncs[0], timesyncs[0].pps_xtime) : 0;
    LOG(MOD_SYN|INFO, "Time sync: NOW          ustime=0x%012lX utc=0x%lX gpsOffset=0x%lX ppsOffset=%ld syncQual=%d\n",
        now, rt_ustime2utc(now),  gpsOffset, ppsOffset, syncQual[0]);
    LOG(MOD_SYN|INFO, "Time sync: MCU/SX130X#0 ustime=0x%012lX xtime=0x%lX pps_ustime=0x%lX pps_xtime=0x%lX",
        timesyncs[0].ustime, timesyncs[0].xtime, pps_ustime, timesyncs[0].pps_xtime);
    if( !ppsOffset )
        return;
    pps_ustime = xtime2ustime(&timesyncs[0], ppsSync.pps_xtime);
    LOG(MOD_SYN|INFO, "Time sync: Last PPS     ustime=0x%012lX xtime=0x%lX pps_ustime=0x%lX pps_xtime=0x%lX",
        ppsSync.ustime, ppsSync.xtime, pps_ustime, ppsSync.pps_xtime);
    if( !gpsOffset )
        return;
    LOG(MOD_SYN|INFO, "Time ref:  Last PPS     sys->UTC=%>.6T  SX130X->GPS=%>.6T  leaps=%02lus diff=%~T",
        rt_ustime2utc(pps_ustime), ts_xtime2gpstime(ppsSync.pps_xtime) + UTC_GPS_EPOCH_US*PPM,
        (ts_xtime2gpstime(ppsSync.pps_xtime) + UTC_GPS_EPOCH_US*PPM - rt_ustime2utc(pps_ustime) + PPM/2)/PPM,
        (ts_xtime2gpstime(ppsSync.pps_xtime) + UTC_GPS_EPOCH_US*PPM - rt_ustime2utc(pps_ustime) + PPM/2)%PPM - PPM/2 );
}


static int encodeDriftPPM (double drift) {
    return (int)round((drift - 1.0) * PPM * iPPM_SCALE);
}

static double decodeDriftPPM (double scaled_ppm) {
    return 1.0 + scaled_ppm / (PPM * fPPM_SCALE);
}

static double decodePPM (double scaled_ppm) {
    return scaled_ppm / fPPM_SCALE;
}

static int cmp_abs_int (const void* a, const void* b) {
    return abs(*(int*)a) - abs(*(int*)b);
}

static int drift_stats (int* drifts, struct quants *q, int thresQ, int* auxQ) {
    int sorted_drifts[N_DRIFTS];
    memcpy(sorted_drifts, drifts, sizeof(sorted_drifts));
    qsort(sorted_drifts,N_DRIFTS,sizeof(sorted_drifts[0]), cmp_abs_int);
    q->qmin = sorted_drifts[0];
    q->q50  = sorted_drifts[N_DRIFTS/2];
    q->q80  = sorted_drifts[(N_DRIFTS*80+50)/100];
    q->qmax = sorted_drifts[N_DRIFTS-1];
    if( auxQ )
        *auxQ = sorted_drifts[(*auxQ*N_DRIFTS+50)/100];
    return sorted_drifts[(thresQ*N_DRIFTS+50)/100];
}

static int log_drift_stats (str_t msg, int* drifts, int thresQ, int* auxQ) {
    struct quants q;
    int thres = drift_stats(drifts, &q, thresQ, auxQ);
    LOG(MOD_SYN|INFO, "%s: min: %+4.1fppm  q50: %+4.1fppm  q80: %+4.1fppm  max: %+4.1fppm - threshold q%d: %+4.1fppm",
        msg,
        q.qmin / fPPM_SCALE, q.q50 / fPPM_SCALE, q.q80 / fPPM_SCALE, q.qmax / fPPM_SCALE,
        thresQ, thres / fPPM_SCALE);
    return thres;
}


ustime_t ts_normalizeTimespanMCU (ustime_t timespan) {
    
    
    return (ustime_t)round(timespan / decodeDriftPPM((double)sum_mcu_drifts / N_DRIFTS));
    return (ustime_t)round( timespan / (1.0 + sum_mcu_drifts / (PPM*iPPM_SCALE) / N_DRIFTS) );
}

ustime_t ts_updateTimesync (u1_t txunit, int quality, const timesync_t* curr) {
    syncQual[syncQual_widx] = quality;
    syncQual_widx = (syncQual_widx + 1) % N_SYNC_QUAL;
    if( syncQual_widx == 0 ) {
        int sorted_qual[N_SYNC_QUAL];
        memcpy(sorted_qual, syncQual, sizeof(sorted_qual));
        qsort(sorted_qual,N_SYNC_QUAL,sizeof(sorted_qual[0]),cmp_abs_int);
        int thres = sorted_qual[(N_SYNC_QUAL*SYNC_QUAL_THRES+50)/100];
        LOG(MOD_SYN|INFO, "Time sync qualities: min=%d q%d=%d max=%d (previous q%d=%d)",
            sorted_qual[0], SYNC_QUAL_THRES, thres, sorted_qual[N_SYNC_QUAL-1], SYNC_QUAL_THRES, syncQual_thres);
        syncQual_thres = max(SYNC_QUAL_GOOD, abs(thres));
    }
    if( abs(quality) > syncQual_thres ) {
        LOG(MOD_SYN|VERBOSE, "Time sync rejected: quality=%d threshold=%d", quality, syncQual_thres);
        return TIMESYNC_RADIO_INTV;
    }

    timesync_t* last = &timesyncs[txunit];
    if( last->ustime == 0 ) {
        // Very first call - just setup last
        *last = *curr;
        return TIMESYNC_RADIO_INTV;
    }
    ustime_t dus = curr->ustime - last->ustime;
    sL_t dxc = curr->xtime - last->xtime;
    if( dxc <= 0 ) {
        LOG(MOD_SYN|ERROR, "SX130X#%d trigger count not ticking or weird value: 0x%lX .. 0x%lX (dxc=%d)",
            txunit, last->xtime, curr->xtime, dxc);
        return TIMESYNC_RADIO_INTV;
    }
    if( dus < TIMESYNC_RADIO_INTV/5 ) {
        // Don't consider if measurements too close together
        return TIMESYNC_RADIO_INTV;
    }
    struct txunit_stats* stats = &txunit_stats[txunit];
    int drift_ppm = encodeDriftPPM( (double)dus/(double)dxc );
    if( txunit == 0 )
        sum_mcu_drifts += drift_ppm - stats->mcu_drifts[stats->mcu_drifts_widx];
    stats->mcu_drifts[stats->mcu_drifts_widx] = drift_ppm;
    stats->mcu_drifts_widx = (stats->mcu_drifts_widx + 1) % N_DRIFTS;
    if( stats->mcu_drifts_widx == 0 ) {
        
        
        
        int thres = log_drift_stats("MCU/SX130X drift stats", stats->mcu_drifts, MCU_DRIFT_THRES, NULL);
        stats->drift_thres = max(MIN_MCU_DRIFT_THRES, min(MAX_MCU_DRIFT_THRES, abs(thres)));
        double mean_ppm = decodePPM( ((double)sum_mcu_drifts) / N_DRIFTS);
        LOG(MOD_SYN|INFO, "Mean MCU drift vs SX130X#0: %.1fppm",  mean_ppm);
        if( rt_utcOffset_ts != 0 && !ppsSync.pps_xtime) {
            rt_utcOffset -= (curr->ustime - rt_utcOffset_ts) * mean_ppm/PPM;
            rt_utcOffset_ts = curr->ustime;
        }
    }
    if( abs(drift_ppm) > stats->drift_thres ) {
        stats->excessive_drift_cnt += 1;
        if( (stats->excessive_drift_cnt % QUICK_RETRIES) == 0 ) {
            LOG(MOD_SYN|WARNING, "Repeated excessive clock drifts between MCU/SX130X#%d (%d retries): %.1fppm (threshold %.1fppm)",
                txunit, stats->excessive_drift_cnt, drift_ppm/fPPM_SCALE, stats->drift_thres/fPPM_SCALE);
        }
        if( stats->excessive_drift_cnt >= 2*QUICK_RETRIES )
            stats->drift_thres = MAX_MCU_DRIFT_THRES;  // reset - we might be stuck on a very low value
        return TIMESYNC_RADIO_INTV/2;
    }
    stats->excessive_drift_cnt = 0;
    ustime_t delay = TIMESYNC_RADIO_INTV;

    // Only txunit#0 can have PPS or we're not tracking a PPS
    if( txunit != 0 )
        goto done;

    if( ppsSync.pps_xtime ) {
        // We are actually tracking PPS - complain if PPS lost
        s4_t no_pps_secs = (curr->xtime - ppsSync.pps_xtime + PPM/2) / PPM;
        if( no_pps_secs > no_pps_thres ) {
            LOG(MOD_SYN|WARNING, "No PPS pulse for ~%d secs", no_pps_secs);
            no_pps_thres = no_pps_thres >= NO_PPS_ALARM_MAX
                ? no_pps_thres + NO_PPS_ALARM_MAX
                : (u4_t)(no_pps_thres * NO_PPS_ALARM_RATE);
        }
    }
    // We update ppsSync only if we have two consecutive time syncs with valid PPS timestamps
    // and if they are apart ~1s - we might see weird values if no PPS pulse occurred during time sync span.
    if( !last->pps_xtime || !curr->pps_xtime ) {
        goto done;
    }
    if( curr->xtime - curr->pps_xtime > PPM+TX_MIN_GAP ) {
        LOG(MOD_SYN|XDEBUG, "PPS: Rejecting PPS (xtime/pps_xtime spread): curr->xtime=0x%lX   curr->pps_xtime=0x%lX   diff=%lu (>%u)",
            curr->xtime, curr->pps_xtime, curr->xtime - curr->pps_xtime, PPM+TX_MIN_GAP);
        goto done;  // no PPS since last time sync
    }
    sL_t err = (curr->pps_xtime - last->pps_xtime) % PPM;
    if( err < 0 ) err += PPM;
    if( err > MAX_PPS_ERROR && err < PPM-MAX_PPS_ERROR ) {
        LOG(MOD_SYN|XDEBUG, "PPS: Rejecting PPS (consecutive pps_xtime error): curr->pps_xtime=0x%lX   last->pps_xtime=0x%lX   diff=%lu",
            curr->pps_xtime, last->pps_xtime, curr->pps_xtime - last->pps_xtime);
        goto done;  // out of scope - probably no value latched
    }
    if( !ppsSync.pps_xtime )
        LOG(MOD_SYN|INFO, "First PPS pulse acquired");

    // Time sync in curr a new valid PPS reference point
    // Update PPS drift stats
    double pps_drift = (double)(curr->pps_xtime - last->pps_xtime)
        / (double)((curr->pps_xtime - last->pps_xtime + PPM/2) / PPM * PPM);
    pps_drifts[pps_drifts_widx] = encodeDriftPPM(pps_drift);
    pps_drifts_widx = (pps_drifts_widx + 1) % N_DRIFTS;
    if( pps_drifts_widx == 0 )
        pps_drifts_thres = log_drift_stats("PPS/SX130X drift stats", pps_drifts, PPS_DRIFT_THRES, NULL);

    ustime_t pps_ustime = xtime2ustime(curr, curr->pps_xtime);
    ustime_t off = pps_ustime % PPM;
    if( syncLnsCnt == 0 ) {
        ppsOffset = off;
        syncLnsCnt = 1;
        wsBufFull = 0;
        rt_yieldTo(&syncLnsTmr, onTimesyncLns);
        LOG(MOD_SYN|INFO, "Obtained initial PPS offset (%ld) - starting timesync with LNS", ppsOffset);
    }
    else if( abs(ppsOffset-off) > (stats->drift_thres * TIMESYNC_RADIO_INTV)/PPM  ) {
        LOG(MOD_SYN|XDEBUG, "Changed PPS offset: %ld => %ld (delta: %ld)", ppsOffset, off, off-ppsOffset);
        // Adjust ppsOffset to accout for MCU/PPS drift
        ppsOffset = off;
    }
    // Correct the fractional second of the UTC reference so it lines up with PPS
    ustime_t pps_utctime_us = rt_ustime2utc(pps_ustime) % PPM;
    rt_utcOffset += pps_utctime_us < PPM/2 ? -pps_utctime_us : PPM-pps_utctime_us;
    // Shift timesync into the middle of two PPS pulses
    // Avoid turning off PPS latching during SX130X sync procedure near the PPS.
    // We might miss a PPS pulse and a scheduled frame might not be sent.
    // Also wobble the sync time a bir otherwise we might track the value when enabling PPS latching
    // as PPS. This happens with a rate resembling 1Hz
    syncWobble *= -1;
    off = syncWobble *PPM/10 + PPM/2 - (curr->ustime - ppsOffset + delay) % PPM;
    delay += off + (off < 0 ? 0 : PPM);
    // Update time reference for conversions + update GPS offset based on # of seconds passed
    // ppsSync->pps_xtime and gpsOffset are pairs related to same point in time
    if( gpsOffset )
        gpsOffset += ustimeRoundSecs(curr->pps_xtime - ppsSync.pps_xtime);
    ppsSync = *curr;
  done:
    *last = *curr;
    return delay;
}


sL_t ts_gpstime2xtime (u1_t txunit, sL_t gpstime) {
    if( txunit >= MAX_TXUNITS || !timesyncs[txunit].xtime || !ppsSync.pps_xtime || ppsOffset < 0 || !gpsOffset ) {
        LOG(MOD_SYN|ERROR, "Cannot convert GPS time - missing %s time sync",
            !timesyncs[txunit].xtime ? "SX130X"
            : !ppsSync.pps_xtime || ppsOffset ? "PPS"
            : !gpsOffset ? "GPS" : "?");
        return 0;
    }
    
    if( timesyncs[0].xtime - ppsSync.pps_xtime > PPS_VALID_INTV ) {
        LOG(MOD_SYN|ERROR, "Failed to convert gpstime to xtime - last PPS sync to old: %~T",
            timesyncs[0].xtime - ppsSync.pps_xtime);
        return 0;
    }
    sL_t xtime = gpstime - gpsOffset + ppsSync.pps_xtime;
    return txunit==0 ? xtime : xtime2xtime(&ppsSync, &timesyncs[txunit], xtime);
}


sL_t ts_xtime2gpstime (sL_t xtime) {
    if( ppsSync.pps_xtime == 0 ) {
        return 0;
    }
    xtime = ts_xtime2xtime(xtime, 0);
    if( !xtime || xtime - ppsSync.pps_xtime > PPS_VALID_INTV ) {
        LOG(MOD_SYN|ERROR, "Failed to convert xtime to gpstime - last PPS sync too old: %~T",
            xtime - ppsSync.pps_xtime);
        return 0;
    }
    return gpsOffset + xtime - ppsSync.pps_xtime;
}


sL_t ts_ustime2xtime (u1_t txunit, ustime_t ustime) {
    if( txunit >= MAX_TXUNITS || timesyncs[txunit].xtime == 0 )
        return 0; // cannot convert
    const timesync_t* sync = &timesyncs[txunit];
    return ustime2xtime(sync, ustime);
}


ustime_t ts_xtime2ustime (sL_t xtime) {
    int txunit = ral_xtime2txunit(xtime);
    if( txunit >= MAX_TXUNITS || timesyncs[txunit].xtime == 0 ) {
        LOG(MOD_SYN|ERROR, "Cannot convert xtime=0x%lX - missing SX130X#%d time sync",
            timesyncs[txunit].xtime, txunit);
        return 0;
    }
    const timesync_t* sync = &timesyncs[txunit];
    if( ral_xtime2sess(xtime) != ral_xtime2sess(sync->xtime) ) {
        LOG(MOD_SYN|ERROR, "Cannot convert xtime=0x%lX - obsolete session: %d (current %d)",
            xtime, ral_xtime2sess(xtime), ral_xtime2sess(sync->xtime));
        return 0;
    }
    return xtime2ustime(sync, xtime);
}


sL_t ts_xtime2xtime (sL_t xtime, u1_t dst_txunit) {
    int src_txunit = ral_xtime2txunit(xtime);
    if( src_txunit == dst_txunit )
        return xtime;
    if( src_txunit >= MAX_TXUNITS ||
        timesyncs[src_txunit].xtime == 0 ||
        timesyncs[dst_txunit].xtime == 0 ) {
        LOG(MOD_SYN|ERROR, "Cannot convert xtime=%ld from txunit#%d to txunit#%d", xtime, src_txunit, dst_txunit);
        return 0; // cannot convert
    }
    const timesync_t* src_sync = &timesyncs[src_txunit];
    const timesync_t* dst_sync = &timesyncs[dst_txunit];
    return xtime2xtime(src_sync, dst_sync, xtime);
}


// Convert a 32bit SX130X tick counter into a xtime reported back to the LNS
// This can only be called in a process with access to libloragw (aka not ral_master)
sL_t ts_xticks2xtime (u4_t xticks, sL_t last_xtime) {
    // Time sync should be frequent so that we should never see a roll over
    // from positive to negative (takes 2^31us ~ 35min)
    // However, we might see small negative numbers because time sync might
    // be slightly younger than time stamps of frames being stuck in the SX130X fifo.
    //
    sL_t d;
    if( (d = (s4_t)(xticks - last_xtime)) < -XTICKS_DECAY ) {
        LOG(MOD_SYN|CRITICAL,
            "SX130X RX time roll over - no update for a long time: xticks=0x%X last_xtime=0x%lX",
            xticks, last_xtime);
        return 0;
    }
    return last_xtime + d;
}


sL_t ts_newXtimeSession (u1_t txunit) {
    // This disambiguates SX130X timestamps
    // If we have a new session (currently reconnect to TC) the SX130X counter restarts
    // Old timestamps coming in from TC with timestamps before the restart must be rejetced.
    sL_t ext = ((sL_t)rand() & RAL_XTSESS_MASK) << RAL_XTSESS_SHIFT;
    if( !ext ) ext = (sL_t)1<<RAL_XTSESS_SHIFT;          // session is never 0
    ext |= ((sL_t)txunit & RAL_TXUNIT_MASK) << RAL_TXUNIT_SHIFT;
    return ext;
}


// Initialize timesync module - run every time we start a new session
void ts_iniTimesync () {
    ppsOffset = -1;
    gpsOffset = 0;
    no_pps_thres = NO_PPS_ALARM_INI;
    memset(&ppsSync, 0, sizeof(ppsSync));   // no PPS ever seen
    memset(&txunit_stats, 0, sizeof(txunit_stats));
    for( int i=0; i<MAX_TXUNITS; i++ )
        txunit_stats[i].drift_thres = MAX_MCU_DRIFT_THRES;
    syncWobble = -1;
    pps_drifts_widx = 0;
    syncQual_thres = INT_MAX;
    syncLnsCnt = 0;
    lastReport = 0;
    sum_mcu_drifts = 0;
    memset(timesyncs, 0, sizeof(timesyncs));
    rt_clrTimer(&syncLnsTmr);
}


// --------------------------------------------------------------------------------
//
// Time sync with LNS - maintains gpsOffset
//
// --------------------------------------------------------------------------------


// Contact server to get a timesync to GPS epoch
// Repeat this as long as we haven't found a solution.
static void onTimesyncLns (tmr_t* tmr) {
    timesyncReport(0);
    if( TC == NULL || ppsOffset < 0 || gpsOffset ) {
        // not connected || no SX130X/PPS sync yet || we have a sync to GPS epoch
        rt_setTimer(tmr, rt_micros_ahead(TIMESYNC_LNS_PAUSE));
        return;
    }
    s2ctx_t* s2ctx = &TC->s2ctx;
    ujbuf_t sendbuf = (*s2ctx->getSendbuf)(s2ctx, MIN_UPJSON_SIZE/2);
    if( sendbuf.buf == NULL ) {
        if( !wsBufFull )
            LOG(MOD_SYN|ERROR, "Failed to send timesync to server - no buffer space");
        rt_setTimer(tmr, rt_micros_ahead(TIMESYNC_LNS_RETRY)); // Retry later
        wsBufFull = 1;
        return;
    }
    wsBufFull = 0;
    uj_encOpen(&sendbuf, '{');
    uj_encKVn(&sendbuf,
              "msgtype",   's', "timesync",
              "txtime",    'I', rt_getTime(),
              NULL);
    uj_encClose(&sendbuf, '}');
    (*s2ctx->sendText)(s2ctx, &sendbuf);
    ustime_t delay = syncLnsCnt % TIMESYNC_LNS_BURST ? TIMESYNC_LNS_RETRY : TIMESYNC_LNS_PAUSE;
    syncLnsCnt += 1;
    rt_setTimer(tmr, rt_micros_ahead(delay));
    LOG(MOD_SYN|DEBUG, "Timesync #%d sent to server", syncLnsCnt);
}


// Server forces inferred GPS time
void ts_setTimesyncLns (ustime_t xtime, sL_t gpstime) {
    ustime_t ustime = ts_xtime2ustime(xtime);
    if( ustime == 0 || (xtime = ts_xtime2xtime(xtime, 0)) == 0 )
        return;
    ustime_t gps_us = gpstime % PPM;
    ppsOffset = (ustime - gps_us) % PPM;
    gpsOffset = gpstime;
    ppsSync.pps_xtime = xtime;
    ppsSync.xtime = xtime;
    ppsSync.ustime = ustime;
    LOG(MOD_SYN|INFO, "Server time sync: xtime=0x%lX gpstime=0x%lX ppsOffset=%ld gpsOffset=0x%lX",
        xtime, gpstime, ppsOffset, gpsOffset);
}


// Server reported back a timestamp - infer GPS second label for a specific PPS edge
void ts_processTimesyncLns (ustime_t txtime, ustime_t rxtime, sL_t gpstime) {
    if( ppsOffset < 0 || rxtime - txtime >= 2*PPM || gpsOffset )
        return;    // need ppsOffset || roundtrip too long || we already have a solution
    if( sys_modePPS == PPS_FUZZY ) {
        // In this timing mode the PPS of the gateway and the PPS of the server are not aligned.
        // This mode facilitates beaconing while not perfectly aligned to an absolute GPS time.
        sL_t xtime = ustime2xtime(&timesyncs[0], (txtime + rxtime)/2);
        LOG(MOD_SYN|INFO, "Timesync with LNS - fuzzy PPS: tx/rx=0x%lX..0x%lX xtime=0x%lX gpsOffset=0x%lX", txtime, rxtime, xtime, gpsOffset);
        ts_setTimesyncLns(xtime, gpstime);
        return;
    }
    txtime -= ppsOffset;
    rxtime -= ppsOffset;
    sL_t tx_s   = txtime / PPM;
    sL_t rx_s   = rxtime / PPM;
    sL_t gps_us = gpstime % PPM;
    sL_t gps_s  = gpstime - gps_us;
    sL_t us_s = 0;
    int  cnt = 0;
    // Try all combinations of server offset from PPS and all
    // possible seconds on the gateway side from TX start to receive time.
    // If only one solution makes sense then save the seconds offset from
    // monotonic ustime to GPS time.
    for( sL_t try_s=tx_s; try_s <= rx_s; try_s++ ) {
        ustime_t candidate = try_s*PPM + gps_us;
        if( candidate >= txtime && candidate <= rxtime ) {
            us_s = try_s*PPM + ppsOffset;  // possible solution
            cnt++;
        }
    }
    LOG(MOD_SYN|VERBOSE, "Timesync LNS: tx/rx:0x%lX..0x%lX (%~T) us/gps:0x%lX/0x%lX (pps offset=%ld) - %d solutions",
        txtime, rxtime, rxtime-txtime, us_s, gpstime, ppsOffset, cnt);
    if( cnt != 1 )
        return;

    // Only one solution - calculate the GPS time label
    //    us_s (localtime) equivalent to gps_s (GPS seconds since epoch)
    // Translate into a seconds offset
    const timesync_t* sync0 = &timesyncs[0];
    sL_t pps_xtime_inferred = ustime2xtime(sync0, us_s);    // inferred PPS pulse in xtime (subject to ustime->xtime error)
    sL_t delta  = ustimeRoundSecs(pps_xtime_inferred - ppsSync.pps_xtime);  // seconds between last latched PPS and inferred
    sL_t pps_xtime = ppsSync.pps_xtime + delta;
    sL_t jitter = pps_xtime - pps_xtime_inferred;
    if( abs(jitter) * iPPM_SCALE > txunit_stats[0].drift_thres ) {
        LOG(MOD_SYN|ERROR, "Timesync LNS: Too much drift between last PPS and inferred PPS: %ldus", jitter);
        return;
    }
    gpsOffset = gps_s - delta;
    LOG(MOD_SYN|INFO, "Timesync with LNS: gpsOffset=0x%lX", gpsOffset);
    timesyncReport(1);
}

// --------------------------------------------------------------------------------
//
// Time sync health data
//
// --------------------------------------------------------------------------------
