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
