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

#if defined(CFG_nogps)

#include "rt.h"

int sys_enableGPS (str_t _device) {
    LOG(MOD_GPS|ERROR, "GPS function not compiled.");
    return 0;
}

#else // ! defined(CFG_nogps)

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <termios.h>

#include "rt.h"
#include "sys_linux.h"

#include "s2e.h"
#include "tc.h"

#include "sys.h"
#include "s2conf.h"


// Special value to mark absent NMEA float/int field - e.g. $GPGGA,170801.00,,,,,0,00,99.99,,,,,,*69
#define NILFIELD 0x423a0a60

#if defined(CFG_ubx)
// We don't need UBX to operate station - we get time from server
// under the assumption both station and server are synced to a PPS.
// station infers the time label of a PPS pulse with the help of the server (see timesync.c)
// UBX code is still here in case we might need it again.
#define UBX_SYN1 (0xB5)
#define UBX_SYN2 (0x62)

static u1_t UBX_EN_NAVTIMEGPS[] = {
    UBX_SYN1, UBX_SYN2,
    0x06, 0x01, // class/ID
    0x03, 0x00, // payload length
    0x01, 0x20, 0x01, // Enable NAV-TIMEGPS messages on current port (serial) with 1s rate
    0x2C, 0x83 // checksum
};
#endif // defined(CFG_ubx)


typedef struct termios tio_t;

static u1_t   isTTY;
static u1_t   garbageCnt;
static str_t  device;
static int    ubx;
static int    baud;
static aio_t* aio;
static tio_t  saved_tio;
static int    gpsfill;
static u1_t   gpsline[1024];
static tmr_t  reopen_tmr;
static double last_lat, last_lon, last_alt, last_dilution;
static double orig_lat, orig_lon, from_lat, from_lon;
static int    last_satellites;
static int    last_quality;

static str_t const lastpos_filename = "~temp/station.lastpos";
static int      report_move;
static int      last_reported_fix;
static int      nofix_backoff;
static ustime_t time_fixchange;


#if defined(CFG_ubx)
static u2_t fletcher8 (u1_t* data, int len) {
    u1_t a=0, b=0;
    for( int i=0; i<len; i++) {
        a += data[i];
        b += a;
    }
    return a | (b<<8);
}
#endif // defined(CFG_ubx)


static int nmea_cksum ( u1_t* data, int len) {
    if( data[0] != '$' )
        return 0;
    int v = 0;
    for( int i=1; i<len; i++ ) {
        if( data[i] == '*' ) {
            int s = (rt_hexDigit(data[i+1]) << 4) | rt_hexDigit(data[i+2]);
            if( s!=v)
                LOG(MOD_GPS|ERROR,"NMEA checksum error: %02X vs %02X", s, v);
            data[i+1] = data[i+2] = 0;  // used for missing fields detection
            return s==v;
        }
        v ^= data[i];
    }
    return 0;
}


// Parse a set of NMEA fields as string values.
// Return zero terinated pointers. Note, field terminators (, / * ) are
// overwritten with \0.
// IN:
//    pp   - current read pointer into NMEA sentecne - at the start of a field
//    cnt  - number fields to parse
//    args - array of pointers to found field starts (zero terminated strings)
// RETURN:
//    0    - parsing failed - not enough fields
//    1    - parsing ok - field starts in args[0:cnt]
// OUT:
//    pp - advanced read pointer - stops after cnt-th field (after , or *)
//    args[..] - pointers to found field starts
static int nmea_str (char** pp, int cnt, char** args) {
    char* p = *pp;
    int c, i = 0;
    while( i < cnt ) {
        if( p[0] == '\0' ) {
            return 0;  // field missing
        }
        args[i] = p;
        while( (c=p[0]) != ',' && c != '*' )
            p++;
        *p++ = 0;
        i += 1;
    }
    *pp = p;
    return 1;
}


static int nmea_decimal(char** pp, sL_t* pv) {
    char* p = *pp;
    if( p[0] == '\0' ) {
        return 0;  // field missing
    }
    if( p[0] == '*' || p[0] == ',' ) {
        pv[0] = NILFIELD;
        return 1;
    }
    int sign = 0;
    if( *p == '-' ) {
        p++;
        sign = 1;
    }
    uL_t v = rt_readDec((str_t*)&p);
    if( *pp + sign == p )
        return 0;
    if( *p != ',' && *p != '*' )
        return 0;
    *pp = p+1;
    *pv = (sL_t)((sign?-1:1)*v);
    return 1;
}


static int nmea_float(char** pp, double* pv) {
    char* p = *pp;
    if( p[0] == '\0' ) {
        return 0;  // field missing
    }
    if( p[0] == '*' || p[0] == ',' ) {
        pv[0] = NILFIELD;
        return 1;
    }
    int sign = 0;
    if( *p == '-' ) {
        p++;
        sign = 1;
    }
    uL_t p10 = 1;
    uL_t w = 0;
    uL_t v = rt_readDec((str_t*)&p);
    if( *pp + sign == p )
        return 0;
    if( *p == '.' ) {
        char* f = ++p;
        w = rt_readDec((str_t*)&f);
        while( p < f ) {
            p++;
            p10 *= 10;
        }
    }
    if( *p != ',' && *p != '*' )
        return 0;
    *pp = p+1;
    *pv = (double)((sign?-1:1)*v) + (double)w/p10;
    return 1;
}


static int check_tolerance (double a, double b, double thres) {
    double d = a-b;
    return d<=-thres || thres<=d;
}


static int send_alarm (str_t fmt, ...) {
    if( !TC )
        return 0;
    ujbuf_t sendbuf = (*TC->s2ctx.getSendbuf)(&TC->s2ctx, MIN_UPJSON_SIZE);
    if( sendbuf.buf == NULL )
        return 0;
    va_list ap;
    va_start(ap, fmt);
    int ok = vxprintf(&sendbuf, fmt, ap);
    va_end(ap);
    if( !ok ) {
        LOG(MOD_GPS|ERROR, "JSON encoding of alarm exceeds available buffer space: %d", sendbuf.bufsize);
        return 0;
    }
    (*TC->s2ctx.sendText)(&TC->s2ctx, &sendbuf);
    return 1;
}


str_t GPSEV_MOVE = "move";
str_t GPSEV_FIX = "fix";
str_t GPSEV_NOFIX = "nofix";

static int send_gpsev_fix(str_t gpsev, float lat, float lon, float alt,
                          float dilution, int satellites, int quality, float from_lat, float from_lon) {
    assert(gpsev == GPSEV_MOVE  || gpsev == GPSEV_FIX || gpsev == GPSEV_NOFIX);
    ujbuf_t sendbuf = (*TC->s2ctx.getSendbuf)(&TC->s2ctx, MIN_UPJSON_SIZE);
    if( sendbuf.buf == NULL ) {
        LOG(MOD_S2E|ERROR, "Failed to send GPS event. Either no TC connection or insufficient IO buffer space.");
        return 0;
    }
    uj_encOpen(&sendbuf, '{');
    uj_encKVn(&sendbuf,
            "msgtype",    's', "event",
            "evcat",      's', "gps",
            "evmsg",      '{',
            /**/ "evtype",     's', gpsev,
            /**/ "lat",        'g', lat,
            /**/ "lon",        'g', lon,
            /**/ "alt",        'g', alt,
            /**/ "dilution",   'g', dilution,
            /**/ "satellites", 'i', satellites,
            /**/ "quality",    'i', quality,
            "}",
            NULL);
    uj_encClose(&sendbuf, '}');
    (*TC->s2ctx.sendText)(&TC->s2ctx, &sendbuf);

    if( gpsev == GPSEV_FIX ) {
        LOG(MOD_GPS|INFO, "GPS fix: %.7f,%.7f alt=%.1f dilution=%f satellites=%d quality=%d",
            lat, lon, alt, dilution, satellites, quality);
        return send_alarm("{\"msgtype\":\"alarm\","
                          "\"text\":\"GPS fix: %.7f,%.7f alt=%.1f dilution=%f satellites=%d quality=%d\"}",
                          lat, lon, alt, dilution, satellites, quality);
    } else {
        LOG(MOD_GPS|INFO, "GPS move %.7f,%.7f => %.7f,%.7f (alt=%.1f dilution=%f satellites=%d quality=%d)",
            from_lat, from_lon, lat, lon, alt, dilution, satellites, quality);
        return send_alarm("{\"msgtype\":\"alarm\","
                          "\"text\":\"GPS move %.7f,%.7f => %.7f,%.7f (alt=%.1f dilution=%f satellites=%d quality=%d)\"}",
                          from_lat, from_lon, lat, lon, alt, dilution, satellites, quality);
    }
}


static int send_gpsev_nofix(ustime_t since) {
    ujbuf_t sendbuf = (*TC->s2ctx.getSendbuf)(&TC->s2ctx, MIN_UPJSON_SIZE);
    if( sendbuf.buf == NULL ) {
        LOG(MOD_S2E|ERROR, "Failed to send gps event', no buffer space");
        return 0;
    }
    uj_encOpen(&sendbuf, '{');
    uj_encKVn(&sendbuf,
        "msgtype",    's', "event",
        "evcat",      's', "gps",
        "evmsg",      '{',
        /**/ "evtype",    's', GPSEV_NOFIX,
        /**/ "since",     'I', since,
        "}",
        NULL);
    uj_encClose(&sendbuf, '}');
    (*TC->s2ctx.sendText)(&TC->s2ctx, &sendbuf);

    LOG(MOD_GPS|INFO, "GPS nofix: since %~T", since);

    return send_alarm("{\"msgtype\":\"alarm\","
                      "\"text\":\"No GPS fix since %~T\"}", since);
}




static float nmea_p2dec(float lat, char d) {
    s4_t dd = (s4_t)(lat/100);
    float ss = lat - dd * 100;
    float dec = (ss/60.0 + dd);
    return (d == 'S' || d == 'W') ? (-1 * dec) : dec;
}


static void nmea_gga (char* p) {
    double time_of_fix, lat, lon, dilution, alt;
    char *latD, *lonD;
    char *pp = p;
    sL_t quality, satellites;
    if( !nmea_float  (&p, &time_of_fix) ||
        !nmea_float  (&p, &lat        ) ||
        !nmea_str    (&p, 1, &latD    ) ||
        !nmea_float  (&p, &lon        ) ||
        !nmea_str    (&p, 1, &lonD    ) ||
        !nmea_decimal(&p, &quality    ) ||
        !nmea_decimal(&p, &satellites ) ||
        !nmea_float  (&p, &dilution   ) ||
        !nmea_float  (&p, &alt        )) {
        int len = 0;
        while (pp[len]>31 && pp[len]<128 && ++len );
        LOG(MOD_GPS|ERROR, "Failed to parse GPS GGA sentence: (len=%d) %.*s", len, len, pp);
        return;
    }
    if( lat == NILFIELD || lon == NILFIELD ) {
        LOG(MOD_GPS|WARNING, "GGA sentence without a fix - bad GPS signal?");
        return;
    }
    lat = nmea_p2dec(lat, latD[0]);
    lon = nmea_p2dec(lon, lonD[0]);
    LOG(MOD_GPS|XDEBUG, "nmea_gga: lat %f, lon %f", lat, lon);

    if( (quality == 0) ^ (last_quality == 0) )
        time_fixchange = rt_getTime();

    int fix = (quality == 0 ? -1 : 1);
    ustime_t now = rt_getTime();
    ustime_t delay = GPS_REPORT_DELAY;

    //if (fix > 0) {
    //  send_gpsev_fix(GPSEV_FIX, lat, lon, alt, dilution, satellites, quality, 0.0, 0.0);
    //} else {
    //  send_gpsev_nofix(0);
    //}

    if( last_reported_fix <= 0 && fix > 0 && now > time_fixchange + delay &&
        send_gpsev_fix(GPSEV_FIX, lat, lon, alt, dilution, satellites, quality, 0.0, 0.0)) {
        last_reported_fix = fix;
        nofix_backoff = 0;
    }
    if( fix < 0 ) {
        ustime_t thres = time_fixchange + (1<<nofix_backoff)*delay;
        if( now > thres &&
            send_gpsev_nofix(now-time_fixchange)) {
            last_reported_fix = fix;
            nofix_backoff = max(nofix_backoff+1, 16);
        }
    }

    if( quality > 0 ) {
        if( check_tolerance(orig_lat, lat, 0.001) ||
            check_tolerance(orig_lon, lon, 0.001) ) {
            // GW changed position
            char json[100];
            dbuf_t jbuf = dbuf_ini(json);
            xprintf(&jbuf, "[%.6f,%.6f]", lat, lon);
            sys_writeFile(lastpos_filename, &jbuf);
            if( !report_move ) {
                from_lat = orig_lat;
                from_lon = orig_lon;
            }
            orig_lat = last_lat = lat;
            orig_lon = last_lon = lon;
            report_move = 1;
        }
        last_alt = alt;
        last_dilution = dilution;
        last_quality = quality;
        last_satellites = satellites;
    }
    last_quality = quality;

    if( report_move &&
        send_gpsev_fix(GPSEV_MOVE, lat, lon, alt, dilution, satellites, quality, from_lat, from_lon)) {
        report_move = 0;
    }
}


// Fwd decl
static int gps_reopen ();

static void reopen_timeout (tmr_t* tmr) {
    if( tmr == NULL || !gps_reopen() )
        rt_setTimer(&reopen_tmr, rt_micros_ahead(isTTY ? GPS_REOPEN_TTY_INTV : GPS_REOPEN_FIFO_INTV));
}


static void gps_read(aio_t* _aio) {
    assert(aio == _aio);
    int n, done = 0;
    while(1) {
        n = read(aio->fd, gpsline+gpsfill, sizeof(gpsline)-gpsfill);
        if( n == 0 ) {
            // EOF
            aio_close(aio);
            aio = NULL;
            reopen_timeout(NULL);
            return;
        }
        if( n == -1 ) {
            if( errno == EAGAIN )
                return;
            rt_fatal("Failed to read GPS data from '%s': %s", device, strerror(errno));
        }
        gpsfill = n = gpsfill + n;
        for( int i=0; i<n; i++ ) {
            if( gpsline[i] == '\n' ) {
                if( nmea_cksum(gpsline, i) ) {
                    LOG(MOD_GPS|XDEBUG, "NMEA: %.*s", i+1, &gpsline[done]);
                    if( gpsline[done+0] == '$' && gpsline[done+3] == 'G' &&
                        gpsline[done+4] == 'G' && gpsline[done+5] == 'A' && gpsline[done+6] == ',' ) {
                        nmea_gga((char*)gpsline+7);
                    }
                }
                else {
                    if( garbageCnt == 0 ) {
                        LOG(MOD_GPS|XDEBUG, "GPS garbage (%d bytes): %64H", i+1, i+1, &gpsline[done]);
                    } else {
                        garbageCnt -= 1;  // 1st few sentences might be garbage
                    }
                }
                done = i+1;
                break;
            }
#if defined(CFG_ubx)
            // UBX
            if( gpsline[i] == UBX_SYN1 && i+1 < n && gpsline[i+1] == UBX_SYN2 ) {
                if( i+6 > n )
                    break; // need more data to read header
                u2_t ubxlen = rt_rlsbf2(&gpsline[i+4]);
                if( i + ubxlen + 8 > n )
                    break;
                u2_t cksum = rt_rlsbf2(&gpsline[i+6+ubxlen]);
                u2_t fltch = fletcher8(&gpsline[i+2], ubxlen+4);
                if( cksum != fltch ) {
                LOG(MOD_GPS|XDEBUG, "UBX cksum=%04X vs found=%04X", cksum, fltch);
                    done = i+1;
                    break;
                }
                done = i+8+ubxlen;
                // NAV-TIMEGPS
                if( gpsline[i+2] == 0x01 && gpsline[i+3] == 0x20 && ubxlen == 16 ) {
                    u4_t itow     = rt_rlsbf4(&gpsline[i+6]);    // GPS time of week in ms
                    s4_t ftow     = rt_rlsbf4(&gpsline[i+6+4]);  // +/- 500000 ns
                    u2_t week     = rt_rlsbf2(&gpsline[i+6+4+4]);
                    u1_t leapsecs = gpsline[i+6+4+4+2];
                    u1_t valid    = gpsline[i+6+4+4+2+1];
                    u4_t tacc     = rt_rlsbf4(&gpsline[i+6+4+4+2+1+1]);
                    if( ftow < 0 ) {
                        itow -= 1;
                        ftow += 1000000;
                    }
                    LOG(MOD_GPS|XDEBUG, "NAV-TIMEGPS tow(ms)=%d.%06d week=%d leapsecs=%d valid=0x%x tacc(ns)=%d",
                        itow, ftow, week, leapsecs, valid, tacc);
                } else {
                    LOG(MOD_GPS|XDEBUG, "Unknown UBX frame: %H", 8+ubxlen, &gpsline[i]);
                }
                break;
            }
#endif // defined(CFG_ubx)
        }
        if( done ) {
            if( done < gpsfill )
                memmove(&gpsline[0], &gpsline[done], gpsfill-done);
            gpsfill -= done;
            done = 0;
        }
    }
}


static void gps_close () {
    if( aio == NULL )
        return;
    if( isTTY ) {
        if( tcsetattr(aio->fd, TCSANOW, &saved_tio) == -1 ) {
            LOG(MOD_GPS|WARNING, "Failed to restore TTY settings for '%s': %s", device, strerror(errno));
            return;
        }
        tcflush(aio->fd, TCIOFLUSH);
    }
    aio_close(aio);
    aio = NULL;
    isTTY = 0;
}


static int gps_reopen () {
    struct stat st;
    int fd;

    if( aio ) {
        aio_close(aio);
        aio = NULL;
    }

    if( stat(device, &st) != -1  && (st.st_mode & S_IFMT) == S_IFIFO ) {
        if( (fd = open(device, O_RDONLY | O_NONBLOCK)) == -1 ) {
            LOG(MOD_GPS|ERROR, "Failed to open FIFO '%s': %s", device, strerror(errno));
            return 0;
        }
        isTTY = 0;
        garbageCnt = 0;
    }
    else {
        u4_t pids[1];
        int n = sys_findPids(device, pids, SIZE_ARRAY(pids));
        if( n > 0 )
            rt_fatal("GPS device '%s' in use by process: %d%s", device, pids[0], n>1?".. (and others)":"");

        speed_t speed;
        switch( baud ) {
        case   9600: speed =   B9600; break;
        case  19200: speed =  B19200; break;
        case  38400: speed =  B38400; break;
        case  57600: speed =  B57600; break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        default:
            speed = B9600;
            break;
        }
        if( (fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1 ) {
            LOG(MOD_GPS|ERROR, "Failed to open TTY '%s': %s", device, strerror(errno));
            return 0;
        }
        struct termios tio;
        if( tcgetattr(fd, &tio) == -1 ) {
            LOG(MOD_GPS|ERROR, "Failed to retrieve TTY settings from '%s': %s", device, strerror(errno));
            close(fd);
            return 0;
        }
        saved_tio = tio;

        cfsetispeed(&tio, speed);
        cfsetospeed(&tio, speed);

        tio.c_cflag |= CLOCAL | CREAD | CS8;
        tio.c_cflag &= ~(PARENB|CSTOPB);
        tio.c_iflag |= IGNPAR;
        tio.c_iflag &= ~(ICRNL|IGNCR|IXON|IXOFF);
        tio.c_oflag  = 0;
        tio.c_lflag |= ICANON;
        tio.c_lflag &= ~(ISIG|IEXTEN|ECHO|ECHOE|ECHOK);
        //tio.c_lflag &= ~(ICANON|ISIG|IEXTEN|ECHO|ECHOE|ECHOK);
        //tio.c_cc[VMIN]  = 8;
        //tio.c_cc[VTIME] = 0;
        if( tcsetattr(fd, TCSANOW, &tio) == -1 ) {
            LOG(MOD_GPS|ERROR, "Failed to apply TTY settings to '%s': %s", device, strerror(errno));
            close(fd);
            return 0;
        }
        tcflush(fd, TCIOFLUSH);
        isTTY = 1;
        garbageCnt = 4;

#if defined(CFG_ubx)
        if( ubx ) {
            int n = sizeof(UBX_EN_NAVTIMEGPS);
            if( write(fd, UBX_EN_NAVTIMEGPS, n) != n )
                LOG(MOD_GPS|ERROR, "Failed to write UBX enable to GPS: n=%d %s", n, strerror(errno));
        }
#endif // defined(CFG_ubx)
    }
    // use device as dummy context
    aio = aio_open(&device, fd, gps_read, NULL);
    atexit(gps_close);
    gpsfill = 0;
    gps_read(aio);
    return 1;
}


int sys_getLatLon (double* lat, double* lon) {
    *lat = orig_lat;
    *lon = orig_lon;
    return 1;
}


//
// NOTE: Reading NMEA sentences from a GPS device is not used to sync time in any way.
// This information is only indicative of having a fix (and how good) and is used to
// report alarms back to the LNS.
//
int sys_enableGPS (str_t _device) {
    if( _device == NULL )
        return 1;  // no GPS device configured
    device = _device;
    baud = 9600;
    ubx = 1;

    rt_iniTimer(&reopen_tmr, reopen_timeout);
    if( !gps_reopen() ) {
        LOG(MOD_GPS|CRITICAL, "Initial open of GPS %s '%s' failed - GPS disabled!", isTTY ? "TTY":"FIFO", device);
        return 0;
    }
    dbuf_t b = sys_readFile(lastpos_filename);
    if( b.buf != NULL ) {
        ujdec_t D;
        uj_iniDecoder(&D, b.buf, b.bufsize);
        if( uj_decode(&D) ) {
            LOG(MOD_GPS|ERROR, "Parsing of '%s' failed - ignoring last GPS position", lastpos_filename);
            return 1;
        }
        uj_enterArray(&D);
        int slaveIdx;
        while( (slaveIdx = uj_nextSlot(&D)) >= 0 ) {
            double v = uj_num(&D);
            switch(slaveIdx) {
            case 0: orig_lat = v; break;
            case 1: orig_lon = v; break;
            }
        }
        uj_exitArray(&D);
        free(b.buf);
    }
    time_fixchange = rt_getTime();
    return 1;
}

#endif
