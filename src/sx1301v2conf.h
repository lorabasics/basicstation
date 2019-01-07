// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _sx1301v2conf_h_
#define _sx1301v2conf_h_
#if defined(CFG_lgw2)

#include "lgw2/sx1301ar_hal.h"
#include "s2conf.h"


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
int  sx1301v2conf_start (struct sx1301v2conf* sx1301v2conf, u4_t region);


#endif // defined(CFG_lgw2)
#endif // _sx1301v2conf_h_
