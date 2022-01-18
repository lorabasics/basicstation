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

#include "kwcrc.h"
#include "sys.h"
#include "ral.h"

#define SX130X_RFE_MAXCOFF_125 (( 925000 - 125000)/2)
#define SX130X_RFE_MAXCOFF_250 ((1000000 - 250000)/2)
#define SX130X_RFE_MAXCOFF_500 ((1100000 - 500000)/2)

#define SX130X_RFF_NB 2
#define SX130X_IF_NB 10

int ral_challoc (chdefl_t* upchs, challoc_cb alloc_cb, void* ctx) {
    u4_t rfe_spans[MAX_130X*SX130X_RFF_NB*2] = {0};
    for( int chip=0; chip<MAX_130X; chip++ )
        rfe_spans[2*SX130X_RFF_NB*chip] = rfe_spans[2*SX130X_RFF_NB*chip+2] = INT_MAX;

    uint modem_idx = 0;
    uint chip_idx = 0;
    uint multi_chslot = 0;
    uint fsk_chslot = 0;
    uint fl_chslot = 0;

    alloc_cb(ctx, NULL, CHALLOC_START);

    while( chip_idx < MAX_130X && (multi_chslot < MAX_UPCHNLS || fsk_chslot < MAX_UPCHNLS || fl_chslot < MAX_UPCHNLS) ) {
    alloc_cb(ctx, &(challoc_t) {.chip = chip_idx}, CHALLOC_CHIP_START);

    // Allocate 125kHz channels
    modem_idx = 0;
    while( multi_chslot < MAX_UPCHNLS && modem_idx < SX130X_IF_NB-2 ) {
        if( !upchs->freq[multi_chslot] || upchs->rps[multi_chslot].maxSF == FSK || upchs->rps[multi_chslot].bw != BW125 ) {
            multi_chslot++;
            continue;
        }
        u4_t ch_freq = upchs->freq[multi_chslot];
        for( int rfe_idx=0; rfe_idx<SX130X_RFF_NB; rfe_idx++ ) {
            u4_t* rfe_span = &rfe_spans[2*rfe_idx+4*chip_idx];
            if( rfe_span[1] == 0 || (ch_freq - rfe_span[0])/2 <= SX130X_RFE_MAXCOFF_125 ) {
                rfe_span[0] = min(ch_freq, rfe_span[0]);
                rfe_span[1] = max(ch_freq, rfe_span[1]);
                alloc_cb(ctx, &(challoc_t) {
                    .chip = chip_idx, .chan = modem_idx++, .rff = rfe_idx,
                    .rff_freq = (rfe_span[0]+rfe_span[1])/2,
                    .chdef = {.freq = ch_freq, .rps = upchs->rps[multi_chslot]}
                }, CHALLOC_CH);
                ch_freq = 0; multi_chslot++; // Channel allocated, move on to next slot
                break;
            }
        }
        if( ch_freq ) break;
    }

    // FSK
    while( fsk_chslot < MAX_UPCHNLS ) {
        if( !upchs->freq[fsk_chslot] || upchs->rps[fsk_chslot].maxSF != FSK ) {
            fsk_chslot++;
            continue;
        }
        if( chip_idx >= MAX_130X ) break; // Too many chips
        u4_t ch_freq = upchs->freq[fsk_chslot];
        for( int rfe_idx=0; rfe_idx<SX130X_RFF_NB; rfe_idx++ ) {
            u4_t* rfe_span = &rfe_spans[2*rfe_idx+4*chip_idx];
            if( rfe_span[1] == 0 || (ch_freq >= rfe_span[1] - 2*SX130X_RFE_MAXCOFF_125 &&
                                     ch_freq <= rfe_span[0] + 2*SX130X_RFE_MAXCOFF_125) ) {
                rfe_span[0] = min(ch_freq, rfe_span[0]);
                rfe_span[1] = max(ch_freq, rfe_span[1]);
                alloc_cb(ctx, &(challoc_t) {
                    .chip = chip_idx, .chan = SX130X_IF_NB-1, .rff = rfe_idx,
                    .rff_freq = (rfe_span[0]+rfe_span[1])/2,
                    .chdef = {.freq = ch_freq, .rps = upchs->rps[fsk_chslot]}
                }, CHALLOC_CH);
                fsk_chslot++;
                modem_idx++;
                break;
            }
        }
        break;
    }

    // Fast LoRa
    while( fl_chslot < MAX_UPCHNLS ) {
        if( !upchs->freq[fl_chslot] || (upchs->rps[fl_chslot].bw != BW250 && upchs->rps[fl_chslot].bw != BW500) ) {
            fl_chslot++;
            continue;
        }
        if( chip_idx >= MAX_130X ) break; // Too many chips
        u4_t ch_freq = upchs->freq[fl_chslot];
        int maxcoff = upchs->rps[fl_chslot].bw == BW250 ? SX130X_RFE_MAXCOFF_250 : SX130X_RFE_MAXCOFF_500;
        for( int rfe_idx=0; rfe_idx<SX130X_RFF_NB; rfe_idx++ ) {
            u4_t* rfe_span = &rfe_spans[2*rfe_idx+4*chip_idx];
            u4_t cmin = rfe_span[1] - SX130X_RFE_MAXCOFF_125;
            u4_t cmax = rfe_span[0] + SX130X_RFE_MAXCOFF_125;
            if( rfe_span[1] == 0 || (ch_freq >= cmin - maxcoff && ch_freq <= cmax + maxcoff) ) {
                rfe_span[0] = min(ch_freq, rfe_span[0]);
                rfe_span[1] = max(ch_freq, rfe_span[1]);
                alloc_cb(ctx, &(challoc_t) {
                    .chip = chip_idx, .chan = SX130X_IF_NB-2, .rff = rfe_idx,
                    .rff_freq = (max(cmin, ch_freq-maxcoff)+min(cmax, ch_freq+maxcoff))/2,
                    .chdef = { .freq = ch_freq, .rps = upchs->rps[fl_chslot]}
                }, CHALLOC_CH);
                fl_chslot++;
                modem_idx++;
                break;
            }
        }
        break;
    }

    alloc_cb(ctx, &(challoc_t) {
        .chipid = chip_idx,
        .chans  = modem_idx,
        .minFreq= modem_idx ? rfe_spans[4*chip_idx] : 0,
        .maxFreq= modem_idx ? max(rfe_spans[4*chip_idx+3],rfe_spans[4*chip_idx+1]) : 0,
        }, CHALLOC_CHIP_DONE);
    chip_idx++;
    }

    // Done allocating
    alloc_cb(ctx, NULL, CHALLOC_DONE);
    return 1;
}
