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

#if defined(CFG_lgw1) && defined(CFG_ral_master_slave)

#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "uj.h"
#include "ral.h"
#include "ralsub.h"
#include "timesync.h"
#include "sys_linux.h"
#include "sx130xconf.h"
#include "lgw/loragw_hal.h"

#if !defined(LGW_PKT_FIFO_SIZE)
#define LGW_PKT_FIFO_SIZE 16
#endif

static u1_t   pps_en;
static sL_t   last_xtime;
static u4_t   region;
static tmr_t  rxpoll_tmr;
static aio_t* rd_aio;
static aio_t* wr_aio;
static s2_t   txpowAdjust; // scaled by TXPOW_SCALE
static struct lgw_pkt_rx_s pkt_rx[LGW_PKT_FIFO_SIZE];


static void pipe_write_data (void* data, int len) {
    assert(len < PIPE_BUF);
    int retries = 0;
    while(1) {
        int n = write(wr_aio->fd, data, len);
        if( n == len )
            return;
        if( errno == EPIPE )
            rt_fatal("Slave (%d) - Broken pipe", sys_slaveIdx);
        if( errno == EAGAIN ) {
            if( ++retries > 5 ) {
                // rt_fatal("Slave (%d) - Pipe full - master too slow", sys_slaveIdx);
                LOG(MOD_RAL|ERROR, "Slave (%d) - Pipe full - dropping message", sys_slaveIdx);
                return;
            }
            rt_usleep(rt_millis(1));
        }
    }
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

static void rx_polling (tmr_t* tmr) {
    int n;
    while( (n = lgw_receive(LGW_PKT_FIFO_SIZE, pkt_rx)) != 0 ) {
        if( n < 0 || n > LGW_PKT_FIFO_SIZE ) {
            LOG(MOD_RAL|ERROR, "lgw_receive error: %d", n);
            break;
        }
        for( int i=0; i<n; i++ ) {
            struct lgw_pkt_rx_s* p = &pkt_rx[i];
            if( p->status != STAT_CRC_OK ) {
                if( log_shallLog(MOD_RAL|DEBUG) ) {
                    log_rawpkt(DEBUG, "", p);
                }
                continue; // silently ignore bad CRC
            }
            if( p->size > MAX_RXFRAME_LEN ) {
                // This should not happen since caller provides
                // space for max frame length - 255 bytes
                log_rawpkt(ERROR, "Dropped RX frame - frame size too large: ", p);
                continue;
            }
            struct ral_rx_resp resp;
            memset(&resp, 0, sizeof(resp));
            resp.rctx   = sys_slaveIdx;
            resp.cmd    = RAL_CMD_RX;
            resp.xtime  = ts_xticks2xtime(p->count_us, last_xtime);
            resp.rps    = ral_lgw2rps(p);
            resp.freq   = p->freq_hz;
#if defined(CFG_sx1302)
            resp.rssi  = (u1_t)-p->rssis;
#else
            resp.rssi  = (u1_t)-p->rssi;
#endif
            resp.snr    = (s1_t)(p->snr  *  4);
            resp.rxlen  = p->size;
            memcpy(resp.rxdata, p->payload, p->size);

            if( log_shallLog(MOD_RAL|XDEBUG) ) {
                log_rawpkt(XDEBUG, "", p);
            }

            pipe_write_data(&resp, sizeof(resp));
        }
    }
    rt_setTimer(&rxpoll_tmr, rt_micros_ahead(RX_POLL_INTV));
}


static void sendTimesync () {
    struct ral_timesync_resp resp;
    resp.rctx = sys_slaveIdx;
    resp.cmd = RAL_CMD_TIMESYNC;
    resp.quality = ral_getTimesync(pps_en, &last_xtime, &resp.timesync);
    pipe_write_data(&resp, sizeof(resp));
}


static void pipe_read (aio_t* aio) {
    u1_t buf[PIPE_BUF];
    while(1) {
        int n = read(aio->fd, buf, sizeof(buf));
        if( n == 0 ) {
            // EOF
            LOG(MOD_RAL|INFO, "EOF from master (%d)", sys_slaveIdx);
            exit(2);
            return;
        }
        if( n == -1 ) {
            if( errno == EAGAIN )
                return;
            rt_fatal("Slave pipe read fail: %s", strerror(errno));
        }
        int off = 0;
        while( off < n ) {
            struct ral_header* req = (struct ral_header*)&buf[off];
            assert(n >= off + sizeof(*req));
            if( n >= off + sizeof(struct ral_txstatus_req) && req->cmd == RAL_CMD_TXSTATUS ) {
                off += sizeof(struct ral_txstatus_req);
                struct ral_response* resp = (struct ral_response*)req;
                u1_t ret=TXSTATUS_IDLE, status;
#if defined(CFG_sx1302)
                int err = lgw_status(0, TX_STATUS, &status);  
#else
                int err = lgw_status(TX_STATUS, &status);
#endif
                /**/ if (err != LGW_HAL_SUCCESS)  { LOG(MOD_RAL|ERROR, "lgw_status failed"); }
                else if( status == TX_SCHEDULED ) { ret = TXSTATUS_SCHEDULED; }
                else if( status == TX_EMITTING  ) { ret = TXSTATUS_EMITTING; }
                resp->status = ret;
                pipe_write_data(resp, sizeof(*resp));
                continue;
            }
            else if( n >= off + sizeof(struct ral_txabort_req) && req->cmd == RAL_CMD_TXABORT) {
                off += sizeof(struct ral_txabort_req);
#if defined(CFG_sx1302)
                lgw_abort_tx(0); 
#else
                lgw_abort_tx();
#endif
                continue;
            }
            else if( n >= off + sizeof(struct ral_timesync_req) && req->cmd == RAL_CMD_TIMESYNC) {
                off += sizeof(struct ral_timesync_req);
                sendTimesync();
                continue;
            }
            else if( n >= off + sizeof(struct ral_tx_req) && (req->cmd == RAL_CMD_TX_NOCCA || req->cmd == RAL_CMD_TX  )) {
                off += sizeof(struct ral_tx_req);
                struct ral_tx_req* txreq = (struct ral_tx_req*)req;
                struct lgw_pkt_tx_s pkt_tx;

                pkt_tx.invert_pol = true;
                pkt_tx.no_header  = false;

                if( (txreq->rps & RPS_BCN) ) {  
                    pkt_tx.tx_mode = ON_GPS;
                    pkt_tx.preamble = 10;
                    pkt_tx.invert_pol = false;
                    pkt_tx.no_header  = true;
                } else {
                    pkt_tx.tx_mode = TIMESTAMPED;
                    pkt_tx.preamble = 8;
                }
                ral_rps2lgw(txreq->rps, &pkt_tx);
                pkt_tx.freq_hz    = txreq->freq;
                pkt_tx.count_us   = txreq->xtime;
                pkt_tx.rf_chain   = 0;
                pkt_tx.rf_power   = (float)(txreq->txpow - txpowAdjust)/TXPOW_SCALE;
                pkt_tx.coderate   = CR_LORA_4_5;
                pkt_tx.no_crc     = !txreq->addcrc;
                pkt_tx.size       = txreq->txlen;
                memcpy(pkt_tx.payload, txreq->txdata, txreq->txlen);
#if defined(CFG_sx1302)
                int err = lgw_send(&pkt_tx);
#else
                int err = lgw_send(pkt_tx);
#endif
                if( region == 0 ) {
                    continue;
                }
                // Send back CCA/LBT result
                struct ral_response* resp = (struct ral_response*)req;
                u1_t ret = RAL_TX_OK;
                if( err == LGW_HAL_SUCCESS ) {
                    ret = RAL_TX_OK;
                } else if( err == LGW_LBT_ISSUE ) {
                    ret = RAL_TX_NOCA;
                } else {
                    LOG(MOD_RAL|ERROR, "lgw_send failed");
                    ret = RAL_TX_FAIL;
                }
                resp->status = ret;
                pipe_write_data(resp, sizeof(*resp));
                continue;
            }
            else if( n >= off + sizeof(struct ral_config_req) && req->cmd == RAL_CMD_CONFIG) {
                off += sizeof(struct ral_config_req);
                struct ral_config_req* confreq = (struct ral_config_req*)req;
                struct sx130xconf sx1301conf;
                int status = 0;
                // Note: sx1301conf_start can take considerable amount of time (if LBT on up to 8s!!)
                if( (status = !sx130xconf_parse_setup(&sx1301conf, sys_slaveIdx, confreq->hwspec, confreq->json, confreq->jsonlen)) ||
                    (status = !sx130xconf_challoc(&sx1301conf, &confreq->upchs)   << 1) ||
                    (status = !sys_runRadioInit(sx1301conf.device)                << 2) ||
                    (status = !sx130xconf_start(&sx1301conf, confreq->region)     << 3) )
                    rt_fatal("Slave radio start up failed with status 0x%02x", status);
                if( sx1301conf.pps && sys_slaveIdx ) {
                    LOG(MOD_RAL|ERROR, "Only slave#0 may have PPS enabled");
                    sx1301conf.pps = 0;
                }
                pps_en = sx1301conf.pps;
                region = confreq->region;
                txpowAdjust = sx1301conf.txpowAdjust;
                last_xtime = ts_newXtimeSession(sys_slaveIdx);
                rt_yieldTo(&rxpoll_tmr, rx_polling);
                sendTimesync();
                continue;
            }
            else if( n >= off + sizeof(struct ral_stop_req) && req->cmd == RAL_CMD_STOP) {
                off += sizeof(struct ral_stop_req);
                last_xtime = 0;
                rt_clrTimer(&rxpoll_tmr);
                lgw_stop();
                continue;
            }
            else {
                rt_fatal("Master sent unexpected data: cmd=%d size=%d", req->cmd, n-off);
            }
        }
        assert(off==n); // req fragments should not exist
    }
}


void sys_startupSlave (int rdfd, int wrfd) {
    // Use rxpoll_tmr as dummy context
    rd_aio = aio_open(&rxpoll_tmr, rdfd, pipe_read, NULL);
    wr_aio = aio_open(&rxpoll_tmr, wrfd, NULL, NULL);
    rt_iniTimer(&rxpoll_tmr, NULL);
    pipe_read(rd_aio);
    LOG(MOD_RAL|INFO, "Slave LGW (%d) - started.", sys_slaveIdx);
    aio_loop();
    // NOT REACHED
    assert(0);
}

#endif // defined(CFG_lgw1) && defined(CFG_ral_master_slave)
