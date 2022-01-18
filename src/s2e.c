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

#include <stdio.h>
#include "s2conf.h"
#include "uj.h"
#include "ral.h"
#include "s2e.h"
#include "kwcrc.h"
#include "timesync.h"


u1_t s2e_dcDisabled;    // no duty cycle limits - override for test/dev
u1_t s2e_ccaDisabled;   // no LBT etc           - ditto
u1_t s2e_dwellDisabled; // no dwell time limits - ditto


extern inline int   rps_sf   (rps_t params);
extern inline int   rps_bw   (rps_t params);
extern inline rps_t rps_make (int sf, int bw);

// Fwd decl.
static void s2e_txtimeout (tmr_t* tmr);
static void s2e_bcntimeout (tmr_t* tmr);


static void setDC (s2ctx_t* s2ctx, ustime_t t) {
    for( u1_t u=0; u<MAX_TXUNITS; u++ ) {
        for( u1_t i=0; i<DC_NUM_BANDS; i++ )
            s2ctx->txunits[u].dc_eu868bands[i] = t;
        for( u1_t i=0; i<MAX_DNCHNLS; i++ )
            s2ctx->txunits[u].dc_perChnl[i] = t;
    }
}

static void resetDC (s2ctx_t* s2ctx, u2_t dc_chnlRate) {
    setDC(s2ctx, rt_getTime());
    s2ctx->dc_chnlRate = dc_chnlRate;
}


static int s2e_canTxOK (s2ctx_t* s2ctx, txjob_t* txjob, int* ccaDisabled) {
    return 1;
}


void s2e_ini (s2ctx_t* s2ctx) {
    if( s2e_joineuiFilter == NULL )
        s2e_joineuiFilter = rt_mallocN(uL_t, 2*MAX_JOINEUI_RANGES+2);  // need min one trailing 0 entry

    memset(s2ctx, 0, sizeof(*s2ctx));
    txq_ini(&s2ctx->txq);
    rxq_ini(&s2ctx->rxq);

    s2ctx->canTx = s2e_canTxOK;
    for( u1_t i=0; i<DR_CNT; i++ )
        s2ctx->dr_defs[i] = RPS_ILLEGAL;
    setDC(s2ctx, USTIME_MIN);   // disable until we have a region that needs it

    for( int u=0; u < MAX_TXUNITS; u++ ) {
        rt_iniTimer(&s2ctx->txunits[u].timer, s2e_txtimeout);
        s2ctx->txunits[u].timer.ctx = s2ctx;
        s2ctx->txunits[u].head = TXIDX_END;
    }
    rt_iniTimer(&s2ctx->bcntimer, s2e_bcntimeout);
    s2ctx->bcntimer.ctx = s2ctx;
}


void s2e_free (s2ctx_t* s2ctx) {
    for( int u=0; u < MAX_TXUNITS; u++ )
        rt_clrTimer(&s2ctx->txunits[u].timer);
    rt_clrTimer(&s2ctx->bcntimer);
    memset(s2ctx, 0, sizeof(*s2ctx));
    ts_iniTimesync();
    ral_stop();
}

// --------------------------------------------------------------------------------
//
// RX PART
//
// --------------------------------------------------------------------------------
//
// Check if we can fill the RXQ by retrieving frames from the radio layer.
// Then try to send as many as possible.
//

rxjob_t* s2e_nextRxjob (s2ctx_t* s2ctx) {
    return rxq_nextJob(&s2ctx->rxq);
}


void s2e_addRxjob (s2ctx_t* s2ctx, rxjob_t* rxjob) {
    // Add newly received frame to rxq
    // Check for mirror frame (reflection on a neighboring frequency)
    for( rxjob_t* p = &s2ctx->rxq.rxjobs[s2ctx->rxq.first]; p < rxjob; p++ ) {
        if( p->dr == rxjob->dr &&
            p->len == rxjob->len &&
            memcmp(&s2ctx->rxq.rxdata[p->off], &s2ctx->rxq.rxdata[rxjob->off], rxjob->len) == 0 ) {
            // Duplicate detected - drop the mirror
            if( (8*rxjob->snr - rxjob->rssi) > (8*p->snr - p->rssi) ) {
                // Drop previous frame p
                LOG(MOD_S2E|DEBUG, "Dropped mirror frame freq=%F snr=%5.1f rssi=%d (vs. freq=%F snr=%5.1f rssi=%d) - DR%d mic=%d (%d bytes)",
                    p->freq, p->snr/4.0, -p->rssi, rxjob->freq, rxjob->snr/4.0, -rxjob->rssi,
                    p->dr, (s4_t)rt_rlsbf4(&s2ctx->rxq.rxdata[p->off]+rxjob->len-4), p->len);

                rxq_commitJob(&s2ctx->rxq, rxjob);
                rxjob = rxq_dropJob(&s2ctx->rxq, p);
            } else {
                // else: Drop newly retrieved frame - aka don't commit it
                LOG(MOD_S2E|DEBUG, "Dropped mirror frame freq=%F snr=%5.1f rssi=%d (vs. freq=%F snr=%5.1f rssi=%d) - DR%d mic=%d (%d bytes)",
                    rxjob-> freq, rxjob->snr/4.0, -rxjob->rssi, p->freq, p->snr/4.0, -p->rssi,
                    rxjob->dr, (s4_t)rt_rlsbf4(&s2ctx->rxq.rxdata[rxjob->off]+rxjob->len-4), rxjob->len);
            }
            return;
        }
    }
    // No mirror frame found
    rxq_commitJob(&s2ctx->rxq, rxjob);
}

void s2e_flushRxjobs (s2ctx_t* s2ctx) {
    while( s2ctx->rxq.first < s2ctx->rxq.next ) {
        // Get a send buffer - parse frame / check filter
        ujbuf_t sendbuf = (*s2ctx->getSendbuf)(s2ctx, MIN_UPJSON_SIZE);
        if( sendbuf.buf == NULL ) {
            // Websocket has no space - WS will call again
            return;
        }
        rxjob_t* j = &s2ctx->rxq.rxjobs[s2ctx->rxq.first++];
        dbuf_t lbuf = { .buf = NULL };
        if( log_special(MOD_S2E|VERBOSE, &lbuf) )
            xprintf(&lbuf, "RX %F DR%d %R snr=%.1f rssi=%d xtime=0x%lX - ",
                    j->freq, j->dr, s2e_dr2rps(s2ctx, j->dr), j->snr/4.0, -j->rssi, j->xtime);

        uj_encOpen(&sendbuf, '{');
        if( !s2e_parse_lora_frame(&sendbuf, &s2ctx->rxq.rxdata[j->off], j->len, lbuf.buf ? &lbuf : NULL) ) {
            // Frame failed sanity checks or stopped by filters
            sendbuf.pos = 0;
            continue;
        }
        if( lbuf.buf )
            log_specialFlush(lbuf.pos);
        double reftime = 0.0;
        if( s2ctx->muxtime ) {
            reftime = s2ctx->muxtime +
                ts_normalizeTimespanMCU(rt_getTime()-s2ctx->reftime) / 1e6;
        }
        uj_encKVn(&sendbuf,
                  "RefTime",  'T', reftime,
                  "DR",       'i', j->dr,
                  "Freq",     'i', j->freq,
                  "upinfo",   '{',
                  /**/ "rctx",    'I', j->rctx,
                  /**/ "xtime",   'I', j->xtime,
                  /**/ "gpstime", 'I', ts_xtime2gpstime(j->xtime),
                  /**/ "fts",     'i', j->fts,
                  /**/ "rssi",    'i', -(s4_t)j->rssi,
                  /**/ "snr",     'g', j->snr/4.0,
                  /**/ "rxtime",  'T', rt_getUTC()/1e6,
                  "}",
                  NULL);
        uj_encClose(&sendbuf, '}');
        if( !xeos(&sendbuf) ) {
            LOG(MOD_S2E|ERROR, "JSON encoding exceeds available buffer space: %d", sendbuf.bufsize);
        } else {
            (*s2ctx->sendText)(s2ctx, &sendbuf);
            assert(sendbuf.buf==NULL);
        }
    }
}



// --------------------------------------------------------------------------------
//
// TX PART
//
// --------------------------------------------------------------------------------


static const u2_t DC_EU868BAND_RATE[] = {
    [DC_DECI ]=   10,
    [DC_CENTI]=  100,
    [DC_MILLI]= 1000,
};


static ustime_t _calcAirTime (rps_t rps, u1_t plen, u1_t nocrc, u2_t preamble) {
    if( preamble == 0 )
        preamble = 8;
    if( rps == RPS_ILLEGAL )
        return 0;
    // The impl has been taken from lmic.c and adapted
    u1_t bw = rps_bw(rps);  // 0,1,2 = 125,250,500kHz
    u1_t sf = rps_sf(rps);  // 0=FSK, 1..6 = SF7..12
    if( sf == FSK ) {
        return (plen+/*preamble*/5+/*syncword*/3+/*len*/1+/*crc*/2) * /*bits/byte*/8
            * rt_seconds(1) / /*kbit/s*/50000;
    }
    sf = 7 + (sf - SF7)*(SF8-SF7); // map enums SF7..SF12 to 7..12
    u1_t sfx = 4*sf;
    u1_t q = sfx - (sf >= 11 && bw == 0 ? 8 : 0);  
    u1_t ih = 0;     // station never sends with implicit header
    u1_t cr = 0;     // CR_4_5=0, CR_4_6, CR_4_7, CR_4_8
    int tmp = 8*plen - sfx + 28 + (nocrc?0:16) - (ih?20:0);
    if( tmp > 0 ) {
        tmp = (tmp + q - 1) / q;
        tmp *= cr+5;
        tmp += 8;
    } else {
        tmp = 8;
    }
    tmp = (tmp<<2) + /*preamble: 4*4.25*/ 17 + /*preamble*/(4*preamble);
    // bw = 125000 = 15625 * 2^3
    //      250000 = 15625 * 2^4
    //      500000 = 15625 * 2^5
    // sf = 7..12
    //
    // osticks =  tmp * OSTICKS_PER_SEC * 1<<sf / bw
    //
    // 3 => counter reduced divisor 125000/8 => 15625
    // 2 => counter 2 shift on tmp
    sfx = sf - (3+2) - bw;
    int div = 15625;
    if( sfx > 4 ) {
        // prevent 32bit signed int overflow in last step
        div >>= sfx-4;
        sfx = 4;
    }
    return (((ustime_t)tmp << sfx) * rt_seconds(1) + div/2) / div;
}

ustime_t s2e_calcDnAirTime (rps_t rps, u1_t plen, u1_t addcrc, u2_t preamble) {
    return _calcAirTime(rps, plen, !addcrc, preamble);
}

ustime_t s2e_calcUpAirTime (rps_t rps, u1_t plen) {
    return _calcAirTime(rps, plen, 0, 8);
}

static void send_dntxed (s2ctx_t* s2ctx, txjob_t* txjob) {
    if( txjob->deveui ) {
        // Note: dnsched does not have deveui field set - don't report dntxed
        ujbuf_t sendbuf = (*s2ctx->getSendbuf)(s2ctx, MIN_UPJSON_SIZE/2);
        if( sendbuf.buf == NULL ) {
            LOG(MOD_S2E|ERROR, "%J - failed to send dntxed, no buffer space", txjob);
            return;
        }
        uj_encOpen(&sendbuf, '{');
        uj_encKVn(&sendbuf,
                  "msgtype",   's', "dntxed",
                  "seqno",     'I', txjob->diid,    // for older servers (remove if obsoleted)
                  "diid",      'I', txjob->diid,    // newer servers
                  "DR",        'i', txjob->dr,
                  "Freq",      'u', txjob->freq,
                  rt_deveui,   'E', txjob->deveui,
                  "rctx",      'i', txjob->txunit,  // antenna that sent this frame
                  "xtime",     'I', txjob->xtime,
                  "txtime",    'T', txjob->txtime/1e6,
                  "gpstime",   'I', txjob->gpstime,
                  NULL);
        uj_encClose(&sendbuf, '}');
        (*s2ctx->sendText)(s2ctx, &sendbuf);
    }
    LOG(MOD_S2E|INFO, "TX %J - %s: %F %.1fdBm ant#%d(%d) DR%d %R frame=%12.4H (%u bytes)",
        txjob, txjob->deveui ? "dntxed" : "on air",
        txjob->freq, (double)txjob->txpow/TXPOW_SCALE,
        txjob->txunit, ral_rctx2txunit(txjob->rctx),     // sending/receiving antenna
        txjob->dr, s2e_dr2rps(s2ctx, txjob->dr),
        txjob->len, &s2ctx->txq.txdata[txjob->off], txjob->len);
}


ustime_t s2e_updateMuxtime(s2ctx_t* s2ctx, double muxstime, ustime_t now) {
    if( now == 0 )
        now = rt_getTime();
    s2ctx->muxtime = muxstime;
    s2ctx->reftime = now;
    return now;
}


rps_t s2e_dr2rps (s2ctx_t* s2ctx, u1_t dr) {
    return dr < 16 ? s2ctx->dr_defs[dr] : RPS_ILLEGAL;
}


// This is called only for received frame (maps only to correct *up* DRs)
u1_t s2e_rps2dr (s2ctx_t* s2ctx, rps_t rps) {
    for( u1_t dr=0; dr<DR_CNT; dr++ ) {
        if( s2ctx->dr_defs[dr] == rps )
            return dr;
    }
    return DR_ILLEGAL;
}


static void check_dnfreq (s2ctx_t* s2ctx, ujdec_t* ujd, u4_t* pfreq, u1_t* pchnl) {
    sL_t freq = uj_int(ujd);
    if( freq < s2ctx->min_freq || freq > s2ctx->max_freq )
        uj_error(ujd, "Illegal frequency value: %ld - not in range %d..%d", freq, s2ctx->min_freq, s2ctx->max_freq);
    *pfreq = freq;
    // Find and assign a DN channel to this freq.
    // This channel index is only used locally to tracking duty cycle
    
    int ch;
    for( ch=0; ch<MAX_DNCHNLS; ch++ ) {
        if( s2ctx->dn_chnls[ch] == 0 )
            break;
        if( freq == s2ctx->dn_chnls[ch] ) {
            *pchnl = ch;
            return;
        }
    }
    // New DN frequency detected
    if( ch == MAX_DNCHNLS ) {
        // Never occupy last slot - facilitates graceful operation under overflow
        // Airtime of all excess channels is booked into this last slot
        LOG(MOD_S2E|WARNING, "Out of space for DN channel frequencies");
    } else {
        s2ctx->dn_chnls[ch] = freq;
    }
    *pchnl = ch;
}

static void check_dr (s2ctx_t* s2ctx, ujdec_t* ujd, u1_t* pdr) {
    sL_t dr = uj_int(ujd);
    if( dr < 0 || dr >= DR_CNT || s2ctx->dr_defs[dr] == RPS_ILLEGAL )
        uj_error(ujd, "Illegal datarate value: %d for region %s", dr, s2ctx->region_s);
    *pdr = dr;
}

static int freq2band (u4_t freq) {
    if( freq >= 869400000 && freq <= 869650000 )
        return DC_DECI;
    if( (freq >= 868000000 && freq <= 868600000) || (freq >= 869700000 && freq <= 870000000) )
        return DC_CENTI;
    return DC_MILLI;
}

static void update_DC (s2ctx_t* s2ctx, txjob_t* txj) {
    if( s2ctx->region == J_EU868 ) {
        u1_t band = freq2band(txj->freq);
        ustime_t* dcbands = s2ctx->txunits[txj->txunit].dc_eu868bands;
        ustime_t t = dcbands[band];
        // Update unless disabled or blocked
        if( t != USTIME_MIN && t != USTIME_MAX ) {
            dcbands[band] = t = txj->txtime + txj->airtime * DC_EU868BAND_RATE[band];
            LOG(MOD_S2E|XDEBUG, "DC EU band %d blocked until %>.3T (txtime=%>.3T airtime=%~T)",
                DC_EU868BAND_RATE[band], rt_ustime2utc(t), rt_ustime2utc(txj->txtime), (ustime_t)txj->airtime);
        }
    }
    int dnchnl = txj->dnchnl;
    ustime_t* dclist = s2ctx->txunits[txj->txunit].dc_perChnl;
    ustime_t t = dclist[dnchnl];
    // Update unless disabled or blocked
    if( t != USTIME_MIN && t != USTIME_MAX ) {
        dclist[dnchnl] = t = txj->txtime + txj->airtime * s2ctx->dc_chnlRate;
        LOG(MOD_S2E|XDEBUG, "DC dnchnl %d blocked until %>.3T (txtime=%>.3T airtime=%~T)",
            dnchnl, rt_ustime2utc(t), rt_ustime2utc(txj->txtime), (ustime_t)txj->airtime);
    }
}

static s2_t calcTxpow (s2ctx_t* s2ctx, txjob_t* txjob) {
    s2_t txpow = s2ctx->txpow;   // default TX power
    // Check upper bound first - will be false for all tx freq if 0 - no range
    if( txjob->freq <= s2ctx->txpow2_freq[1] && txjob->freq >= s2ctx->txpow2_freq[0] ) {
        txpow = s2ctx->txpow2;
    }
    // in case of more complicated regulation: switch( s2ctx->region ) { .. }
    return txpow;
}

static void updateAirtimeTxpow (s2ctx_t* s2ctx, txjob_t* txjob) {
    txjob->airtime = s2e_calcDnAirTime(s2e_dr2rps(s2ctx, txjob->dr), txjob->len, txjob->addcrc, txjob->preamble);
    txjob->txpow = calcTxpow(s2ctx, txjob);
}

static int calcPriority (txjob_t* txjob) {
    int prio = txjob->prio;
    if( txjob->rx2freq || ((txjob->txflags & TXFLAG_CLSC) && txjob->retries < CLASS_C_BACKOFF_MAX) )
        prio -= PRIO_PENALTY_ALTTXTIME;
    if( txjob->altAnts )
        prio -= PRIO_PENALTY_ALTANTENNA;
    return prio;
}


// Switch to alternative (later) TX time - if any available
// This also updates airtime/txpow if parameters change.
static int altTxTime (s2ctx_t* s2ctx, txjob_t* txjob, ustime_t earliest) {
    if( (txjob->txflags & TXFLAG_CLSC) ) {
      again:
        if( txjob->rx2freq ) {
            // Switch over from RX1 to RX2 - we can sent anytime, since we move forward
            // something it's unlikely to conflict with RX1 spot.
            txjob->txtime   = earliest - CLASS_C_BACKOFF_BY;  // earliest possible time
            txjob->xtime    = ts_ustime2xtime(txjob->txunit, txjob->txtime);
            txjob->retries  = 0;
            txjob->freq     = txjob->rx2freq;
            txjob->dr       = txjob->rx2dr;
            txjob->dnchnl   = txjob->dnchnl2;
            txjob->rx2freq  = 0;  // invalidate RX2
            updateAirtimeTxpow(s2ctx, txjob);
            if( txjob->xtime == 0 ) {
                LOG(MOD_S2E|VERBOSE, "%J - class C dropped - no time sync to SX130X yet", txjob);
                return 0;
            }
        }
        if( txjob->retries > CLASS_C_BACKOFF_MAX ) {
            LOG(MOD_S2E|VERBOSE, "%J - class C out of TX tries (%d in %~T)",
                txjob, txjob->retries, txjob->retries*CLASS_C_BACKOFF_BY);
            return 0; // no alternative TX
        }
        // Push TX time back by backoff and check again
        // - we don't need high precision here because class C listens always
        txjob->retries += 1;
        txjob->xtime += CLASS_C_BACKOFF_BY;
        txjob->txtime += CLASS_C_BACKOFF_BY;
        if( txjob->txtime < earliest )
            goto again;
        return 1;
    }
    if( (txjob->txflags & TXFLAG_PING) ) {
        // Class B ping slot - server currently supplies only one time slot
        LOG(MOD_S2E|VERBOSE, "%J - class B ping has no alternate TX time", txjob);
        return 0;
    }
    // Class A
    if( txjob->rx2freq == 0 ) {
        LOG(MOD_S2E|VERBOSE, "%J - class A has no more alternate TX time", txjob);
        return 0; // no alternative TX
    }
    txjob->freq     = txjob->rx2freq;
    txjob->dr       = txjob->rx2dr;
    txjob->dnchnl   = txjob->dnchnl2;
    txjob->txtime  += rt_seconds(1);
    txjob->xtime   += rt_seconds(1);
    txjob->rx2freq  = 0;  // invalidate RX2
    updateAirtimeTxpow(s2ctx, txjob);
    if( txjob->txtime < earliest ) {
        LOG(MOD_S2E|VERBOSE, "%J - too late for RX2 by %~T", txjob, earliest - txjob->txtime);
        return 0;
    }
    LOG(MOD_S2E|VERBOSE, "%J - trying RX2 %F DR%d", txjob, txjob->freq, txjob->dr);
    return 1;
}


static int s2e_canTxEU868 (s2ctx_t* s2ctx, txjob_t* txjob, int* ccaDisabled) {
    ustime_t txtime = txjob->txtime;
    ustime_t band_exp = s2ctx->txunits[txjob->txunit].dc_eu868bands[freq2band(txjob->freq)];
    if( txtime >= band_exp ) {
        
        return 1;   // clear channel analysis not required
    }
    // No DC in band
    LOG(MOD_S2E|VERBOSE, "%J %F - no DC in band: txtime=%>.3T free=%>.3T",
        txjob, txjob->freq, rt_ustime2utc(txtime), rt_ustime2utc(band_exp));
    return 0;
}


static int s2e_canTxPerChnlDC (s2ctx_t* s2ctx, txjob_t* txjob, int* ccaDisabled) {
    ustime_t txtime = txjob->txtime;
    ustime_t chfree = s2ctx->txunits[txjob->txunit].dc_perChnl[txjob->dnchnl];
    if( txtime >= chfree )
        return 2;  // can send if channel clear
    LOG(MOD_S2E|VERBOSE, "%J %F - no DC in channel: txtime=%>.3T until=%>.3T",
        txjob, txjob->freq, rt_ustime2utc(txtime), rt_ustime2utc(chfree));
    return 0;

    ustime_t band_exp = s2ctx->txunits[txjob->txunit].dc_eu868bands[freq2band(txjob->freq)];
    if( txtime >= band_exp )
        return 1;   // clear channel analysis not required
    // No DC in band
    LOG(MOD_S2E|VERBOSE, "%J %F - no DC in band: txtime=%>.3T free=%>.3T",
        txjob, txjob->freq, rt_ustime2utc(txtime), rt_ustime2utc(band_exp));
    return 0;
}


// Add a txjob to the TX queue and insert ordered by txtime.
// Only basic exclusion constraints are checked for newly arriving txjobs:
// Independent on antenna choice:
//  - if too late, try alternative TX times, if out of alternatives drop it
// Per antenna, start with preferred antenna and then try alternative antennas:
//  - definitely no duty cycle, thus the frame can't be sent for sure. If it could be sent under CCA enter it
//  - collision with ongoing TX on current antenna (never stop a running TX)
// If not excluded enter based on txtime. If txtime is head of txunit queue reset processing timer
// to kick start s2e_nextTxAction
//
int s2e_addTxjob (s2ctx_t* s2ctx, txjob_t* txjob, int relocate, ustime_t now) {
    ustime_t earliest = now + TX_AIM_GAP;
    u1_t txunit;
    if( !relocate ) {
        // txjob is fresh entry from LNS and not one that got reschduled due to TX conflicts
        ustime_t txtime = txjob->txtime;    //
        txunit = txjob->txunit = ral_rctx2txunit(txjob->rctx);
        txjob->altAnts = ral_altAntennas(txunit);
        updateAirtimeTxpow(s2ctx, txjob);

        if( txtime > now + TX_MAX_AHEAD ) {
            LOG(MOD_S2E|WARNING, "%J - Tx job too far ahead: %~T", txjob, txtime-now);
            return 0;
        }

        if( txtime < earliest  &&  !altTxTime(s2ctx, txjob, earliest) )
            
            return 0;
        goto start;
    }
  check_alt: {
        u1_t alts = txjob->altAnts;
        if( alts==0 ) {
            // No more alternative antennas - try later TX time
            if( !altTxTime(s2ctx, txjob, earliest) ) {
                LOG(MOD_S2E|WARNING, "%J - unable to place frame", txjob);
                
                return 0;
            }
            // and reset antenna options
            txunit = txjob->txunit = ral_rctx2txunit(txjob->rctx);
            txjob->altAnts = ral_altAntennas(txunit);
        } else {
            // Try to find alternative antenna
            txunit = 0;
            while( (alts & (1<<txunit)) == 0 )
                txunit += 1;
            txjob->txunit = txunit;
            txjob->altAnts &= ~(1<<txunit);
        }
    }
  start: {
        int ccaDisabled = 0;
        if( !s2e_dcDisabled && !(*s2ctx->canTx)(s2ctx, txjob, &ccaDisabled) )
            goto check_alt;
        ustime_t txtime = txjob->txtime;
        txidx_t* pidx = &s2ctx->txunits[txunit].head;
        txidx_t  idx  = pidx[0];
        txjob_t* curr = txq_idx2job(&s2ctx->txq, idx);
        if( curr && (curr->txflags & TXFLAG_TXING) && txtime < curr->txtime + curr->airtime + TX_MIN_GAP ) {
            // Would interfer with currently ongoing TX
            LOG(MOD_S2E|DEBUG, "%J - frame colliding with ongoing TX on ant#%d", txjob, txunit);
            goto check_alt;
        }
        // Insert into Q by ascending txtime
        do {
            if( idx == TXIDX_END  ||  txtime < curr->txtime ) {
                // Found my place - insert/append
                assert(txjob->next == TXIDX_NIL);
                txjob->next = idx;
                pidx[0] = txq_job2idx(&s2ctx->txq, txjob);
                if( pidx == &s2ctx->txunits[txunit].head ) // new txjob is head of q?
                    rt_yieldTo(&s2ctx->txunits[txunit].timer, s2e_txtimeout);
                return 1;
            }
            idx = (pidx = &curr->next)[0];
            curr = txq_idx2job(&s2ctx->txq, idx);
        } while(1);
    }
}


// Analyze TX queue and decide on next action.
// Return the time when the next action is due if the queue head is not changed.
// This can be called any time to reevaluate actions.
// State engine:
//  - A TXing job is pushed trhu its states before it is dequeued and the next txjob is even considered
//    - entering and goining thru TXing states:
//       - recalc xtime from latest timesync data
//       - check clear channel
//       - submit to radio
//    - check that frame is being emitted (protects against radio failures, xticks rollovers)
//    - at txend consider next txjob
//  - If head txjob too far out, wait until it is TXable
//
// The return value makes a suggestion as to when the next call should be done.
//
ustime_t s2e_nextTxAction (s2ctx_t* s2ctx, u1_t txunit) {
    ustime_t now = rt_getTime();
    txidx_t *phead = &s2ctx->txunits[txunit].head;
 again:
    if( phead[0] == TXIDX_END )
        return USTIME_MAX;
    txjob_t* curr = txq_idx2job(&s2ctx->txq, phead[0]);
    ustime_t txdelta = curr->txtime - now;

    if( (curr->txflags & TXFLAG_TXING) ) {
        // Head job in mode TXING
        ustime_t txend = curr->txtime + curr->airtime;
        if( now >= txend ) {
            // TX is over - drop job
            LOG(MOD_S2E|DEBUG, "Tx done diid=%ld", curr->diid);
            if( !(curr->txflags & TXFLAG_TXCHECKED) ) {
                update_DC(s2ctx, curr);
                curr->txflags |= TXFLAG_TXCHECKED;
                send_dntxed(s2ctx, curr);
            }
            txq_unqJob(&s2ctx->txq, phead);
            txq_freeJob(&s2ctx->txq, curr);
            goto again;
        }
        // Frame is still being transmitted - come back at end of TX
        if( !(curr->txflags & TXFLAG_TXCHECKED) ) {
            if( txdelta > -TXCHECK_FUDGE )
                return curr->txtime + TXCHECK_FUDGE;
            int txs = ral_txstatus(txunit);
            if( txs != TXSTATUS_EMITTING ) {
                // Something went wrong - should be emitting
                LOG(MOD_S2E|ERROR, "%J - radio is not emitting frame - abandoning TX, trying alternative", curr);
                ral_txabort(txunit);
                curr->txflags &= ~TXFLAG_TXING;
                goto check_alt;
            }
            // Looks like it's on air
            update_DC(s2ctx, curr);
            
            
            
            
            
            
            

            curr->txflags |= TXFLAG_TXCHECKED;
            // sending dntxed here instead @txend gives nwks more time to update/inform muxs (join)
            send_dntxed(s2ctx, curr);
        }
        return txend;
    }
    if( txdelta < TX_MIN_GAP ) {
        // Missed TX start time - try alternative or drop frame
        LOG(MOD_S2E|ERROR, "%J - missed TX time: txdelta=%~T min=%~T", curr, txdelta, TX_MIN_GAP);
      check_alt:
        txq_unqJob(&s2ctx->txq, phead);
        if( !s2e_addTxjob(s2ctx, curr, /*relocate*/1, now) )  // note: might change queue head! (reload @ again)
            txq_freeJob(&s2ctx->txq, curr);
        goto again;
    }
    // Txtime time too far out Head is TXable - is it time to feed the radio?
    if( txdelta > TX_AIM_GAP ) {
        LOG(MOD_S2E|DEBUG, "%J - next TX start ahead by %~T (%>.6T)",
            curr, txdelta, rt_ustime2utc(curr->txtime));
        return curr->txtime - TX_AIM_GAP;
    }

    // Re-calc exact xtime based on latest timesync data
    if( curr->gpstime ) {
        // Recalc xtime against latest time sync data
        curr->xtime = ts_gpstime2xtime(txunit, curr->gpstime);
        curr->txtime = ts_xtime2ustime(curr->xtime);
        txdelta = curr->txtime - now;
    }
    else if( ral_xtime2txunit(curr->xtime) != txunit ) {
        curr->xtime = ts_xtime2xtime(curr->xtime, txunit);
    }
    if( curr->xtime == 0 ) {
        LOG(MOD_S2E|ERROR, "%J - time sync problems - trying alternative", curr);
        goto check_alt;
    }
    // Txtime close enough to make a decision
    // Check channel access
    int ccaDisabled = s2e_ccaDisabled;
    if( !s2e_dcDisabled && !(*s2ctx->canTx)(s2ctx, curr, &ccaDisabled) )
        goto check_alt;

    // Check collision with subsequent frames and weigh priorities
    // Assuming a txjob with later txstart time is not blocked by duty cycle
    // if the earlier current txjob isn't
    ustime_t txend = curr->txtime + curr->airtime;
    txjob_t* other_txjob = curr;
    int prio = calcPriority(curr);
    do {
        other_txjob = txq_idx2job(&s2ctx->txq, other_txjob->next);
        if( other_txjob == NULL )
            break;
        if( txend < other_txjob->txtime - TX_MIN_GAP )
            break;  // no overlap
        int oprio = calcPriority(other_txjob);
        if( prio < oprio ) {
            LOG(MOD_S2E|ERROR, "%J - Hindered by %J %~T later: prio %d<%d - trying alternative",
                curr, other_txjob, other_txjob->txtime - curr->txtime, prio, oprio);
            goto check_alt;
        }
    } while(1);

    LOG(MOD_S2E|VERBOSE, "%J - starting TX in %~T: %F %.1fdBm ant#%d(%d) DR%d %R frame=%12.4H (%u bytes)",
        curr, txdelta,
        curr->freq, (double)curr->txpow/TXPOW_SCALE,
        curr->txunit, ral_rctx2txunit(curr->rctx),     // sending/receiving antenna
        curr->dr, s2e_dr2rps(s2ctx, curr->dr),
        curr->len, &s2ctx->txq.txdata[curr->off], curr->len);

    int txerr = ral_tx(curr, s2ctx, ccaDisabled);
    if( txerr != RAL_TX_OK ) {
        if( txerr == RAL_TX_NOCA ) {
            LOG(MOD_S2E|ERROR, "%J - channel busy - trying alternative", curr);
        } else {
            LOG(MOD_S2E|ERROR, "%J - radio layer failed to TX - trying alternative", curr);
        }
        goto check_alt;
    }
    curr->txflags |= TXFLAG_TXING;

    // Unqueue all overlapping subsequent txjobs and find alternatives (antenna/txtime)
    // If no alternatives drop txjob.
    while(1) {
        txjob_t* next_txjob = txq_idx2job(&s2ctx->txq, curr->next);
        if( next_txjob == NULL || txend < next_txjob->txtime - TX_MIN_GAP )
            break;  // no next or no overlap
        LOG(MOD_S2E|INFO, "%J - displaces %J due to %~T overlap", curr, next_txjob, next_txjob->txtime - TX_MIN_GAP - txend);
        txq_unqJob(&s2ctx->txq, &curr->next);
        if( !s2e_addTxjob(s2ctx, next_txjob, /*relocate*/1, now) )  // note: might change next!
            txq_freeJob(&s2ctx->txq, next_txjob);
    }
    return curr->txtime + TXCHECK_FUDGE;
}



static void s2e_txtimeout (tmr_t* tmr) {
    s2ctx_t* s2ctx = tmr->ctx;
    u1_t txunit = (s2txunit_t*)((u1_t*)tmr - offsetof(s2txunit_t, timer)) - s2ctx->txunits;
    ustime_t t = s2e_nextTxAction(s2ctx, txunit);
    if( t == USTIME_MAX )
        return;
    rt_setTimer(tmr, t);
}


static void s2e_bcntimeout (tmr_t* tmr) {
    s2ctx_t* s2ctx = tmr->ctx;
    ustime_t now = rt_getTime();
    sL_t xtime = ts_ustime2xtime(0, now);
    sL_t gpstime = ts_xtime2gpstime(xtime);
    double lat, lon;
    int latlon_ok = sys_getLatLon(&lat, &lon);
    u1_t state = (gpstime ? BCNING_OK:BCNING_NOTIME) | (latlon_ok?BCNING_OK:BCNING_NOPOS);

    if( state != s2ctx->bcn.state ) {
        str_t msg = state==BCNING_OK
            ? "Beaconing resumed - recovered GPS data: %s %s"
            : "Beaconing suspend - missing GPS data: %s %s";
        u1_t change = state ^ s2ctx->bcn.state;
        LOG(MOD_S2E|INFO, msg, (change&BCNING_NOTIME)?"time":"", (change&BCNING_NOPOS)?"position":"");
        s2ctx->bcn.state = state;
    }
    if( state != BCNING_OK ) {
        // We don't have PPS or we are not yet time synced -- retry after a while
        rt_setTimer(tmr, now + rt_seconds(10));
        return;
    }

    // Next beacon TX time is on upcoming multipl of 128s GPS time which is at least 1s ahead
    ustime_t ahead = BEACON_INTVL - gpstime % BEACON_INTVL;
    sL_t gpstxtime = gpstime + ahead;
    txjob_t* txjob = txq_reserveJob(&s2ctx->txq);
    if( txjob == NULL ) {
        LOG(MOD_S2E|ERROR, "Out of TX jobs - cannot send beacon");
        goto nextbcn;
    }
    int ctrl = s2ctx->bcn.ctrl;
    int bcn_len = s2ctx->bcn.layout[2];
    u1_t* p = txq_reserveData(&s2ctx->txq, bcn_len);
    if( p == NULL ) {
        LOG(MOD_S2E|ERROR, "Out of TX data space - cannot send beacon");
        goto nextbcn;
    }
    sL_t epoch = gpstxtime/BEACON_INTVL;
    txjob->gpstime = gpstxtime;
    txjob->xtime   = ts_gpstime2xtime(0, txjob->gpstime);
    txjob->txtime  = ts_xtime2ustime(txjob->xtime);
    txjob->freq    = s2ctx->bcn.freqs[epoch % (ctrl>>4)];
    txjob->dr      = ctrl & 0xF;
    txjob->addcrc  = false;
    txjob->txflags = TXFLAG_BCN;
    txjob->prio    = PRIO_BEACON;
    txjob->len     = bcn_len;
    s2e_make_beacon(s2ctx->bcn.layout, epoch*128, 0, lat, lon, p);

    txq_commitJob(&s2ctx->txq, txjob);
    if( !s2e_addTxjob(s2ctx, txjob, /*initial placement*/0, now) )
        txq_freeJob(&s2ctx->txq, txjob);

  nextbcn:
    // Sleep until next beacon is 800ms ahead
    ahead += BEACON_INTVL - rt_millis(800);
    rt_setTimer(tmr, now + ahead);
}

static bool hasFastLora(s2ctx_t* s2ctx, int minDR, int maxDR, rps_t* rpsp) {
    for( int dr=minDR; dr<=maxDR; dr++ ) {
        rps_t rps = s2e_dr2rps(s2ctx, dr);
        if( rps_bw(rps) == BW250 || rps_bw(rps) == BW500 ) {
            *rpsp = rps;
            return true;
        }
    }
    return false;
}

static bool hasFSK(s2ctx_t* s2ctx, int minDR, int maxDR) {
    for( int dr=minDR; dr<=maxDR; dr++ ) {
        if( s2e_dr2rps(s2ctx, dr) == RPS_FSK )
            return true;
    }
    return false;
}

static bool any125kHz(s2ctx_t* s2ctx, int minDR, int maxDR, rps_t* min_rps, rps_t* max_rps) {
    *min_rps = *max_rps = RPS_ILLEGAL;
    bool any125kHz = false;
    for( int dr=minDR; dr<=maxDR; dr++ ) {
        rps_t rps = s2e_dr2rps(s2ctx, dr);
        if( rps != RPS_FSK && rps_bw(rps) == BW125 ) {
            any125kHz = true;
            *min_rps = rps;
            if( *max_rps == RPS_ILLEGAL ) *max_rps = rps;
        }
    }
    return any125kHz;
}

inline static void upch_insert (chdefl_t* upchs, uint idx, u4_t freq, u1_t bw, u1_t minSF, u2_t maxSF) {
    if( idx >= MAX_UPCHNLS ) return;
    upchs->freq[idx] = freq;
    upchs->rps[idx].bw = bw;
    upchs->rps[idx].minSF = minSF;
    upchs->rps[idx].maxSF = maxSF;
}

static int handle_router_config (s2ctx_t* s2ctx, ujdec_t* D) {
    char hwspec[MAX_HWSPEC_SIZE] = { 0 };
    ujbuf_t sx130xconf = { .buf=NULL };
    ujcrc_t field;
    u1_t ccaDisabled=0, dcDisabled=0, dwellDisabled=0;   // fields not present
    s2_t max_eirp = 100 * TXPOW_SCALE;  // special value - no setting requested
    int jlistlen = 0;
    chdefl_t upchs = {{0}};
    int chslots = 0;
    s2bcn_t bcn = { 0 };

    s2ctx->txpow = 14 * TXPOW_SCALE;  // builtin default

    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_freq_range: {
            uj_enterArray(D);
            s2ctx->min_freq = (uj_nextSlot(D), uj_uint(D));
            s2ctx->max_freq = (uj_nextSlot(D), uj_uint(D));
            uj_exitArray(D);
            break;
        }
        case J_DRs: {
            int dr = 0;
            uj_enterArray(D);
            while( uj_nextSlot(D) >= 0 ) {
                uj_enterArray(D);
                int sfin   = (uj_nextSlot(D), uj_int(D));
                int bwin   = (uj_nextSlot(D), uj_int(D));
                int dnonly = (uj_nextSlot(D), uj_int(D));
                uj_exitArray(D);
                if( sfin < 0 ) {
                    s2ctx->dr_defs[dr] = RPS_ILLEGAL;
                } else {
                    // We're not tracking/checking dnonly right now
                    int bw = bwin==125 ? BW125 : bwin==250 ? BW250 : BW500;
                    int sf = 12-sfin;
                    rps_t rps = (sfin==0 ? FSK : rps_make(sf,bw)) | (dnonly ? RPS_DNONLY : 0);
                    s2ctx->dr_defs[dr] = rps;
                }
                dr = min(DR_CNT-1, dr+1);
            }
            uj_exitArray(D);
            break;
        }
        case J_upchannels: {
            uj_enterArray(D);
            while( uj_nextSlot(D) >= 0 ) {
                if( chslots > MAX_UPCHNLS-1 ) {
                    uj_skipValue(D);
                    continue;
                }
                uj_enterArray(D);
                u4_t freq = (uj_nextSlot(D), uj_int(D));
                int insert = chslots;
                while( insert > 0 && upchs.freq[insert-1] > freq ) {
                    upch_insert(&upchs, insert, upchs.freq[insert-1],
                        BWNIL, upchs.rps[insert-1].minSF, upchs.rps[insert-1].maxSF);
                    insert--;
                }
                int minDR = (uj_nextSlot(D), uj_intRange(D, 0, 8-1)); // Currently all upchannel DRs must be specified within DRs 0-7
                int maxDR = (uj_nextSlot(D), uj_intRange(D, 0, 8-1)); // Currently all upchannel DRs must be specified within DRs 0-7
                upch_insert(&upchs, insert, freq, BWNIL, minDR, maxDR);
                uj_exitArray(D);
                chslots++;
            }
            uj_exitArray(D);
            break;
        }
        case J_NetID: {
            if( !uj_null(D) ) {
                for( int i=0; i<4; i++ )
                    s2e_netidFilter[i] = 0;
                uj_enterArray(D);
                while( uj_nextSlot(D) >= 0 ) {
                    u4_t netid = uj_uint(D);
                    s2e_netidFilter[(netid >> 5) & 3] |= 1 << (netid & 0x1F);
                }
                uj_exitArray(D);
            } else {
                for( int i=0; i<4; i++ )
                    s2e_netidFilter[i] = 0xffFFffFF;
            }
            break;
        }
        case J_JoinEUI: {
            rt_joineui = "JoinEUI";
            rt_deveui  = "DevEUI";
            // FALL THRU
        }
        case J_JoinEui: {
            for( int i=0; i<2*MAX_JOINEUI_RANGES; i++ )
                s2e_joineuiFilter[i] = 0;
            if( !uj_null(D) ) {
                uj_enterArray(D);
                int off;
                while( (off = uj_nextSlot(D)) >= 0 ) {
                    uj_enterArray(D);
                    if( off < MAX_JOINEUI_RANGES ) {
                        s2e_joineuiFilter[2*off+0] = (uj_nextSlot(D), uj_int(D));
                        s2e_joineuiFilter[2*off+1] = (uj_nextSlot(D), uj_int(D));
                    } else {
                        LOG(MOD_S2E|ERROR, "Too many Join EUI filter ranges - max %d supported", MAX_JOINEUI_RANGES);
                    }
                    uj_exitArray(D);
                }
                uj_exitArray(D);
                jlistlen = min(off, MAX_JOINEUI_RANGES);
                s2e_joineuiFilter[2*jlistlen] = 0; // terminate list
                
            }
            break;
        }
        case J_region: {
            const char* region_s = uj_str(D);
            ujcrc_t region = D->str.crc;
            switch( region ) {
            case J_EU863: { // non-std obsolete naming
                region = J_EU868;
                region_s = "EU868";
                // FALL THRU
            }
            case J_EU868: { // common region name
                s2ctx->canTx  = s2e_canTxEU868;
                s2ctx->txpow  = 16 * TXPOW_SCALE;
                s2ctx->txpow2 = 27 * TXPOW_SCALE;
                s2ctx->txpow2_freq[0] = 869400000;
                s2ctx->txpow2_freq[1] = 869650000;
                resetDC(s2ctx, 3600/100);  // 100s / 1h cummulative on time under PSA = ~2.78%
                break;
            }
            case J_IL915: {
                s2ctx->txpow  = 14 * TXPOW_SCALE;
                s2ctx->txpow2 = 20 * TXPOW_SCALE;
                s2ctx->txpow2_freq[0] = 916200000;
                s2ctx->txpow2_freq[1] = 916400000;
                resetDC(s2ctx, 100);      // 1%
                break;
            }
            case J_KR920: {
                s2ctx->ccaEnabled = 1;
                s2ctx->canTx = s2e_canTxPerChnlDC;
                s2ctx->txpow = 23 * TXPOW_SCALE;
                resetDC(s2ctx, 50);      // 2%
                break;
            }
            case J_AS923JP: { // non-std obsolete naming
                region = J_AS923_1;
                region_s = "AS923-1";
                // FALL THRU
            }
            case J_AS923_1: { // common region name
                s2ctx->ccaEnabled = 1;
                s2ctx->canTx = s2e_canTxPerChnlDC;
                s2ctx->txpow = 13 * TXPOW_SCALE;
                resetDC(s2ctx, 10);      // 10%
                break;
            }
            case J_US902: { // non-std obsolete naming
                region = J_US915;
                region_s = "US915";
                // FALL THRU
            }
            case J_US915: { // common region name
                s2ctx->txpow = 26 * TXPOW_SCALE;
                break;
            }
            case J_AU915: {
                s2ctx->txpow = 30 * TXPOW_SCALE;
                break;
            }
            default: {
                LOG(MOD_S2E|WARNING, "Unrecognized region: %s - ignored", region_s);
                s2ctx->txpow = 14 * TXPOW_SCALE;
                region = 0;
                break;
            }
            }
            snprintf(s2ctx->region_s, sizeof(s2ctx->region_s), "%s", region_s);
            s2ctx->region = region;
            break;
        }
        case J_max_eirp: { // Request a specific max value - see below for if it takes effect
            max_eirp = (s2_t)(uj_num(D) * TXPOW_SCALE);
            break;
        }
        case J_MuxTime: {
            s2e_updateMuxtime(s2ctx, uj_num(D), 0);
            rt_utcOffset = s2ctx->muxtime*1e6 - s2ctx->reftime;
            rt_utcOffset_ts = s2ctx->reftime;
            break;
        }
        case J_hwspec: {
            str_t s = uj_str(D);
            if( D->str.len > sizeof(hwspec)-1 )
                uj_error(D, "Hardware specifier is too long");
            strcpy(hwspec, s);
            break;
        }
#if defined(CFG_prod)
        case J_nocca:
        case J_nodc:
        case J_nodwell:
        case J_device_mode: {
            LOG(MOD_S2E|WARNING, "Feature not supported in production level code (router_config) - ignored: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
#else // !defined(CFG_prod)
        case J_nocca: {
            ccaDisabled = uj_bool(D) ? 2 : 1;
            break;
        }
        case J_nodc: {
            dcDisabled = uj_bool(D) ? 2 : 1;
            break;
        }
        case J_nodwell: {
            dwellDisabled = uj_bool(D) ? 2 : 1;
            break;
        }
        case J_device_mode: {
            sys_deviceMode = uj_bool(D) ? 1 : 0;
            break;
        }
#endif // !defined(CFG_prod)
        case J_sx1301_conf:
        case J_SX1301_conf:
        case J_sx1302_conf:
        case J_SX1302_conf:
        case J_radio_conf: {
            // Processed in ral layer
            sx130xconf = uj_skipValue(D);
            break;
        }
        case J_msgtype: {
            // Silently ignored fields
            uj_skipValue(D);
            break;
        }
        case J_bcning: {
            if( uj_null(D) )
                break;
            uj_enterObject(D);
            while( (field = uj_nextField(D)) ) {
                switch(field) {
                case J_DR: {
                    bcn.ctrl = (uj_uint(D) & 0xF) | (bcn.ctrl & 0xF0);
                    break;
                }
                case J_layout: {
                    uj_enterArray(D);
                    bcn.layout[0] = (uj_nextSlot(D), uj_uint(D));
                    bcn.layout[1] = (uj_nextSlot(D), uj_uint(D));
                    bcn.layout[2] = (uj_nextSlot(D), uj_uint(D));
                    uj_exitArray(D);
                    break;
                }
                case J_freqs: {
                    uj_enterArray(D);
                    int off = 0;
                    while( uj_nextSlot(D) >= 0 ) {
                        if( off < SIZE_ARRAY(bcn.freqs) ) {
                            bcn.freqs[off++] = uj_int(D);
                        } else {
                            LOG(MOD_S2E|ERROR, "Too many beacon frequencies: %d - max %d supported", off, SIZE_ARRAY(bcn.freqs));
                        }
                    }
                    uj_exitArray(D);
                    bcn.ctrl = (bcn.ctrl & 0xF) | (off<<4);
                    break;
                }
                default: {
                    LOG(MOD_S2E|WARNING, "Unknown field in router_config.bcning - ignored: %s (0x%X)", D->field.name, D->field.crc);
                    uj_skipValue(D);
                    break;
                }
                }
            }
            uj_exitObject(D);
            break;
        }
        default: {
            LOG(MOD_S2E|WARNING, "Unknown field in router_config - ignored: %s (0x%X)", D->field.name, D->field.crc);
            uj_skipValue(D);
            break;
        }
        }
    }
    if( !hwspec[0] ) {
        LOG(MOD_S2E|ERROR, "No 'hwspec' in 'router_config' message");
        return 0;
    }
    if( sx130xconf.buf == NULL ) {
        LOG(MOD_S2E|ERROR, "No 'sx1301_conf' or 'sx1302_conf' in 'router_config' message");
        return 0;
    }
    int chdefs = chslots;
    for( int chslot=0; chslot<chdefs && upchs.freq[chslot]; chslot++ ) {
        int minDR = upchs.rps[chslot].minSF;
        int maxDR = upchs.rps[chslot].maxSF;
        rps_t rps0 = RPS_ILLEGAL;
        rps_t rps1 = RPS_ILLEGAL;
        if( any125kHz(s2ctx, minDR, maxDR, &rps0, &rps1) ) {
            upch_insert(&upchs, chslot,
                upchs.freq[chslot], BW125, rps_sf(rps0), rps_sf(rps1));
        }
        rps0 = RPS_ILLEGAL;
        if( hasFastLora(s2ctx, minDR, maxDR, &rps0) ) {
            upch_insert(&upchs, upchs.rps[chslot].bw == BWNIL ? chslot : chslots++,
                upchs.freq[chslot], rps_bw(rps0), rps_sf(rps0), rps_sf(rps0));
        }
        if( hasFSK(s2ctx, minDR, maxDR) ) {
            upch_insert(&upchs, upchs.rps[chslot].bw == BWNIL ? chslot : chslots++,
            upchs.freq[chslot], 0, FSK, FSK);
        }
    }
    ts_iniTimesync();
    if( !ral_config(hwspec,
                    s2ctx->ccaEnabled ? s2ctx->region : 0,
                    sx130xconf.buf, sx130xconf.bufsize,
                    &upchs) ) {
        return 0;
    }
    // Override local settings with server settings if provided
    if( ccaDisabled   ) s2e_ccaDisabled   = ccaDisabled   & 2;
    if( dcDisabled    ) s2e_dcDisabled    = dcDisabled    & 2;
    if( dwellDisabled ) s2e_dwellDisabled = dwellDisabled & 2;
    if( max_eirp != 100*TXPOW_SCALE ) {
        if( s2ctx->region==0 || max_eirp < s2ctx->txpow ) {
            // If no region specified use max_eirp whatever the value
            // If a region was specified (unknown region = 14dBm) then only allow lowering
            s2ctx->txpow = max_eirp;
        }
        if( max_eirp < s2ctx->txpow2 ) {
            s2ctx->txpow2 = max_eirp;
        }
    }
    LOG(MOD_S2E|INFO, "Configuring for region: %s%s -- %F..%F",
        s2ctx->region_s, s2ctx->ccaEnabled ? " (CCA)":"", s2ctx->min_freq, s2ctx->max_freq);
    if( log_shallLog(MOD_S2E|INFO) ) {
        for( int dr=0; dr<16; dr++ ) {
            int rps = s2ctx->dr_defs[dr];
            if( rps == RPS_ILLEGAL ) {
                LOG(MOD_S2E|INFO, "  DR%-2d undefined", dr);
            } else {
                LOG(MOD_S2E|INFO, "  DR%-2d %R %s", dr, rps, rps & RPS_DNONLY ? "(DN only)" : "");
            }
        }
        LOG(MOD_S2E|INFO,
            "  TX power: %.1f dBm EIRP",
            s2ctx->txpow/(double)TXPOW_SCALE);
        if( s2ctx->txpow2_freq[0] ) {
            LOG(MOD_S2E|INFO, "            %.1f dBm EIRP for %F..%F",
                s2ctx->txpow2/(double)TXPOW_SCALE, s2ctx->txpow2_freq[0], s2ctx->txpow2_freq[1]);
        }
        LOG(MOD_S2E|INFO, "  %s list: %d entries", rt_joineui, jlistlen);
        LOG(MOD_S2E|INFO, "  NetID filter: %08X-%08X-%08X-%08X",
            s2e_netidFilter[3], s2e_netidFilter[2], s2e_netidFilter[1], s2e_netidFilter[0]);
        LOG(MOD_S2E|INFO, "  Dev/test settings: nocca=%d nodc=%d nodwell=%d",
            (s2e_ccaDisabled!=0), (s2e_dcDisabled!=0), (s2e_dwellDisabled!=0));
    }
    if( (bcn.ctrl&0xF0) != 0 ) {
        // At least one beacon frequency was specified
        LOG(MOD_S2E|INFO, "Beaconing every %~T on %F(%d) @ DR%d (frame layout %d/%d/%d)",
            BEACON_INTVL, bcn.freqs[0],
            (bcn.ctrl>>4), bcn.ctrl & 0xF,
            bcn.layout[0], bcn.layout[1], bcn.layout[2]);
        s2ctx->bcn = bcn;
        s2e_bcntimeout(&s2ctx->bcntimer);
    }
    return 1;
}


// Obsolete message format - newer servers use dnmsg which carries more context!
void handle_dnframe (s2ctx_t* s2ctx, ujdec_t* D) {
    ustime_t now = rt_getTime();
    txjob_t* txjob = txq_reserveJob(&s2ctx->txq);
    if( txjob == NULL ) {
        LOG(MOD_S2E|ERROR, "Out of TX jobs - dropping incoming message");
        return;
    }
    int flags = 0;
    ujcrc_t field;
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_msgtype: {
            uj_skipValue(D);
            break;
        }
        case J_DR: {
            check_dr(s2ctx, D, &txjob->dr);
            flags |= 0x01;
            break;
        }
        case J_Freq: {
            check_dnfreq(s2ctx, D, &txjob->freq, &txjob->dnchnl);
            flags |= 0x02;
            break;
        }
        case J_DevEUI:
        case J_DevEui: {
            txjob->deveui = uj_eui(D);
            flags |= 0x04;
            break;
        }
        case J_xtime: {
            txjob->xtime = uj_int(D);
            flags |= 0x08;
            break;
        }
        case J_asap: {
            if( uj_bool(D) )
                txjob->txflags |= TXFLAG_CLSC;
            break;
        }
        case J_seqno:   // older server (remove if obsoleted)
        case J_diid: {  // newer servers use this field name
            txjob->diid = uj_int(D);
            flags |= 0x10;
            break;
        }
        case J_MuxTime: {
            s2e_updateMuxtime(s2ctx, uj_num(D), now);
            break;
        }
        case J_pdu: {
            uj_str(D);
            int xlen = D->str.len/2;
            u1_t* p = txq_reserveData(&s2ctx->txq, xlen);
            if( p == NULL ) {
                uj_error(D, "Out of TX data space");
                return;
            }
            txjob->len = uj_hexstr(D, p, xlen);
            flags |= 0x20;
            break;
        }
        case J_rctx: {
            txjob->rctx = uj_int(D);
            flags |= 0x40;
            break;
        }
        default: {
            LOG(MOD_S2E|WARNING, "Unknown field in dnframe - ignored: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
        }
    }
    if( (flags & 0x40) == 0 ) {
        txjob->rctx = ral_xtime2rctx(txjob->xtime);
        flags |= 0x40;
    }
    if( flags != 0x7F ) {
        LOG(MOD_S2E|WARNING, "Some mandatory fields are missing (flags=0x%X)", flags);
        return;
    }
    txjob->txtime = ts_xtime2ustime(txjob->xtime);
    if( txjob->xtime == 0 || txjob->txtime == 0 ) {
        LOG(MOD_S2E|ERROR, "%J - dropped due to time conversion problems (MCU/GPS out of sync, obsolete input) - xtime=%ld", txjob, txjob->xtime);
        return;  // illegal/obsolete xtime
    }
    txq_commitJob(&s2ctx->txq, txjob);
    if( !s2e_addTxjob(s2ctx, txjob, /*initial placement*/0, now) )
        txq_freeJob(&s2ctx->txq, txjob);
}


void handle_dnmsg (s2ctx_t* s2ctx, ujdec_t* D) {
    ustime_t now = rt_getTime();
    txjob_t* txjob = txq_reserveJob(&s2ctx->txq);
    if( txjob == NULL ) {
        LOG(MOD_S2E|ERROR, "Out of TX jobs - dropping incoming message");
        return;
    }
    int flags = 0;
    ujcrc_t field;
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_msgtype: {
            uj_skipValue(D);
            break;
        }
        case J_DevEUI:
        case J_DevEui: {
            txjob->deveui = uj_eui(D);
            flags |= 0x01;
            break;
        }
        case J_dC: {
            int txflags = 0, dc = uj_intRange(D, 0, 2);
            switch(dc) {
                case 0: txflags = TXFLAG_CLSA; break;
                case 1: txflags = TXFLAG_PING; break;
                case 2: txflags = TXFLAG_CLSC; break;
            }
            txjob->txflags = txflags;
            flags |= 0x02;
            break;
        }
        case J_seqno:   // older server (remove if obsoleted)
        case J_diid: {  // newer servers use this field name
            txjob->diid = uj_int(D);
            flags |= 0x04;
            break;
        }
        case J_pdu: {
            uj_str(D);
            int xlen = D->str.len/2;
            if( xlen > 255 ) {
                uj_error(D, "TX pdu too large. Maximum is 255 bytes.");
                return;
            }
            u1_t* p = txq_reserveData(&s2ctx->txq, xlen);
            if( p == NULL ) {
                uj_error(D, "Out of TX data space");
                return;
            }
            txjob->len = uj_hexstr(D, p, xlen);
            flags |= 0x08;
            break;
        }
        case J_RxDelay: {
            // Map zero to one
            txjob->rxdelay = max(1, uj_intRange(D, 0, 15));
            flags |= 0x10;
            break;
        }
        case J_priority: {
            txjob->prio = uj_intRange(D, 0, 255);
            break;
        }
        case J_dnmode: {
            // Currently not needed to make decisions
            //str_t mode = uj_str(D); // "updn" or "dn"
            //dnmode = (mode[0]=='d' && mode[1]=='n');
            uj_skipValue(D);
            break;
        }
        case J_xtime: {  // 0==absent
            txjob->xtime = uj_int(D);
            break;
        }
        case J_DR: {
            txjob->rxdelay = 0;
            flags |= 0x10;  // rxdelay flag - RxDelay is implicitly 0
            // FALL THRU
        }
        case J_RX1DR: {
            check_dr(s2ctx, D, &txjob->dr);
            flags |= 0x0100;
            break;
        }
        case J_Freq:
        case J_RX1Freq: {
            check_dnfreq(s2ctx, D, &txjob->freq, &txjob->dnchnl);
            flags |= 0x0200;
            break;
        }
        case J_RX2DR: {
            check_dr(s2ctx, D, &txjob->rx2dr);
            flags |= 0x0400;
            break;
        }
        case J_RX2Freq: {
            check_dnfreq(s2ctx, D, &txjob->rx2freq, &txjob->dnchnl2);
            flags |= 0x0800;
            break;
        }
        case J_MuxTime: {
            s2e_updateMuxtime(s2ctx, uj_num(D), now);
            break;
        }
        case J_rctx: {
            txjob->rctx = uj_int(D);
            flags |= 0x1000;
            break;
        }
        case J_gpstime: {  // GPS microseconds
            txjob->gpstime = uj_uint(D);
            break;
        }
        case J_preamble: {
            txjob->preamble = uj_uint(D);
            break;
        }
        case J_addcrc: {
            txjob->addcrc = uj_uint(D);
            break;
        }
        default: {
            LOG(MOD_S2E|WARNING, "Unknown field in dnmsg - ignored: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
        }
    }
    if ( (flags & 0x10) != 0x10) {
        // Map zero to one
        txjob->rxdelay = 1;
        flags |= 0x10;
        LOG(MOD_S2E|WARNING, "RxDelay mapped to 1 as it was not present!");
    }
    if( (flags & 0x1F) != 0x1F ||
        // flags & 0x300 in {0x000,0x300}  RX1DR/RX1Freq both present/absent
        ((1 << ((flags >> 8) & 3)) & ((1<<3)|(1<<0))) == 0 ||
        // flags & 0xC00 in {0x000,0x300}  -- ditto RX2
        ((1 << ((flags >> 10) & 3)) & ((1<<3)|(1<<0))) == 0 ) {
        LOG(MOD_S2E|WARNING, "Some mandatory fields are missing (flags=0x%X)", flags);
        return;
    }
    if( (flags & 0x1000) == 0 && txjob->xtime ) {
        // We have no rctx but xtime - set it with radio unit from xtime
        // If no xtime field was provided rctx defaults to zero
        txjob->rctx = ral_xtime2rctx(txjob->xtime);
    }
    txjob->txunit = ral_rctx2txunit(txjob->rctx);

    if( (txjob->txflags & TXFLAG_PING) ) {
        txjob->xtime  = ts_gpstime2xtime(txjob->txunit, txjob->gpstime);
        txjob->txtime = ts_xtime2ustime(txjob->xtime);
    }
    else {
        if( txjob->xtime != 0 ) {
            txjob->xtime += txjob->rxdelay * 1000000;
            txjob->txtime = ts_xtime2ustime(txjob->xtime);
        }
        if( txjob->freq == 0 ) {
            // Switch over to RX2:
            //  class A (device class A/C) - no RX1 provided
            //  class C spontaneous dn: - no RX1 provided
            if( txjob->rx2freq == 0 ) {
                LOG(MOD_S2E|WARNING, "Ignoring 'dnmsg' with neither RX1/RX2 frequencies");
                return;
            }
            if( !altTxTime(s2ctx, txjob, now+TX_AIM_GAP) ) {
                LOG(MOD_S2E|WARNING, "Ignoring 'dnmsg' with no viable RX2");
                return;
            }
        }
    }
    if( txjob->xtime == 0 || txjob->txtime == 0 ) {
        LOG(MOD_S2E|ERROR, "%J - dropped due to time conversion problems (MCU/GPS out of sync, obsolete input) - xtime=%ld", txjob, txjob->xtime);
        return;
    }
    txq_commitJob(&s2ctx->txq, txjob);
    if( !s2e_addTxjob(s2ctx, txjob, /*initial placement*/0, now) )
        txq_freeJob(&s2ctx->txq, txjob);
}


void handle_dnsched (s2ctx_t* s2ctx, ujdec_t* D) {
    ustime_t now = rt_getTime();
    ujcrc_t field;
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_msgtype: {
            uj_skipValue(D);
            break;
        }
        case J_MuxTime: {
            s2e_updateMuxtime(s2ctx, uj_num(D), now);
            break;
        }
        case J_schedule: {
            int slot;
            uj_enterArray(D);
            while( (slot = uj_nextSlot(D)) >= 0 ) {
                txjob_t* txjob = txq_reserveJob(&s2ctx->txq);
                if( txjob == NULL ) {
                    uj_error(D, "Out of TX jobs - stopping parsing of 'dnsched' message");
                    return;
                }
                int flags = 0;
                uj_enterObject(D);
                while( (field = uj_nextField(D)) ) {
                    switch(field) {
                    case J_diid: {  // newer servers use this field name
                        txjob->diid = uj_int(D);
                        break;
                    }
                    case J_priority: {
                        txjob->prio = uj_intRange(D, 0, 255);
                        break;
                    }
                    case J_DR: {
                        check_dr(s2ctx, D, &txjob->dr);
                        flags |= 0x01;
                        break;
                    }
                    case J_Freq: {
                        check_dnfreq(s2ctx, D, &txjob->freq, &txjob->dnchnl);
                        flags |= 0x02;
                        break;
                    }
                    case J_ontime: {   // GPS seconds - no fractions currently
                        txjob->gpstime = rt_seconds(uj_uint(D));
                        flags |= 0x04;
                        break;
                    }
                    case J_gpstime: {  // GPS microseconds
                        txjob->gpstime = uj_uint(D);
                        flags |= 0x04;
                        break;
                    }
                    case J_xtime: {    // send on xtime
                        txjob->xtime = uj_uint(D);
                        flags |= 0x04;
                        break;
                    }
                    case J_pdu: {
                        uj_str(D);
                        int xlen = D->str.len/2;
                        u1_t* p = txq_reserveData(&s2ctx->txq, xlen);
                        if( p == NULL ) {
                            uj_error(D, "Out of TX data space");
                            return;
                        }
                        txjob->len = uj_hexstr(D, p, xlen);
                        flags |= 0x08;
                        break;
                    }
                    case J_rctx: {
                        txjob->rctx = uj_int(D);
                        break;
                    }
                    case J_preamble: {
                        txjob->preamble = uj_uint(D);
                        break;
                    }
                    case J_addcrc: {
                        txjob->addcrc = uj_uint(D);
                        break;
                    }
                    default: {
                        LOG(MOD_S2E|WARNING, "Unknown field in dnsched.schedule[%d] - ignored: %s", slot, D->field.name);
                        uj_skipValue(D);
                        break;
                    }
                    }
                }
                if( flags != 0xF ) {
                    LOG(MOD_S2E|WARNING, "Some mandatory fields in dnsched.schedule[%d] are missing (flags=0x%X)", slot, flags);
                } else {
                    u1_t txunit = ral_rctx2txunit(txjob->rctx);
                    txjob->txunit = txunit;
                    if( txjob->gpstime ) {
                        txjob->xtime = ts_gpstime2xtime(txunit, txjob->gpstime);
                        txjob->txtime = ts_xtime2ustime(txjob->xtime);
                        txjob->txflags = TXFLAG_PING;
                    } else {
                        txjob->txtime = ts_xtime2ustime(txjob->xtime);
                        txjob->txflags = TXFLAG_CLSA;
                    }
                    if( txjob->txtime != 0 ) {
                        LOG(MOD_S2E|INFO, "DNSCHED diid=%ld %>T %~T DR%-2d %F - %d bytes",
                            txjob->diid, rt_ustime2utc(txjob->txtime), txjob->txtime-now, txjob->dr, txjob->freq, txjob->len);
                        txq_commitJob(&s2ctx->txq, txjob);
                        if( !s2e_addTxjob(s2ctx, txjob, /*initial placement*/0, now) )
                            txq_freeJob(&s2ctx->txq, txjob);
                    } else {
                        LOG(MOD_S2E|ERROR, "DNSCHED failed to convert %stime: %ld",
                            txjob->gpstime ? "gps":"x",
                            txjob->gpstime ? txjob->gpstime : txjob->xtime);
                    }
                }
                uj_exitObject(D);
            }
            uj_exitArray(D);
            break;
        }
        default: {
            LOG(MOD_S2E|WARNING, "Unknown field in dnsched - ignored: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
        }
    }
}


void handle_timesync (s2ctx_t* s2ctx, ujdec_t* D) {
    ustime_t rxtime = rt_getTime();
    ustime_t txtime = 0;
    ustime_t xtime  = 0;
    sL_t     gpstime = 0;
    ujcrc_t  field;
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_msgtype: {
            uj_skipValue(D);
            break;
        }
        case J_xtime: {
            xtime = uj_int(D);
            break;
        }
        case J_txtime: {
            txtime = uj_int(D);
            break;
        }
        case J_gpstime: {
            gpstime = uj_int(D);
            break;
        }
        case J_MuxTime: {
            s2e_updateMuxtime(s2ctx, uj_num(D), rxtime);
            break;
        }
        default: {
            LOG(MOD_S2E|WARNING, "Unknown field in timesync - ignored: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
        }
    }
    if( xtime )
        ts_setTimesyncLns(xtime, gpstime);
    if( txtime && gpstime )
        ts_processTimesyncLns(txtime, rxtime, gpstime);
}


void handle_getxtime (s2ctx_t* s2ctx, ujdec_t* D) {
    // No fields required - skip everything
    ujcrc_t field;
    double  muxtime = 0;
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_msgtype: {
            uj_skipValue(D);
            break;
        }
        case J_MuxTime: {
            muxtime = uj_num(D);
            break;
        }
        default: {
            LOG(MOD_S2E|WARNING, "Unknown field in getxtime - ignored: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
        }
    }
    // Get a send buffer - parse frame / check filter
    ujbuf_t sendbuf = (*s2ctx->getSendbuf)(s2ctx, MIN_UPJSON_SIZE);
    if( sendbuf.buf == NULL ) {
        // Websocket has no space - WS will call again
        return;
    }
    ustime_t ustime = rt_getTime();
    uj_encOpen(&sendbuf, '{');
    uj_encKVn(&sendbuf,
              "msgtype",  's', "getxtime",
              "MuxTime",  'T', muxtime,
              "ustime",   'T', ustime/1e6,
              "UTCtime",  'T', rt_ustime2utc(ustime)/1e6,
              "xtimes",   '[', 0);
    for( int txunit=0; txunit<MAX_TXUNITS; txunit++ ) {
        sL_t xtime = ts_ustime2xtime(txunit, ustime);
        uj_encInt(&sendbuf, xtime);
    }
    uj_encClose(&sendbuf, ']');
    uj_encClose(&sendbuf, '}');
    if( !xeos(&sendbuf) ) {
        LOG(MOD_S2E|ERROR, "JSON encoding exceeds available buffer space: %d", sendbuf.bufsize);
    } else {
        (*s2ctx->sendText)(s2ctx, &sendbuf);
        assert(sendbuf.buf==NULL);
    }
}


void handle_runcmd (s2ctx_t* s2ctx, ujdec_t* D) {
    ujcrc_t field;
    char* argv[MAX_CMDARGS+2] = { NULL };
    int argc = 1;
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_msgtype: {
            uj_skipValue(D);
            break;
        }
        case J_command: {
            argv[0] = uj_str(D);
            break;
        }
        case J_arguments: {
            uj_enterArray(D);
            while( uj_nextSlot(D) >= 0 ) {
                if( argc <= MAX_CMDARGS )
                    argv[argc] = uj_str(D);
                argc++;
            }
            uj_exitArray(D);
            break;
        }
        default: {
            LOG(MOD_S2E|WARNING, "Unknown field in runcmd - ignored: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
        }
    }
    if( !argv[0] ) {
        LOG(MOD_S2E|ERROR, "No command provided - runcmd ignored");
        return;
    }
    if( argc > MAX_CMDARGS+1 ) {
        LOG(MOD_S2E|WARNING, "Too many arguments (max %d but got %d) - runcmd ignored", MAX_CMDARGS, argc-1);
        return;
    }
    argv[argc] = NULL;
    sys_execCommand(0, (str_t*)argv); // 0: detach cmd and don't wait for completion
}



// --------------------------------------------------------------------------------
//
// Decode incoming JSON records
//
// --------------------------------------------------------------------------------

int s2e_onMsg (s2ctx_t* s2ctx, char* json, ujoff_t jsonlen) {
    ujdec_t D;
    uj_iniDecoder(&D, json, jsonlen);
    ujcrc_t msgtype = uj_msgtype(&D);
    if( uj_decode(&D) ) {
        LOG(MOD_S2E|ERROR, "Parsing of JSON message failed - ignored");
        return 1;   // return fail? would trigger a reconnect
    }
    if( s2ctx->region == 0 && (msgtype == J_dnmsg || msgtype == J_dnsched || msgtype == J_dnframe) ) {
        // Might happen if messages are still queued
        LOG(MOD_S2E|WARNING, "Received '%.*s' before 'router_config' - dropped", D.str.len, D.str.beg);
        return 1;
    }
    // All JSON data must be a single object per frame
    uj_nextValue(&D);
    uj_enterObject(&D);
    int ok = 1;

    switch(msgtype) {
    case 0: {
        LOG(MOD_S2E|ERROR, "No msgtype - ignored");
        break;
    }
    case J_router_config: {
        ok = handle_router_config(s2ctx, &D);
        if( ok ) sys_inState(SYSIS_TC_CONNECTED);
        break;
    }
    case J_dnframe: {
        LOG(MOD_S2E|ERROR, "Received obsolete 'dnframe' message!");
        handle_dnframe(s2ctx, &D);
        break;
    }
    case J_dnmsg: {
        handle_dnmsg(s2ctx, &D);
        break;
    }
    case J_dnsched: {
        handle_dnsched(s2ctx, &D);
        break;
    }
    case J_timesync: {
        handle_timesync(s2ctx, &D);
        break;
    }
    case J_getxtime: {
        handle_getxtime(s2ctx, &D);
        break;
    }
    case J_runcmd: {
        handle_runcmd(s2ctx, &D);
        break;
    }
    case J_rmtsh: {
        s2e_handleRmtsh(s2ctx, &D);
        break;
    }
    case J_error: {
        ujcrc_t  field;
        while( (field = uj_nextField(&D)) ) {
            switch(field) {
            case J_error: {
                LOG(MOD_S2E|WARNING, "LNS ERROR Msg: %s", uj_str(&D));
                break;
            }
            default: {
                uj_skipValue(&D);
                break;
            }
            }
        }
        break;
    }
    default: {
        // Platform specific commands
        if( !s2e_handleCommands(msgtype, s2ctx, &D) )
            uj_error(&D, "Unknown msgtype: %.*s", D.str.len, D.str.beg);
        break;
    }
    }
    uj_exitObject(&D);
    uj_assertEOF(&D);
    return ok;
}


#if defined(CFG_no_rmtsh)
void s2e_handleRmtsh (s2ctx_t* s2ctx, ujdec_t* D) {
    uj_error(D, "Rmtsh not implemented");
}

int s2e_onBinary (s2ctx_t* s2ctx, u1_t* data, ujoff_t datalen) {
    LOG(MOD_S2E|ERROR, "Ignoring rmtsh binary data (%d bytes)", datalen);
    return 0;
}
#endif

