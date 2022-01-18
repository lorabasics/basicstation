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

#if defined(CFG_lgwsim)
// LCOV_EXCL_START
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#if defined(CFG_lgw1)
#include "lgw/loragw_reg.h"
#include "lgw/loragw_hal.h"
#include "lgw/loragw_fpga.h"
#include "lgw/loragw_lbt.h"
#elif defined(CFG_lgw2)
#include "lgw2/sx1301ar_hal.h"
#include "lgw2/sx1301ar_err.h"
#include "lgw2/sx1301ar_gps.h"
#include "lgw2/sx1301ar_dsp.h"
#endif

#include "rt.h"
#include "s2e.h"
#include "sys.h"

#include "sys_linux.h"

#define MAX_CCA_INFOS   10
#define MAGIC_CCA_FREQ  0xCCAFCCAF

#define RX_NPKTS 1000

struct cca_info {
    u4_t freq;
    sL_t beg;
    sL_t end;
};

struct cca_msg {
    u4_t magic;    // corresponds to freq_hz in struct lgw_pkt_rx_s
    struct cca_info infos[MAX_CCA_INFOS];
};

#if defined(CFG_lgw1)
static struct lgw_pkt_tx_s tx_pkt;
static struct lgw_pkt_rx_s rx_pkts[RX_NPKTS+1];
static u1_t     ppsLatched;
#elif defined(CFG_lgw2)
static sx1301ar_tx_pkt_t tx_pkt;
static sx1301ar_rx_pkt_t rx_pkts[RX_NPKTS+1];
#endif

static sL_t     timeOffset;
static sL_t     txbeg;
static sL_t     txend;
static int      rxblen  = sizeof(rx_pkts[0])*RX_NPKTS;
static int      rx_ridx = 0;
static int      rx_widx = 0;
static u4_t     rx_dsc = 0;
static aio_t*   aio;
static tmr_t    conn_tmr;
static struct sockaddr_un sockAddr;
static struct cca_msg     cca_msg;

uint8_t lgwx_device_mode = 0;
uint8_t lgwx_beacon_len = 0;
uint8_t lgwx_beacon_sf = 0;
uint8_t lgwx_lbt_mode = 0;


#define rbfree(widx,ridx,len) (widx >= ridx ? len-widx : ridx-widx-1)
#define rbused(widx,ridx,len) (widx >= ridx ? widx-ridx : len-ridx+widx)

static int cca (sL_t txtime, u4_t txfreq) {
    for( int i=0; i<MAX_CCA_INFOS; i++ ) {
        u4_t freq = cca_msg.infos[i].freq;
        if( freq == 0 )
            break;
        if( txfreq == freq &&
            txtime >= cca_msg.infos[i].beg &&
            txtime <= cca_msg.infos[i].end ) {
            return 0;
        }
    }
    return 1;
}


static sL_t xticks () {
    // Make it different from ustime_t to increase test coverage
    return sys_time() - timeOffset;
}


static u4_t airtime (int datarate, int bandwidth, int plen) {
    int sf, bw;
#if defined(CFG_lgw1)
    switch(bandwidth) {
    case BW_125KHZ: bw = BW125; break;
    case BW_250KHZ: bw = BW250; break;
    case BW_500KHZ: bw = BW500; break;
    }
    switch(datarate) {
    case DR_LORA_SF12: sf = SF12; break;
    case DR_LORA_SF11: sf = SF11; break;
    case DR_LORA_SF10: sf = SF10; break;
    case DR_LORA_SF9 : sf = SF9 ; break;
    case DR_LORA_SF8 : sf = SF8 ; break;
    case DR_LORA_SF7 : sf = SF7 ; break;
    }
#elif defined(CFG_lgw2)
    switch(bandwidth) {
    case BW_125K: bw = BW125; break;
    case BW_250K: bw = BW250; break;
    case BW_500K: bw = BW500; break;
    default: bw = BWNIL; break;
    }
    switch(datarate) {
    case MR_SF12: sf = SF12; break;
    case MR_SF11: sf = SF11; break;
    case MR_SF10: sf = SF10; break;
    case MR_SF9 : sf = SF9 ; break;
    case MR_SF8 : sf = SF8 ; break;
    case MR_SF7 : sf = SF7 ; break;
    }
#endif
    return s2e_calcDnAirTime(rps_make(sf,bw), plen, /*addcrc*/0, /*preamble*/0);
}


static void read_socket (aio_t* aio);
static void write_socket (aio_t* aio);

static void try_connecting (tmr_t* tmr) {
    if( aio ) {
        aio_close(aio);
        aio = NULL;
    }
    int fd;
    // Would like to use SOCK_DGRAM but this only works in python/asyncio 3.7 (currently at 3.6.5)
    if( (fd = socket(PF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, 0)) == -1 ) {
        LOG(MOD_SIM|ERROR, "LGWSIM: Failed to open unix domain socket '%s': %d (%s)", sockAddr.sun_path, errno, strerror(errno));
        goto retry;
    }
    if( connect(fd, (struct sockaddr*)&sockAddr, sizeof(sockAddr)) == -1 ) {
        LOG(MOD_SIM|ERROR, "LGWSIM: Failed to connect to unix domain socket '%s': %d (%s)", sockAddr.sun_path, errno, strerror(errno));
        close(fd);
        goto retry;
    }
    aio = aio_open(&conn_tmr, fd, read_socket, write_socket);
    // Send a fake packet with our socket
    tx_pkt.tx_mode = 255;
    tx_pkt.count_us = timeOffset;
    tx_pkt.freq_hz = timeOffset>>32;
    tx_pkt.f_dev = max(0, sys_slaveIdx);
    LOG(MOD_SIM|INFO, "LGWSIM: Connected txunit#%d timeOffset=0x%lX xticksNow=0x%lX", max(0, sys_slaveIdx), timeOffset, xticks());
    write_socket(aio);
    read_socket(aio);
    return;

 retry:
    rt_setTimer(tmr, rt_seconds_ahead(1));
}


static void read_socket (aio_t* aio) {
    while(1) {
        u1_t * rxbuf = &((u1_t*)rx_pkts)[rx_widx];
        int rxlen = 4;
        if( rx_dsc ) { // Currently discarding bytes until next packet boundary
            if( rx_dsc % sizeof(rx_pkts[0]) == 0 ) { // Packet boundary
                LOG(MOD_SIM|ERROR, "LGWSIM(%s): RX buffer full. Dropping frame.", sockAddr.sun_path);
                rx_dsc = 0;
                continue;
            } else {
                rxlen = sizeof(rx_pkts[0]) - rx_dsc;
            }
        } else if( (rxlen = rbfree(rx_widx, rx_ridx, rxblen)) == 0 ) {
            rx_dsc = rx_widx  % sizeof(rx_pkts[0]);
            rx_widx -= rx_dsc;
            rxbuf = &((u1_t*)rx_pkts)[rx_widx];
            rxlen = sizeof(rx_pkts[0]) - rx_dsc;
        }
        int n = read(aio->fd, rxbuf, rxlen);
        if( n == 0 ) {
            LOG(MOD_SIM|ERROR, "LGWSIM(%s) closed (recv)", sockAddr.sun_path);
            rt_yieldTo(&conn_tmr, try_connecting);
            return;
        }
        if( n==-1 ) {
            if( errno == EAGAIN )
                return;
            LOG(MOD_SIM|ERROR, "LGWSIM(%s): Recv error: %d (%s)", sockAddr.sun_path, errno, strerror(errno));
            rt_yieldTo(&conn_tmr, try_connecting);
            return;
        }

        if( rx_dsc || rbfree(rx_widx, rx_ridx, rxblen) == 0 ) {
            rx_dsc += n;
            continue;
        } else {
            rx_widx = (rx_widx+n) % rxblen;
        }

        if( rbused(rx_widx, rx_ridx, rxblen) >= sizeof(rx_pkts[0]) && rx_pkts[rx_ridx/sizeof(rx_pkts[0])].freq_hz == MAGIC_CCA_FREQ ){
            cca_msg = *(struct cca_msg*)&rx_pkts[rx_ridx/sizeof(rx_pkts[0])];
            rx_ridx = (rx_ridx+sizeof(rx_pkts[0])) % rxblen;
        }
    }
}


static void write_socket (aio_t* aio) {
    int n = write(aio->fd, &tx_pkt, sizeof(tx_pkt));
    if( n == 0 ) {
        LOG(MOD_SIM|ERROR, "LGWSIM(%s) closed (send)", sockAddr.sun_path);
        rt_yieldTo(&conn_tmr, try_connecting);
        return;
    }
    if( n==-1 ) {
        if( errno == EAGAIN )
            return;
        LOG(MOD_SIM|ERROR, "LGWSIM(%s): Send error: %d (%s)", sockAddr.sun_path, errno, strerror(errno));
        rt_yieldTo(&conn_tmr, try_connecting);
        return;
    }
    assert(n == sizeof(tx_pkt));
    aio_set_wrfn(aio, NULL);
}

#if defined(CFG_lgw1)
/* **********************************************
   ****              LGW 1                   ****
   ********************************************** */

int lgw_receive (uint8_t max_pkt, struct lgw_pkt_rx_s *pkt_data) {
    int npkts = 0;
    while( npkts < max_pkt && rbused(rx_widx, rx_ridx, rxblen) >= sizeof(rx_pkts[0]) ){
        pkt_data[npkts] = rx_pkts[rx_ridx/sizeof(rx_pkts[0])];
        rx_ridx = (rx_ridx+sizeof(rx_pkts[0])) % rxblen;
        npkts += 1;
    }
    if( npkts )
        LOG(MOD_SIM|DEBUG, "LGWSIM(%s): received %d packets", sockAddr.sun_path, npkts);
    return npkts;
}


int lgw_send (struct lgw_pkt_tx_s pkt_data) {
    sL_t t = xticks();
    txbeg = t + (s4_t)((u4_t)pkt_data.count_us - (u4_t)t);
    txend = txbeg + airtime(pkt_data.datarate, pkt_data.bandwidth, pkt_data.size);
    if( !cca(txbeg, pkt_data.freq_hz) )
        return LGW_LBT_ISSUE;
    tx_pkt = pkt_data;
    if( !aio || aio->ctx == NULL || aio->fd == 0 )
        return LGW_HAL_ERROR;
    aio_set_wrfn(aio, write_socket);
    write_socket(aio);
    return LGW_HAL_SUCCESS;
}


int lgw_status (uint8_t select, uint8_t *code) {
    sL_t t = xticks();
    if( t <= txbeg )
        *code = TX_SCHEDULED;
    else if( t <= txend )
        *code = TX_EMITTING;
    else
        *code = TX_FREE;
    return LGW_HAL_SUCCESS;
}


int lgw_abort_tx (void) {
    txbeg = txend = 0;
    return LGW_HAL_SUCCESS;
}


int lgw_stop (void) {
    rt_clrTimer(&conn_tmr);
    txbeg = txend = 0;
    aio_close(aio);
    aio = NULL;
    return LGW_HAL_SUCCESS;
}


int lgw_get_trigcnt(uint32_t* trig_cnt_us) {
    sL_t t = xticks();
    if( ppsLatched )
        t -= sys_utc()%1000000;
    trig_cnt_us[0] = t;
    return LGW_HAL_SUCCESS;
}


int lgw_start () {
    const char* sockPath = getenv("LORAGW_SPI");
    if( aio )
        return LGW_HAL_ERROR;
    memset(&cca_msg, 0, sizeof(cca_msg));
    memset(&sockAddr, 0, sizeof(sockAddr));
    // Make xticks different from ustime to cover more test ground.
    // xticks start at ~(1<<28) whenever a radio simulation starts.
    timeOffset = sys_time() - 0x10000000;
    sockAddr.sun_family = AF_UNIX;
    snprintf(sockAddr.sun_path, sizeof(sockAddr.sun_path), "%s", sockPath);
    rt_yieldTo(&conn_tmr, try_connecting);
    return LGW_HAL_SUCCESS;
}


int lgw_reg_w (uint16_t register_id, int32_t reg_value) {
    assert(register_id == LGW_GPS_EN);
    ppsLatched = reg_value;
    return LGW_HAL_SUCCESS;
}


int lgw_board_setconf (struct lgw_conf_board_s conf) {
    return LGW_HAL_SUCCESS;
}


int lgw_rxrf_setconf (uint8_t rf_chain, struct lgw_conf_rxrf_s conf) {
    /* check input range (segfault prevention) */
    if (rf_chain >= LGW_RF_CHAIN_NB) {
        LOG(MOD_SIM|ERROR, "ERROR: NOT A VALID RF_CHAIN NUMBER\n");
        return LGW_HAL_ERROR;
    }

    /* check if radio type is supported */
    if ((conf.type != LGW_RADIO_TYPE_SX1255) && (conf.type != LGW_RADIO_TYPE_SX1257)) {
        LOG(MOD_SIM|ERROR, "ERROR: NOT A VALID RADIO TYPE\n");
        return LGW_HAL_ERROR;
    }

    /* check if TX notch filter frequency is supported */
    if ((conf.tx_enable == true) && ((conf.tx_notch_freq < LGW_MIN_NOTCH_FREQ) || (conf.tx_notch_freq > LGW_MAX_NOTCH_FREQ))) {
        LOG(MOD_SIM|ERROR, "WARNING: NOT A VALID TX NOTCH FILTER FREQUENCY [%u..%u]Hz\n", LGW_MIN_NOTCH_FREQ, LGW_MAX_NOTCH_FREQ);
        conf.tx_notch_freq = 0;
    }

    /* set internal config according to parameters */
    // rf_enable[rf_chain] = conf.enable;
    // rf_rx_freq[rf_chain] = conf.freq_hz;
    // rf_rssi_offset[rf_chain] = conf.rssi_offset;
    // rf_radio_type[rf_chain] = conf.type;
    // rf_tx_enable[rf_chain] = conf.tx_enable;
    // rf_tx_notch_freq[rf_chain] = conf.tx_notch_freq;

    LOG(MOD_SIM|INFO, "Note: rf_chain %d configuration; en:%d freq:%d rssi_offset:%f radio_type:%d tx_enable:%d tx_notch_freq:%u\n",
        rf_chain, conf.enable, conf.freq_hz, conf.rssi_offset, conf.type, conf.tx_enable, conf.tx_notch_freq);

    return LGW_HAL_SUCCESS;
}

const uint8_t ifmod_config[LGW_IF_CHAIN_NB] = LGW_IFMODEM_CONFIG;

#define LGW_RF_RX_BANDWIDTH_125KHZ  925000      /* for 125KHz channels */
#define LGW_RF_RX_BANDWIDTH_250KHZ  1000000     /* for 250KHz channels */
#define LGW_RF_RX_BANDWIDTH_500KHZ 1100000 /* for 500KHz channels */

int32_t lgw_bw_getval(int x) {
    switch (x) {
        case BW_500KHZ: return 500000;
        case BW_250KHZ: return 250000;
        case BW_125KHZ: return 125000;
        case BW_62K5HZ: return 62500;
        case BW_31K2HZ: return 31200;
        case BW_15K6HZ: return 15600;
        case BW_7K8HZ : return 7800;
        default: return -1;
    }
}

int lgw_rxif_setconf (uint8_t if_chain, struct lgw_conf_rxif_s conf) {
    int32_t bw_hz;
    uint32_t rf_rx_bandwidth;
    uint8_t ifmod_config[LGW_IF_CHAIN_NB] = LGW_IFMODEM_CONFIG;
    // uint8_t fsk_sync_word_size = 3; /* default number of bytes for FSK sync word */
    uint64_t fsk_sync_word = 0xC194C1; /* default FSK sync word (ALIGNED RIGHT, MSbit first) */

    /* check input range (segfault prevention) */
    if (if_chain >= LGW_IF_CHAIN_NB) {
        LOG(MOD_SIM|ERROR, "ERROR: %d NOT A VALID IF_CHAIN NUMBER\n", if_chain);
        return LGW_HAL_ERROR;
    }

    /* if chain is disabled, don't care about most parameters */
    if (conf.enable == false) {
        LOG(MOD_SIM|INFO, "Note: if_chain %d disabled\n", if_chain);
        return LGW_HAL_SUCCESS;
    }

    if (conf.rf_chain >= LGW_RF_CHAIN_NB) {
        LOG(MOD_SIM|ERROR, "ERROR: INVALID RF_CHAIN TO ASSOCIATE WITH A LORA_STD IF CHAIN\n");
        return LGW_HAL_ERROR;
    }
    /* check if IF frequency is optimal based on channel and radio bandwidths */
    switch (conf.bandwidth) {
        case BW_250KHZ:
            rf_rx_bandwidth = LGW_RF_RX_BANDWIDTH_250KHZ; /* radio bandwidth */
            break;
        case BW_500KHZ:
            rf_rx_bandwidth = LGW_RF_RX_BANDWIDTH_500KHZ; /* radio bandwidth */
            break;
        default:
            /* For 125KHz and below */
            rf_rx_bandwidth = LGW_RF_RX_BANDWIDTH_125KHZ; /* radio bandwidth */
            break;
    }
    bw_hz = lgw_bw_getval(conf.bandwidth); /* channel bandwidth */
    if ((conf.freq_hz + ((bw_hz==-1)?LGW_REF_BW:bw_hz)/2) > ((int32_t)rf_rx_bandwidth/2)) {
        LOG(MOD_SIM|ERROR, "ERROR: IF FREQUENCY %d TOO HIGH\n", conf.freq_hz);
        return LGW_HAL_ERROR;
    } else if ((conf.freq_hz - ((bw_hz==-1)?LGW_REF_BW:bw_hz)/2) < -((int32_t)rf_rx_bandwidth/2)) {
        LOG(MOD_SIM|ERROR, "ERROR: IF FREQUENCY %d TOO LOW\n", conf.freq_hz);
        return LGW_HAL_ERROR;
    }

    /* check parameters according to the type of IF chain + modem,
    fill default if necessary, and commit configuration if everything is OK */
    switch (ifmod_config[if_chain]) {
        case IF_LORA_STD:
            /* fill default parameters if needed */
            if (conf.bandwidth == BW_UNDEFINED) {
                conf.bandwidth = BW_250KHZ;
            }
            if (conf.datarate == DR_UNDEFINED) {
                conf.datarate = DR_LORA_SF9;
            }
            /* check BW & DR */
            if (!IS_LORA_BW(conf.bandwidth)) {
                LOG(MOD_SIM|ERROR, "ERROR: BANDWIDTH NOT SUPPORTED BY LORA_STD IF CHAIN\n");
                return LGW_HAL_ERROR;
            }
            if (!IS_LORA_STD_DR(conf.datarate)) {
                LOG(MOD_SIM|ERROR, "ERROR: DATARATE NOT SUPPORTED BY LORA_STD IF CHAIN\n");
                return LGW_HAL_ERROR;
            }
            /* set internal configuration  */
            // if_enable[if_chain] = conf.enable;
            // if_rf_chain[if_chain] = conf.rf_chain;
            // if_freq[if_chain] = conf.freq_hz;
            // lora_rx_bw = conf.bandwidth;
            // lora_rx_sf = (uint8_t)(DR_LORA_MULTI & conf.datarate); /* filter SF out of the 7-12 range */
            // if (SET_PPM_ON(conf.bandwidth, conf.datarate)) {
            //     lora_rx_ppm_offset = true;
            // } else {
            //     lora_rx_ppm_offset = false;
            // }
            LOG(MOD_SIM|INFO, "Note: LoRa 'std' if_chain %d configuration; en:%d rf_chain:%d freq:%d bw:%d dr:%d\n",
                if_chain, conf.enable, conf.rf_chain, conf.freq_hz, conf.bandwidth, (uint8_t)(DR_LORA_MULTI & conf.datarate));
            break;

        case IF_LORA_MULTI:
            /* fill default parameters if needed */
            if (conf.bandwidth == BW_UNDEFINED) {
                conf.bandwidth = BW_125KHZ;
            }
            if (conf.datarate == DR_UNDEFINED) {
                conf.datarate = DR_LORA_MULTI;
            }
            /* check BW & DR */
            if (conf.bandwidth != BW_125KHZ) {
                LOG(MOD_SIM|ERROR, "ERROR: BANDWIDTH NOT SUPPORTED BY LORA_MULTI IF CHAIN\n");
                return LGW_HAL_ERROR;
            }
            if (!IS_LORA_MULTI_DR(conf.datarate)) {
                LOG(MOD_SIM|ERROR, "ERROR: DATARATE(S) NOT SUPPORTED BY LORA_MULTI IF CHAIN\n");
                return LGW_HAL_ERROR;
            }
            /* set internal configuration  */
            // if_enable[if_chain] = conf.enable;
            // if_rf_chain[if_chain] = conf.rf_chain;
            // if_freq[if_chain] = conf.freq_hz;
            // lora_multi_sfmask[if_chain] = (uint8_t)(DR_LORA_MULTI & conf.datarate); /* filter SF out of the 7-12 range */
            LOG(MOD_SIM|INFO, "Note: LoRa 'multi' if_chain %d configuration; en:%d rf_chain:%d freq:%d SF_mask:0x%02x\n",
                if_chain, conf.enable, conf.rf_chain, conf.freq_hz, (uint8_t)(DR_LORA_MULTI & conf.datarate));
            break;

        case IF_FSK_STD:
            /* fill default parameters if needed */
            if (conf.bandwidth == BW_UNDEFINED) {
                conf.bandwidth = BW_250KHZ;
            }
            if (conf.datarate == DR_UNDEFINED) {
                conf.datarate = 64000; /* default datarate */
            }
            /* check BW & DR */
            if(!IS_FSK_BW(conf.bandwidth)) {
                LOG(MOD_SIM|ERROR, "ERROR: BANDWIDTH NOT SUPPORTED BY FSK IF CHAIN\n");
                return LGW_HAL_ERROR;
            }
            if(!IS_FSK_DR(conf.datarate)) {
                LOG(MOD_SIM|ERROR, "ERROR: DATARATE NOT SUPPORTED BY FSK IF CHAIN\n");
                return LGW_HAL_ERROR;
            }
            /* set internal configuration  */
            // if_enable[if_chain] = conf.enable;
            // if_rf_chain[if_chain] = conf.rf_chain;
            // if_freq[if_chain] = conf.freq_hz;
            // fsk_rx_bw = conf.bandwidth;
            // fsk_rx_dr = conf.datarate;
            if (conf.sync_word > 0) {
                // fsk_sync_word_size = conf.sync_word_size;
                fsk_sync_word = conf.sync_word;
            }
            LOG(MOD_SIM|INFO, "Note: FSK if_chain %d configuration; en:%d rf_chain:%d freq:%d bw:%d dr:%d (%d real dr) sync:0x%0X\n",
                if_chain, conf.enable, conf.rf_chain, conf.freq_hz, conf.bandwidth, conf.datarate, LGW_XTAL_FREQU/(LGW_XTAL_FREQU/conf.datarate), fsk_sync_word);
            break;

        default:
            LOG(MOD_SIM|ERROR, "ERROR: IF CHAIN %d TYPE NOT SUPPORTED\n", if_chain);
            return LGW_HAL_ERROR;
}
    return LGW_HAL_SUCCESS;
}


int lgw_txgain_setconf (struct lgw_tx_gain_lut_s* conf) {
    int i;

    /* Check LUT size */
    if ((conf->size < 1) || (conf->size > TX_GAIN_LUT_SIZE_MAX)) {
        LOG(MOD_SIM|ERROR, "ERROR: TX gain LUT must have at least one entry and  maximum %d entries\n", TX_GAIN_LUT_SIZE_MAX);
        return LGW_HAL_ERROR;
    }

    // txgain_lut.size = conf->size;

    for (i = 0; i < conf->size; i++) {
        /* Check gain range */
        if (conf->lut[i].dig_gain > 3) {
            LOG(MOD_SIM|ERROR, "ERROR: TX gain LUT: SX1301 digital gain must be between 0 and 3\n");
            return LGW_HAL_ERROR;
        }
        if (conf->lut[i].dac_gain != 3) {
            LOG(MOD_SIM|ERROR, "ERROR: TX gain LUT: SX1257 DAC gains != 3 are not supported\n");
            return LGW_HAL_ERROR;
        }
        if (conf->lut[i].mix_gain > 15) {
            LOG(MOD_SIM|ERROR, "ERROR: TX gain LUT: SX1257 mixer gain must not exceed 15\n");
            return LGW_HAL_ERROR;
        } else if (conf->lut[i].mix_gain < 8) {
            LOG(MOD_SIM|ERROR, "ERROR: TX gain LUT: SX1257 mixer gains < 8 are not supported\n");
            return LGW_HAL_ERROR;
        }
        if (conf->lut[i].pa_gain > 3) {
            LOG(MOD_SIM|ERROR, "ERROR: TX gain LUT: External PA gain must not exceed 3\n");
            return LGW_HAL_ERROR;
        }

        // /* Set internal LUT */
        // txgain_lut.lut[i].dig_gain = conf->lut[i].dig_gain;
        // txgain_lut.lut[i].dac_gain = conf->lut[i].dac_gain;
        // txgain_lut.lut[i].mix_gain = conf->lut[i].mix_gain;
        // txgain_lut.lut[i].pa_gain  = conf->lut[i].pa_gain;
        // txgain_lut.lut[i].rf_power = conf->lut[i].rf_power;
    }

    return LGW_HAL_SUCCESS;
}

int lgw_lbt_setconf (struct lgw_conf_lbt_s conf) {
    return LGW_HAL_SUCCESS;
}

str_t lgw_version_info () {
    return "LGW Simulation";
}

#if defined(CFG_smtcpico)
int lgw_connect (const char *com_path) {
    return LGW_HAL_SUCCESS;
}
#endif

#endif // CFG_lgw1
#if defined(CFG_lgw2)
/* **********************************************
   ****              LGW 2                   ****
   ********************************************** */

static struct
{
    sx1301ar_btype_t    btype; /* Board type: Master or slave */
    int16_t             fpga_version; /* FPGA version */
    bool                is_started; /* Is the board started or not */
    /* Radio configuration */
    uint32_t            rx_freq; /* Center RX frequency of the radio, in Hz */
    uint32_t            rx_bw; /* RX bandwidth of the radio, in Hz */
    bool                full_duplex; /* Full-duplex / Half-duplex mode */
    /* Radio chains configuration */
    sx1301ar_rfchain_t  rf_chain[SX1301AR_BOARD_RFCHAIN_NB];
    bool                rf_diversity; /* indicates if antenna diversity is used on this board */
    /* Phy parameters that can be protocol-specific */
    uint32_t            fsk_sync_msb; /* FSK sync word register value (aligned left) */
    uint32_t            fsk_sync_lsb; /* FSK sync word register value (aligned left) */
    uint8_t             fsk_sync_size; /* FSK sync word size */
    bool                loramac_public; /* Enable ONLY for *public* networks using the LoRa MAC protocol */
    /* SX1301 settings */
    uint8_t             chip_nb; /* Number of chip per board */
    bool                chip_en[SX1301AR_BOARD_CHIPS_NB]; /* Is SX1301 enabled or not */
    uint8_t             chip_rf_chain[SX1301AR_BOARD_CHIPS_NB]; /* Select Rx RF path */
    uint32_t            chip_freq[SX1301AR_BOARD_CHIPS_NB]; /* Center RX frequency of the SX1301 chips, in Hz */
    /* Channels settings */
    uint32_t            chan_en[SX1301AR_BOARD_CHIPS_NB][SX1301AR_CHIP_CHAN_NB]; /* Is RX channel enabled or not */
    uint32_t            chan_freq[SX1301AR_BOARD_CHIPS_NB][SX1301AR_CHIP_CHAN_NB]; /* Center RX frequency of the channels, in Hz */
    uint8_t             multi_sf[SX1301AR_BOARD_CHIPS_NB][SX1301AR_CHIP_MULTI_NB]; /* SF mask for LoRa multi-SF channels */
    sx1301ar_bandw_t    lsa_bw[SX1301AR_BOARD_CHIPS_NB]; /* RX bandwidth for the LoRa stand-alone channel */
    uint8_t             lsa_sf[SX1301AR_BOARD_CHIPS_NB]; /* Spreading factor for the LoRa stand-alone channel */
    sx1301ar_bandw_t    fsk_bw[SX1301AR_BOARD_CHIPS_NB]; /* RX bandwidth for the FSK channel */
    uint32_t            fsk_br[SX1301AR_BOARD_CHIPS_NB]; /* Baud rate for the FSK channel */
    /* DSP settings */
    uint8_t             dsp_nb;       /* Number of DSP */
    int16_t             dsp_version;  /* DSP version, -1 if unknown */
    uint8_t             dsp_stat_interval;
    int8_t              room_temp_ref; /* reference room temperature (Tref) */
    uint8_t             ad9361_temp_ref; /* temperature code returned by radio sensor when room is at Tref */
    /* Options */
    bool                match_tmst_crc_err; /* match fine timestamps for packets with CRC error */
    uint8_t             main_tmst_version; /* version of the encrypted/main fine timestamp to be sent by the DSP */
    bool                debug_tmst; /* enable/disable fine timestamp debug mode of DSP */
}
brd_cfg_priv[SX1301AR_MAX_BOARD_NB];

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define CHAN_IF_MAX         1500000     /* filter has ~3 MHz bandwidth */
#define MULTI_DEFAULT_SF    MR_SF7_10   /* LoRa multi-SF default SFs */
#define LSA_DEFAULT_BW      BW_125K     /* LoRa stand-alone default bandwidth */
#define LSA_DEFAULT_SF      10          /* LoRa stand-alone default SF */
#define FSK_DEFAULT_BW      BW_125K     /* FSK default bandwidth */
#define FSK_DEFAULT_MR      MR_64K      /* FSK default modulation rate */


const char * sx1301ar_version_info( uint8_t brd, int16_t *fpga_version, int16_t *dsp_version ) {
    return "LGW2 Simulation";
}

/*
    Configuration functions
    ~~~~~~~~~~~~~~~~~~~~~~~~

    Affect the internal configuration state of the library.
    Check to consistency of user-set parameters.
*/

int sx1301ar_conf_tx_gain( uint8_t brd, uint8_t rf_chain, const sx1301ar_tx_gain_lut_t * cfg );

int sx1301ar_conf_board( uint8_t brd, const sx1301ar_board_cfg_t * cfg )
{
    int i, x;
    uint64_t sync_word_reg;

    /* Check input parameters */
    if( brd >= SX1301AR_MAX_BOARD_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_BOARD_NB );
    }
    if( cfg->rx_freq_hz < SX1301AR_MIN_FREQ )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CFG );
    }
    if( (cfg->spi_read == NULL) || (cfg->spi_write == NULL) )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CFG );
    }
    if( cfg->fsk_sync_size > 8 )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CFG );
    }
    if( (cfg->rx_bw_hz != 4e6) && (cfg->rx_bw_hz != 7e6) && (cfg->rx_bw_hz != 8e6) && (cfg->rx_bw_hz != 13e6) )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CFG );
    }
    if( (cfg->board_type != BRD_MASTER) && (cfg->board_type != BRD_SLAVE) )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CFG );
    }
    if( (cfg->nb_chip == 0) || (cfg->nb_chip > SX1301AR_BOARD_CHIPS_NB) )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CFG );
    }

    /* initialize memory for global variables */
    // memset( &brd_channel_diversity_table[brd], 0, sizeof brd_channel_diversity_table[brd] );

    /* No reconfiguration if board is running */
    if( brd_cfg_priv[brd].is_started == true )
    {
        SX1301AR_ERR_RETURN( ERR_CANT_CFG );
    }

    /* Default values */
    brd_cfg_priv[brd].fpga_version = -1; /* unknown yet */
    brd_cfg_priv[brd].dsp_version = -1; /* unknown yet */
    brd_cfg_priv[brd].rf_diversity = false; /* unknown yet */

    /* Record configuration values */
    brd_cfg_priv[brd].btype = cfg->board_type;
    brd_cfg_priv[brd].chip_nb = cfg->nb_chip;
    brd_cfg_priv[brd].dsp_nb = cfg->nb_dsp;
    brd_cfg_priv[brd].rx_freq = cfg->rx_freq_hz;
    brd_cfg_priv[brd].rx_bw = cfg->rx_bw_hz;
    brd_cfg_priv[brd].full_duplex = cfg->full_duplex;
    for( i = 0; i < SX1301AR_BOARD_RFCHAIN_NB; i++ )
    {
        brd_cfg_priv[brd].rf_chain[i].rx_enable = cfg->rf_chain[i].rx_enable;
        brd_cfg_priv[brd].rf_chain[i].tx_enable = cfg->rf_chain[i].tx_enable;
        brd_cfg_priv[brd].rf_chain[i].rssi_offset = cfg->rf_chain[i].rssi_offset;
        brd_cfg_priv[brd].rf_chain[i].rssi_offset_coeff_a = cfg->rf_chain[i].rssi_offset_coeff_a;
        brd_cfg_priv[brd].rf_chain[i].rssi_offset_coeff_b = cfg->rf_chain[i].rssi_offset_coeff_b;
        if( cfg->rf_chain[i].tx_lut.size > 0 )
        {
            x = sx1301ar_conf_tx_gain( brd, i, &(cfg->rf_chain[i].tx_lut) );
            if( x != 0 )
            {
                printf( "ERROR: failed to configure TX Gain LUT for RF chain %d (%s)\n", i, sx1301ar_err_message( sx1301ar_errno ) );
            }
        }
    }
    brd_cfg_priv[brd].room_temp_ref = cfg->room_temp_ref;
    brd_cfg_priv[brd].ad9361_temp_ref = cfg->ad9361_temp_ref;

    brd_cfg_priv[brd].fsk_sync_size = cfg->fsk_sync_size;
    if( cfg->fsk_sync_size > 0 )
    {
        sync_word_reg = cfg->fsk_sync_word << (8 - cfg->fsk_sync_size) * 8; /* left-align the sync word */
        brd_cfg_priv[brd].fsk_sync_msb = 0xFFFFFFFF & (sync_word_reg >> 32);
        brd_cfg_priv[brd].fsk_sync_lsb = 0xFFFFFFFF &  sync_word_reg;
    }
    brd_cfg_priv[brd].loramac_public = cfg->loramac_public;
    brd_cfg_priv[brd].dsp_stat_interval = cfg->dsp_stat_interval;
    // memcpy(brd_aes_key[brd], cfg->aes_key, sizeof brd_aes_key[brd]);

    brd_cfg_priv[brd].match_tmst_crc_err = cfg->match_tmst_crc_err;
    brd_cfg_priv[brd].main_tmst_version = cfg->main_tmst_version;
    brd_cfg_priv[brd].debug_tmst = cfg->debug_tmst;

    /* Record SPI callbacks */
    // return sx1301ar_reg_setup( brd, cfg->board_type, cfg->spi_read, cfg->spi_write );
    return 0;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_get_conf_board( uint8_t brd, sx1301ar_board_cfg_t * cfg )
{
    int i;

    /* Check input parameters */
    if( brd >= SX1301AR_MAX_BOARD_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_BOARD_NB );
    }
    if( cfg == NULL )
    {
        SX1301AR_ERR_RETURN( ERR_NULL_POINTER );
    }

    /* Get recorded configuration values */
    cfg->board_type = brd_cfg_priv[brd].btype;
    cfg->nb_chip = brd_cfg_priv[brd].chip_nb;
    cfg->nb_dsp = brd_cfg_priv[brd].dsp_nb;
    cfg->rx_freq_hz = brd_cfg_priv[brd].rx_freq;
    cfg->rx_bw_hz = brd_cfg_priv[brd].rx_bw;
    cfg->full_duplex = brd_cfg_priv[brd].full_duplex;
    for( i = 0; i < SX1301AR_BOARD_RFCHAIN_NB; i++ )
    {
        cfg->rf_chain[i].rx_enable = brd_cfg_priv[brd].rf_chain[i].rx_enable;
        cfg->rf_chain[i].tx_enable = brd_cfg_priv[brd].rf_chain[i].tx_enable;
    }

    return 0;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_conf_chip( uint8_t brd, uint8_t chip, const sx1301ar_chip_cfg_t * cfg )
{
    /* Check input parameters */
    if( brd >= SX1301AR_MAX_BOARD_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_BOARD_NB );
    }
    if( chip >= SX1301AR_BOARD_CHIPS_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CHIP_NB );
    }
    if( (cfg->enable == true) && (cfg->freq_hz < SX1301AR_MIN_FREQ) )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CFG );
    }

    /* No reconfiguration if board is running */
    if( brd_cfg_priv[brd].is_started == true )
    {
        SX1301AR_ERR_RETURN( ERR_CANT_CFG );
    }

    /* Record enable setting */
    brd_cfg_priv[brd].chip_en[chip] = cfg->enable;
    brd_cfg_priv[brd].chip_rf_chain[chip] = cfg->rf_chain;

    if( cfg->enable == true )
    {
        /* Record frequency setting */
        brd_cfg_priv[brd].chip_freq[chip] = cfg->freq_hz;
    }

    return 0;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_conf_chan( uint8_t brd, uint8_t chan, const sx1301ar_chan_cfg_t * cfg )
{
    uint8_t chip = chan >> 4;
    uint8_t cx = chan & 0x0F; /* local channel address in the chip */
    int sf;

    /* Check input parameters */
    if( brd >= SX1301AR_MAX_BOARD_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_BOARD_NB );
    }
    if( chip >= SX1301AR_BOARD_CHIPS_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CHAN_NB );
    }
    if( cx >= SX1301AR_CHIP_CHAN_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CHAN_NB );
    }
    if( (cfg->enable == true) && (cfg->freq_hz < SX1301AR_MIN_FREQ) )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_CFG );
    }

    /* No reconfiguration if board is running */
    if( brd_cfg_priv[brd].is_started == true )
    {
        SX1301AR_ERR_RETURN( ERR_CANT_CFG );
    }

    /* Record enable setting */
    brd_cfg_priv[brd].chan_en[chip][cx] = cfg->enable;

    if( cfg->enable == true )
    {
        /* Check that the corresponding chip is enabled */
        if( brd_cfg_priv[brd].chip_en[chip] == false )
        {
            SX1301AR_ERR_RETURN( ERR_CHIP_DISABLE );
        }

        /* Check channel frequency against radio & chip configuration:
         *  => we want to ensure that all channels fit in the the sx1301 band
         *     AND in the radio band.
         *  Note: the sx1301 bandwidth could be partly outside from the radio
         *        band as long as there is no channel in this "outer" band.
         *
         *                             rx_freq (ad9361)
         *                                ^              chip_freq (sx1301)
         *                                |                    ^ chan_freq
         *                                |                    |    ^
         *                                |                    |    |
         *                                |<----------rx_bw/2-------->
         *   -----------------------------|--------------------|----|--
         *  /                             |                    |    |  \
         * /                              |         -----------|----|---\--
         *                                |        /<--------->|    |    \ \
         *                                |       / CHAN_IF_MAX|    |     \ \
         *                                |      /             |    |      \ \
         *--------------------------------|-----/--------------|-|--|--|----\-\----->f
         *                                |    /                 |<--->|     \ \
         *                                |                      chan_bw
         */
        /* Check that the channel is in the SX1301 band */
        if( cfg->freq_hz > (brd_cfg_priv[brd].chip_freq[chip] + CHAN_IF_MAX - sx1301ar_bw_enum2nb(cfg->bandwidth)/2) )
        {
            SX1301AR_ERR_RETURN( ERR_IF_LIMIT );
        }
        if( cfg->freq_hz < (brd_cfg_priv[brd].chip_freq[chip] - CHAN_IF_MAX + sx1301ar_bw_enum2nb(cfg->bandwidth)/2) )
        {
            SX1301AR_ERR_RETURN( ERR_IF_LIMIT );
        }
        /* Check that the channel is in the radio band */
        if( cfg->freq_hz > (brd_cfg_priv[brd].rx_freq + brd_cfg_priv[brd].rx_bw/2 - sx1301ar_bw_enum2nb(cfg->bandwidth)/2) )
        {
            SX1301AR_ERR_RETURN( ERR_IF_LIMIT );
        }
        if( cfg->freq_hz < (brd_cfg_priv[brd].rx_freq - brd_cfg_priv[brd].rx_bw/2 + sx1301ar_bw_enum2nb(cfg->bandwidth)/2) )
        {
            SX1301AR_ERR_RETURN( ERR_IF_LIMIT );
        }

        /* Record frequency setting */
        brd_cfg_priv[brd].chan_freq[chip][cx] = cfg->freq_hz;

        /* Check and record channel-specific settings */
        if( cx < SX1301AR_CHIP_MULTI_NB )
        {
            /* LoRa multi-SF channel */
            if( (cfg->bandwidth != BW_UNDEFINED) && (cfg->bandwidth != BW_125K) )
            {
                SX1301AR_ERR_RETURN( ERR_INVALID_BW ); /* only 125 kHz bandwidth is supported */
            }

            if( cfg->modrate == MR_UNDEFINED )
            {
                brd_cfg_priv[brd].multi_sf[chip][cx] = MULTI_DEFAULT_SF; /* force default setting */
            }
            else if( (cfg->modrate & ~MR_SF7_12) == 0 )
            {
                brd_cfg_priv[brd].multi_sf[chip][cx] = (uint8_t)(cfg->modrate & MR_SF7_12);
            }
            else
            {
                SX1301AR_ERR_RETURN( ERR_INVALID_SF );
            }
        }
        else if( cx == SX1301AR_CHIP_LSA_IDX )
        {
            /* LoRa stand-alone channel */
            if( cfg->bandwidth == BW_UNDEFINED )
            {
                brd_cfg_priv[brd].lsa_bw[chip] = LSA_DEFAULT_BW; /* force default setting */
            }
            else if( (cfg->bandwidth == BW_125K) || (cfg->bandwidth == BW_250K) || (cfg->bandwidth == BW_500K) )
            {
                brd_cfg_priv[brd].lsa_bw[chip] = cfg->bandwidth;
            }
            else
            {
                SX1301AR_ERR_RETURN( ERR_INVALID_BW );
            }

            if( cfg->modrate == MR_UNDEFINED )
            {
                brd_cfg_priv[brd].lsa_sf[chip] = LSA_DEFAULT_SF; /* force default setting */
            }
            else
            {
                sf = sx1301ar_sf_enum2nb( cfg->modrate );
                if( sf != -1 )
                {
                    brd_cfg_priv[brd].lsa_sf[chip] = sf;
                }
                else
                {
                    SX1301AR_ERR_RETURN( ERR_INVALID_SF );
                }
            }
        }
        else if( cx == SX1301AR_CHIP_FSK_IDX )
        {
            /* FSK channel */
            if( cfg->bandwidth == BW_UNDEFINED )
            {
                brd_cfg_priv[brd].fsk_bw[chip] = FSK_DEFAULT_BW; /* force default setting */
            }
            else if( cfg->bandwidth <= BW_7K8 ) /* bandwidth index in the enum is inversely proportional to bandwidth */
            {
                brd_cfg_priv[brd].fsk_bw[chip] = cfg->bandwidth;
            }
            else
            {
                SX1301AR_ERR_RETURN( ERR_INVALID_BW );
            }

            if( cfg->modrate == MR_UNDEFINED )
            {
                brd_cfg_priv[brd].fsk_br[chip] = FSK_DEFAULT_MR;
            }
            else if( (cfg->modrate >= MR_300) && (cfg->modrate <= MR_250K) )
            {
                brd_cfg_priv[brd].fsk_br[chip] = cfg->modrate;
            }
            else
            {
                SX1301AR_ERR_RETURN( ERR_INVALID_BR );
            }
        }
    }

    return 0;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_conf_lbt( uint8_t brd, const sx1301ar_lbt_cfg_t * cfg )
{

    /* Check input parameters */
    if( brd >= SX1301AR_MAX_BOARD_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_BOARD_NB );
    }
    if( cfg == NULL )
    {
        SX1301AR_ERR_RETURN( ERR_NULL_POINTER );
    }

    /* No reconfiguration if board is running */
    if( brd_cfg_priv[brd].is_started == true )
    {
        SX1301AR_ERR_RETURN( ERR_CANT_CFG );
    }

    // x = sx1301ar_i_conf_lbt( brd, cfg );
    // SX1301AR_ERR_PROPAGATE( x );

    return 0;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_conf_tx_gain( uint8_t brd, uint8_t rf_chain, const sx1301ar_tx_gain_lut_t * cfg )
{
    int i;

    /* Check input parameters */
    if( brd >= SX1301AR_MAX_BOARD_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_BOARD_NB );
    }
    if( cfg == NULL )
    {
        SX1301AR_ERR_RETURN( ERR_NULL_POINTER );
    }
    if( (cfg->size < 1) || (cfg->size > SX1301AR_BOARD_MAX_LUT_NB) )
    {
        SX1301AR_ERR_RETURN( ERR_INVALID_LUT );
    }
    if( rf_chain >= SX1301AR_BOARD_RFCHAIN_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_RFCHAIN_NB );
    }

    /* Update internal TX gain LUT with given values */
    brd_cfg_priv[brd].rf_chain[rf_chain].tx_lut.size = cfg->size;

    for( i = 0; i < cfg->size; i++ )
    {
        /* Check parameter range */
        if( cfg->lut[i].fpga_dig_gain > 13 )
        {
            SX1301AR_ERR_RETURN( ERR_INVALID_LUT );
        }
        if( cfg->lut[i].ad9361_gain.auxdac_word > 1023 )
        {
            SX1301AR_ERR_RETURN( ERR_INVALID_LUT );
        }


        /* Set internal LUT */
        brd_cfg_priv[brd].rf_chain[rf_chain].tx_lut.lut[i].rf_power = cfg->lut[i].rf_power;
        brd_cfg_priv[brd].rf_chain[rf_chain].tx_lut.lut[i].fpga_dig_gain = cfg->lut[i].fpga_dig_gain;
        brd_cfg_priv[brd].rf_chain[rf_chain].tx_lut.lut[i].ad9361_gain.atten = cfg->lut[i].ad9361_gain.atten;
        brd_cfg_priv[brd].rf_chain[rf_chain].tx_lut.lut[i].ad9361_gain.auxdac_vref = cfg->lut[i].ad9361_gain.auxdac_vref;
        brd_cfg_priv[brd].rf_chain[rf_chain].tx_lut.lut[i].ad9361_gain.auxdac_word = cfg->lut[i].ad9361_gain.auxdac_word;
        brd_cfg_priv[brd].rf_chain[rf_chain].tx_lut.lut[i].ad9361_tcomp.coeff_a  = cfg->lut[i].ad9361_tcomp.coeff_a;
        brd_cfg_priv[brd].rf_chain[rf_chain].tx_lut.lut[i].ad9361_tcomp.coeff_b  = cfg->lut[i].ad9361_tcomp.coeff_b;
    }

    return 0;
}

/*
    Core functions
    ~~~~~~~~~~~~~~~

    Interact with the hardware concentrator board.
*/

int sx1301ar_start( uint8_t nb_brd ) {
    const char* sockPath = getenv("LORAGW_SPI");
    if( aio )
        return -1;
    memset(&cca_msg, 0, sizeof(cca_msg));
    memset(&sockAddr, 0, sizeof(sockAddr));
    // Make xticks different from ustime to cover more test ground.
    // xticks start at ~(1<<28) whenever a radio simulation starts.
    timeOffset = sys_time() - 0x10000000;
    sockAddr.sun_family = AF_UNIX;
    snprintf(sockAddr.sun_path, sizeof(sockAddr.sun_path), "%s", sockPath);
    rt_yieldTo(&conn_tmr, try_connecting);
    return 0;
}

int sx1301ar_stop( uint8_t nb_brd ) {
    rt_clrTimer(&conn_tmr);
    txbeg = txend = 0;
    aio_close(aio);
    aio = NULL;
    return 0;
}

int sx1301ar_fetch( uint8_t brd, sx1301ar_rx_pkt_t * p, uint8_t max_nb, uint8_t * nb_pkt ) {
    int npkts = 0;
    while( npkts < max_nb && rbused(rx_widx, rx_ridx, rxblen) >= sizeof(rx_pkts[0]) ){
        p[npkts] = rx_pkts[rx_ridx/sizeof(rx_pkts[0])];
        rx_ridx = (rx_ridx+sizeof(rx_pkts[0])) % rxblen;
        npkts += 1;
    }
    if( npkts )
        LOG(MOD_SIM|DEBUG, "LGWSIM(%s): received %d packets", sockAddr.sun_path, npkts);
    *nb_pkt = npkts;
    return 0;
}

int sx1301ar_send( uint8_t brd, const sx1301ar_tx_pkt_t * p ) {
    sL_t t = xticks();
    txbeg = t + (s4_t)((u4_t)p->count_us - (u4_t)t);
    txend = txbeg + airtime(p->modrate, p->bandwidth, p->size);
    if( !cca(txbeg, p->freq_hz) )
        return -1;
    tx_pkt = *p;
    if( !aio || aio->ctx == NULL || aio->fd == 0 )
        return -1;
    aio_set_wrfn(aio, write_socket);
    write_socket(aio);
    return 0;
}

int sx1301ar_tx_status( uint8_t brd, sx1301ar_tstat_t * s ) {
    sL_t t = xticks();
    if( t <= txbeg )
        *s = TX_SCHEDULED;
    else if( t <= txend )
        *s = TX_EMITTING;
    else
        *s = TX_FREE;
    return 0;

}

int sx1301ar_abort_tx( uint8_t brd ) {
    txbeg = txend = 0;
    return 0;
}

// int sx1301ar_brd_status( uint8_t brd, sx1301ar_bstat_t * s ) {
//     return 0;
// }

int sx1301ar_get_instcnt( uint8_t brd, uint32_t * cnt_us ) {
    sL_t t = xticks();
    cnt_us[0] = t & 0xFFffFFff;
    return 0;
}

int sx1301ar_get_trigcnt( uint8_t brd, uint32_t * cnt_us ) {
    sL_t t = xticks();
    t -= sys_utc()%1000000;
    cnt_us[0] = t  & 0xFFffFFff;
    return 0;
}

// int sx1301ar_get_trighs( uint8_t brd, uint32_t * cnt_hs) {
//     return 0;
// }

// int sx1301ar_get_temperature( uint8_t brd, uint8_t * temp_code, int16_t * temp_celsius ) {
//     return 0;
// }

// double sx1301ar_rx_pkt_time_on_air( const sx1301ar_rx_pkt_t * pkt, double * t_preamble, double * t_syncword, double * t_payload ) {
//     return 0;
// }

// double sx1301ar_tx_pkt_time_on_air( uint8_t brd, const sx1301ar_tx_pkt_t * pkt, double * t_preamble, double * t_syncword, double * t_payload ) {
//     return 0;
// }

/*
    Helper functions
    ~~~~~~~~~~~~~~~~~

    Assist for internal types manipulation.
    Initialize structures defined by the library.
    Covert to/from abstract (enum) types.
*/

sx1301ar_board_cfg_t sx1301ar_init_board_cfg( void )
{
    sx1301ar_board_cfg_t a;
    int i;

    a.board_type = BRD_TYPE_UNKNOWN;
    a.spi_read = NULL;
    a.spi_write = NULL;
    a.rx_freq_hz = 0;
    a.rx_bw_hz = 0;
    a.full_duplex = false;

    /* RF chain initialization */
    for( i = 0; i < SX1301AR_BOARD_RFCHAIN_NB; i++ )
    {
        a.rf_chain[i].rx_enable = false;
        a.rf_chain[i].tx_enable = false;
        a.rf_chain[i].rssi_offset = SX1301AR_DEFAULT_RSSI_OFFSET;
        a.rf_chain[i].rssi_offset_coeff_a = 0;
        a.rf_chain[i].rssi_offset_coeff_b = 0;
        a.rf_chain[i].tx_lut = sx1301ar_init_tx_gain_lut( );
    }

    /* No board-specific corrections */
    a.room_temp_ref = SX1301AR_DEFAULT_ROOM_TEMP_REF;
    a.ad9361_temp_ref = SX1301AR_DEFAULT_AD9361_TEMP_REF;

    /* Phy parameters that are protocol dependant */
    a.fsk_sync_word = 0;
    a.fsk_sync_size = 0;
    a.loramac_public = false;

    /* Localization parameters */
    a.dsp_stat_interval = 0; /* Turn off dsp report by default */
    memset(a.aes_key, 0, sizeof a.aes_key);
    a.match_tmst_crc_err = false;
    a.main_tmst_version = SX1301AR_DEFAULT_FTS_VERSION;
    a.debug_tmst = false;

    a.nb_chip = 0;
    a.nb_dsp = 0;

    return a;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_chip_cfg_t sx1301ar_init_chip_cfg( void )
{
    sx1301ar_chip_cfg_t a;

    a.enable  = false;
    a.freq_hz = 0;
    a.rf_chain = 0;

    return a;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_chan_cfg_t sx1301ar_init_chan_cfg( void )
{
    sx1301ar_chan_cfg_t a;

    a.enable    = false;
    a.freq_hz   = 0;
    a.modrate   = MR_UNDEFINED;
    a.bandwidth = BW_UNDEFINED;

    return a;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_lbt_cfg_t sx1301ar_init_lbt_cfg( void )
{
    sx1301ar_lbt_cfg_t a;

    a.enable        = false;
    a.rssi_target   = 0;
    a.rssi_shift    = 0;
    a.nb_channel    = 0;
    memset( a.channels, 0, sizeof a.channels );

    return a;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_tx_pkt_t sx1301ar_init_tx_pkt( void )
{
    sx1301ar_tx_pkt_t a;

    a.tx_mode    = TX_IMMEDIATE;
    a.count_us   = 0;
    a.freq_hz    = 0;
    a.rf_power   = 0;
    a.modulation = MOD_UNDEFINED;
    a.bandwidth  = BW_UNDEFINED;
    a.modrate    = MR_UNDEFINED;
    a.coderate   = CR_UNDEFINED;
    a.f_dev      = 0;
    a.preamble   = 0;
    a.invert_pol = false;
    a.no_crc     = false;
    a.no_header  = false;
    a.size       = 0;
    memset( (void *)(a.payload), 0, ARRAY_SIZE(a.payload) );

    return a;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_tx_gain_t sx1301ar_init_tx_gain( void )
{
    sx1301ar_tx_gain_t a;

    a.rf_power = 0;
    a.fpga_dig_gain = 0;
    a.ad9361_gain.atten = 0;
    a.ad9361_gain.auxdac_vref = 0;
    a.ad9361_gain.auxdac_word = 0;
    a.ad9361_tcomp.coeff_a = 0;
    a.ad9361_tcomp.coeff_b = 0;

    return a;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_tx_gain_lut_t sx1301ar_init_tx_gain_lut( void )
{
    sx1301ar_tx_gain_lut_t a;

    a.size = SX1301AR_BOARD_MAX_LUT_NB;
    memset( a.lut, 0, sizeof( sx1301ar_tx_gain_lut_t ) );

    return a;
}


sx1301ar_modr_t sx1301ar_sf_nb2enum( int x )
{
    switch( x )
    {
    case 7 : return MR_SF7;
    case 8 : return MR_SF8;
    case 9 : return MR_SF9;
    case 10: return MR_SF10;
    case 11: return MR_SF11;
    case 12: return MR_SF12;
    default: return MR_UNDEFINED;
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_sf_enum2nb( sx1301ar_modr_t x )
{
    switch( x )
    {
    case MR_SF7 : return 7;
    case MR_SF8 : return 8;
    case MR_SF9 : return 9;
    case MR_SF10: return 10;
    case MR_SF11: return 11;
    case MR_SF12: return 12;
    default: return -1;
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_modr_t sx1301ar_sf_range_nb2enum( int a, int b )
{
    switch( a )
    {
    case 7 :
        switch( b )
        {
        case 7 : return MR_SF7;
        case 8 : return MR_SF7_8;
        case 9 : return MR_SF7_9;
        case 10: return MR_SF7_10;
        case 11: return MR_SF7_11;
        case 12: return MR_SF7_12;
        default: return MR_UNDEFINED;
        }
    case 8 :
        switch( b )
        {
        case 7 : return MR_SF7_8;
        case 8 : return MR_SF8;
        case 9 : return MR_SF8_9;
        case 10: return MR_SF8_10;
        case 11: return MR_SF8_11;
        case 12: return MR_SF8_12;
        default: return MR_UNDEFINED;
        }
    case 9 :
        switch( b )
        {
        case 7 : return MR_SF7_9;
        case 8 : return MR_SF8_9;
        case 9 : return MR_SF9;
        case 10: return MR_SF9_10;
        case 11: return MR_SF9_11;
        case 12: return MR_SF9_12;
        default: return MR_UNDEFINED;
        }
    case 10:
        switch( b )
        {
        case 7 : return MR_SF7_10;
        case 8 : return MR_SF8_10;
        case 9 : return MR_SF9_10;
        case 10: return MR_SF10;
        case 11: return MR_SF10_11;
        case 12: return MR_SF10_12;
        default: return MR_UNDEFINED;
        }
    case 11:
        switch( b )
        {
        case 7 : return MR_SF7_11;
        case 8 : return MR_SF8_11;
        case 9 : return MR_SF9_11;
        case 10: return MR_SF10_11;
        case 11: return MR_SF11;
        case 12: return MR_SF11_12;
        default: return MR_UNDEFINED;
        }
    case 12:
        switch( b )
        {
        case 7 : return MR_SF7_12;
        case 8 : return MR_SF8_12;
        case 9 : return MR_SF9_12;
        case 10: return MR_SF10_12;
        case 11: return MR_SF11_12;
        case 12: return MR_SF12;
        default: return MR_UNDEFINED;
        }
    default: return MR_UNDEFINED;
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_sf_min_enum2nb( sx1301ar_modr_t x )
{
    switch( x )
    {
    case MR_SF7    : return 7;
    case MR_SF7_8  : return 7;
    case MR_SF7_9  : return 7;
    case MR_SF7_10 : return 7;
    case MR_SF7_11 : return 7;
    case MR_SF7_12 : return 7;
    case MR_SF8    : return 8;
    case MR_SF8_9  : return 8;
    case MR_SF8_10 : return 8;
    case MR_SF8_11 : return 8;
    case MR_SF8_12 : return 8;
    case MR_SF9    : return 9;
    case MR_SF9_10 : return 9;
    case MR_SF9_11 : return 9;
    case MR_SF9_12 : return 9;
    case MR_SF10   : return 10;
    case MR_SF10_11: return 10;
    case MR_SF10_12: return 10;
    case MR_SF11   : return 11;
    case MR_SF11_12: return 11;
    case MR_SF12   : return 12;
    default: return -1;
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_sf_max_enum2nb( sx1301ar_modr_t x )
{
    switch( x )
    {
    case MR_SF7    : return 7;
    case MR_SF7_8  : return 8;
    case MR_SF7_9  : return 9;
    case MR_SF7_10 : return 10;
    case MR_SF7_11 : return 11;
    case MR_SF7_12 : return 12;
    case MR_SF8    : return 8;
    case MR_SF8_9  : return 9;
    case MR_SF8_10 : return 10;
    case MR_SF8_11 : return 11;
    case MR_SF8_12 : return 12;
    case MR_SF9    : return 9;
    case MR_SF9_10 : return 10;
    case MR_SF9_11 : return 11;
    case MR_SF9_12 : return 12;
    case MR_SF10   : return 10;
    case MR_SF10_11: return 11;
    case MR_SF10_12: return 12;
    case MR_SF11   : return 11;
    case MR_SF11_12: return 12;
    case MR_SF12   : return 12;
    default: return -1;
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_coder_t sx1301ar_cr_nb2enum( int x )
{
    switch( x )
    {
    case 1: return CR_4_5;
    case 2: return CR_4_6;
    case 3: return CR_4_7;
    case 4: return CR_4_8;
    default: return CR_UNDEFINED;
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

int sx1301ar_cr_enum2nb( sx1301ar_coder_t x )
{
    switch( x )
    {
    case CR_4_5: return 1;
    case CR_4_6: return 2;
    case CR_4_7: return 3;
    case CR_4_8: return 4;
    default: return -1;
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

const char * sx1301ar_cr_enum2str( sx1301ar_coder_t x )
{
    switch( x )
    {
    case CR_4_5: return "4/5";
    case CR_4_6: return "4/6";
    case CR_4_7: return "4/7";
    case CR_4_8: return "4/8";
    default: return "???";
    }
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

sx1301ar_bandw_t sx1301ar_bw_nb2enum( long x )
{
    if( x <= 0 ) return BW_UNDEFINED;
    else if( x <= 7800 ) return BW_7K8;
    else if( x <= 15600 ) return BW_15K6;
    else if( x <= 31200 ) return BW_31K2;
    else if( x <= 62500 ) return BW_62K5;
    else if( x <= 125000 ) return BW_125K;
    else if( x <= 250000 ) return BW_250K;
    else if( x <= 500000 ) return BW_500K;
    else return BW_UNDEFINED;
}

/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

long sx1301ar_bw_enum2nb( sx1301ar_bandw_t x )
{
    switch( x )
    {
    case BW_500K: return 500000;
    case BW_250K: return 250000;
    case BW_125K: return 125000;
    case BW_62K5: return 62500;
    case BW_31K2: return 31200;
    case BW_15K6: return 15600;
    case BW_7K8 : return 7800;
    default: return -1;
    }
}

// int8_t sx1301ar_ad9361_temp2atten( uint8_t T, uint8_t Tref, int16_t a, int16_t b ) {
//     return 0;
// }

// float sx1301ar_ad9361_temp2rssi( uint8_t T, uint8_t Tref, int16_t a, int16_t b ) {
//     return 0;
// }

/** sx1301ar_gps.c ___________________________________________________________________ */

sx1301ar_tref_t sx1301ar_init_tref( void )
{
    sx1301ar_tref_t a;

    a.systime = (time_t)(-1);
    a.hs_pps = 0;
    a.count_us = 0;
    a.utc.tv_sec = (time_t)(-1);
    a.utc.tv_nsec = 0;
    a.xtal_err = 1.0;
    a.xtal_hs_err = 1.0;
    a.sync_cnt = 0;

    return a;
}

int sx1301ar_set_xtal_err( uint8_t brd, sx1301ar_tref_t ref ) {
    return 0;
}

/** sx1301ar_dsp.c ___________________________________________________________________ */

int sx1301ar_get_trighs( uint8_t brd, uint32_t * cnt_hs)
{
    /* Check input parameters */
    if( brd >= SX1301AR_MAX_BOARD_NB )
    {
        SX1301AR_ERR_RETURN( ERR_BAD_BOARD_NB );
    }
    if( cnt_hs == NULL )
    {
        SX1301AR_ERR_RETURN( ERR_NULL_POINTER );
    }

    /* Get high speed counter value at last PPS (HSPPS) */
    sL_t t = xticks();
    t -= sys_utc()%1000000;
    cnt_hs[0] = (t  & 0xFFffFFff)*256;
    return 0;
}

#endif

// LCOV_EXCL_STOP
#endif // CFG_lgwsim
