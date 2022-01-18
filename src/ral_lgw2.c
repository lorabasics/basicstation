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

#if defined(CFG_lgw2)

#if defined(CFG_ral_master_slave)
#error ral_master_slave not compatible with lgw2
#endif

#include "s2conf.h"
#include "tc.h"
#include "timesync.h"
#include "sys.h"
#if defined(CFG_linux)
#include "sys_linux.h"
#include <errno.h>
#endif // defined(CFG_linux)
#include "sx1301v2conf.h"
#include "ral.h"
#include "lgw2/sx1301ar_err.h"
#include "lgw2/sx1301ar_gps.h"
#include "lgw2/spi_linuxdev.h"

static u1_t       pps_en;
static s2_t       txpowAdjust;    // scaled by TXPOW_SCALE
static sL_t       last_xtime;
static tmr_t      rxpollTmr;
static tmr_t      syncTmr;
static int        spiFd = -1;

static int _spi_read (u1_t header, u2_t address, u1_t* data, u4_t size, u1_t* status) {
    return spi_linuxdev_read(header, spiFd, address, data, size, status);
}

static int _spi_write (u1_t header, u2_t address, const u1_t* data, u4_t size, u1_t* status) {
    return spi_linuxdev_write(header, spiFd, address, data, size, status);
}

static const u2_t SF_MAP[] = {
    [SF12 ]= MR_SF12,
    [SF11 ]= MR_SF11,
    [SF10 ]= MR_SF10,
    [SF9  ]= MR_SF9,
    [SF8  ]= MR_SF8,
    [SF7  ]= MR_SF7,
    [FSK  ]= MR_UNDEFINED,
    [SFNIL]= MR_UNDEFINED,
};

static const u1_t BW_MAP[] = {
    [BW125]= BW_125K,
    [BW250]= BW_250K,
    [BW500]= BW_500K,
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


static rps_t ral_lgw2rps (sx1301ar_rx_pkt_t* p) {
    return p->modulation == MOD_LORA
        ? rps_make(to_sf(p->modrate), to_bw(p->bandwidth))
        : FSK;
}


static void ral_rps2lgw (rps_t rps, sx1301ar_tx_pkt_t* p) {
    assert(rps != RPS_ILLEGAL);
    if( rps_sf(rps) == FSK ) {
        p->modulation = MOD_FSK;
        p->modrate    = MR_57600;
        p->f_dev      = 25;
        p->preamble   = 5;
    } else {
        p->modulation = MOD_LORA;
        p->modrate    = SF_MAP[rps_sf(rps)];
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
//  timesync    return measured data - isochronous times for MCU/SX1301 and opt. latched PPS
// Return:
//  Measure of quality (absolute value) - caller uses this to weed out bad measurements
//  In this impl. we return the time the measurement took - smallest values are best values
//
int ral_getTimesync (u1_t pps_en, sL_t* last_xtime, timesync_t* timesync) {
    static u4_t last_pps_xticks;
    u4_t pps_xticks;
    if( pps_en ) {
        // Note: we only get a proper latched value if a PPS egde was detected while
        // the flag LGW_GPS_EN was enabled. If LGW_GPS_EN is set to 0 the latches value seems to be lost?
        // Therefore, read the latch value at the very beginning after a >1sec delay
        if( sx1301ar_get_trigcnt(SX1301AR_BOARD_MASTER, &pps_xticks) != 0 )
            goto failed;
    }
    u4_t hs_pps = 0;
    if( sx1301ar_get_trighs(SX1301AR_BOARD_MASTER, &hs_pps) != 0 ) hs_pps = 0;
    
    sx1301ar_tref_t tref = sx1301ar_init_tref();
    sx1301ar_set_xtal_err(0,tref);
    ustime_t t0 = rt_getTime();
    u4_t xticks = 0;
    if( sx1301ar_get_instcnt(SX1301AR_BOARD_MASTER, &xticks) != 0 )
        goto failed;
    ustime_t t1 = rt_getTime();
    sL_t d = (s4_t)(xticks - *last_xtime);
    if( d < 0 ) {
        LOG(MOD_SYN|CRITICAL,
            "SX1301 time sync roll over - no update for a long time: xticks=0x%08x last_xtime=0x%lX",
            xticks, *last_xtime);
        d += (sL_t)1<<32;
    }
    timesync->xtime = *last_xtime += d;
    timesync->ustime = (t0+t1)/2;
    timesync->pps_xtime = 0; // Will be set if pps_en is set and valid PPS observation is available
    if( pps_en && pps_xticks && last_pps_xticks != pps_xticks ) {
        timesync->pps_xtime = timesync->xtime + (s4_t)(pps_xticks - xticks);
        last_pps_xticks = pps_xticks;
    }
    LOG(MOD_SYN|XDEBUG, "SYNC: ustime=0x%012lX (Q=%3d): xticks=0x%08x xtime=0x%lX - PPS: pps_xticks=0x%08x (%u) pps_xtime=0x%lX (pps_en=%d)",
        timesync->ustime, (int)(t1-t0), xticks, timesync->xtime, pps_xticks, pps_xticks, timesync->pps_xtime, pps_en);
    return (int)(t1-t0);
  failed:
    LOG(MOD_SYN|CRITICAL, "SX1301 time sync failed: %s", sx1301ar_err_message(sx1301ar_errno));
    return INT_MAX;
}

static void synctime (tmr_t* tmr) {
    timesync_t timesync = {0};
    int quality = ral_getTimesync(pps_en, &last_xtime, &timesync);
    ustime_t delay = ts_updateTimesync(0, quality, &timesync);
    rt_setTimer(&syncTmr, rt_micros_ahead(delay));
}

u1_t ral_altAntennas (u1_t txunit) {
    // Only board #0 can TX - no other antennas
    return 0;
}


int ral_tx (txjob_t* txjob, s2ctx_t* s2ctx, int nocca) {
    sx1301ar_tx_pkt_t pkt_tx = sx1301ar_init_tx_pkt();

    pkt_tx.invert_pol = true;
    pkt_tx.no_header  = false;

    if( txjob->preamble == 0 ) {
        if( txjob->txflags & TXFLAG_BCN ) {
            pkt_tx.tx_mode = TX_ON_GPS;
            pkt_tx.preamble = 10;
            pkt_tx.invert_pol = false;
            pkt_tx.no_header  = true;
        } else {
            pkt_tx.tx_mode = TX_TIMESTAMPED;
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
    pkt_tx.coderate   = CR_4_5;
    pkt_tx.no_crc     = !txjob->addcrc;
    pkt_tx.size       = txjob->len;
    memcpy(pkt_tx.payload, &s2ctx->txq.txdata[txjob->off], pkt_tx.size);

    // NOTE: nocca not possible to implement with current libloragw API
    if( sx1301ar_send(0, &pkt_tx) != 0 ) {
        if( sx1301ar_errno == ERR_LBT_FORBIDDEN )
            return RAL_TX_NOCA;
        LOG(MOD_RAL|ERROR, "sx1301ar_send failed: %s", sx1301ar_err_message(sx1301ar_errno));
        return RAL_TX_FAIL;
    }
    return RAL_TX_OK;
}


int ral_txstatus (u1_t txunit) {
    sx1301ar_tstat_t status;
    if( sx1301ar_tx_status(txunit, &status) != 0 ) {
        LOG(MOD_RAL|ERROR, "sx1301ar_tx_status failed: %s", sx1301ar_err_message(sx1301ar_errno));
        return TXSTATUS_IDLE;
    }
    if( status == TX_SCHEDULED )
        return TXSTATUS_SCHEDULED;
    if( status == TX_EMITTING )
        return TXSTATUS_EMITTING;
    return TXSTATUS_IDLE;
}


void ral_txabort (u1_t txunit) {
    if( sx1301ar_abort_tx(txunit) != 0 )
        LOG(MOD_RAL|ERROR, "sx1301ar_abort_tx failed: %s", sx1301ar_err_message(sx1301ar_errno));
}

static void rxpolling (tmr_t* tmr) {
    while(1) {
        sx1301ar_rx_pkt_t pkt_rx[SX1301AR_MAX_PKT_NB];
        u1_t n;
        
        if( sx1301ar_fetch(0, pkt_rx, SIZE_ARRAY(pkt_rx), &n) == -1 ) {
            LOG(MOD_RAL|ERROR, "sx1301ar_fetch: %s", sx1301ar_err_message(sx1301ar_errno));
            break;
        }
        if( n==0 ) {
            break;
        }
        for( int i=0; i<n; i++ ) {
            rxjob_t* rxjob = !TC ? NULL : s2e_nextRxjob(&TC->s2ctx);
            if( rxjob == NULL ) {
                LOG(ERROR, "SX1301 RX frame dropped - out of space");
                continue;
            }
            sx1301ar_rx_pkt_t* p = &pkt_rx[i];
            if( p->status != STAT_CRC_OK ) {
                LOG(XDEBUG, "Dropped frame without CRC or with broken CRC");
                continue; // silently ignore bad CRC
            }
            if( p->size > MAX_RXFRAME_LEN ) {
                // This should not happen since caller provides
                // space for max frame length - 255 bytes
                LOG(MOD_RAL|ERROR, "Frame size (%d) exceeds offered buffer (%d)", p->size, MAX_RXFRAME_LEN);
                continue;
            }
            
            
            
            
            
            memcpy(&TC->s2ctx.rxq.rxdata[rxjob->off], p->payload, p->size);
            rxjob->len   = p->size;
            rxjob->freq  = p->freq_hz;
            rxjob->xtime = ts_xticks2xtime(p->count_us, last_xtime);
            rxjob->rssi = 255;
            for (int j = 0; j < SX1301AR_BOARD_RFCHAIN_NB; j++) {
                // LOG(MOD_RAL|XDEBUG, " --- rssi=%d  rsig[%d].rssi_chan=%f",rxjob->rssi,j,p->rsig[j].rssi_chan, j);
                if(rxjob->rssi < -p->rsig[j].rssi_chan || !p->rsig[j].is_valid) {
                    continue;
                }
                rxjob->fts = p->rsig[j].fine_received ? p->rsig[j].fine_tmst : -1;
                rxjob->rssi = (u1_t)-p->rsig[j].rssi_chan;  
                rxjob->snr = p->rsig[j].snr*4;
                rxjob->rctx = j;
            }
            rps_t rps = ral_lgw2rps(p);
            rxjob->dr = s2e_rps2dr(&TC->s2ctx, rps);
            if( rxjob->dr == DR_ILLEGAL ) {
                LOG(MOD_RAL|ERROR, "Unable to map to an up DR: %R", rps);
                continue;
            }
            s2e_addRxjob(&TC->s2ctx, rxjob);
        }
    }
    s2e_flushRxjobs(&TC->s2ctx);
    rt_setTimer(tmr, rt_micros_ahead(RX_POLL_INTV));
}

int ral_config (str_t hwspec, u4_t cca_region, char* json, int jsonlen, chdefl_t* upchs) {
    struct sx1301v2conf sx1301v2conf;
    if( !sx1301v2conf_parse_setup(&sx1301v2conf, -1, hwspec, json, jsonlen) )
        return 0;
    str_t device = sx1301v2conf.boards[0].device;
    for( int i=0; i < SX1301AR_MAX_BOARD_NB; i++ ) {
        if( sx1301v2conf.boards[i].boardConf.board_type == BRD_TYPE_UNKNOWN )
            continue;
        if( sx1301v2conf.boards[i].device[0] && strcmp(device, sx1301v2conf.boards[i].device) != 0 ) {
            LOG(MOD_RAL|ERROR, "Multiple SPI devices not (yet) supported: %s and %s",
                device, sx1301v2conf.boards[i].device);
            goto errexit;
        }
        s2_t fpga_version, dsp_version;
        str_t v = sx1301ar_version_info(i, &fpga_version, &dsp_version);
        LOG(MOD_RAL|INFO, "Board#%d sx1301ar library version: %s", i, v);
        sx1301v2conf.boards[i].boardConf.spi_read  = _spi_read;
        sx1301v2conf.boards[i].boardConf.spi_write = _spi_write;
    }
    ral_stop();

#if defined(CFG_linux)
    u4_t pids[1];
    int n = sys_findPids(device, pids, SIZE_ARRAY(pids));
    if( n > 0 )
        rt_fatal("Radio device '%s' in use by process: %d%s", device, pids[0], n>1?".. (and others)":"");
#endif // defined(CFG_linux)

#if !defined(CFG_variant_testsim)
    int err;
    if( (err = spi_linuxdev_open(device, /*default speed*/-1, &spiFd)) != 0 ) {
        LOG(MOD_RAL|ERROR, "Failed to open SPI device '%s': ret=%d errno=%s", device, err, strerror(errno));
        goto errexit;
    }
    // Configure SPI devices to master/slave modes (Needed for ATMEL CPU only)
    //   SPI 0: HOST <-> FPGA
    //   SPI 1: HOST/DSP <-> Flash    -- so that DSP can access the flash for booting
    if( (err = spi_set_mode(0, SPI_MODE_MASTER)) != 0 ||
        (err = spi_set_mode(1, SPI_MODE_SLAVE)) != 0 ) {
        LOG(MOD_RAL|ERROR, "Failed to set mode for SPI device '%s': %s", device, err);
        goto errexit;
    }
#endif

    if( !sys_runRadioInit(sx1301v2conf.boards[0].device) ||
        !sx1301v2conf_challoc(&sx1301v2conf, upchs) ||
        !sx1301v2conf_start(&sx1301v2conf, cca_region) ) {
        goto errexit;
    }
    // Radio started
    txpowAdjust = sx1301v2conf.boards[0].txpowAdjusts[0];
    pps_en = sx1301v2conf.boards[0].pps;
    last_xtime = ts_newXtimeSession(0);
    rt_yieldTo(&rxpollTmr, rxpolling);
    rt_yieldTo(&syncTmr, synctime);

    LOG(MOD_RAL|INFO, "Station device: %s (PPS capture %sabled)", device, pps_en ? "en":"dis");
    return 1;

  errexit:
    if( spiFd >= 0 ) {
#if !defined(CFG_variant_testsim)
        (void)spi_linuxdev_close(spiFd);
#endif
        spiFd = -1;
    }
    return 0;
}


// Lora gateway library is run locally - no subprocesses needed.
void ral_ini() {
    last_xtime = 0;
    rt_iniTimer(&rxpollTmr, rxpolling);
    rt_iniTimer(&syncTmr, synctime);
}

void ral_stop() {
    sx1301ar_stop(SX1301AR_MAX_BOARD_NB);
    if( spiFd >= 0 ) {
        (void)spi_linuxdev_close(spiFd);
        spiFd = -1;
    }
    last_xtime = 0;
    rt_clrTimer(&rxpollTmr);
    rt_clrTimer(&syncTmr);
}

#endif // defined(CFG_lgw2)
