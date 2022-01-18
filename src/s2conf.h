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

#ifndef _s2conf_h_
#define _s2conf_h_

#include "rt.h"

#define DFLT_LOGFILE_SIZE        "\"10MB\""
#define DFLT_LOGFILE_ROTATE             "3"
#define DFLT_CUPS_BUFSZ           "\"8KB\""
/* TC */
#define DFLT_MAX_RXDATA           (10*1024)
#define DFLT_MAX_TXDATA           (16*1024)
#define DFLT_MAX_WSSDATA               2048
#define DFLT_TC_RECV_BUFSZ        (40*1024)
#define DFLT_TC_SEND_BUFSZ        (80*1024)
#define DFLT_RADIO_INIT_WAIT    "\"200ms\""
#define DFLT_MAX_TXUNITS                  4
#define DFLT_MAX_130X                     8
#define DFLT_MAX_TXJOBS                 128
#define DFLT_MAX_RXJOBS                  64
#define DFLT_RADIODEV  "\"/dev/spidev?.0\""
#define DFLT_TX_MIN_GAP          "\"10ms\""   // worst case for ODU as of 07.2018 (horrible SPI performance)
#define DFLT_TX_AIM_GAP          "\"20ms\""   //  -ditto-
#define DFLT_TX_MAX_AHEAD        "\"600s\""
#define DFLT_TXCHECK_FUDGE        "\"5ms\""
/* TCP keepalive */
#define DFLT_TCP_KEEPALIVE              "1"   // Connections use keep alive
#define DFLT_TCP_KEEPIDLE              "60"   // Connection idle time (in seconds) before sending keepalive probes
#define DFLT_TCP_KEEPINTVL             "15"   // The time (in seconds) between individual keepalive probes
#define DFLT_TCP_KEEPCNT                "4"   // The maximum number of keepalive probes sent before dropping the connection

#define DFLT_MAX_RMTSH                    2   // The maximum number of keepalive probes sent before dropping the connection
#define DFLT_BEACON_INTVL        "\"128s\""   // Time between beacons in microseconds




#if defined(CFG_platform_cisco) || defined(CFG_platform_rpi64)
#undef DFLT_TX_MIN_GAP
#undef DFLT_TX_AIM_GAP
#define DFLT_TX_MIN_GAP          "\"10ms\""
#define DFLT_TX_AIM_GAP          "\"60ms\""
#endif // defined(CFG_platform_cisco)




// --------------------------------------------------------------------------------
// Flash specs
//
//      _ FLASH_ADDR         _ FLASH_BEG_A   _ FLASH_BEG_B
// |___/____________________/.............../..............._____|_BYTES_
// |   \                  _/\________             _________/  /  | PAGES
//      \   FS_PAGE_START             FS_PAGE_CNT            /
//       \________________                __________________/
//                         FLASH_PAGE_CNT
//
//  - FLASH_ADDR, FLASH_PAGE_CNT define addressible flash.
//  - FS_PAGE_START, FS_PAGE_CNT define location of FS inside addressible flash
// --------------------------------------------------------------------------------

#define FLASH_PAGE_SIZE  (4*1024)
#define FLASH_PAGE_CNT   (1024)
#define FLASH_SIZE       (FLASH_PAGE_CNT*FLASH_PAGE_SIZE)
#define FLASH_ADDR       (0*FLASH_PAGE_SIZE)
#define FLASH_ERASED     ((u4_t)0xFFFFFFFF)
#define FS_PAGE_START    (512)
#define FS_PAGE_CNT      (500)
#define FS_MAX_FD        8
#define FS_MAX_FNSIZE    256

// --------------------------------------------------------------------------------
// Non Lora runtime parameters
// --------------------------------------------------------------------------------

enum {  MAX_DEVICE_LEN = 64 };      // max size of SPI/FTDI etc radio device
enum {  MAX_HOSTNAME_LEN = 128 };   // max size of FQDN in a URI or elsewhere
enum {  MAX_PORT_LEN = 16 };        // max size of port section in a URI
enum {  MAX_URI_LEN = 128 };        // max size of a URI
enum {  MAX_FILEPATH_LEN = 256 };   // max size of a file path

enum {  TC_RECV_BUFFER_SIZE =   DFLT_TC_RECV_BUFSZ }; // websocket connections to TC (infos/muxs)
enum {  TC_SEND_BUFFER_SIZE =   DFLT_TC_SEND_BUFSZ };

enum {  MAX_HWSPEC_SIZE = 32 };
enum {  MAX_CMDARGS = 64 };
enum {  MUXS_PROTOCOL_VERSION = 2 };
enum {  MAX_RMTSH = DFLT_MAX_RMTSH };

enum {  LOGLINE_LEN = 512 };

// --------------------------------------------------------------------------------
// Lora processing
// --------------------------------------------------------------------------------

enum {  RTT_SAMPLES     = 100 };
enum {  MAX_WSSFRAMES   =  32 };
enum {  MIN_UPJSON_SIZE = 384 };
enum {  MAX_TXUNITS     = DFLT_MAX_TXUNITS };
enum {  MAX_130X        = DFLT_MAX_130X };
enum {  MAX_TXJOBS      = DFLT_MAX_TXJOBS  };
enum {  MAX_TXFRAME_LEN =  255 };
enum {  MAX_RXFRAME_LEN =  255 };
enum {  MAX_RXJOBS      = DFLT_MAX_RXJOBS };
enum {  TXPOW_SCALE     =   10 };   // keep TX power internally as s2_t scaled by this
enum {  MAX_RXDATA      = DFLT_MAX_RXDATA };
enum {  MAX_TXDATA      = DFLT_MAX_TXDATA };
enum {  MAX_WSSDATA     = DFLT_MAX_WSSDATA };

struct conf_param {
    str_t name;
    str_t type;
    str_t info;
    str_t src;
    str_t value;
    void* pvalue;
    int (*parseFn)(struct conf_param* param);
};

extern struct conf_param conf_params[];

void  s2conf_ini ();
int   s2conf_set (str_t src, str_t name, str_t value);
void* s2conf_get (str_t name);   // it name a config param?
void  s2conf_printAll ();

#endif // _s2conf_h_

#ifndef _s2conf_x_
#define _s2conf_x_

#ifndef CONF_PARAM
#define CONF_PARAM(name,type,fn,value,info) extern type##_t name;
#endif

CONF_PARAM(RADIODEV            , str   , str     ,        DFLT_RADIODEV, "default radio device")
CONF_PARAM(LOGFILE_SIZE        , u4    , size_mb ,    DFLT_LOGFILE_SIZE, "default size of a logfile")
CONF_PARAM(LOGFILE_ROTATE      , u4    , u4      ,  DFLT_LOGFILE_ROTATE, "besides current log file keep *.1..N (none if 0)")
CONF_PARAM(TCP_KEEPALIVE_EN    , u4    , u4      ,   DFLT_TCP_KEEPALIVE, "TCP keepalive enabled")
CONF_PARAM(TCP_KEEPALIVE_IDLE  , u4    , u4      ,    DFLT_TCP_KEEPIDLE, "TCP keepalive TCP_KEEPIDLE [s]")
CONF_PARAM(TCP_KEEPALIVE_INTVL , u4    , u4      ,   DFLT_TCP_KEEPINTVL, "TCP keepalive TCP_KEEPINTVL [s]")
CONF_PARAM(TCP_KEEPALIVE_CNT   , u4    , u4      ,     DFLT_TCP_KEEPCNT, "TCP keepalive TCP_KEEPCNT")
CONF_PARAM(MAX_JOINEUI_RANGES  , u4    , u4      ,                 "10", "max ranges to suppress unwanted join requests")
CONF_PARAM(CUPS_CONN_TIMEOUT   , ustime, tspan_s ,            "\"60s\"", "connection timeout")
CONF_PARAM(CUPS_OKSYNC_INTV    , ustime, tspan_h ,            "\"24h\"", "regular check-in with CUPS for updates")
CONF_PARAM(CUPS_RESYNC_INTV    , ustime, tspan_m ,             "\"1m\"", "check-in with CUPS for updates after a failure")
CONF_PARAM(CUPS_BUFSZ          , u4    , size_kb ,      DFLT_CUPS_BUFSZ, "read from CUPS in chunks of this size")
CONF_PARAM(GPS_REPORT_DELAY    , ustime, tspan_s ,           "\"120s\"", "delay GPS reports and consolidate")
CONF_PARAM(GPS_REOPEN_TTY_INTV , ustime, tspan_ms,             "\"1s\"", "recheck TTY open if it failed")
CONF_PARAM(GPS_REOPEN_FIFO_INTV, ustime, tspan_ms,             "\"1s\"", "recheck if FIFO writer fake GPS")
CONF_PARAM(CMD_REOPEN_FIFO_INTV, ustime, tspan_ms,             "\"1s\"", "recheck if FIFO writer")
CONF_PARAM(RX_POLL_INTV        , ustime, tspan_ms,           "\"20ms\"", "interval to poll SX1301 RX FIFO")
CONF_PARAM(TC_TIMEOUT          , ustime, tspan_s ,            "\"60s\"", "reconnected to muxs")
CONF_PARAM(CLASS_C_BACKOFF_BY  , ustime, tspan_s ,          "\"100ms\"", "retry interval for class C TX attempts")
CONF_PARAM(CLASS_C_BACKOFF_MAX , u4    , u4      ,                 "10", "max number of class C TX attempts")
CONF_PARAM(RADIO_INIT_WAIT     , ustime, tspan_s , DFLT_RADIO_INIT_WAIT, "max wait for radio init command to finish")
CONF_PARAM(PPS_VALID_INTV      , ustime, tspan_ms,            "\"10m\"", "max age of last PPS sync for GPS time conversions")
CONF_PARAM(TIMESYNC_RADIO_INTV , ustime, tspan_ms,         "\"2100ms\"", "interval to resync MCU/SX1301")
CONF_PARAM(TIMESYNC_LNS_RETRY  , ustime, tspan_s ,           "\"71ms\"", "resend timesync message to server")
CONF_PARAM(TIMESYNC_LNS_PAUSE  , ustime, tspan_s ,             "\"5s\"", "pause after unsuccessful volley of timesync messages")
CONF_PARAM(TIMESYNC_LNS_BURST  , u4    , u4      ,                 "10", "volley of timesync messages before pausing")
CONF_PARAM(TIMESYNC_REPORTS    , ustime, tspan_s ,             "\"5m\"", "report interval for current timesync status")
CONF_PARAM(TX_MIN_GAP          , ustime, tspan_s ,      DFLT_TX_MIN_GAP, "min distance between two frames being TXed")
CONF_PARAM(TX_AIM_GAP          , ustime, tspan_s ,      DFLT_TX_AIM_GAP, "aim for this TX lead time, if delayed should not fall under min")
CONF_PARAM(TX_MAX_AHEAD        , ustime, tspan_s ,    DFLT_TX_MAX_AHEAD, "maximum time message can be scheduled into the future")
CONF_PARAM(TXCHECK_FUDGE       , ustime, tspan_s ,   DFLT_TXCHECK_FUDGE, "check radio state this time into ongoing TX")
CONF_PARAM(BEACON_INTVL        , ustime, tspan_s ,    DFLT_BEACON_INTVL, "beaconing interval")
CONF_PARAM(TLS_SNI             ,     u4,    bool ,               "true", "Set and verify server name of TLS connections")

#endif // _s2conf_x_

