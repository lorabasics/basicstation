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

#if defined(CFG_lgw2)

#include <stdio.h>
#include "uj.h"
#include "kwcrc.h"
#include "sys.h"
#include "sx1301v2conf.h"
#include "lgw2/sx1301ar_err.h"
#include "lgw2/sx1301ar_reg.h"

// Describes radio config for HW spec sx1301/n as sent by LNS
struct lns_sx1301_conf {
    u4_t chanFreqs[SX1301AR_CHIP_CHAN_NB];
    u2_t chanEnabled;
    struct chanLSA {
        u1_t spreadfactor;
        u1_t bandwidth;
    } lsaChan;
    u4_t fskDatarate;
};


static void parse_tx_lut (ujdec_t* D, sx1301ar_tx_gain_lut_t* txlut) {
    int slot;
    uj_enterArray(D);
    while( (slot = uj_nextSlot(D)) >= 0 ) {
        if( slot >= SX1301AR_BOARD_MAX_LUT_NB )
            uj_error(D, "Too many 'tx_lut' entries (no more than %d allowed)", SX1301AR_BOARD_MAX_LUT_NB);
        ujcrc_t field;
        uj_enterObject(D);
        while( (field = uj_nextField(D)) ) {
            switch(field) {
            case J_rf_power            : { txlut->lut[slot].rf_power                = uj_intRange(D,  -128,  127); break; }
            case J_fpga_dig_gain       : { txlut->lut[slot].fpga_dig_gain           = uj_intRange(D,     0,  255); break; }
            case J_ad9361_atten        : { txlut->lut[slot].ad9361_gain.atten       = uj_intRange(D,     0,65535); break; }
            case J_ad9361_auxdac_vref  : { txlut->lut[slot].ad9361_gain.auxdac_vref = uj_intRange(D,     0,  255); break; }
            case J_ad9361_auxdac_word  : { txlut->lut[slot].ad9361_gain.auxdac_word = uj_intRange(D,     0,65535); break; }
            case J_ad9361_tcomp_coeff_a: { txlut->lut[slot].ad9361_tcomp.coeff_a    = uj_intRange(D,-32768,32767); break; }
            case J_ad9361_tcomp_coeff_b: { txlut->lut[slot].ad9361_tcomp.coeff_b    = uj_intRange(D,-32768,32767); break; }
            default: {
                uj_error(D, "Illegal 'txlut' field: %s", D->field.name);
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


static void parse_rf_chain_conf (ujdec_t* D, sx1301ar_board_cfg_t* board, float* txpow_adjusts, u1_t* antenna_types) {
    int slot;
    uj_enterArray(D);
    while( (slot = uj_nextSlot(D)) >= 0 ) {
        if( slot >= SX1301AR_BOARD_RFCHAIN_NB )
            uj_error(D, "Too many 'rf_chain_conf' entries (no more than %d allowed)", SX1301AR_BOARD_RFCHAIN_NB);
        sx1301ar_rfchain_t* rfchain  = &board->rf_chain[slot];
        ujcrc_t field;
        uj_enterObject(D);
        while( (field = uj_nextField(D)) ) {
            switch(field) {
            case J_tx_enable:           { rfchain->tx_enable     = uj_bool(D); break; }
            case J_rx_enable:           { rfchain->rx_enable     = uj_bool(D); break; }
            case J_rssi_offset:         { rfchain->rssi_offset   = uj_num(D);  break; }
            case J_rssi_offset_coeff_a: { rfchain->rssi_offset_coeff_a = uj_intRange(D,-32768,32767);  break; }
            case J_rssi_offset_coeff_b: { rfchain->rssi_offset_coeff_a = uj_intRange(D,-32768,32767);  break; }
            case J_tx_freq_min:
            case J_tx_freq_max:         { uj_uint(D); break; }  // we're not using this - checked via LNS channel plan
            case J_tx_lut:              { parse_tx_lut(D, &rfchain->tx_lut); break; }
                // -------------------- station specific
            case J_txpow_adjust:        { txpow_adjusts[slot] = uj_num(D); break; }
            case J_antenna_type:        { antenna_types[slot] = parse_antenna_type(uj_str(D)); break; }
            default: {
                uj_error(D, "Illegal field (ignored): %s", D->field.name);
            }
            }
        }
        uj_exitObject(D);
    }
    uj_exitArray(D);
}


static void parse_lbt_conf (ujdec_t* D, sx1301ar_lbt_cfg_t* lbtconf) {
    ujcrc_t field;
    uj_enterObject(D);
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_enable:           { lbtconf->enable      = uj_bool(D); break; }
        case J_rssi_target:      { lbtconf->rssi_target = uj_intRange(D,-128,127);  break; }
        case J_rssi_shift:       { lbtconf->rssi_shift  = uj_intRange(D,   0,255);  break; }
        case J_chan_cfg:         { uj_skipValue(D); break; }   // we auto populate from channel plan
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
    case 500000: return BW_500K; break;
    case 250000: return BW_250K; break;
    case 125000: return BW_125K; break;
    default:
        uj_error(D, "Illegal bandwidth value: %ld (must be 125000, 250000, or 500000)", bw);
        return BW_UNDEFINED; // NOT REACHED
    }
}


static int parse_spread_factor_range (ujdec_t* D) {
    if( uj_nextValue(D) == UJ_STRING ) {
        str_t s = uj_str(D);
        int sfmin = rt_readDec(&s);
        int sfmax = sfmin;
        if( s[0] == '-' ) {
            s += 1;
            sfmax = rt_readDec(&s);
        }
        if( sfmin < 7 || sfmin > 12 || sfmin > sfmax || s[0] )
            uj_error(D, "Failed to parse spread factor range (expecting \"num-num\" or \"num\")");
        return ((MR_SF7 << (sfmax-7+1)) - 1) & ~((MR_SF7 << (sfmin-7)) - 1);
    } else {
        return MR_SF7 << (uj_intRange(D, 7,12) - 7);
    }
}


static int parse_spread_factor (ujdec_t* D) {
    sL_t sf = uj_int(D);
    if( sf < 7 || sf > 12 )
        uj_error(D, "Illegal spread_factor value: %ld (must be 7,..,12)", sf);
    return MR_SF7 << (sf-7);
}


static void parse_SX1301_conf (ujdec_t* D, sx1301ar_chip_cfg_t* chipconf, sx1301ar_chan_cfg_t* chanconfs) {
    ujcrc_t field;
    int chan = 0;
    uj_enterObject(D);
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_chip_enable     : { chipconf->enable   = uj_bool(D); break; }
        case J_chip_center_freq: { chipconf->freq_hz  = uj_int(D); break; }
        case J_chip_rf_chain   : { chipconf->rf_chain = uj_intRange(D, 0, SX1301AR_BOARD_RFCHAIN_NB-1); break; }
        case J_chan_multiSF_7  : { chan = 7; goto multiSFx; }
        case J_chan_multiSF_6  : { chan = 6; goto multiSFx; }
        case J_chan_multiSF_5  : { chan = 5; goto multiSFx; }
        case J_chan_multiSF_4  : { chan = 4; goto multiSFx; }
        case J_chan_multiSF_3  : { chan = 3; goto multiSFx; }
        case J_chan_multiSF_2  : { chan = 2; goto multiSFx; }
        case J_chan_multiSF_1  : { chan = 1; goto multiSFx; }
        case J_chan_multiSF_0  : { chan = 0;
              multiSFx:
            uj_enterObject(D);
            while( (field = uj_nextField(D)) ) {
                switch(field) {
                case J_chan_rx_freq : { chanconfs[chan].freq_hz   = uj_uint(D); break; }
                case J_bandwidth    : { chanconfs[chan].bandwidth = parse_bandwidth(D); break; }
                case J_spread_factor: { chanconfs[chan].modrate   = parse_spread_factor_range(D); break; }
                default: { uj_error(D, "Illegal field: %s", D->field.name); }
                }
            }
            uj_exitObject(D);
            break;
        }
        case J_chan_LoRa_std: {
            uj_enterObject(D);
            while( (field = uj_nextField(D)) ) {
                switch(field) {
                case J_chan_rx_freq : { chanconfs[SX1301AR_CHIP_LSA_IDX].freq_hz   = uj_uint(D); break; }
                case J_bandwidth    : { chanconfs[SX1301AR_CHIP_LSA_IDX].bandwidth = parse_bandwidth(D); break; }
                case J_spread_factor: { chanconfs[SX1301AR_CHIP_LSA_IDX].modrate   = parse_spread_factor(D); break; }
                default: { uj_error(D, "Illegal field: %s", D->field.name); }
                }
            }
            uj_exitObject(D);
            break;
        }
        case J_chan_FSK: {
            uj_enterObject(D);
            while( (field = uj_nextField(D)) ) {
                switch(field) {
                case J_chan_rx_freq : { chanconfs[SX1301AR_CHIP_FSK_IDX].freq_hz   = uj_uint(D); break; }
                case J_bandwidth    : { chanconfs[SX1301AR_CHIP_FSK_IDX].bandwidth = parse_bandwidth(D); break; }
                case J_bit_rate     : { chanconfs[SX1301AR_CHIP_FSK_IDX].modrate   = uj_uint(D); break; }
                default: { uj_error(D, "Illegal field: %s", D->field.name); }
                }
            }
            uj_exitObject(D);
            break;
        }
        default: {
            uj_error(D, "Illegal field: %s", D->field.name);
        }
        }
    }
    uj_exitObject(D);
}


static void setDevice (struct board_conf* boardconf, str_t device) {
    str_t dev = sys_radioDevice(device, NULL);
    int sz = sizeof(boardconf->device);
    int n = snprintf(boardconf->device, sz, "%s", dev);
    if( n > sz-1 )
        LOG(ERROR, "Device string too long (max %d chars): %s", sz-1, dev);
    rt_free((void*)dev);
}


static void parse_radio_conf (ujdec_t* D, struct sx1301v2conf* sx1301v2conf) {
    int boardidx;
    uj_enterArray(D);
    while( (boardidx = uj_nextSlot(D)) >= 0 ) {
        if( boardidx >= SX1301AR_MAX_BOARD_NB )
            uj_error(D, "Too many radio boards - max %d supported", SX1301AR_MAX_BOARD_NB);
        sx1301ar_board_cfg_t* boardconf = &sx1301v2conf->boards[boardidx].boardConf;
        ujcrc_t field;
        uj_enterObject(D);
        while( (field = uj_nextField(D)) ) {
            switch(field) {
            case J_loramac_public: { boardconf->loramac_public = uj_bool(D); break; }
            case J_device        : { setDevice(&sx1301v2conf->boards[boardidx], uj_str(D)); break; }  // station specific
            case J_pps           : { sx1301v2conf->boards[boardidx].pps = uj_bool(D); break; }        //  ditto
            case J_board_rx_freq : { boardconf->rx_freq_hz = uj_uint(D); break; }
            case J_board_rx_bw   : { boardconf->rx_bw_hz = uj_uint(D); break; }
            case J_full_duplex   : { boardconf->full_duplex = uj_bool(D); break; }
            case J_board_type: {
                str_t s = uj_str(D);
                /**/ if( strcmp(s,"MASTER") == 0 ) boardconf->board_type = BRD_MASTER;
                else if( strcmp(s,"SLAVE")  == 0 ) boardconf->board_type = BRD_SLAVE;
                else uj_error(D, "Wrong board type: %s (must be MASTER or SLAVE)", s);
                break;
            }
            case J_FSK_sync: {
                u1_t buf[8];
                int n = uj_hexstr(D, buf, sizeof(buf));
                uL_t fsk_sync = 0;
                for( int i=0; i<n; i++ )
                    fsk_sync = (fsk_sync<<8) | buf[i];
                boardconf->fsk_sync_word = fsk_sync;
                boardconf->fsk_sync_size = n;
                break;
            }
            case J_calibration_temperature_celsius_room: {
                boardconf->room_temp_ref = uj_intRange(D,-128,127);
                break;
            }
            case J_calibration_temperature_code_ad9361: {
                boardconf->ad9361_temp_ref = uj_intRange(D,0,255);
                break;
            }
            case J_nb_dsp: {
                boardconf->nb_dsp = uj_intRange(D,0,SX1301AR_BOARD_NB_CHIP_PER_DSP);
                break;
            }
            case J_dsp_stat_interval: {
                boardconf->dsp_stat_interval = uj_intRange(D,0,255);
                break;
            }
            case J_fpga_flavor: {
                uj_str(D);
                sx1301v2conf->boards[boardidx].fpga_flavor = D->str.crc;
                break;
            }
            case J_aes_key: {
                int n = uj_hexstr(D, boardconf->aes_key, sizeof(boardconf->aes_key));
                if( n != 16 )
                    uj_error(D, "AES key must be %d bytes long", sizeof(boardconf->aes_key));
                break;
            }
            case J_rf_chain_conf: {
                parse_rf_chain_conf(D, boardconf,
                                    sx1301v2conf->boards[boardidx].txpowAdjusts,
                                    sx1301v2conf->boards[boardidx].antennaTypes);
                break;
            }
            case J_SX1301_conf: {
                int sxidx, chipidx = 0;
                for( int i=0; i < boardidx; i++ )
                    chipidx = sx1301v2conf->boards[i].boardConf.nb_chip;
                uj_enterArray(D);
                while( (sxidx = uj_nextSlot(D)) >= 0 ) {
                    if( chipidx+sxidx >= MAX_SX1301_NUM )
                        uj_error(D, "Too many SX1301 chips - max %d supported", MAX_SX1301_NUM);
                    struct chip_conf* p = &sx1301v2conf->sx1301[chipidx+sxidx];
                    parse_SX1301_conf(D, &p->chipConf, p->chanConfs);
                    sx1301v2conf->boards[boardidx].boardConf.nb_chip = sxidx+1;
                }
                uj_exitArray(D);
                break;
            }
            case J_lbt_conf: {
                parse_lbt_conf(D, &sx1301v2conf->boards[boardidx].lbtConf);
                break;
            }
            default: {
                LOG(MOD_RAL|WARNING, "Ignoring unsupported/unknown field: %s", D->field.name);
                uj_skipValue(D);
                break;
            }
            }
        }
        uj_exitObject(D);
    }
    uj_exitArray(D);
}


static int find_and_parse_radio_conf (str_t filename, struct sx1301v2conf* sx1301v2conf) {
    dbuf_t jbuf = sys_readFile(filename);
    if( jbuf.buf == NULL )
        return 0;
    ujdec_t D;
    uj_iniDecoder(&D, jbuf.buf, jbuf.bufsize);
    if( uj_decode(&D) ) {
        LOG(MOD_RAL|ERROR, "Parsing of JSON failed - '%s' ignored", filename);
        rt_free(jbuf.buf);
        return 0;
    }
    ujcrc_t field;
    uj_enterObject(&D);
    while( (field = uj_nextField(&D)) ) {
        switch(field) {
        case J_radio_conf: {
            parse_radio_conf(&D, sx1301v2conf);
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


static int setup_LBT (struct sx1301v2conf* sx1301v2conf, u4_t cca_region) {
    u2_t scantime_us = 0;
    s1_t rssi_target = 0;

    if( cca_region == J_AS923_1 ) {
        scantime_us = 5000;
        rssi_target = -80;
    }
    else if( cca_region == J_KR920 ) {
        scantime_us = 5000;
        rssi_target = -67;
    }
    else {
        LOG(MOD_RAL|ERROR, "Failed to setup CCA/LBT for region (crc=0x%08X)", cca_region);
        return 0;
    }
    for( int i=0; i < MAX_SX1301_NUM; i++ ) {
        sx1301v2conf->boards[SX1301AR_MAX_BOARD_NB].lbtConf.rssi_target = rssi_target;
    }
    // By default use 125KHz up link frequencies as LBT frequencies
    // Otherwise we should have gotten a freq list from the server
    int lbtchan = 0;
    int boardidx = 0;

    for( int i=0; i < MAX_SX1301_NUM; i++ ) {
        for( int j=0; j < SX1301AR_CHIP_MULTI_NB; j++ ) {
            u4_t f = sx1301v2conf->sx1301[i].chanConfs[j].freq_hz;
            if( f==0 ) continue;
            if( lbtchan == 0 )
                sx1301v2conf->boards[boardidx].lbtConf.enable = 1;
            sx1301v2conf->boards[boardidx].lbtConf.channels[lbtchan].freq_hz = f;
            sx1301v2conf->boards[boardidx].lbtConf.channels[lbtchan].scan_time_us = scantime_us;
            if( (lbtchan += 1) == SX1301AR_LBT_CHANNEL_NB_MAX ) {
                lbtchan = 0;
                if( (boardidx += 1) == SX1301AR_MAX_BOARD_NB )
                    goto stop;
            }
        }
    }
  stop:
    for( int i=0; i < SX1301AR_MAX_BOARD_NB; i++ ) {
        if( sx1301v2conf->boards[i].lbtConf.enable ) {
            if( sx1301ar_conf_lbt(i, &sx1301v2conf->boards[i].lbtConf) ) {
                LOG(MOD_RAL|ERROR, "sx1301ar_conf_lbt(%d,..) failed: %s", i, sx1301ar_err_message(sx1301ar_errno));
                return 0;
            }
        }
    }
    return 1;
}


static int parse_sx1301_lns_conf (ujdec_t* D, struct lns_sx1301_conf* confs) {
    int sx1301idx = 0, sx1301num = 0;
    uj_enterArray(D);
    while( (sx1301idx = uj_nextSlot(D)) >= 0 ) {
        sx1301num = sx1301idx+1;
        if( sx1301idx >= MAX_SX1301_NUM )
            uj_error(D, "Too many SX1301 - max %d supported", MAX_SX1301_NUM);
        struct lns_sx1301_conf* conf = &confs[sx1301idx];
        u4_t rfconf_freq[2] = {0,0};
        u4_t chanRadio = 0;
        ujcrc_t field;
        uj_enterObject(D);
        while( (field = uj_nextField(D)) ) {
            switch(field) {
            case J_radio_0:
            case J_radio_1: {
                int idx = uj_indexedField(D,"radio_");
                uj_enterObject(D);
                while( (field = uj_nextField(D)) ) {
                    switch(field) {
                    case J_enable: { uj_bool(D); break; }
                    case J_freq  : { rfconf_freq[idx] = uj_intRangeOr(D, 1000000, 1000000000, 0); break; }
                    default: goto err;
                    }
                }
                uj_exitObject(D);
                break;
            }
            case J_chan_multiSF_0:
            case J_chan_multiSF_1:
            case J_chan_multiSF_2:
            case J_chan_multiSF_3:
            case J_chan_multiSF_4:
            case J_chan_multiSF_5:
            case J_chan_multiSF_6:
            case J_chan_multiSF_7: {
                int idx = uj_indexedField(D,"chan_multiSF_");
                uj_enterObject(D);
                while( (field = uj_nextField(D)) ) {
                    switch(field) {
                    case J_enable :{ conf->chanEnabled |= uj_bool(D) << idx; break; }
                    case J_if     :{ conf->chanFreqs[idx] = uj_int(D); break; }
                    case J_radio  :{ chanRadio |= uj_intRange(D,0,1) << idx; break; }
                    default: goto err;
                    }
                }
                uj_exitObject(D);
                break;
            }
            case J_chan_Lora_std:
            case J_chan_LoRa_std: {
                int idx = SX1301AR_CHIP_LSA_IDX;
                uj_enterObject(D);
                while( (field = uj_nextField(D)) ) {
                    switch(field) {
                    case J_enable   :{ conf->chanEnabled |= uj_bool(D) << idx; break; }
                    case J_if       :{ conf->chanFreqs[idx] = uj_int(D); break; }
                    case J_radio    :{ chanRadio |= uj_intRange(D,0,1) << idx; break; }
                    case J_bandwidth:{ conf->lsaChan.bandwidth = parse_bandwidth(D); break; }
                    case J_spread_factor:{ conf->lsaChan.spreadfactor = parse_spread_factor(D); break; }
                    default: goto err;
                    }
                }
                uj_exitObject(D);
                break;
            }
            case J_chan_FSK: {
                int idx = SX1301AR_CHIP_FSK_IDX;
                uj_enterObject(D);
                while( (field = uj_nextField(D)) ) {
                    switch(field) {
                    case J_enable   :{ conf->chanEnabled |= uj_bool(D) << idx; break; }
                    case J_if       :{ conf->chanFreqs[idx] = uj_int(D); break; }
                    case J_radio    :{ chanRadio |= uj_intRange(D,0,1) << idx; break; }
                    case J_datarate :{ conf->fskDatarate = uj_uint(D); break; }
                    default: goto err;
                    }
                }
                uj_exitObject(D);
                break;
            }
            default: goto err;
            }
        }
        uj_exitObject(D);
        // Fix freq offsets into absolute frequencies
        for( int i=0; i<SX1301AR_CHIP_CHAN_NB; i++ ) {
            if( (conf->chanEnabled >> i) & 1 ) {
                conf->chanFreqs[i] += rfconf_freq[(chanRadio >> i) & 1];
            }
        }
    }
    uj_exitArray(D);
    return sx1301num;

  err:
    uj_error(D, "Server side radio config - Illegal field: %s", D->field.name);
    return 0;
}


int sx1301v2conf_parse_setup (struct sx1301v2conf* sx1301v2conf, int slaveIdx,
                            str_t hwspec, char* json, int jsonlen) {
    if( strncmp(hwspec, "sx1301/", 7) != 0 ) {
        LOG(MOD_RAL|ERROR, "Unsupported hwspec: %s", hwspec);
        return 0;
    }
    // Zero and setup some defaults
    memset(sx1301v2conf, 0, sizeof(*sx1301v2conf));
    for( int i=0; i < SX1301AR_MAX_BOARD_NB; i++ ) {
        sx1301v2conf->boards[i].boardConf = sx1301ar_init_board_cfg();
        sx1301v2conf->boards[i].lbtConf = sx1301ar_init_lbt_cfg();
        sx1301v2conf->boards[i].boardConf.loramac_public = 1;
        setDevice(&sx1301v2conf->boards[i], NULL);

        for( int j=0; j<SX1301AR_BOARD_RFCHAIN_NB; j++ ) {
            sx1301v2conf->boards[i].boardConf.rf_chain[j].tx_lut = sx1301ar_init_tx_gain_lut();
            for( int k=0; k < SX1301AR_BOARD_MAX_LUT_NB; k++ ) {
                sx1301v2conf->boards[i].boardConf.rf_chain[j].tx_lut.lut[k] = sx1301ar_init_tx_gain();
            }
        }
    }
    for( int i=0; i<MAX_SX1301_NUM; i++ ) {
        sx1301v2conf->sx1301[i].chipConf = sx1301ar_init_chip_cfg();
        for( int j=0; j<SX1301AR_CHIP_CHAN_NB; j++ )
            sx1301v2conf->sx1301[i].chanConfs[j] = sx1301ar_init_chan_cfg();
    }
    if( !find_and_parse_radio_conf("station.conf", sx1301v2conf) )
        return 0;

    ujdec_t D;
    uj_iniDecoder(&D, json, jsonlen);
    if( uj_decode(&D) ) {
        LOG(MOD_RAL|ERROR, "Parsing of JSON failed - 'router_config.sx1301_conf' ignored");
        return 0;
    }
    if( uj_null(&D) ) {
        LOG(MOD_RAL|ERROR, "LNS sx1301_conf is null but a HW setup IS required - no fallbacks");
        return 0;
    }
    struct lns_sx1301_conf lnsconfs[MAX_SX1301_NUM];
    memset(lnsconfs, 0, sizeof(lnsconfs));
    int lns_sx1301_num = parse_sx1301_lns_conf(&D, lnsconfs);
    uj_assertEOF(&D);

    int hw_sx1301_num = 0;
    for( int i=0; i < SX1301AR_MAX_BOARD_NB; i++ ) {
        hw_sx1301_num += sx1301v2conf->boards[i].boardConf.nb_chip;
    }
    // Merge LNS settings into local config
    if( lns_sx1301_num > hw_sx1301_num ) {
        LOG(MOD_RAL|ERROR, "Cannot map region plan onto available SX1301 chips - LNS/HW: %d/%d", lns_sx1301_num, hw_sx1301_num);
        return 0;
    }
    

    // Assign SX1301 definitions from LNS to loaded HW layout
    for( int i=0; i < lns_sx1301_num; i++ ) {
        struct chip_conf* c = &sx1301v2conf->sx1301[i];
        u4_t minFreq = -1, maxFreq = 0;
        for( int j=0; j < SX1301AR_CHIP_CHAN_NB; j++ ) {
            sx1301ar_chan_cfg_t* chanc = &c->chanConfs[j];
            u4_t f = lnsconfs[i].chanFreqs[j];
            if( f==0 )
                continue;
            minFreq = min(minFreq, f);
            maxFreq = max(maxFreq, f);
            chanc->enable = 1;
            chanc->freq_hz = f;
            if( j == SX1301AR_CHIP_FSK_IDX ) {
                chanc->modrate = MR_56000;
            }
            else if( j == SX1301AR_CHIP_LSA_IDX ) {
                chanc->modrate = lnsconfs[i].lsaChan.spreadfactor;
                chanc->bandwidth = lnsconfs[i].lsaChan.bandwidth;
            }
            else {
                chanc->modrate = MR_SF7_12;
                chanc->bandwidth = BW_125K;
            }
        }
        
        c->chipConf.enable = 1;
        c->chipConf.rf_chain = 0;  
        c->chipConf.freq_hz = (maxFreq+minFreq)/2;
    }

    // if(
    //     lns_sx1301_num == 2 &&
    //     sx1301v2conf->sx1301[1].chipConf.freq_hz == sx1301v2conf->sx1301[0].chipConf.freq_hz &&
    //     sx1301v2conf->boards[0].boardConf.rf_chain[1].rx_enable &&
    //     sx1301v2conf->boards[0].boardConf.rf_chain[0].rx_enable
    // ) {
    //     sx1301v2conf->sx1301[1].chipConf.rf_chain = 1;
    // }

    

    return 1;
}


static void dump_boardConf (int bid, sx1301ar_board_cfg_t* c) {
    LOG(MOD_RAL|VERBOSE,
        "__ BRD#%d : %^8F bw=%F %s",
        bid,
        c->rx_freq_hz,
        c->rx_bw_hz,
        c->board_type == BRD_MASTER ? "MASTER" : "SLAVE_");
    if( c->board_type == BRD_MASTER ) {
        for( int r = 0; r < SX1301AR_BOARD_RFCHAIN_NB; r++ ) {
            sx1301ar_rfchain_t* rfc = &c->rf_chain[r];
            LOG(MOD_RAL|VERBOSE, "   rf  %d : %s%s%s", r,
                rfc->rx_enable ? "RX " : "",
                rfc->tx_enable ? "TX"  : "",
                !rfc->rx_enable && !rfc->tx_enable ? "disabled" : ""
            );
        }
    }
    log_flushIO();
}

static void dump_chipConf (int chipid, sx1301ar_chip_cfg_t* c) {
    if( !c->enable ) {
        LOG(MOD_RAL|VERBOSE, "SX1301#%d : disabled", chipid);
    } else {
        LOG(MOD_RAL|VERBOSE,
            "SX1301#%d : %^8F rf_chain=%d",
            chipid,
            c->freq_hz,
            c->rf_chain);
    }
    log_flushIO();
}

static void dump_chanConf (int chipid, int chanid, sx1301ar_chan_cfg_t* c) {
    if( ! c->enable ) {
        LOG(MOD_RAL|VERBOSE, "  ch %d,%d : disabled", chipid, chanid);
        log_flushIO();
        return;
    }
    if( chanid == SX1301AR_CHIP_FSK_IDX ) {
        LOG(MOD_RAL|VERBOSE,
            "  ch %d,%d : %^8F FSK %d baud",
            chipid,
            chanid,
            c->freq_hz,
            c->modrate);
        log_flushIO();
        return;
    }
    LOG(MOD_RAL|VERBOSE,
        "  ch %d,%d : %^8F bw=%^5~F SF%d-%d",
        chipid,
        chanid,
        c->freq_hz,
        sx1301ar_bw_enum2nb(c->bandwidth),
        sx1301ar_sf_min_enum2nb(c->modrate),
        sx1301ar_sf_max_enum2nb(c->modrate));
    log_flushIO();
}


static void sx1301v2conf_challoc_cb (void* ctx, challoc_t* ch, int flag) {
    if( ctx == NULL ) return;
    struct sx1301v2conf* sx1301v2conf = (struct sx1301v2conf*)ctx;

    switch( flag ) {
    case CHALLOC_START: {
        break;
    }
    case CHALLOC_CHIP_START: {
        break;
    }
    case CHALLOC_CH: {
        sx1301ar_chan_cfg_t* chanc = &sx1301v2conf->sx1301[ch->chip].chanConfs[ch->chan];
        chanc->enable = 1;
        chanc->freq_hz = ch->chdef.freq;

        if( ch->chan == SX1301AR_CHIP_FSK_IDX ) {
            chanc->modrate = MR_56000;
            chanc->bandwidth = BW_UNDEFINED;
        }
        else if( ch->chan == SX1301AR_CHIP_LSA_IDX ) {
            chanc->modrate   = ral_rps2sf(rps_make(ch->chdef.rps.maxSF, ch->chdef.rps.bw));
            chanc->bandwidth = ral_rps2bw(rps_make(ch->chdef.rps.maxSF, ch->chdef.rps.bw));
        }
        else {
            chanc->modrate = sx1301ar_sf_range_nb2enum(
                sx1301ar_sf_enum2nb(ral_rps2sf(rps_make(ch->chdef.rps.minSF, ch->chdef.rps.bw))),
                sx1301ar_sf_enum2nb(ral_rps2sf(rps_make(ch->chdef.rps.maxSF, ch->chdef.rps.bw))));
            chanc->bandwidth = BW_125K;
        }

        break;
    }
    case CHALLOC_CHIP_DONE: {
        if( !ch->chans ) break;
        sx1301v2conf->sx1301[ch->chipid].chipConf.enable = 1;
        sx1301v2conf->sx1301[ch->chipid].chipConf.rf_chain = 0; 
        sx1301v2conf->sx1301[ch->chipid].chipConf.freq_hz = (ch->maxFreq+ch->minFreq)/2;
        break;
    }
    case CHALLOC_DONE: {
        if( sx1301v2conf->boards[0].boardConf.rf_chain[1].rx_enable &&
            !sx1301v2conf->sx1301[1].chipConf.enable ) {
            memcpy(&sx1301v2conf->sx1301[1], &sx1301v2conf->sx1301[0], sizeof(struct chip_conf));
            sx1301v2conf->sx1301[1].chipConf.rf_chain = 1;
        }
        break;
    }
    }
}

int sx1301v2conf_challoc (struct sx1301v2conf* sx1301v2conf, chdefl_t* upchs) {
    return ral_challoc(upchs, sx1301v2conf_challoc_cb, sx1301v2conf);
}


int sx1301v2conf_start (struct sx1301v2conf* sx1301v2conf, u4_t cca_region) {
    int nboards = 0;
    for( int boardidx=0; boardidx < SX1301AR_MAX_BOARD_NB; boardidx++ ) {
        sx1301ar_board_cfg_t* bc = &sx1301v2conf->boards[boardidx].boardConf;
        if( !bc->nb_chip ) continue;
        nboards = boardidx+1;
        dump_boardConf(boardidx, bc);
        if( sx1301ar_conf_board(boardidx, bc) != 0 ) {
            LOG(MOD_RAL|ERROR, "sx1301ar_conf_board(%d,..) failed: %s", boardidx, sx1301ar_err_message(sx1301ar_errno));
            return 0;
        }
        for( int chipidx=0; chipidx < bc->nb_chip; chipidx++ ) {
            struct chip_conf* cc = &sx1301v2conf->sx1301[chipidx];
            if( !cc->chipConf.enable )
                continue;
            dump_chipConf(chipidx, &cc->chipConf);
            if( sx1301ar_conf_chip(boardidx, chipidx, &cc->chipConf) != 0 ) {
                LOG(MOD_RAL|ERROR, "sx1301ar_conf_chip(%d,%d,..) failed: %s",
                    boardidx, chipidx, sx1301ar_err_message(sx1301ar_errno));
                return 0;
            }
            for( int chanidx=0; chanidx < SX1301AR_CHIP_CHAN_NB; chanidx++ ) {
                dump_chanConf(chipidx, chanidx, &cc->chanConfs[chanidx]);
                if( !cc->chanConfs[chanidx].enable )
                    continue;
                if( sx1301ar_conf_chan(boardidx, (chipidx << 4) | chanidx, &cc->chanConfs[chanidx]) != 0 ) {
                    LOG(MOD_RAL|ERROR, "sx1301ar_conf_chan(%d,%d,%d,..) failed: %s",
                        boardidx, chipidx, chanidx, sx1301ar_err_message(sx1301ar_errno));
                    return 0;
                }
            }
        }
    }
    if( cca_region && !setup_LBT(sx1301v2conf, cca_region) ) {
        goto fail;
    }
    if( sx1301ar_start(nboards) != 0 ) {
        LOG(MOD_RAL|ERROR, "sx1301ar_start(%d) failed: %s", nboards, sx1301ar_err_message(sx1301ar_errno));
        return 0;
    }
    return 1;
 fail:
    return 0;
}


#endif // defined(CFG_lgw2)
