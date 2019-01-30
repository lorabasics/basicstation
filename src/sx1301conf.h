/*
 *  --- Revised 3-Clause BSD License ---
 *  Copyright (C) 2016-2019, SEMTECH (International) AG.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without modification,
 *  are permitted provided that the following conditions are met:
 *
 *      * Redistributions of source code must retain the above copyright notice,
 *        this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright notice,
 *        this list of conditions and the following disclaimer in the documentation
 *        and/or other materials provided with the distribution.
 *      * Neither the name of the copyright holder nor the names of its contributors
 *        may be used to endorse or promote products derived from this software
 *        without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL SEMTECH BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 *  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _sx1301conf_h_
#define _sx1301conf_h_
#if defined(CFG_lgw1)

#include <stdio.h> // loragw_fpga.h refers to FILE
#include "lgw/loragw_hal.h"
#include "lgw/loragw_lbt.h"
#include "lgw/loragw_fpga.h"
#include "s2conf.h"


#define SX1301_ANT_NIL    0
#define SX1301_ANT_OMNI   1
#define SX1301_ANT_SECTOR 2
#define SX1301_ANT_UNDEF  3

struct sx1301conf {
    struct lgw_conf_board_s  boardconf;
    struct lgw_tx_gain_lut_s txlut;
    struct lgw_conf_rxrf_s   rfconf[LGW_RF_CHAIN_NB];
    struct lgw_conf_rxif_s   ifconf[LGW_IF_CHAIN_NB];
    struct lgw_conf_lbt_s    lbt;
    s2_t  txpowAdjust;   // assuming there is only one TX path / SX1301 (scaled by TXPOW_SCALE)
    u1_t  pps;           // enable PPS latch of trigger count
    u1_t  antennaType;   // type of antenna
    char  device[MAX_DEVICE_LEN];   // SPI device, FTDI spec etc.
};

extern str_t station_conf_USAGE;

int  sx1301conf_parse_setup (struct sx1301conf* sx1301conf, int slaveIdx, str_t hwspec, char* json, int jsonlen);
int  sx1301conf_start (struct sx1301conf* sx1301conf, u4_t region);


#endif // defined(CFG_lgw1)
#endif // _sx1301conf_h_
