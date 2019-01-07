// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#if defined(CFG_lgwsim)
// LCOV_EXCL_START
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "lgw/loragw_reg.h"
#include "lgw/loragw_hal.h"
#include "lgw/loragw_lbt.h"
#include "rt.h"
#include "s2e.h"
#include "sys.h"
#include "sys_linux.h"

#define MAX_CCA_INFOS   10
#define MAGIC_CCA_FREQ  0xCCAFCCAF

#define RX_NPKTS 10

struct cca_info {
    u4_t freq;
    sL_t beg;
    sL_t end;
};

struct cca_msg {
    u4_t magic;    // corresponds to freq_hz in struct lgw_pkt_rx_s
    struct cca_info infos[MAX_CCA_INFOS];
};

static sL_t     timeOffset;
static int      rx_ridx;
static int      rx_widx;
static struct lgw_pkt_rx_s rx_pkts[RX_NPKTS+1];
static struct lgw_pkt_tx_s tx_pkt;
static sL_t     txbeg;
static sL_t     txend;
static u1_t     ppsLatched;
static aio_t*   aio;
static tmr_t    conn_tmr;
static struct sockaddr_un sockAddr;
static struct cca_msg     cca_msg;

uint8_t lgwx_device_mode = 0;
uint8_t lgwx_beacon_len = 0;
uint8_t lgwx_beacon_sf = 0;
uint8_t lgwx_lbt_mode = 0;


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
    return s2e_calcDnAirTime(rps_make(sf,bw), plen);
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
    LOG(MOD_SIM|INFO, "LGWSIM: Connected txunit#%d timeOffset=0x%lX xticksNow=0x%lX", sys_slaveIdx, timeOffset, xticks());
    write_socket(aio);
    read_socket(aio);
    return;

 retry:
    rt_setTimer(tmr, rt_seconds_ahead(1));
}


static void read_socket (aio_t* aio) {
    while(1) {
        if( rx_widx >= RX_NPKTS ) {
            if( rx_ridx == 0 ) {
                LOG(MOD_SIM|ERROR, "LGWSIM(%s): RX packet lost - buffer full", sockAddr.sun_path);
                rx_widx = RX_NPKTS-1;
            } else {
                memcpy(&rx_pkts[0], &rx_pkts[rx_ridx], sizeof(rx_pkts[0])*(rx_widx-rx_ridx));
                rx_widx -= rx_ridx;
                rx_ridx = 0;
            }
        }
        int n = read(aio->fd, &rx_pkts[rx_widx], sizeof(rx_pkts[0]));
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
        assert(n == sizeof(rx_pkts[0]));
        if( rx_pkts[rx_widx].freq_hz == MAGIC_CCA_FREQ ) {
            cca_msg = *(struct cca_msg*)&rx_pkts[rx_widx];
            continue;
        }
        rx_widx += 1;
        LOG(MOD_SIM|DEBUG, "LGWSIM(%s): %d packets pending", sockAddr.sun_path, rx_widx-rx_ridx);
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


int lgw_receive (uint8_t max_pkt, struct lgw_pkt_rx_s *pkt_data) {
    int npkts = 0;
    while( npkts < max_pkt && rx_ridx < rx_widx ) {
        pkt_data[npkts] = rx_pkts[rx_ridx];
        rx_ridx += 1;
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
    return LGW_HAL_SUCCESS;
}


int lgw_rxif_setconf (uint8_t if_chain, struct lgw_conf_rxif_s conf) {
    return LGW_HAL_SUCCESS;
}


int lgw_txgain_setconf (struct lgw_tx_gain_lut_s* conf) {
    return LGW_HAL_SUCCESS;
}

int lgw_lbt_setconf (struct lgw_conf_lbt_s conf) {
    return LGW_HAL_SUCCESS;
}

str_t lgw_version_info () {
    return "LGW Simulation";
}
// LCOV_EXCL_STOP
#endif // CFG_lgwsim
