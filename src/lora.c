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

#include "s2conf.h"
#include "uj.h"


#define MHDR_FTYPE  0xE0
#define MHDR_RFU    0x1C
#define MHDR_MAJOR  0x03
#define MHDR_DNFLAG 0x20

#define MAJOR_V1    0x00

#define FRMTYPE_JREQ   0x00
#define FRMTYPE_JACC   0x20
#define FRMTYPE_DAUP   0x40  // data (unconfirmed) up
#define FRMTYPE_DADN   0x60  // data (unconfirmed) dn
#define FRMTYPE_DCUP   0x80  // data confirmed up
#define FRMTYPE_DCDN   0xA0  // data confirmed dn
#define FRMTYPE_REJOIN 0xC0  // rejoin
#define FRMTYPE_PROP   0xE0  // propriatary
#define FTYPE_BIT(t) (1<<(((t) & MHDR_FTYPE)>>5))
#define DNFRAME_TYPE (FTYPE_BIT(FRMTYPE_JACC) | \
                      FTYPE_BIT(FRMTYPE_DADN) | \
                      FTYPE_BIT(FRMTYPE_DCDN) )


// +-----------------------------------------+
// |                JOIN FRAME               |
// +-----+---------+--------+----------+-----+
// |  1  |     8   |    8   |    2     |  4  |  bytes - all fields little endian
// +=====+=========+========+==========+=====+
// | mhdr| joineui | deveui | devnonce | MIC |
// +-----+---------+--------+----------+-----+
#define OFF_mhdr      0
#define OFF_joineui   1
#define OFF_deveui    9
#define OFF_devnonce 17
#define OFF_jreq_mic 19
#define OFF_jreq_len 23

// +------------------------------------------------------------+
// |                           DATA FRAME                       |
// +-----+---------+-----+-------+-------+------+---------+-----+
// |  1  |    4    |  1  |   2   |  0/15 | 0/1  |   0-?   |  4  |   bytes - all fields little endian
// +=====+=========+=====+=======+=======+======+=========+=====+
// | mhdr| devaddr |fctrl|  fcnt | fopts | port | payload | MIC |
// +-----+---------+-----+-------+-------+------+---------+-----+
#define OFF_devaddr     1
#define OFF_fctrl       5
#define OFF_fcnt        6
#define OFF_fopts       8
#define OFF_df_minlen  12


uL_t* s2e_joineuiFilter;
u4_t  s2e_netidFilter[4] = { 0xffFFffFF, 0xffFFffFF, 0xffFFffFF, 0xffFFffFF };


int s2e_parse_lora_frame (ujbuf_t* buf, const u1_t* frame , int len, dbuf_t* lbuf) {
    if( len == 0 ) {
    badframe:
        LOG(MOD_S2E|DEBUG, "Not a LoRaWAN frame: %16.4H", len, frame);
        return 0;
    }
    int ftype = frame[OFF_mhdr] & MHDR_FTYPE;
    if( (len < OFF_df_minlen && ftype != FRMTYPE_PROP) ||
        // (FTYPE_BIT(ftype) & DNFRAME_TYPE) != 0 || --- because of device_mode feature we parse everything
        (frame[OFF_mhdr] & (MHDR_RFU|MHDR_MAJOR)) != MAJOR_V1 ) {
	goto badframe;
    }
    if( ftype == FRMTYPE_PROP || ftype == FRMTYPE_JACC ) {
        str_t msgtype = ftype == FRMTYPE_PROP ? "propdf" : "jacc";
        uj_encKVn(buf,
                  "msgtype",   's', msgtype,
                  "FRMPayload",'H', len, &frame[0],
                  NULL);
        xprintf(lbuf, "%s %16.16H", msgtype, len, &frame[0]);
        return 1;
    }
    if( ftype == FRMTYPE_JREQ || ftype == FRMTYPE_REJOIN ) {
        if( len != OFF_jreq_len)
            goto badframe;
        uL_t joineui = rt_rlsbf8(&frame[OFF_joineui]);
        
        if( s2e_joineuiFilter[0] != 0 ) {
            uL_t* f = s2e_joineuiFilter-2;
            while( *(f += 2) ) {
                if( joineui >= f[0] && joineui <= f[1] )
                    goto out1;
            }
            
            xprintf(lbuf, "Join EUI %E filtered", joineui);
            return 0;
          out1:;
        }
        str_t msgtype = (ftype == FRMTYPE_JREQ ? "jreq" : "rejoin");
        u1_t  mhdr = frame[OFF_mhdr];
        uL_t  deveui = rt_rlsbf8(&frame[OFF_deveui]);
        u2_t  devnonce = rt_rlsbf2(&frame[OFF_devnonce]);
        s4_t  mic = (s4_t)rt_rlsbf4(&frame[len-4]);
        uj_encKVn(buf,
                  "msgtype", 's', msgtype,
                  "MHdr",    'i', mhdr,
                  rt_joineui,'E', joineui,
                  rt_deveui, 'E', deveui,
                  "DevNonce",'i', devnonce,
                  "MIC",     'i', mic,
                  NULL);
        xprintf(lbuf, "%s MHdr=%02X %s=%:E %s=%:E DevNonce=%d MIC=%d",
                msgtype, mhdr, rt_joineui, joineui, rt_deveui, deveui, devnonce, mic);
        return 1;
    }
    u1_t foptslen = frame[OFF_fctrl] & 0xF;
    u1_t portoff = foptslen + OFF_fopts;
    if( portoff > len-4  )
        goto badframe;
    u4_t devaddr = rt_rlsbf4(&frame[OFF_devaddr]);
    u1_t netid = devaddr >> (32-7);
    if( ((1 << (netid & 0x1F)) & s2e_netidFilter[netid>>5]) == 0 ) {
        
        xprintf(lbuf, "DevAddr=%X with NetID=%d filtered", devaddr, netid);
        return 0;
    }
    u1_t  mhdr  = frame[OFF_mhdr];
    u1_t  fctrl = frame[OFF_fctrl];
    u2_t  fcnt  = rt_rlsbf2(&frame[OFF_fcnt]);
    s4_t  mic   = (s4_t)rt_rlsbf4(&frame[len-4]);
    str_t dir   = ftype==FRMTYPE_DAUP || ftype==FRMTYPE_DCUP ? "updf" : "dndf";
    uj_encKVn(buf,
              "msgtype",   's', dir,
              "MHdr",      'i', mhdr,
              "DevAddr",   'i', (s4_t)devaddr,
              "FCtrl",     'i', fctrl,
              "FCnt",      'i', fcnt,
              "FOpts",     'H', foptslen, &frame[OFF_fopts],
              "FPort",     'i', portoff == len-4 ? -1 : frame[portoff],
              "FRMPayload",'H', max(0, len-5-portoff), &frame[portoff+1],
              "MIC",       'i', mic,
              NULL);
    xprintf(lbuf, "%s mhdr=%02X DevAddr=%08X FCtrl=%02X FCnt=%d FOpts=[%H] %4.2H mic=%d (%d bytes)",
            dir, mhdr, devaddr, fctrl, fcnt,
            foptslen, &frame[OFF_fopts],
            max(0, len-4-portoff), &frame[portoff], mic, len);
    return 1;
}


static int crc16_no_table(uint8_t* pdu, int len) {
    uint32_t remainder = 0;
    uint32_t polynomial = 0x1021;
    for( int i=0; i<len; i++ ) {
        remainder ^= pdu[i] << 8;
        for( int bit=0; bit < 8; bit++ ) {
            if( remainder & 0x8000 ) {
                remainder = (remainder << 1) ^ polynomial;
            } else {
                remainder <<= 1;
            }
        }
    }
    return remainder & 0xFFFF;
}



// Pack parameters into a BEACON pdu with the following layout:
//    | 0-n |       4    |  2  |     1    |  3  |  3  | 0-n |  2  |   bytes - all fields little endian
//    | RFU | epoch_secs | CRC | infoDesc | lat | lon | RFU | CRC |
//
void s2e_make_beacon (uint8_t* layout, sL_t epoch_secs, int infodesc, double lat, double lon, uint8_t* pdu) {
    int time_off     = layout[0];
    int infodesc_off = layout[1];
    int bcn_len      = layout[2];
    memset(pdu, 0, bcn_len);
    for( int i=0; i<4; i++ ) 
        pdu[time_off+i] = epoch_secs>>(8*i);
    uint32_t ulon = (uint32_t)(lon / 180 * (1U<<31));
    uint32_t ulat = (uint32_t)(lat /  90 * (1U<<31));
    for( int i=0; i<3; i++ ) {
        pdu[infodesc_off+1+i] = ulon>>(8*i);
        pdu[infodesc_off+4+i] = ulat>>(8*i);
    } 
    pdu[infodesc_off] = infodesc;
    int crc1 = crc16_no_table(&pdu[0],infodesc_off-2);
    int crc2 = crc16_no_table(&pdu[infodesc_off], bcn_len-2-infodesc_off);
    for( int i=0; i<2; i++ ) {
        pdu[infodesc_off-2+i] = crc1>>(8*i);
        pdu[bcn_len-2+i]      = crc2>>(8*i);
    }
}
