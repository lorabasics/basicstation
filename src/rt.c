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

#include "sys.h"
#include "rt.h"

// More recent version of protocol uses standards compliant
// fields with capital EUI spelling
str_t rt_deveui  = "DevEui";
str_t rt_joineui = "JoinEui";

// We're using a simple linked list.
// Complexity is O(n) but we don't expect to have many entries on a router.
static tmr_t* timerQ = TMR_END;
// Buffer holding feature list
static dbuf_t features;

ustime_t rt_utcOffset;
ustime_t rt_utcOffset_ts;


void rt_usleep (sL_t us) {
    sys_usleep(us);
}


uL_t rt_eui() {
    return sys_eui();
}


ustime_t rt_getTime () {
    return (ustime_t)sys_time();
}

ustime_t rt_ustime2utc (ustime_t ustime) {
    return ustime + rt_utcOffset;
}

ustime_t rt_getUTC () {
    return rt_utcOffset + rt_getTime();
}


// Non leap year days per month
static const unsigned char DAYSPERMONTH[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

struct datetime rt_datetime (ustime_t ustime) {
    struct datetime dt;
    if( ustime < 0 ) {
        memset(&dt, 0, sizeof(dt));
        return dt;
    }
    dt.usec   = ustime%1000000; ustime /= 1000000;
    dt.second = ustime%60;      ustime /= 60;
    dt.minute = ustime%60;      ustime /= 60;
    dt.hour   = ustime%24;      ustime /= 24;

    int year = (unsigned)(ustime/365) + /*epoche*/1970 - 1;
    int daysinyear =  (int)(ustime%365)  // estimate of year
        // remove estimate of leap days
        - ((year/4)-(year/100)+(year/400))
        + ( 1970/4 - 1970/100 + 1970/400);
    // Adjust estimates
    if( daysinyear < 0 ) {
        year--;
        daysinyear += 365;
    }
    dt.year = year += 1;
    if( ((year%4)==0 && (year%100)!=0) || (year%400)==0 ) {
        // Current year is a leap year! We did not account for this year leap day.
        // We have to adjust the days depending on the date
        if( daysinyear == 31+29-1 ) {
            dt.day = 29;
            dt.month = 2;
            return dt;
        }
        if( daysinyear > 31+29-1 )  // before leap day
            daysinyear--;
    }
    int month = 0;
    while( daysinyear >= DAYSPERMONTH[month] ) {
        daysinyear -= DAYSPERMONTH[month];
        month++;
    }
    dt.month = month+1;
    dt.day = daysinyear+1;
    return dt;
}


// LCOV_EXCL_START
void rt_fatal(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vmsg(CRITICAL, fmt, ap);
    va_end(ap);
    sys_fatal(0);
}
// LCOV_EXCL_STOP


void rt_ini() {
    // If system does not have a native UTC time then sys_utc() returns zero and we run with
    // ustime_t (time since startup) for the time being. Once we get a connection to TC we
    // replace this offset with MuxTime which is the servers UTC time.
    ustime_t now = rt_getTime();
    rt_utcOffset = sys_utc() - now;
    rt_utcOffset_ts = now;
}


ATTR_FASTCODE
ustime_t rt_processTimerQ () {
    while(1) {
        if( timerQ == TMR_END )
            return USTIME_MAX;
#if defined(CFG_timerfd)
        ustime_t deadline = timerQ->deadline;
        if( (deadline - rt_getTime()) > 0 )
            return deadline;
#else // !defined(CFG_timerfd)
        ustime_t ahead;
        if( (ahead = timerQ->deadline - rt_getTime()) > 0 )
            return ahead;
#endif // !defined(CFG_timerfd)
        tmr_t* expired = timerQ;
        timerQ = expired->next;
        expired->next = TMR_NIL;
        if (expired->callback) {
            expired->callback(expired);
        } else {
            LOG(ERROR, "Timer due with NULL callback (tmr %p)", expired);
        }
    }
}


void rt_iniTimer (tmr_t* tmr, tmrcb_t callback) {
    tmr->next     = TMR_NIL;
    tmr->deadline = rt_getTime();
    tmr->callback = callback;
    tmr->ctx      = NULL;
}


void rt_setTimerCb (tmr_t* tmr, ustime_t deadline, tmrcb_t callback) {
    tmr->callback = callback;
    rt_setTimer(tmr, deadline);
}


ATTR_FASTCODE
void rt_setTimer (tmr_t* tmr, ustime_t deadline) {
    assert(tmr != NULL && tmr != TMR_END && tmr != TMR_NIL);
    if( tmr->next != TMR_NIL )
        rt_clrTimer(tmr); // still active
    tmr->deadline = deadline;
    tmr_t *p, **pp = &timerQ;
    while( (p = *pp) != TMR_END ) {
        if( deadline < p->deadline )
            break;
        pp = &p->next;
    }
    tmr->next = p;
    *pp = tmr;
}


void rt_yieldTo (tmr_t* tmr, tmrcb_t callback) {
    tmr->callback = callback;
    rt_setTimer(tmr, rt_getTime());
}


void rt_clrTimer (tmr_t* tmr) {
    if( (tmr == NULL || tmr == TMR_END) || tmr->next == TMR_NIL )
        return;  // not active or NULL
    tmr_t *p, **pp = &timerQ;
    while( (p = *pp) != TMR_END ) {
        if( p == tmr ) {
            *pp = tmr->next;
            tmr->next = TMR_NIL;
            return;
        }
        pp = &p->next;
    }
    assert(0);     // LCOV_EXCL_LINE
}



u2_t rt_rlsbf2 (const u1_t* buf) {
    return (u2_t)(buf[0] | (buf[1]<<8));
}

u2_t rt_rmsbf2 (const u1_t* buf) {
    return (u2_t)((buf[0]<<8) | buf[1]);
}

u4_t rt_rlsbf4 (const u1_t* buf) {
    return (u4_t)(buf[0] | (buf[1]<<8) | ((u4_t)buf[2]<<16) | ((u4_t)buf[3]<<24));
}

uL_t rt_rlsbf8 (const u1_t* buf) {
    return rt_rlsbf4(buf) | ((uL_t)rt_rlsbf4(buf+4) << 32);
}


void* _rt_malloc(int size, int zero) {
    void* p = malloc(size);
    if( p == NULL )
        rt_fatal("Out of memory - requesting %d bytes", size);
    if( zero )
        memset(p, 0, size);
    return p;
}

void* _rt_malloc_d(int size, int zero, const char* f, int l) {
    void* p = _rt_malloc(size, zero);
    // LOG (XDEBUG, "  rt_malloc(%d) %s:%d -> %p", size, f, l, p);
    return p;
}

void _rt_free_d (void* p, const char* f, int l) {
    // LOG (XDEBUG, "  rt_free() %s:%d -> %p", f, l, p);
    free(p);
}

char* rt_strdup (str_t s) {
    if( s == NULL ) return NULL;
    return strcpy(_rt_malloc(strlen(s)+1, 0), s);
}

char* rt_strdupn (str_t s, int n) {
    if( s == NULL ) return NULL;
    char* s2 = strncpy(_rt_malloc(n+1, 0), s, n);
    s2[n] = 0;
    return s2;
}

char* rt_strdupq (str_t s) { // copy and double quote a string
    if( s == NULL ) return NULL;
    int n = strlen(s);
    char* s2 = (char*)memcpy((char*)_rt_malloc(n+3, 0)+1, s, n)-1;
    s2[0] = s2[n+1] = '"';
    s2[n+2] = 0;
    return s2;
}


dbuf_t dbuf_dup (dbuf_t b) {
    dbuf_t b2 = b;
    b2.buf = _rt_malloc(b.bufsize+1, 0);
    if( b.buf == NULL )
        memset(b2.buf, 0, b2.bufsize);
    else
        memcpy(b2.buf, b.buf, b.bufsize);
    b2.buf[b.bufsize] = 0;  // just make it zero terminated
    return  b2;
}


void dbuf_free (dbuf_t* b) {
    rt_free(b->buf);
    b->buf = NULL;
    b->bufsize = b->pos = 0;
}


int rt_hexDigit (int c) {
    /**/ if( c >= '0' && c <= '9') return c - '0';
    else if( c >= 'a' && c <= 'f') return c - ('a'-10);
    else if( c >= 'A' && c <= 'F') return c - ('A'-10);
    else return -1;
}


sL_t rt_readDec (str_t* pp) {
    str_t p = *pp;
    if( p == NULL )
        return -1;
    int c, n=0;
    sL_t v=0;
    if( p[0] == '0' && (p[1] == 'X' || p[1] == 'x') ) {
        p += 2;
        while( (c=rt_hexDigit(p[n])) >= 0 ) {
            v = (v<<4) + c;
            n++;
        }
    } else {
        while( (c=p[n]) >= '0' && c <= '9' ) {
            v = v*10 + (c-'0');
            n++;
        }
    }
    *pp = p+n;
    return n==0 ? -1 : v;
}


sL_t rt_readSpan (str_t* pp, ustime_t defaultUnit) {
    str_t p = *pp;
    if( p == NULL )
        return -1;
    sL_t span = -1;
    while(1) {
        sL_t u, v = rt_readDec(&p);
        if( v < 0 ) {
            *pp = p;
            return span;
        }
        switch(p[0]) {
        case 'd': {
            u = rt_seconds(24*3600);
            break;
        }
        case 'h': {
            u = rt_seconds(   3600);
            break;
        }
        case 'm': {
            if( p[1]=='s' ) {
                u = rt_millis(1); p++;
            } else {
                u = rt_seconds(60);
            }
            break;
        }
        case 's': {
            u = rt_seconds(1);
            break;
        }
        default: {
            if( defaultUnit == 0 )
                return -1;
            u = defaultUnit;
            p--;
            break;
        }
        }
        p++;
        span = (span < 0 ? 0 : span) + v*u;
    }
}


sL_t rt_readSize (str_t* pp, ustime_t defaultUnit) {
    str_t p = *pp;
    if( p == NULL )
        return -1;
    sL_t size = -1;
    while(1) {
        sL_t u, v = rt_readDec(&p);
        if( v < 0 ) {
            *pp = p;
            return size;
        }
        sL_t uu = 1000;
        if( p[0] && (p[1]=='b' || p[1]=='B') )
            uu = 1024;
        switch(p[0]) {
        case 'k': case 'K': { u = uu; break; }
        case 'm': case 'M': { u = uu*uu; break; }
        case 'g': case 'G': { u = uu*uu*uu; break; }
        default: {
            if( defaultUnit == 0 )
                return -1;
            u = defaultUnit;
            uu = 1000;
            p--;
            break;
        }
        }
        p += uu==1000 ? 1 : 2;
        size = (size < 0 ? 0 : size) + v*u;
    }
}


static int parseId6Fragment (str_t p, int n, uL_t* peui) {
    uL_t group = 0;
    int bits = 16;
    for( int i=n-1, s=0; i >= -1; i-- ) {
        int x = i<0 ? -1 : rt_hexDigit(p[i]);
        if( x < 0 ) {
            if( s == 0 || s > 16 )
                return 0;
            if( i<0 ) break;
            bits += 16;
            s = 0;
        } else {
            group |= (uL_t)x << (s+bits-16);
            s += 4;
        }
    }
    *peui = group;
    return bits;
}

// Read EUI/MAC as pure hex digits, separated by - or :
// If only up to 4 colons in the string then treat as ID6
// Parsing stops at anything other than hex digit, -, :
// Parse up to len chars if len > 0, len=0 no limit
uL_t rt_readEui (const char** pp, int len) {
    str_t p = *pp;
    if( p == NULL )
        return 0;
    int c, n=0;
    int dashes=0, colons=0, hexdigits=0;
    while( (len==0 || n < len) && (c=p[n]) != 0 ) {
        /**/ if( c == '-' ) dashes++;
        else if( c == ':' ) colons++;
        else if( rt_hexDigit(c) >= 0 ) hexdigits++;
        else break;
        n++;
    }
    // What do we have?
    if( hexdigits==0 || (dashes && colons) || dashes > 7 || colons > 7 || hexdigits > 16 )
        return 0;  // looks strange
    if( colons == 2 || colons == 3 ) {
        // Parse as ID6
        uL_t eui;
        for( int i=1; i<n; i++ ) {
            // look for double colon
            if( p[i] == ':' && p[i-1] == ':' ) {
                if( i==1 ) {
                    // ::123
                    if( !parseId6Fragment(p+2, n-2, &eui) )
                        goto id6err;
                    goto id6exit;
                }
                if( i==n-1 ) {
                    // 123::
                    int bits = parseId6Fragment(p, n-2, &eui);
                    if( !bits ) goto id6err;
                    eui <<= (64-bits);
                    goto id6exit;
                }{
                    // 1::2
                    uL_t euix;
                    int bits = parseId6Fragment(p, i-1, &euix);
                    if( !bits || !parseId6Fragment(p+i+1, n-i-1, &eui) )
                        goto id6err;
                    eui |= (euix << (64-bits));
                    goto id6exit;
                }
            }
        }
        // No double colon
        if( !parseId6Fragment(p, n, &eui) )
            goto id6err;
    id6exit:
        *pp = p+n;
        return eui;
    id6err:
        return 0;
    }
    // Any missing hex digits are asumed to be leading zeros
    uL_t eui = 0;
    for( int i=0; i<n; i++ ) {
        int c = rt_hexDigit(p[i]);
        if( c >= 0 )
            eui = (eui<<4) | c;
    }
    *pp = p+n;
    return eui;
}


static const uint32_t crc_table[256] = {
    0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
    0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,
    0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
    0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
    0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
    0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
    0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
    0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
    0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
    0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
    0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
    0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
    0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
    0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
    0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
    0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
    0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
    0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
    0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
    0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
    0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
    0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
    0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
    0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
    0x86D3D2D4,0xF1D4E242,0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
    0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
    0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
    0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
    0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,0x54DE5729,0x23D967BF,
    0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D,
};

u4_t rt_crc32 (u4_t crc, const void* buf, int size) {
    const u1_t *p = (u1_t*)buf;

    crc = crc ^ ~0U;
    while( size-- > 0 )
        crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc ^ ~0U;
}

void rt_addFeature (str_t s) {
    int l = strlen(s);
    int n = features.pos+l+1;
    if( features.buf == NULL || n > features.bufsize ) {
        int sz = max(features.bufsize + 32, n);
        char* b = rt_mallocN(char, sz);
        if( features.pos > 0 )
            memcpy(b, features.buf, features.pos);
        rt_free(features.buf);
        features.buf = b;
        features.bufsize = sz;
    }
    int i = 0;
    char* p = features.buf;

    n = features.pos;
    while( i < n ) {
        if( strncmp(&p[i], s, l) == 0 && (p[i+l] == ' ' || p[i+l] == 0) )
            return; // already set
        while( p[i] != ' ' && p[i] != 0 ) i++;
        i += 1;
    }
    if( i > 0 )
        features.buf[i-1] = ' ';
    memcpy(&features.buf[i], s, l+1);
    features.pos = i+l+1;;
}

str_t rt_features () {
    return features.buf==NULL ? "" : features.buf;
}
