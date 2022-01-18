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

#if defined(CFG_lgw1)

#if (!defined(CFG_ral_lgw) && !defined(CFG_ral_master_slave)) || (defined(CFG_ral_lgw) && defined(CFG_ral_master_slave))
#error Exactly one of the two params must be set: CFG_ral_lgw CFG_ral_master_slave
#endif

#include "s2conf.h"
#include "tc.h"
#include "timesync.h"
#include "sys.h"
#include "sx130xconf.h"
#include "ral.h"
#include "lgw/loragw_reg.h"
#include "lgw/loragw_hal.h"
#if defined(CFG_sx1302)
#include "lgw/loragw_sx1302_timestamp.h"
extern timestamp_counter_t counter_us; // from loragw_sx1302.c
#endif // defined(CFG_sx1302)

#define RAL_MAX_RXBURST 10

#define FSK_BAUD      50000
#define FSK_FDEV      25  // [kHz]
#define FSK_PRMBL_LEN 5

static const u2_t SF_MAP[] = {
    [SF12 ]= DR_LORA_SF12,
    [SF11 ]= DR_LORA_SF11,
    [SF10 ]= DR_LORA_SF10,
    [SF9  ]= DR_LORA_SF9,
    [SF8  ]= DR_LORA_SF8,
    [SF7  ]= DR_LORA_SF7,
    [FSK  ]= DR_UNDEFINED,
    [SFNIL]= DR_UNDEFINED,
};

static const u1_t BW_MAP[] = {
    [BW125]= BW_125KHZ,
    [BW250]= BW_250KHZ,
    [BW500]= BW_500KHZ,
    [BWNIL]= BW_UNDEFINED
};


static int to_sf (int lgw_sf) {
    for( u1_t sf=SF12; sf<=FSK; sf++ )
        if( SF_MAP[sf] == lgw_sf )
            return sf;
    return SFNIL;
}

static int to_bw (int lgw_bw) {
    for( u1_t bw=BW125; bw<=BW500; bw++ )
        if( BW_MAP[bw] == lgw_bw )
            return bw;
    return BWNIL;
}


rps_t ral_lgw2rps (struct lgw_pkt_rx_s* p) {
    return p->modulation == MOD_LORA
        ? rps_make(to_sf(p->datarate), to_bw(p->bandwidth))
        : FSK;
}


void ral_rps2lgw (rps_t rps, struct lgw_pkt_tx_s* p) {
    assert(rps != RPS_ILLEGAL);
    if( rps_sf(rps) == FSK ) {
        p->modulation = MOD_FSK;
        p->datarate   = FSK_BAUD;
        p->f_dev      = FSK_FDEV;
        p->preamble   = FSK_PRMBL_LEN;
    } else {
        p->modulation = MOD_LORA;
        p->datarate   = SF_MAP[rps_sf(rps)];
        p->bandwidth  = BW_MAP[rps_bw(rps)];
    }
}

int ral_rps2bw (rps_t rps) {
    assert(rps != RPS_ILLEGAL);
    return BW_MAP[rps_bw(rps)];
}

int ral_rps2sf (rps_t rps) {
    assert(rps != RPS_ILLEGAL);
    return SF_MAP[rps_sf(rps)];
}

// Make a clock sync measurement:
//  pps_en      w/o checking on latches PPS xticks
//  last_xtime  read and update last xticks to form a continuous 64 bit time
//  timesync    return measured data - isochronous times for MCU/SX130X and opt. latched PPS
// Return:
//  Measure of quality (absolute value) - caller uses this to weed out bad measurements
//  In this impl. we return the time the measurement took - smallest values are best values
//
int ral_getTimesync (u1_t pps_en, sL_t* last_xtime, timesync_t* timesync) {
    static u4_t last_pps_xticks;
    u4_t pps_xticks = 0;
#if !defined(CFG_sx1302)
    if( pps_en ) {
        // First read last latched value - interval between time syncs needs to be >1s so that a PPS could have happened.
        // Read last latched value - when PPS occurred. If no PPS happened this returns
        // the time when LGW_GPS_EN was set to 1.
        lgw_get_trigcnt(&pps_xticks);
        // lgw1 has a single xtick register which is either PPS latched or free running.
        // We temporarily disable PPS latching to obtain a free running xtick.
        lgw_reg_w(LGW_GPS_EN, 0);  // PPS latch holds current

    }
#endif
    ustime_t t0 = rt_getTime();
    u4_t xticks = 0;
    // Obtain current free running xtick
#if defined(CFG_sx1302)
    timestamp_counter_get(&counter_us, &xticks, &pps_xticks);
#else
    lgw_get_trigcnt(&xticks);
#endif
    ustime_t t1 = rt_getTime();
    sL_t d = (s4_t)(xticks - *last_xtime);
    if( d < 0 ) {
        LOG(MOD_SYN|CRITICAL,
            "SX130x time sync roll over - no update for a long time: xticks=0x%08x last_xtime=0x%lX",
            xticks, *last_xtime);
        d += (sL_t)1<<32;
    }
    timesync->xtime = *last_xtime += d;
    timesync->ustime = (t0+t1)/2;
    timesync->pps_xtime = 0; // Will be set if pps_en is set and valid PPS observation is available
    if( pps_en ) {
        // PPS latch will hold now current xticks
#if !defined(CFG_sx1302)
        lgw_reg_w(LGW_GPS_EN, 1);
#endif
        // Catch behavior when PPS is lost:
        //  - pps_xticks = 0 and pps_xticks = const are illegal PPS observations.
        //  - Upper layer informed by timesync->pps_xtime = 0.
        if( pps_xticks && last_pps_xticks != pps_xticks ) {
            timesync->pps_xtime = timesync->xtime + (s4_t)(pps_xticks - xticks);
            last_pps_xticks = pps_xticks;
        }
    }
    LOG(MOD_SYN|XDEBUG, "SYNC: ustime=0x%012lX (Q=%3d): xticks=0x%08x xtime=0x%lX - PPS: pps_xticks=0x%08x (%u) pps_xtime=0x%lX (pps_en=%d)",
        timesync->ustime, (int)(t1-t0), xticks, timesync->xtime, pps_xticks, pps_xticks, timesync->pps_xtime, pps_en);
    return (int)(t1-t0);
}

#if defined(CFG_ral_lgw)
static u1_t       pps_en;
static s2_t       txpowAdjust;    // scaled by TXPOW_SCALE
static sL_t       last_xtime;
static tmr_t      rxpollTmr;
static tmr_t      syncTmr;



// ATTR_FASTCODE 
static void synctime (tmr_t* tmr) {
    timesync_t timesync;
    int quality = ral_getTimesync(pps_en, &last_xtime, &timesync);
    ustime_t delay = ts_updateTimesync(0, quality, &timesync);
    rt_setTimer(&syncTmr, rt_micros_ahead(delay));
}

u1_t ral_altAntennas (u1_t txunit) {
    return 0;
}


int ral_tx (txjob_t* txjob, s2ctx_t* s2ctx, int nocca) {
    struct lgw_pkt_tx_s pkt_tx;
    memset(&pkt_tx, 0, sizeof(pkt_tx));

    pkt_tx.invert_pol = true;
    pkt_tx.no_header  = false;

    if( txjob->preamble == 0 ) {
        if( txjob->txflags & TXFLAG_BCN ) {
            pkt_tx.tx_mode = ON_GPS;
            pkt_tx.preamble = 10;
            pkt_tx.invert_pol = false;
            pkt_tx.no_header  = true;
        } else {
            pkt_tx.tx_mode = TIMESTAMPED;
            pkt_tx.preamble = 8;
        }
    } else {
        pkt_tx.preamble = txjob->preamble;
    }
    rps_t rps = s2e_dr2rps(s2ctx, txjob->dr);
    ral_rps2lgw(rps, &pkt_tx);
    pkt_tx.freq_hz    = txjob->freq;
    pkt_tx.count_us   = txjob->xtime;
    pkt_tx.rf_chain   = 0;
    pkt_tx.rf_power   = (float)(txjob->txpow - txpowAdjust) / TXPOW_SCALE;
    pkt_tx.coderate   = CR_LORA_4_5;
    pkt_tx.no_crc     = !txjob->addcrc;
    pkt_tx.size       = txjob->len;
    memcpy(pkt_tx.payload, &s2ctx->txq.txdata[txjob->off], pkt_tx.size);

    // NOTE: nocca not possible to implement with current libloragw API
#if defined(CFG_sx1302)
    int err = lgw_send(&pkt_tx);
#else
    int err = lgw_send(pkt_tx);
#endif
    if( err != LGW_HAL_SUCCESS ) {
        if( err != LGW_LBT_ISSUE ) {
            LOG(MOD_RAL|ERROR, "lgw_send failed");
            return RAL_TX_FAIL;
        }
        return RAL_TX_NOCA;
    }
    return RAL_TX_OK;
}


int ral_txstatus (u1_t txunit) {
    u1_t status;
#if defined(CFG_sx1302)
    int err = lgw_status(txunit, TX_STATUS, &status);
#else
    int err = lgw_status(TX_STATUS, &status);
#endif
    if (err != LGW_HAL_SUCCESS) {
        LOG(MOD_RAL|ERROR, "lgw_status failed");
        return TXSTATUS_IDLE;
    }
    if( status == TX_SCHEDULED )
        return TXSTATUS_SCHEDULED;
    if( status == TX_EMITTING )
        return TXSTATUS_EMITTING;
    return TXSTATUS_IDLE;
}


void ral_txabort (u1_t txunit) {
#if defined(CFG_sx1302)
    lgw_abort_tx(txunit);
#else
    lgw_abort_tx();
#endif
}

static void log_rawpkt(u1_t level, str_t msg, struct lgw_pkt_rx_s * pkt_rx) {
    LOG(MOD_RAL|level, "%s[CRC %s] %^.3F %.2f/%.1f %R (mod=%d/dr=%d/bw=%d) xtick=%08x (%u) %d bytes: %64H",
        msg,
        pkt_rx->status == STAT_CRC_OK ? "OK"  : "FAIL",
        pkt_rx->freq_hz,
        pkt_rx->snr,
#if defined(CFG_sx1302)
        pkt_rx->rssis,
#else
        pkt_rx->rssi,
#endif
        ral_lgw2rps(pkt_rx),
        pkt_rx->modulation,
        pkt_rx->datarate,
        pkt_rx->bandwidth,
        pkt_rx->count_us,
        pkt_rx->count_us,
        pkt_rx->size,
        pkt_rx->size, pkt_rx->payload
    );
}

//ATTR_FASTCODE 
static void rxpolling (tmr_t* tmr) {
    int rounds = 0;
    while(rounds++ < RAL_MAX_RXBURST) {
        struct lgw_pkt_rx_s pkt_rx;
        int n = lgw_receive(1, &pkt_rx);
        if( n < 0 || n > 1 ) {
            LOG(MOD_RAL|ERROR, "lgw_receive error: %d", n);
            break;
        }
        if( n==0 ) {
            break;
        }

        rxjob_t* rxjob = !TC ? NULL : s2e_nextRxjob(&TC->s2ctx);
        if( rxjob == NULL ) {
            log_rawpkt(ERROR, "Dropped RX frame - out of space: ", &pkt_rx);
            break; // Allow to flush RX jobs
        }
        if( pkt_rx.status != STAT_CRC_OK ) {
            if( log_shallLog(MOD_RAL|DEBUG) ) {
                log_rawpkt(DEBUG, "", &pkt_rx);
            }
            continue; // silently ignore bad CRC
        }
        if( pkt_rx.size > MAX_RXFRAME_LEN ) {
            // This should not happen since caller provides
            // space for max frame length - 255 bytes
            log_rawpkt(ERROR, "Dropped RX frame - frame size too large: ", &pkt_rx);
            continue;
        }

        memcpy(&TC->s2ctx.rxq.rxdata[rxjob->off], pkt_rx.payload, pkt_rx.size);
        rxjob->len   = pkt_rx.size;
        rxjob->freq  = pkt_rx.freq_hz;
        rxjob->xtime = ts_xticks2xtime(pkt_rx.count_us, last_xtime);
#if defined(CFG_sx1302)
        rxjob->rssi  = (u1_t)-pkt_rx.rssis;
#else
        rxjob->rssi  = (u1_t)-pkt_rx.rssi;
#endif
        rxjob->snr   = (s1_t)(pkt_rx.snr*4);
        rps_t rps = ral_lgw2rps(&pkt_rx);
        rxjob->dr = s2e_rps2dr(&TC->s2ctx, rps);
        if( rxjob->dr == DR_ILLEGAL ) {
            log_rawpkt(ERROR, "Dropped RX frame - unable to map to an up DR: ", &pkt_rx);
            continue;
        }

        if( log_shallLog(MOD_RAL|XDEBUG) ) {
            log_rawpkt(XDEBUG, "", &pkt_rx);
        }

        s2e_addRxjob(&TC->s2ctx, rxjob);

    }
    s2e_flushRxjobs(&TC->s2ctx);
    rt_setTimer(tmr, rt_micros_ahead(RX_POLL_INTV));
}


int ral_config (str_t hwspec, u4_t cca_region, char* json, int jsonlen, chdefl_t* upchs) {
    if( strcmp(hwspec, "sx1301/1") != 0 ) {
        LOG(MOD_RAL|ERROR, "Unsupported hwspec=%s", hwspec);
        return 0;
    }
    ujdec_t D;
    uj_iniDecoder(&D, json, jsonlen);
    if( uj_decode(&D) ) {
        LOG(MOD_RAL|ERROR, "Parsing of sx130x channel setup JSON failed");
        return 0;
    }
    if( uj_null(&D) ) {
        LOG(MOD_RAL|ERROR, "sx130x_conf is null but a hw setup IS required - no fallbacks");
        return 0;
    }
    uj_enterArray(&D);
    int ok=0, slaveIdx;
    while( (slaveIdx = uj_nextSlot(&D)) >= 0 ) {
        dbuf_t json = uj_skipValue(&D);
        if( slaveIdx == 0 ) {
            struct sx130xconf sx130xconf;
            int status = 0;

            if( (status = !sx130xconf_parse_setup(&sx130xconf, -1, hwspec, json.buf, json.bufsize) << 0) ||
                (status = !sx130xconf_challoc(&sx130xconf, upchs)    << 1) ||
                (status = !sys_runRadioInit(sx130xconf.device)       << 2) ||
                (status = !sx130xconf_start(&sx130xconf, cca_region) << 3) ) {
                LOG(MOD_RAL|ERROR, "ral_config failed with status 0x%02x", status);
            } else {
                // Radio started
                txpowAdjust = sx130xconf.txpowAdjust;
                pps_en = sx130xconf.pps;
                last_xtime = ts_newXtimeSession(0);
                rt_yieldTo(&rxpollTmr, rxpolling);
                rt_yieldTo(&syncTmr, synctime);
                ok = 1;
            }
        }
    }
    uj_exitArray(&D);
    uj_assertEOF(&D);
    return ok;
}


// Lora gateway library is run locally - no subprocesses needed.
void ral_ini() {
    last_xtime = 0;
    rt_iniTimer(&rxpollTmr, rxpolling);
    rt_iniTimer(&syncTmr, synctime);
}

void ral_stop() {
    rt_clrTimer(&syncTmr);
    last_xtime = 0;
    rt_clrTimer(&rxpollTmr);
    lgw_stop();
}

#endif // defined(CFG_ral_lgw)
#endif // defined(CFG_lgw1)
