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

#if defined(CFG_lgw1)

#include "uj.h"
#include "kwcrc.h"
#include "sys.h"
#if defined(CFG_linux)
#include "sys_linux.h"
#endif // defined(CFG_linux)
#include "sx130xconf.h"
#include "lgw/loragw_reg.h"
#if defined(CFG_sx1302)
#include "lgw/loragw_sx1302.h"
#endif // defined(CFG_sx1302)

#define SX130X_RFE_MAX 400000  // Max if offset 400kHz

extern const uint8_t ifmod_config[LGW_IF_CHAIN_NB];

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
#if defined(CFG_sx1302)
            case J_pwr_idx:  {
	                /*Setting for  sx1250 */
	                txlut->lut[slot].pwr_idx = uj_intRange(D,    0, 27);
	                /*TODO: rework this, should not be needed for sx1250 */
	                txlut->lut[slot].mix_gain = 5;
	                /* This is only dac_gain supported for now */
	                txlut->lut[slot].dac_gain = 3;
	                break;
	            }
#else
            case J_dig_gain: { txlut->lut[slot].dig_gain = uj_intRange(D,    0,  3); break; }
            case J_dac_gain: { txlut->lut[slot].dac_gain = uj_intRange(D,    0,  3); break; }
            case J_mix_gain: { txlut->lut[slot].mix_gain = uj_intRange(D,    0, 15); break; }
#endif
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

#if defined(CFG_sx1302)
static void parse_rssi_tcomp (ujdec_t* D, struct lgw_rssi_tcomp_s* rssi_tcomp) {
    ujcrc_t field;
    uj_enterObject(D);
    while( (field = uj_nextField(D)) ) {
        switch(field) {
            case J_coeff_a:  { rssi_tcomp->coeff_a = uj_num(D); break; }
            case J_coeff_b:  { rssi_tcomp->coeff_b = uj_num(D); break; }
            case J_coeff_c:  { rssi_tcomp->coeff_c = uj_num(D); break; }
            case J_coeff_d:  { rssi_tcomp->coeff_d = uj_num(D); break; }
            case J_coeff_e:  { rssi_tcomp->coeff_e = uj_num(D); break; }
        }
    }
    uj_exitObject(D);
}
#endif

static u1_t parse_antenna_type (str_t s) {
    if( strcasecmp(s,"omni") == 0 )
        return SX130X_ANT_OMNI;
    if( strcasecmp(s,"sector") == 0 )
        return SX130X_ANT_SECTOR;
    LOG(MOD_RAL|ERROR,"Unknown antenna info: %s (treating as undefined)", s);
    return SX130X_ANT_UNDEF;
}


static void parse_rfconf (ujdec_t* D, struct sx130xconf* sx130xconf, int rfidx) {
    struct lgw_conf_rxrf_s* rfconf = &sx130xconf->rfconf[rfidx];
    ujcrc_t field;
    uj_enterObject(D);
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_enable:         { rfconf->enable        = uj_bool(D); break; }
        case J_tx_enable:      { rfconf->tx_enable     = uj_bool(D); break; }
        case J_txpow_adjust:
        case J_antenna_gain:   { sx130xconf->txpowAdjust = (s2_t)(uj_num(D)*TXPOW_SCALE); break; }
        case J_antenna_type:   { sx130xconf->antennaType = parse_antenna_type(uj_str(D)); break; }
        case J_freq:           { rfconf->freq_hz       = uj_intRangeOr(D, 1000000, 1000000000, 0); break; }
#if !defined(CFG_sx1302)
        case J_tx_notch_freq:  { rfconf->tx_notch_freq = uj_intRange(D, LGW_MIN_NOTCH_FREQ, LGW_MAX_NOTCH_FREQ); break; }
        case J_rssi_offset_lbt:{ sx130xconf->lbt.rssi_offset = uj_intRange(D, -128, 127); break; }
#endif
        case J_rssi_offset:    { rfconf->rssi_offset   = uj_num(D); break; }
        case J_type:           {
            uj_str(D);
            /**/ if( D->str.crc == J_SX1255 ) rfconf->type = LGW_RADIO_TYPE_SX1255;
            else if( D->str.crc == J_SX1257 ) rfconf->type = LGW_RADIO_TYPE_SX1257;
            else if( D->str.crc == J_SX1272 ) rfconf->type = LGW_RADIO_TYPE_SX1272;
            else if( D->str.crc == J_SX1276 ) rfconf->type = LGW_RADIO_TYPE_SX1276;
#if defined(CFG_sx1302)
            else if( D->str.crc == J_SX1250 ) rfconf->type = LGW_RADIO_TYPE_SX1250;
#endif
            else uj_error(D, "Illegal value for field \"type\": %s", D->str.beg);
            break;
        }
#if defined(CFG_sx1302)
        case J_tx_gain_lut: {
            parse_tx_gain_lut(D, &sx130xconf->txlut);
            break;
        }
        case J_rssi_tcomp: {
            parse_rssi_tcomp(D, &rfconf->rssi_tcomp);
	        break;
        }
#endif
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
        uj_error(D, "Illegal bandwidth value: %ld (must be 125000, 250000, or 500000)", bw);
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
#if defined(CFG_sx1302)
        /* implicit hdr */
        case J_implicit_hdr:           { ifconf->implicit_hdr            = uj_bool(D); break; }
        case J_implicit_payload_length:{ ifconf->implicit_payload_length = uj_uint(D); break; }
        case J_implicit_crc_en:        { ifconf->implicit_crc_en         = uj_bool(D); break; }
        case J_implicit_coderate:      { ifconf->implicit_coderate       = uj_uint(D); break; }
#endif
        default: {
            uj_error(D, "Illegal field: %s", D->field.name);
        }
        }
    }
    uj_exitObject(D);
}

static void setDevice (struct sx130xconf* sx130xconf, str_t device) {
    u1_t comtype;
    str_t dev = sys_radioDevice(device, &comtype);
    int sz = sizeof(sx130xconf->device);
    int n = snprintf(sx130xconf->device, sz, "%s", dev);
    if( n > sz-1 )
        LOG(ERROR, "Device string too long (max %d chars): %s", sz-1, dev);
#if defined(CFG_sx1302)
    sx130xconf->boardconf.com_type = (comtype == COMTYPE_SPI) ? LGW_COM_SPI : LGW_COM_USB;
    sz = sizeof(sx130xconf->boardconf.com_path);
    n = snprintf(sx130xconf->boardconf.com_path, sz, "%s", dev);
    if( n > sz-1 )
        LOG(ERROR, "Device string too long (max %d chars): %s", sz-1, dev);
#endif
    rt_free((void*)dev);
}


static void parse_sx130x_conf (ujdec_t* D, struct sx130xconf* sx130xconf) {
    ujcrc_t field;
    uj_enterObject(D);
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_lorawan_public: {
            sx130xconf->boardconf.lorawan_public = uj_bool(D);
            break;
        }
        case J_device: {
            // Slave config might override shared device specification
            setDevice(sx130xconf, uj_str(D));
            break;
        }
        case J_no_gps_capture: {
            sx130xconf->pps = !uj_bool(D);
            break;
        }
        case J_pps: {
            sx130xconf->pps = uj_bool(D);
            break;
        }
        case J_clksrc: {
            sx130xconf->boardconf.clksrc = uj_intRange(D, 0, LGW_RF_CHAIN_NB-1);
            break;
        }
#if defined(CFG_sx1302)
        case J_full_duplex: {
            sx130xconf->boardconf.full_duplex = uj_bool(D);
            break;
        }
#else
        case J_tx_gain_lut: {
            parse_tx_gain_lut(D, &sx130xconf->txlut);
            break;
        }
#endif
        case J_chan_FSK: {
            parse_ifconf(D, &sx130xconf->ifconf[LGW_MULTI_NB+1]);
            break;
        }
        case J_chan_Lora_std: {
            parse_ifconf(D, &sx130xconf->ifconf[LGW_MULTI_NB]);
            break;
        }
        default: {
            int n = uj_indexedField(D, "chan_multiSF_");
            if( n >= 0 ) {
                if( n >= LGW_IF_CHAIN_NB )
                    uj_error(D, "Illegal field (index suffix out range, not in 0..%d): %s", LGW_IF_CHAIN_NB-1, D->field.name);
                parse_ifconf(D, &sx130xconf->ifconf[n]);
                break;
            }
            n = uj_indexedField(D, "radio_");
            if( n >= 0 ) {
                if( n >= LGW_RF_CHAIN_NB )
                    uj_error(D, "Illegal field (index suffix out range, not in 0..%d): %s", LGW_RF_CHAIN_NB-1, D->field.name);
                parse_rfconf(D, sx130xconf, n);
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


static int find_sx130x_conf (str_t filename, struct sx130xconf* sx130xconf) {
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
        case J_sx1302_conf:
        case J_SX1302_conf:
        case J_radio_conf: {
            parse_sx130x_conf(&D, sx130xconf);
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


static int setup_LBT (struct sx130xconf* sx130xconf, u4_t cca_region) {
#if !defined(CFG_sx1302) // For now sx1302 does not support CCA
    u2_t scantime_us = 0;

    if( cca_region == J_AS923_1 ) {
        scantime_us = 5000;
        sx130xconf->lbt.rssi_target = -80;
    }
    else if( cca_region == J_KR920 ) {
        scantime_us = 5000;
        sx130xconf->lbt.rssi_target = -67;
    }
    else {
        LOG(MOD_RAL|ERROR, "Failed to setup CCA/LBT for region (crc=0x%08X)", cca_region);
        return 0;
    }
    // By default use up link frequencies as LBT frequencies
    // Otherwise we should have gotten a freq list from the server
    if( sx130xconf->lbt.nb_channel == 0 ) {
        for( int rfi=0; rfi < LGW_RF_CHAIN_NB; rfi++ ) {
            if( !sx130xconf->rfconf[rfi].enable )
                continue;
            u4_t cfreq = sx130xconf->rfconf[rfi].freq_hz;
            int n = max(8,LGW_IF_CHAIN_NB);  // only consider normal Lora modems (aka not fast/FSK)
            for( int ifi=0; ifi < n; ifi++ ) {
                if( !sx130xconf->ifconf[ifi].enable )
                    continue;
                if( sx130xconf->lbt.nb_channel < LBT_CHANNEL_FREQ_NB ) {
                    u4_t freq = cfreq + sx130xconf->ifconf[ifi].freq_hz;
                    sx130xconf->lbt.channels[sx130xconf->lbt.nb_channel].freq_hz = freq;
                    sx130xconf->lbt.nb_channel += 1;
                }
            }
        }
    }
    for( int i=0; i<sx130xconf->lbt.nb_channel; i++ )
        sx130xconf->lbt.channels[i].scan_time_us = scantime_us;
    sx130xconf->lbt.enable = 1;
    int e = lgw_lbt_setconf(sx130xconf->lbt);
    if( e != LGW_HAL_SUCCESS ) {
        LOG(MOD_RAL|ERROR, "lgw_lbt_setconf failed: %s", sx130xconf->device);
        return 0;
    }
#endif // !defined(CFG_sx1302)
    return 1;
}


int sx130xconf_parse_setup (struct sx130xconf* sx130xconf, int slaveIdx,
                            str_t hwspec, char* json, int jsonlen) {
    if( strcmp(hwspec, "sx1301/1") != 0 ) {
        LOG(MOD_RAL|ERROR, "Unsupported hwspec: %s", hwspec);
        return 0;
    }
    // Zero and setup some defaults
    memset(sx130xconf, 0, sizeof(*sx130xconf));
    sx130xconf->boardconf.lorawan_public = 1;
    setDevice(sx130xconf, NULL);

    if( !find_sx130x_conf("station.conf", sx130xconf) )
        return 0;
    if( slaveIdx >= 0 ) {
        char cfname[64];
        snprintf(cfname, sizeof(cfname), "slave-%d.conf", slaveIdx);
        if( !find_sx130x_conf(cfname, sx130xconf) )
            return 0;
    }

    ujdec_t D;
    uj_iniDecoder(&D, json, jsonlen);
    if( uj_decode(&D) ) {
        LOG(MOD_RAL|ERROR, "Parsing of JSON failed - 'router_config.sx130x_conf' ignored");
        return 0;
    }
    parse_sx130x_conf(&D, sx130xconf);
    uj_assertEOF(&D);
    return 1;
}


static void sx130xconf_challoc_cb (void* ctx, challoc_t* ch, int flag) {
    if( ctx == NULL ) return;
    struct sx130xconf* sx130xconf = (struct sx130xconf*)ctx;

    switch( flag ) {
    case CHALLOC_START: {
        break;
    }
    case CHALLOC_CHIP_START: {
        break;
    }
    case CHALLOC_CH: {
        if( ch->chip > 0 ) return;

        sx130xconf->rfconf[ch->rff].freq_hz = ch->rff_freq;
        sx130xconf->rfconf[ch->rff].enable  = true;

        struct lgw_conf_rxif_s * ifconf = &sx130xconf->ifconf[ch->chan];
        ifconf->freq_hz   = ch->chdef.freq; // Write full frequency for now
        ifconf->rf_chain  = ch->rff;

        if( ch->chan < LGW_IF_CHAIN_NB-2 ) {
            // MultiSF
            ifconf->bandwidth = BW125;
#if defined(CFG_sx1302)
            ifconf->datarate  = DR_UNDEFINED;
#else
            ifconf->datarate  = DR_LORA_MULTI;
#endif
            ifconf->enable    = true;
        }
        else if( ch->chan == LGW_IF_CHAIN_NB-1 ) {
            // FSK
            ifconf->bandwidth = BW_UNDEFINED;
            ifconf->datarate  = 50000;
            ifconf->enable    = true;
            ifconf->sync_word = 0;
        }
        else if( ch->chan == LGW_IF_CHAIN_NB-2 ) {
            // Fast LoRa
            ifconf->bandwidth = ral_rps2bw(rps_make(ch->chdef.rps.maxSF, ch->chdef.rps.bw));
            ifconf->datarate  = ral_rps2sf(rps_make(ch->chdef.rps.maxSF, ch->chdef.rps.bw));
            ifconf->enable    = true;
        }
        break;
    }
    case CHALLOC_CHIP_DONE: {
        // Convert full if frequency to frequency offset
        if( !ch->chans ) break;
        for( int ch=0; ch<LGW_IF_CHAIN_NB; ch++ ) {
            if( sx130xconf->ifconf[ch].enable && sx130xconf->ifconf[ch].freq_hz && abs(sx130xconf->ifconf[ch].freq_hz) > SX130X_RFE_MAX ) {
                sx130xconf->ifconf[ch].freq_hz = sx130xconf->ifconf[ch].freq_hz -
                    sx130xconf->rfconf[sx130xconf->ifconf[ch].rf_chain].freq_hz;
            }
        }
        break;
    }
    case CHALLOC_DONE: {
        break;
    }
    }
}

int sx130xconf_challoc (struct sx130xconf* sx130xconf, chdefl_t* upchs) {
    return ral_challoc(upchs, sx130xconf_challoc_cb, sx130xconf);
}

static void dump_boardConf (struct lgw_conf_board_s* board) {
#if defined(CFG_sx1302)
    LOG(MOD_RAL|INFO, "[LGW sx1302] full_duplex=%d clksrc=%d lorawan_public=%d",
        board->full_duplex,
        board->clksrc,
        board->lorawan_public
    );
#else
    LOG(MOD_RAL|INFO, "[LGW %s] clksrc=%d lorawan_public=%d",
#if defined(CFG_smtcpico)
        "smtcpico",
#else
        "lgw1",
#endif
        board->clksrc,
        board->lorawan_public
    );
#endif
    log_flushIO();
}

static void dump_txLut (struct lgw_tx_gain_lut_s* txlut) {
    LOG(MOD_RAL|DEBUG, "SX130x txlut table (%d entries)", txlut->size);
    for( int i=0; i<txlut->size; i++ ) {
#if !defined(CFG_sx1302)
        LOG(MOD_RAL|INFO,
            "SX1301 txlut %2d:  dig_gain=%d pa_gain=%d dac_gain=%d mix_gain=%d rf_power=%d", i,
            txlut->lut[i].dig_gain,
            txlut->lut[i].pa_gain,
            txlut->lut[i].dac_gain,
            txlut->lut[i].mix_gain,
            txlut->lut[i].rf_power);
#else
    LOG(MOD_RAL|INFO,
            "SX1302 txlut %2d:  rf_power=%d pa_gain=%d pwr_idx=%d", i,
            txlut->lut[i].rf_power,
            txlut->lut[i].pa_gain,
            txlut->lut[i].pwr_idx);
#endif
    }
    log_flushIO();
}

static void dump_rfConf (int chain, struct lgw_conf_rxrf_s* rfconf) {
    if( !rfconf->enable ) {
        LOG(MOD_RAL|INFO, "       RF%d: disabled", chain);
        log_flushIO();
        return;
    }
    LOG(MOD_RAL|INFO,
#if defined(CFG_sx1302)
        " RX%s RF%d: %^8F rssi_offset=%+6.01f type=%d rssi_tcomp=%.03f %.03f %.03f %.03f %.03f",
#else
        " RX%s RF%d: %^8F rssi_offset=%+6.01f type=%d tx_notch_freq=%d",
#endif
        rfconf->tx_enable ? "/TX" : "   ",
        chain,
        rfconf->freq_hz,
        rfconf->rssi_offset,
        rfconf->type,
#if defined(CFG_sx1302)
        rfconf->rssi_tcomp.coeff_a,
        rfconf->rssi_tcomp.coeff_b,
        rfconf->rssi_tcomp.coeff_c,
        rfconf->rssi_tcomp.coeff_d,
        rfconf->rssi_tcomp.coeff_e
#else
        rfconf->tx_notch_freq
#endif
    );
    log_flushIO();
}

static void dump_ifConf (int chain, struct lgw_conf_rxrf_s rfconfs[LGW_RF_CHAIN_NB], struct lgw_conf_rxif_s* ifconf) {
    if( !ifconf->enable ) {
        LOG(MOD_RAL|INFO," channel %1d disabled", chain);
        log_flushIO();
        return;
    }
    if(ifmod_config[chain] == IF_LORA_STD) {
        LOG(MOD_RAL|INFO,
            " [STD]   %1d: %^8F rf=%d freq=%+6.01f datarate=%d bw=%d %s", chain,
            rfconfs[ifconf->rf_chain].freq_hz + ifconf->freq_hz,
            ifconf->rf_chain,
            (float)ifconf->freq_hz/1000,
            ifconf->datarate,
            ifconf->bandwidth,
#if defined(CFG_sx1302)
            (ifconf->implicit_hdr == true) ? "Implicit header" : "Explicit header"
#else
            ""
#endif
            );

    } else if (ifmod_config[chain] == IF_FSK_STD) {
        LOG(MOD_RAL|INFO,
            " [FSK]   %1d: %^8F rf=%d freq=%+6.01f datarate=%d bw=%d sync_word=%lX/%d", chain,
            rfconfs[ifconf->rf_chain].freq_hz + ifconf->freq_hz,
            ifconf->rf_chain,
            (float)ifconf->freq_hz/1000,
            ifconf->datarate,
            ifconf->bandwidth,
            ifconf->sync_word, ifconf->sync_word_size);
    } else {
        LOG(MOD_RAL|INFO,
            " [mSF]   %1d: %^8F rf=%d freq=%+6.01f datarate=%d", chain,
            rfconfs[ifconf->rf_chain].freq_hz + ifconf->freq_hz,
            ifconf->rf_chain,
            (float)ifconf->freq_hz/1000,
            ifconf->datarate);
    }
    log_flushIO();
}

static void dump_lbtConf (struct sx130xconf* sx130xconf) {
#if !defined(CFG_sx1302)
    if( sx130xconf->lbt.enable ) {
        LOG(MOD_RAL|INFO, "SX130x LBT enabled: rssi_target=%d rssi_offset=%d",
            sx130xconf->lbt.rssi_target, sx130xconf->lbt.rssi_offset);
        for( int i=0; i < sx130xconf->lbt.nb_channel; i++ ) {
            LOG(MOD_RAL|INFO, "  %2d: freq=%F scan=%dus",
                i, sx130xconf->lbt.channels[i].freq_hz, sx130xconf->lbt.channels[i].scan_time_us);
        }
    } else {
        LOG(MOD_RAL|INFO, "SX130x LBT not enabled");
    }
    log_flushIO();
#endif
}


int sx130xconf_start (struct sx130xconf* sx130xconf, u4_t cca_region) {
    str_t errmsg = "";
    lgw_stop();
    LOG(MOD_RAL|INFO,"Lora gateway library version: %s", lgw_version_info());

#if defined(CFG_linux)
    u4_t pids[1];
    int n = sys_findPids(sx130xconf->device, pids, SIZE_ARRAY(pids));
    if( n > 0 )
        rt_fatal("Radio device '%s' in use by process: %d%s", sx130xconf->device, pids[0], n>1?".. (and others)":"");
#endif // defined(CFG_linux)

#if defined(CFG_smtcpico)
    LOG(MOD_RAL|VERBOSE,"Connecting to smtcpico device: %s", sx130xconf->device);
    // Picocell needs some time to start up from reset before we can connect
    sys_usleep(rt_millis(250));
    log_flushIO();  // lgw_connect might block - make sure log output is flushed
    lgw_connect(sx130xconf->device);
    sys_usleep(rt_millis(250));
#endif

    dump_boardConf(&sx130xconf->boardconf);
#if defined(CFG_sx1302)
    if( lgw_board_setconf(&sx130xconf->boardconf) != LGW_HAL_SUCCESS ) {
#else
    if( lgw_board_setconf(sx130xconf->boardconf) != LGW_HAL_SUCCESS ) {
#endif
        errmsg = "lgw_board_setconf";
        goto fail;
    }
    if( sx130xconf->txlut.size > 0) {
        dump_txLut(&sx130xconf->txlut);
#if defined(CFG_sx1302)
        if( lgw_txgain_setconf(0, &sx130xconf->txlut) != LGW_HAL_SUCCESS ) {
#else
        if( lgw_txgain_setconf(&sx130xconf->txlut) != LGW_HAL_SUCCESS ) {
#endif
            errmsg = "lgw_txgain_setconf";
            goto fail;
        }
    }
    for( int i=0; i<LGW_RF_CHAIN_NB; i++ ) {
        dump_rfConf(i, &sx130xconf->rfconf[i]);
#if defined(CFG_sx1302)
        if( lgw_rxrf_setconf(i, &sx130xconf->rfconf[i]) != LGW_HAL_SUCCESS ) {
#else
        if( lgw_rxrf_setconf(i, sx130xconf->rfconf[i]) != LGW_HAL_SUCCESS ) {
#endif
            LOG(MOD_RAL|ERROR,"lgw_rxrf_setconf(%d) failed", i);
            errmsg = "lgw_rxrf_setconf";
            goto fail;
        }
    }
    for( int i=0; i<LGW_IF_CHAIN_NB; i++ ) {
        dump_ifConf(i, sx130xconf->rfconf, &sx130xconf->ifconf[i]);
#if defined(CFG_sx1302)
        if( lgw_rxif_setconf(i, &sx130xconf->ifconf[i]) != LGW_HAL_SUCCESS ) {
#else
        if( lgw_rxif_setconf(i, sx130xconf->ifconf[i]) != LGW_HAL_SUCCESS ) {
#endif
            LOG(MOD_RAL|ERROR,"lgw_rxif_setconf(%d) failed", i);
            errmsg = "lgw_rxif_setconf";
            goto fail;
        }
    }

    dump_lbtConf(sx130xconf);
    if( cca_region && !setup_LBT(sx130xconf, cca_region) ) {
        errmsg = "setup_LBT";
        goto fail;
    }

#if defined(CFG_sx1302)
    LOG(MOD_RAL|INFO, "Station device: %s:%s (PPS capture %sabled)",
        sx130xconf->boardconf.com_type == LGW_COM_USB ? "usb" : "spi",
        sx130xconf->device, sx130xconf->pps ? "en":"dis"
    );
    (void) sys_deviceMode; // TODO: Add device mode to sx1302 hal
#else
    LOG(MOD_RAL|INFO, "Station device: %s (PPS capture %sabled)", sx130xconf->device, sx130xconf->pps ? "en":"dis");
    lgwx_device_mode = sys_deviceMode;
#endif
    log_flushIO();  // flush output since lgw_start may block for quite some time on some concentrators

    ustime_t t0 = rt_getTime();
    int err = lgw_start();
    if( err != LGW_HAL_SUCCESS ) {
        errmsg = "lgw_start";
        goto fail;
    }
#if defined(CFG_sx1302)
    if( sx1302_gps_enable(sx130xconf->pps ? 1 : 0) != LGW_REG_SUCCESS ) {
#else
    if( lgw_reg_w(LGW_GPS_EN, sx130xconf->pps ? 1 : 0) != LGW_REG_SUCCESS ) {
#endif
        errmsg = "LGW GPS Enable";
        goto fail;
    }
    LOG(MOD_RAL|INFO, "Concentrator started (%~T)", rt_getTime()- t0);
#if defined(CFG_smtcpico)
    {
        // Avoid timing issues with picocell MCU firmware - re-adjusts time after first TX
        // (see cmdUSB.cpp Sx1308.firsttx). Seems to have a bad influence on station tracking
        // local MCU clock vs concentrator microsecond ticks.
        // Send a dummy frame to get into a stable state.
        struct lgw_pkt_tx_s pkt_tx;
        memset(&pkt_tx, 0, sizeof(pkt_tx));
        pkt_tx.tx_mode = IMMEDIATE;
        pkt_tx.preamble = 8;
        pkt_tx.modulation = MOD_LORA;
        pkt_tx.datarate   = DR_LORA_SF7;
        pkt_tx.bandwidth     = BW_125KHZ;
        pkt_tx.freq_hz    = sx130xconf->rfconf[0].freq_hz;
        pkt_tx.count_us   = 0;
        pkt_tx.rf_chain   = 0;
        pkt_tx.rf_power   = (float)0.0;
        pkt_tx.coderate   = CR_LORA_4_5;
        pkt_tx.invert_pol = true;
        pkt_tx.no_crc     = 1;
        pkt_tx.no_header  = false;
        pkt_tx.size       = 1;
        pkt_tx.preamble   = 8;
        pkt_tx.payload[0] = 0xE0;  // proprietary LoRaWAN frame
        // NOTE: nocca not possible to implement with current libloragw API
        int err = lgw_send(pkt_tx);
        if( err != LGW_HAL_SUCCESS ) {
            errmsg = "lgw_send";
            goto fail;
        }
    }
#endif // defined(CFG_smtcpico)
    return 1;
 fail:
    LOG(MOD_RAL|ERROR, "Concentrator start failed: %s", errmsg);
    return 0;
}

#endif // defined(CFG_lgw1)
