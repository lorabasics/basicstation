// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _timesync_h_
#define _timesync_h_

#include "rt.h"

typedef struct timesync {
    ustime_t ustime;
    sL_t     xtime;
    sL_t     pps_xtime;
} timesync_t;


sL_t     ts_xtime2gpstime (sL_t xtime);
sL_t     ts_gpstime2xtime (u1_t txunit, sL_t gpstime);
sL_t     ts_xtime2xtime   (sL_t xtime, u1_t txunit);
sL_t     ts_ustime2xtime  (u1_t txunit, ustime_t ustime);
ustime_t ts_xtime2ustime  (sL_t xtime);

ustime_t ts_normalizeTimespanMCU (ustime_t timespan);
ustime_t ts_updateTimesync (u1_t txunit, int quality, const timesync_t* curr);
void     ts_setTimesyncLns (ustime_t xtime, sL_t gpstime);
void     ts_processTimesyncLns (ustime_t txtime, ustime_t rxtime, sL_t servertime);
void     ts_iniTimesync ();

// ------------------------------
// Used by RAL impl only
sL_t ts_xticks2xtime (u4_t xticks, sL_t last_xtime);
sL_t ts_newXtimeSession  (u1_t txunit);


#endif // _timesync_h_
