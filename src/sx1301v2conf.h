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

#ifndef _sx1301v2conf_h_
#define _sx1301v2conf_h_
#if defined(CFG_lgw2)

#include "lgw2/sx1301ar_hal.h"
#include "s2conf.h"
#include "ral.h"


#define SX1301_ANT_NIL    0
#define SX1301_ANT_OMNI   1
#define SX1301_ANT_SECTOR 2
#define SX1301_ANT_UNDEF  3
#define MAX_SX1301_NUM    8

struct board_conf {
    sx1301ar_board_cfg_t boardConf;
    sx1301ar_lbt_cfg_t   lbtConf;
    char  device[MAX_DEVICE_LEN];   // SPI device, FTDI spec etc.
    float txpowAdjusts[SX1301AR_BOARD_RFCHAIN_NB];
    u1_t  antennaTypes[SX1301AR_BOARD_RFCHAIN_NB];
    u1_t  pps;           // enable PPS latch of trigger count
    ujcrc_t fpga_flavor;
};

struct chip_conf {
    sx1301ar_chip_cfg_t chipConf;
    sx1301ar_chan_cfg_t chanConfs[SX1301AR_CHIP_CHAN_NB];
};

struct sx1301v2conf {
    struct board_conf boards[SX1301AR_MAX_BOARD_NB];
    struct chip_conf  sx1301[MAX_SX1301_NUM];
};


int  sx1301v2conf_parse_setup (struct sx1301v2conf* sx1301v2conf, int slaveIdx, str_t hwspec, char* json, int jsonlen);
int  sx1301v2conf_challoc (struct sx1301v2conf* sx1301v2conf, chdefl_t* upchs);
int  sx1301v2conf_start (struct sx1301v2conf* sx1301v2conf, u4_t region);

#endif // defined(CFG_lgw2)
#endif // _sx1301v2conf_h_
