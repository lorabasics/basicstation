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

#if defined(CFG_lgw1)

#include "uj.h"
#include "kwcrc.h"
#include "sys.h"
#if defined(CFG_linux)
#include "sys_linux.h"
#endif // defined(CFG_linux)
#include "sx1301conf.h"
#include "lgw/loragw_reg.h"

static void parse_tx_gain_lut (ujdec_t* D, struct lgw_tx_gain_lut_s* txlut) {
    int slot;
    uj_enterArray(D);
    while( (slot = uj_nextSlot(D)) >= 0 ) {
        if( slot >= TX_GAIN_LUT_SIZE_MAX )
            uj_error(D, "Too many TX_GAIN_LUT entries (no more than %d allowed)", TX_GAIN_LUT_SIZE_MAX);
        ujcrc_t field;
        uj_enterObject(D);
        while( (field = uj_nextField(D)) ) {
            switch(field) {
            case J_pa_gain:  { txlut->lut[slot].pa_gain  = uj_intRange(D,    0,  3); break; }
            case J_dig_gain: { txlut->lut[slot].dig_gain = uj_intRange(D,    0,  3); break; }
            case J_dac_gain: { txlut->lut[slot].dac_gain = uj_intRange(D,    0,  3); break; }
            case J_mix_gain: { txlut->lut[slot].mix_gain = uj_intRange(D,    0, 15); break; }
            case J_rf_power: { txlut->lut[slot].rf_power = uj_intRange(D, -128,127); break; }
            default: {
                uj_error(D, "Illegal field: %s", D->field.name);
            }
            }
        }
        uj_exitObject(D);
        txlut->size = slot+1;
    }
    uj_exitArray(D);
}


static u1_t parse_antenna_type (str_t s) {
    if( strcasecmp(s,"omni") == 0 )
        return SX1301_ANT_OMNI;
    if( strcasecmp(s,"sector") == 0 )
        return SX1301_ANT_SECTOR;
    LOG(MOD_RAL|ERROR,"Unknown antenna info: %s (treating as undefined)", s);
    return SX1301_ANT_UNDEF;
}


static void parse_rfconf (ujdec_t* D, struct sx1301conf* sx1301conf, int rfidx) {
    struct lgw_conf_rxrf_s* rfconf = &sx1301conf->rfconf[rfidx];
    ujcrc_t field;
    uj_enterObject(D);
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_enable:         { rfconf->enable        = uj_bool(D); break; }
        case J_tx_enable:      { rfconf->tx_enable     = uj_bool(D); break; }
        case J_txpow_adjust:
        case J_antenna_gain:   { sx1301conf->txpowAdjust = (s2_t)(uj_num(D)*TXPOW_SCALE); break; }
        case J_antenna_type:   { sx1301conf->antennaType = parse_antenna_type(uj_str(D)); break; }
        case J_freq:           { rfconf->freq_hz       = uj_intRangeOr(D, 1000000, 1000000000, 0); break; }
        case J_tx_notch_freq:  { rfconf->tx_notch_freq = uj_intRange(D, LGW_MIN_NOTCH_FREQ, LGW_MAX_NOTCH_FREQ); break; }
        case J_rssi_offset:    { rfconf->rssi_offset   = uj_num(D); break; }
        case J_rssi_offset_lbt:{ sx1301conf->lbt.rssi_offset = uj_intRange(D, -128, 127); break; }
        case J_type:           {
            uj_str(D);
            /**/ if( D->str.crc == J_SX1255 ) rfconf->type = LGW_RADIO_TYPE_SX1255;
            else if( D->str.crc == J_SX1257 ) rfconf->type = LGW_RADIO_TYPE_SX1257;
            else if( D->str.crc == J_SX1272 ) rfconf->type = LGW_RADIO_TYPE_SX1272;
            else if( D->str.crc == J_SX1276 ) rfconf->type = LGW_RADIO_TYPE_SX1276;
            else uj_error(D, "Illegal value for field \"type\": %s", D->str.beg);
            break;
        }
        default: {
            uj_error(D, "Illegal field: %s", D->field.name);
        }
        }
    }
    uj_exitObject(D);
}


static int parse_bandwidth (ujdec_t* D) {
    sL_t bw = uj_int(D);
    switch(bw) {
    case 500000: return BW_500KHZ; break;
    case 250000: return BW_250KHZ; break;
    case 125000: return BW_125KHZ; break;
    default:
        uj_error(D, "Illegal bandwidth value: %ld (must be 125000, 250000, or 500000)");
        return BW_UNDEFINED; // NOT REACHED
    }
}


static int parse_spread_factor (ujdec_t* D) {
    sL_t sf = uj_int(D);
    switch(sf) {
    case  7: return DR_LORA_SF7;  break;
    case  8: return DR_LORA_SF8;  break;
    case  9: return DR_LORA_SF9;  break;
    case 10: return DR_LORA_SF10; break;
    case 11: return DR_LORA_SF11; break;
    case 12: return DR_LORA_SF12; break;
    default:
        uj_error(D, "Illegal spread_factor value: %ld (must be 7,..,12)", sf);
        return DR_UNDEFINED; // NOT REACHED
    }
}


static void parse_ifconf (ujdec_t* D, struct lgw_conf_rxif_s* ifconf) {
    ujcrc_t field;
    uj_enterObject(D);
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_enable:        { ifconf->enable         = uj_bool(D); break; }
        case J_radio:
        case J_rf_chain:      { ifconf->rf_chain       = uj_intRange(D, 0, LGW_RF_CHAIN_NB-1); break; }
        case J_if:
        case J_freq:          { ifconf->freq_hz        = uj_int(D); break; }
        case J_bandwidth:     { ifconf->bandwidth      = parse_bandwidth(D); break; }
        case J_spread_factor: { ifconf->datarate       = parse_spread_factor(D); break; } // Lora only
        case J_datarate:      { ifconf->datarate       = uj_int(D); break; }   // FSK only
        case J_sync_word:     { ifconf->sync_word      = uj_uint(D); break; }
        case J_sync_word_size:{ ifconf->sync_word_size = uj_uint(D); break; }
        default: {
            uj_error(D, "Illegal field: %s", D->field.name);
        }
        }
    }
    uj_exitObject(D);
}

static void setDevice (struct sx1301conf* sx1301conf, str_t device) {
    str_t dev = sys_radioDevice(device);
    int sz = sizeof(sx1301conf->device);
    int n = snprintf(sx1301conf->device, sz, "%s", dev);
    if( n > sz-1 )
        LOG(ERROR, "Device string too long (max %d chars): %s", sz-1, dev);
    rt_free((void*)dev);
}


static void parse_sx1301_conf (ujdec_t* D, struct sx1301conf* sx1301conf) {
    ujcrc_t field;
    uj_enterObject(D);
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_lorawan_public: {
            sx1301conf->boardconf.lorawan_public = uj_bool(D);
            break;
        }
        case J_device: {
            // Slave config might override shared device specification
            setDevice(sx1301conf, uj_str(D));
            break;
        }
        case J_no_gps_capture: {
            sx1301conf->pps = !uj_bool(D);
            break;
        }
        case J_pps: {
            sx1301conf->pps = uj_bool(D);
            break;
        }
        case J_clksrc: {
            sx1301conf->boardconf.clksrc = uj_intRange(D, 0, LGW_RF_CHAIN_NB-1);
            break;
        }
        case J_tx_gain_lut: {
            parse_tx_gain_lut(D, &sx1301conf->txlut);
            break;
        }
        case J_chan_FSK: {
            parse_ifconf(D, &sx1301conf->ifconf[LGW_MULTI_NB+1]);
            break;
        }
        case J_chan_Lora_std: {
            parse_ifconf(D, &sx1301conf->ifconf[LGW_MULTI_NB]);
            break;
        }
        default: {
            int n = uj_indexedField(D, "chan_multiSF_");
            if( n >= 0 ) {
                if( n >= LGW_IF_CHAIN_NB )
                    uj_error(D, "Illegal field (index suffix out range, not in 0..%d): %s", LGW_IF_CHAIN_NB-1, D->field.name);
                parse_ifconf(D, &sx1301conf->ifconf[n]);
                break;
            }
            n = uj_indexedField(D, "radio_");
            if( n >= 0 ) {
                if( n >= LGW_RF_CHAIN_NB )
                    uj_error(D, "Illegal field (index suffix out range, not in 0..%d): %s", LGW_RF_CHAIN_NB-1, D->field.name);
                parse_rfconf(D, sx1301conf, n);
                break;
            }
            LOG(MOD_RAL|WARNING, "Ignoring unsupported/unknown field: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
        }
    }
    uj_exitObject(D);
}


static int find_sx1301_conf (str_t filename, struct sx1301conf* sx1301conf) {
    dbuf_t jbuf = sys_readFile(filename);
    if( jbuf.buf == NULL )
        return 0;
    ujdec_t D;
    uj_iniDecoder(&D, jbuf.buf, jbuf.bufsize);
    if( uj_decode(&D) ) {
        LOG(MOD_RAL|ERROR, "Parsing of JSON failed - '%s' ignored", filename);
        free(jbuf.buf);
        return 0;
    }
    ujcrc_t field;
    uj_enterObject(&D);
    while( (field = uj_nextField(&D)) ) {
        switch(field) {
        case J_sx1301_conf:
        case J_SX1301_conf:
        case J_radio_conf: {
            parse_sx1301_conf(&D, sx1301conf);
            break;
        }
        case J_station_conf: {
            // Parsed elsewhere
            uj_skipValue(&D);
            break;
        }
        default: {
            LOG(MOD_RAL|WARNING, "Ignoring unsupported/unknown field: %s", D.field.name);
            uj_skipValue(&D);
            break;
        }
        }
    }
    uj_exitObject(&D);
    uj_assertEOF(&D);
    rt_free(jbuf.buf);
    return 1;
}


static int setup_LBT (struct sx1301conf* sx1301conf, u4_t cca_region) {
    u2_t scantime_us = 0;

    if( cca_region == J_AS923JP ) {
        scantime_us = 5000;
        sx1301conf->lbt.rssi_target = -80;
    }
    else if( cca_region == J_KR920 ) {
        scantime_us = 5000;
        sx1301conf->lbt.rssi_target = -67;
    }
    else {
        LOG(MOD_RAL|ERROR, "Failed to setup CCA/LBT for region (crc=0x%08X)", cca_region);
        return 0;
    }
    // By default use up link frequencies as LBT frequencies
    // Otherwise we should have gotten a freq list from the server
    if( sx1301conf->lbt.nb_channel == 0 ) {
        for( int rfi=0; rfi < LGW_RF_CHAIN_NB; rfi++ ) {
            if( !sx1301conf->rfconf[rfi].enable )
                continue;
            u4_t cfreq = sx1301conf->rfconf[rfi].freq_hz;
            int n = max(8,LGW_IF_CHAIN_NB);  // only consider normal Lora modems (aka not fast/FSK)
            for( int ifi=0; ifi < n; ifi++ ) {
                if( !sx1301conf->ifconf[ifi].enable )
                    continue;
                if( sx1301conf->lbt.nb_channel < LBT_CHANNEL_FREQ_NB ) {
                    u4_t freq = cfreq + sx1301conf->ifconf[ifi].freq_hz;
                    sx1301conf->lbt.channels[sx1301conf->lbt.nb_channel].freq_hz = freq;
                    sx1301conf->lbt.nb_channel += 1;
                }
            }
        }
    }
    for( int i=0; i<sx1301conf->lbt.nb_channel; i++ )
        sx1301conf->lbt.channels[i].scan_time_us = scantime_us;
    sx1301conf->lbt.enable = 1;
    int e = lgw_lbt_setconf(sx1301conf->lbt);
    if( e != LGW_HAL_SUCCESS ) {
        LOG(MOD_RAL|ERROR, "lgw_lbt_setconf failed: %s", sx1301conf->device);
        return 0;
    }
    return 1;
}


int sx1301conf_parse_setup (struct sx1301conf* sx1301conf, int slaveIdx,
                            str_t hwspec, char* json, int jsonlen) {
    if( strcmp(hwspec, "sx1301/1") != 0 ) {
        LOG(MOD_RAL|ERROR, "Unsupported hwspec: %s", hwspec);
        return 0;
    }
    // Zero and setup some defaults
    memset(sx1301conf, 0, sizeof(*sx1301conf));
    sx1301conf->boardconf.lorawan_public = 1;
    setDevice(sx1301conf, NULL);

    if( !find_sx1301_conf("station.conf", sx1301conf) )
        return 0;
    if( slaveIdx >= 0 ) {
        char cfname[64];
        snprintf(cfname, sizeof(cfname), "slave-%d.conf", slaveIdx);
        if( !find_sx1301_conf(cfname, sx1301conf) )
            return 0;
    }

    ujdec_t D;
    uj_iniDecoder(&D, json, jsonlen);
    if( uj_decode(&D) ) {
        LOG(MOD_RAL|ERROR, "Parsing of JSON failed - 'router_config.sx1301_conf' ignored");
        return 0;
    }
    parse_sx1301_conf(&D, sx1301conf);
    uj_assertEOF(&D);
    return 1;
}


int sx1301conf_start (struct sx1301conf* sx1301conf, u4_t cca_region) {
#if defined(CFG_linux)
    u4_t pids[1];
    int n = sys_findPids(sx1301conf->device, pids, SIZE_ARRAY(pids));
    if( n > 0 )
        rt_fatal("Radio device '%s' in use by process: %d%s", sx1301conf->device, pids[0], n>1?".. (and others)":"");
#endif // defined(CFG_linux)

    lgw_stop();
    LOG(MOD_RAL|INFO,"Lora gateway library version: %s", lgw_version_info());

    if( lgw_board_setconf(sx1301conf->boardconf) != LGW_HAL_SUCCESS ) {
        LOG(MOD_RAL|ERROR,"lgw_board_setconf failed");
        goto fail;
    }
    if( sx1301conf->txlut.size > 0) {
        if( lgw_txgain_setconf(&sx1301conf->txlut) != LGW_HAL_SUCCESS ) {
            LOG(MOD_RAL|INFO,"lgw_txgain_setconf failed");
            goto fail;
        }
    }
    for( int i=0; i<LGW_RF_CHAIN_NB; i++ ) {
        if( lgw_rxrf_setconf(i, sx1301conf->rfconf[i]) != LGW_HAL_SUCCESS ) {
            LOG(MOD_RAL|INFO,"lgw_rxrf_setconf(%d) failed", i);
            goto fail;
        }
    }
    for( int i=0; i<LGW_IF_CHAIN_NB; i++ ) {
        if( lgw_rxif_setconf(i, sx1301conf->ifconf[i]) != LGW_HAL_SUCCESS ) {
            LOG(MOD_RAL|INFO,"lgw_rxif_setconf(%d) failed", i);
            goto fail;
        }
    }

    if( cca_region && !setup_LBT(sx1301conf, cca_region) ) {
        goto fail;
    }

    if( log_shallLog(MOD_RAL|VERBOSE) ) {
        LOG(MOD_RAL|DEBUG, "SX1301 txlut table (%d entries)", sx1301conf->txlut.size);
        for( int i=0; i<sx1301conf->txlut.size; i++ ) {
            LOG(MOD_RAL|VERBOSE,
                "SX1301 txlut %2d:  dig_gain=%d pa_gain=%d dac_gain=%d mix_gain=%d rf_power=%d", i,
                sx1301conf->txlut.lut[i].dig_gain,
                sx1301conf->txlut.lut[i].pa_gain,
                sx1301conf->txlut.lut[i].dac_gain,
                sx1301conf->txlut.lut[i].mix_gain,
                sx1301conf->txlut.lut[i].rf_power);
        }
        for( int i=0; i<LGW_RF_CHAIN_NB; i++ ) {
            LOG(MOD_RAL|VERBOSE,
                "SX1301 rxrfchain %d: enable=%d freq=%d rssi_offset=%f type=%d tx_enable=%d tx_notch_freq=%d", i,
                sx1301conf->rfconf[i].enable,
                sx1301conf->rfconf[i].freq_hz,
                sx1301conf->rfconf[i].rssi_offset,
                sx1301conf->rfconf[i].type,
                sx1301conf->rfconf[i].tx_enable,
                sx1301conf->rfconf[i].tx_notch_freq);
        }
        for( int i=0; i<LGW_IF_CHAIN_NB; i++ ) {
            LOG(MOD_RAL|VERBOSE,
                "SX1301 ifchain %2d: enable=%d rf_chain=%d freq=%d bandwidth=%d datarate=%d sync_word=%lX/%d", i,
                sx1301conf->ifconf[i].enable,
                sx1301conf->ifconf[i].rf_chain,
                sx1301conf->ifconf[i].freq_hz,
                sx1301conf->ifconf[i].bandwidth,
                sx1301conf->ifconf[i].datarate,
                sx1301conf->ifconf[i].sync_word, sx1301conf->ifconf[i].sync_word_size);
        }
        if( sx1301conf->lbt.enable ) {
            LOG(MOD_RAL|VERBOSE, "SX1301 LBT enabled: rssi_target=%d rssi_offset=%d",
                sx1301conf->lbt.rssi_target, sx1301conf->lbt.rssi_offset);
            for( int i=0; i < sx1301conf->lbt.nb_channel; i++ ) {
                LOG(MOD_RAL|VERBOSE, "  %2d: freq=%F scan=%dus",
                    i, sx1301conf->lbt.channels[i].freq_hz, sx1301conf->lbt.channels[i].scan_time_us);
            }
        } else {
            LOG(MOD_RAL|VERBOSE, "SX1301 LBT not enabled");
        }
    }

    LOG(MOD_RAL|INFO, "Station device: %s (PPS capture %sabled)", sx1301conf->device, sx1301conf->pps ? "en":"dis");
    lgwx_device_mode = sys_deviceMode;
    int err = lgw_start();
    if( err == LGW_HAL_SUCCESS ) {
        lgw_reg_w(LGW_GPS_EN, sx1301conf->pps ? 1 : 0);
        return 1;
    }
    LOG(MOD_RAL|ERROR, "lgw_start failed: %s", sx1301conf->device);
 fail:
    return 0;
}

#endif // defined(CFG_lgw1)
