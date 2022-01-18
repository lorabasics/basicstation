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

#ifndef _ral_h_
#define _ral_h_

#if defined(CFG_lgw1) && defined(CFG_lgw2)
#error Only one of the two params can be set: CFG_lgw1 CFG_lgw2
#endif

#include "s2e.h"
#include "timesync.h"

// Encoding of xtime:
//  bits:
//   63    sign (always positive)
//   62-56 radio unit where the time stamp originated from (max 128)
//   55-48 random value to disambiguate different SX1301 sessions (never 0, aka valid xtime is never 0)
//   47-0  microseconds since SX1301 start (rollover every 9y >> uptime of sessions)
//
// Encoding of rctx:
//   6-0   radio unit
#define RAL_TXUNIT_SHIFT 56
#define RAL_XTSESS_SHIFT 48
#define RAL_TXUNIT_MASK  0x7F
#define RAL_XTSESS_MASK  0xFF

#define RAL_TX_OK     0  // ok
#define RAL_TX_FAIL  -1  // unspecific error
#define RAL_TX_NOCA  -2  // channel access denied (LBT)

#define ral_xtime2sess(  xtime) ((u1_t)(((xtime)>>RAL_XTSESS_SHIFT)&RAL_XTSESS_MASK))
#define ral_xtime2txunit(xtime) ((u1_t)(((xtime)>>RAL_TXUNIT_SHIFT)&RAL_TXUNIT_MASK))
#define ral_xtime2rctx(  xtime) ((sL_t)ral_xtime2txunit(xtime))
#define ral_rctx2txunit(  rctx) ((u1_t)((rctx)&RAL_TXUNIT_MASK))

struct chrps {
    u1_t minSF :3; // s2w SF* enum
    u1_t maxSF :3;
    u1_t bw    :2; // s2e BW* enum
};

typedef struct {
    u4_t freq[MAX_UPCHNLS];
    struct chrps rps[MAX_UPCHNLS];
} chdefl_t;

typedef struct {
    u4_t freq;
    struct chrps rps;
} chdef_t;

enum {
    CHALLOC_START,
    CHALLOC_CHIP_START,
    CHALLOC_CH,
    CHALLOC_CHIP_DONE,
    CHALLOC_DONE
};

typedef union {
struct {
    u1_t chip  :3 ; // MAX_130X <= 8
    u1_t chan  :4 ; // 10 channels per chip
    u1_t rff   :1 ; // 2 RFF per chip
    u4_t rff_freq;
    chdef_t chdef;
};
// CHALLOC_CHIP_DONE
struct {
    u1_t chipid  :4 ; // MAX_130X <= 8
    u1_t chans   :4 ; // 10 channels per chip
    u4_t minFreq;
    u4_t maxFreq;
};
} challoc_t;

typedef void (*challoc_cb) (void* ctx, challoc_t* ch, int flag);
int   ral_challoc (chdefl_t* upchs, challoc_cb alloc_cb, void* ctx);

int ral_rps2bw (rps_t rps);
int ral_rps2sf (rps_t rps);

void  ral_ini ();
void  ral_stop ();
int   ral_config (str_t hwspec, u4_t cca_region, char* json, int jsonlen, chdefl_t* upchs);
int   ral_txstatus (u1_t txunit);
void  ral_txabort (u1_t txunit);
int   ral_tx  (txjob_t* txjob, s2ctx_t* s2ctx, int nocca);
u1_t  ral_altAntennas (u1_t txunit);


// RAL internal APIs and shared code
int ral_getTimesync (u1_t pps_en, sL_t* last_xtime, timesync_t* timesync);


#endif // _ral_h_
