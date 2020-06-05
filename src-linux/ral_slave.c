/*
 * --- Revised 3-Clause BSD License ---
 * Copyright Semtech Corporation 2020. All rights reserved.
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
                LOG(MOD_RAL|DEBUG, "Dropped frame with %s CRC (0x%X)",
                    p->status == STAT_CRC_BAD ? "bad" : p->status == STAT_NO_CRC ? "no" : "undefined", p->status);
                continue; // silently ignore bad CRC
            }
            if( p->size > MAX_RXFRAME_LEN ) {
                // This should not happen since caller provides
                // space for max frame length - 255 bytes
                LOG(MOD_RAL|ERROR, "Frame size (%d) exceeds offered buffer (%d)", p->size, MAX_RXFRAME_LEN);
                continue;
            }
            struct ral_rx_resp resp;
            memset(&resp, 0, sizeof(resp));
            resp.rctx   = sys_slaveIdx;
            resp.cmd    = RAL_CMD_RX;
            resp.xtime  = ts_xticks2xtime(p->count_us, last_xtime);
            resp.rps    = ral_lgw2rps(p);
            resp.freq   = p->freq_hz;
            resp.rssi   = (u1_t)(p->rssi * -1);
            resp.snr    = (s1_t)(p->snr  *  8);
            resp.rxlen  = p->size;
            memcpy(resp.rxdata, p->payload, p->size);
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
                int err = lgw_status(TX_STATUS, &status);
                /**/ if (err != LGW_HAL_SUCCESS)  { LOG(MOD_RAL|ERROR, "lgw_status failed"); }
                else if( status == TX_SCHEDULED ) { ret = TXSTATUS_SCHEDULED; }
                else if( status == TX_EMITTING  ) { ret = TXSTATUS_EMITTING; }
                resp->status = ret;
                pipe_write_data(resp, sizeof(*resp));
                continue;
            }
            else if( n >= off + sizeof(struct ral_txabort_req) && req->cmd == RAL_CMD_TXABORT) {
                off += sizeof(struct ral_txabort_req);
                lgw_abort_tx();
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
                if( (txreq->rps & RPS_BCN) ) {  
                    pkt_tx.tx_mode = ON_GPS;
                    pkt_tx.preamble = 10;
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
                pkt_tx.invert_pol = true;
                pkt_tx.no_crc     = true;
                pkt_tx.no_header  = false;
                pkt_tx.size       = txreq->txlen;
                memcpy(pkt_tx.payload, txreq->txdata, txreq->txlen);
                int err = lgw_send(pkt_tx);
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
