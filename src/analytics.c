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

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include "rt.h"
#include "analytics.h"
#include "s2e.h"
#include "xq.h"
#include "timesync.h"

#if !defined(MSG_DONTWAIT) && defined(O_NONBLOCK)
#define MSG_DONTWAIT O_NONBLOCK
#endif

#define ANA_STAT_INTERVAL_US  (30 * 1000000ULL)

static int ana_fd = -1;
static struct sockaddr_un ana_addr;
static socklen_t ana_addrlen = 0;

static uint32_t ana_rx_packets;
static uint32_t ana_rx_ok;
static uint32_t ana_rx_forwarded;
static uint32_t ana_tx_received;
static uint32_t ana_tx_emitted;

static tmr_t ana_stat_tmr;

static void ana_stat_timeout(tmr_t* tmr) {
    (void)tmr;
    if (ana_fd < 0)
        return;
    struct ana_stat msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.magic = ANA_MAGIC;
    msg.hdr.version = ANA_VERSION;
    msg.hdr.type = ANA_STAT;
    msg.hdr.reserved = 0;
    msg.gw_time_us = (int64_t)rt_getUTC();
    msg.latitude = 0.0f / 0.0f;  /* NAN */
    msg.longitude = 0.0f / 0.0f;
    msg.altitude = 0.0f / 0.0f;
    msg.rx_packets = ana_rx_packets;
    msg.rx_ok = ana_rx_ok;
    msg.rx_forwarded = ana_rx_forwarded;
    msg.tx_received = ana_tx_received;
    msg.tx_emitted = ana_tx_emitted;
    msg.is_gauge = 0;
    msg.temperature = 0.0f / 0.0f;  /* NAN */

    ssize_t n = sendto(ana_fd, &msg, sizeof(msg), MSG_DONTWAIT,
                       (struct sockaddr*)&ana_addr, ana_addrlen);
    (void)n;

    ana_rx_packets = 0;
    ana_rx_ok = 0;
    ana_rx_forwarded = 0;
    ana_tx_received = 0;
    ana_tx_emitted = 0;

    rt_setTimer(&ana_stat_tmr, rt_seconds_ahead(30));
}

void ana_open(const char* path, uint64_t eui) {
    if (!path || path[0] == '\0' || ana_fd >= 0)
        return;
#if defined(__linux__) || defined(__APPLE__)
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG(MOD_SYS|WARNING, "analytics: socket failed: %s", strerror(errno));
        return;
    }
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1) {
        close(fd);
        return;
    }
    memset(&ana_addr, 0, sizeof(ana_addr));
    ana_addr.sun_family = AF_UNIX;
    size_t plen = strlen(path);
    if (plen >= sizeof(ana_addr.sun_path))
        plen = sizeof(ana_addr.sun_path) - 1;
    memcpy(ana_addr.sun_path, path, plen);
    ana_addr.sun_path[plen] = '\0';
    ana_addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + plen + 1);

    ana_fd = fd;
    atexit(ana_close);
    ana_rx_packets = 0;
    ana_rx_ok = 0;
    ana_rx_forwarded = 0;
    ana_tx_received = 0;
    ana_tx_emitted = 0;

    struct ana_hello hello;
    memset(&hello, 0, sizeof(hello));
    hello.hdr.magic = ANA_MAGIC;
    hello.hdr.version = ANA_VERSION;
    hello.hdr.type = ANA_HELLO;
    hello.gateway_eui = eui;
    {
        const char* ver = CFG_version;
        if (ver) {
            size_t i;
            for (i = 0; i < sizeof(hello.station_ver) - 1 && ver[i]; i++)
                hello.station_ver[i] = (uint8_t)ver[i];
            hello.station_ver[i] = '\0';
        }
    }
    sendto(ana_fd, &hello, sizeof(hello), MSG_DONTWAIT,
           (struct sockaddr*)&ana_addr, ana_addrlen);

    rt_iniTimer(&ana_stat_tmr, ana_stat_timeout);
    rt_setTimer(&ana_stat_tmr, rt_seconds_ahead(30));
#else
    (void)path;
    (void)eui;
#endif
}

void ana_close(void) {
#if defined(__linux__) || defined(__APPLE__)
    if (ana_fd >= 0) {
        rt_clrTimer(&ana_stat_tmr);
        close(ana_fd);
        ana_fd = -1;
    }
#endif
}

static void ana_send(const void* buf, size_t len) {
    if (ana_fd < 0 || ana_addrlen == 0)
        return;
    sendto(ana_fd, buf, len, MSG_DONTWAIT,
           (struct sockaddr*)&ana_addr, ana_addrlen);
}

void ana_send_uplink(const struct rxjob* rxjob, const struct s2ctx* s2ctx, const uint8_t* frame) {
    if (ana_fd < 0 || !rxjob || !s2ctx || !frame)
        return;
    const rxjob_t* j = (const rxjob_t*)rxjob;
    const s2ctx_t* s2 = (const s2ctx_t*)s2ctx;
    rps_t rps = s2e_dr2rps((s2ctx_t*)s2, j->dr);
    int sf = rps_sf(rps);
    int bw = rps_bw(rps);
    if (rps == RPS_ILLEGAL)
        sf = 0, bw = 0;

    struct ana_uplink msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.magic = ANA_MAGIC;
    msg.hdr.version = ANA_VERSION;
    msg.hdr.type = ANA_UPLINK;
    msg.rx_wall_us = (int64_t)rt_getUTC();
    msg.rx_gps_time = (int64_t)ts_xtime2gpstime(j->xtime);
    msg.xtime = (int64_t)j->xtime;
    msg.freq = j->freq;
    msg.rssi = (int16_t)(-(int16_t)j->rssi);
    msg.snr_x4 = (int16_t)j->snr;
    msg.fts = (int32_t)j->fts;
    msg.dr = j->dr;
    msg.sf = (uint8_t)(sf <= 0 ? 0 : sf);
    msg.bw = (uint8_t)bw;
    msg.crc_status = 1;  /* we only emit forwarded frames which passed CRC */
    msg.size = (uint16_t)j->len;
    msg.fhdr_len = (uint8_t)(j->len > 16 ? 16 : j->len);
    memcpy(msg.fhdr, frame, (size_t)msg.fhdr_len);

    ana_send(&msg, sizeof(msg));
}

void ana_send_downlink(const struct txjob* txjob, const struct s2ctx* s2ctx, const uint8_t* frame) {
    if (ana_fd < 0 || !txjob || !s2ctx || !frame)
        return;
    const txjob_t* j = (const txjob_t*)txjob;
    const s2ctx_t* s2 = (const s2ctx_t*)s2ctx;
    rps_t rps = s2e_dr2rps((s2ctx_t*)s2, j->dr);
    int sf = rps_sf(rps);
    int bw = rps_bw(rps);
    if (rps == RPS_ILLEGAL)
        sf = 0, bw = 0;

    struct ana_downlink msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.magic = ANA_MAGIC;
    msg.hdr.version = ANA_VERSION;
    msg.hdr.type = ANA_DOWNLINK;
    msg.tx_time_us = (int64_t)j->txtime;
    msg.tx_gps_time = (int64_t)j->gpstime;
    msg.freq = j->freq;
    msg.txpow_x10 = (int16_t)j->txpow;
    msg.dr = j->dr;
    msg.sf = (uint8_t)(sf <= 0 ? 0 : sf);
    msg.bw = (uint8_t)bw;
    msg.txflags = j->txflags;
    msg.txunit = j->txunit;
    msg.addcrc = j->addcrc;
    msg.preamble = j->preamble;
    msg.size = (uint16_t)j->len;
    msg.fhdr_len = (uint8_t)(j->len > 16 ? 16 : j->len);
    memcpy(msg.fhdr, frame, (size_t)msg.fhdr_len);

    ana_send(&msg, sizeof(msg));
}

void ana_send_stat(const struct ana_stat* stat) {
    if (ana_fd < 0 || !stat)
        return;
    ana_send(stat, sizeof(struct ana_stat));
}

void ana_count_rx(uint8_t crc_ok, uint8_t forwarded) {
    ana_rx_packets++;
    if (crc_ok)
        ana_rx_ok++;
    if (forwarded)
        ana_rx_forwarded++;
}

void ana_count_tx_received(void) {
    ana_tx_received++;
}

void ana_count_tx_emitted(void) {
    ana_tx_emitted++;
}
