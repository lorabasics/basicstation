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

#ifndef _analytics_h_
#define _analytics_h_

#include <stdint.h>

/* ----------------------------------------------------------------------------
 * Wire protocol: UNIX SOCK_DGRAM datagrams, little-endian, packed structs.
 * Magic 0xA7, version 1.
 * ---------------------------------------------------------------------------- */

#define ANA_MAGIC   0xA7
#define ANA_VERSION 1

enum {
    ANA_UPLINK   = 0x01,
    ANA_DOWNLINK = 0x02,
    ANA_STAT     = 0x03,
    ANA_HELLO    = 0x04,
};

struct ana_hdr {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  reserved;
} __attribute__((packed));

struct ana_hello {
    struct ana_hdr hdr;
    uint64_t       gateway_eui;
    uint8_t        station_ver[8];
} __attribute__((packed));

struct ana_uplink {
    struct ana_hdr hdr;
    int64_t        rx_wall_us;
    int64_t        rx_gps_time;
    int64_t        xtime;
    uint32_t       freq;
    int16_t        rssi;
    int16_t        snr_x4;
    int32_t        fts;
    uint8_t        dr;
    uint8_t        sf;
    uint8_t        bw;
    uint8_t        crc_status;
    uint16_t       size;
    uint8_t        fhdr_len;
    uint8_t        fhdr[16];
} __attribute__((packed));

struct ana_downlink {
    struct ana_hdr hdr;
    int64_t        tx_time_us;
    int64_t        tx_gps_time;
    uint32_t       freq;
    int16_t        txpow_x10;
    uint8_t        dr;
    uint8_t        sf;
    uint8_t        bw;
    uint8_t        txflags;
    uint8_t        txunit;
    uint8_t        addcrc;
    uint16_t       preamble;
    uint16_t       size;
    uint8_t        fhdr_len;
    uint8_t        fhdr[16];
} __attribute__((packed));

struct ana_stat {
    struct ana_hdr hdr;
    int64_t        gw_time_us;
    float          latitude;
    float          longitude;
    float          altitude;
    uint32_t       rx_packets;
    uint32_t       rx_ok;
    uint32_t       rx_forwarded;
    uint32_t       tx_received;
    uint32_t       tx_emitted;
    uint8_t        is_gauge;
    uint8_t        reserved[3];
    float          temperature;
} __attribute__((packed));

/* ----------------------------------------------------------------------------
 * API (basicstation side). Opaque types: include xq.h/s2e.h before analytics.c.
 * ---------------------------------------------------------------------------- */

struct rxjob;
struct txjob;
struct s2ctx;

void ana_open(const char* path, uint64_t eui);
void ana_close(void);
void ana_send_uplink(const struct rxjob* rxjob, const struct s2ctx* s2ctx, const uint8_t* frame);
void ana_send_downlink(const struct txjob* txjob, const struct s2ctx* s2ctx, const uint8_t* frame);
void ana_send_stat(const struct ana_stat* stat);
void ana_count_rx(uint8_t crc_ok, uint8_t forwarded);
void ana_count_tx_received(void);
void ana_count_tx_emitted(void);
void ana_set_temperature(float temp);

#endif /* _analytics_h_ */
