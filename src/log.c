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
#include "sys.h"
#include "rt.h"
#include "uj.h"

const char* LVLSTR[] = {
    [XDEBUG  ]= "XDEB",
    [DEBUG   ]= "DEBU",
    [VERBOSE ]= "VERB",
    [INFO    ]= "INFO",
    [NOTICE  ]= "NOTI",
    [WARNING ]= "WARN",
    [ERROR   ]= "ERRO",
    [CRITICAL]= "CRIT",
};
const char* MODSTR[] = {
    [MOD_ANY/8]= "any",
    [MOD_RAL/8]= "RAL",
    [MOD_S2E/8]= "S2E",
    [MOD_WSS/8]= "WSS",
    [MOD_JSN/8]= "JSN",
    [MOD_AIO/8]= "AIO",
    [MOD_CUP/8]= "CUP",
    [MOD_SYS/8]= "SYS",
    [MOD_TCE/8]= "TCE",
    [MOD_HAL/8]= "HAL",
    [MOD_SIO/8]= "___",
    [MOD_SYN/8]= "SYN",
    [MOD_GPS/8]= "GPS",
    [MOD_SIM/8]= "SIM",
    [MOD_WEB/8]= "WEB",
};

#ifndef CFG_logini_lvl
#define CFG_logini_lvl INFO
#endif

static char   logline[LOGLINE_LEN];
static dbuf_t logbuf = { .buf=logline, .bufsize=sizeof(logline), .pos=0 };
static char   slaveMod[4];
static u1_t   logLevels[32] = {
    CFG_logini_lvl, CFG_logini_lvl, CFG_logini_lvl, CFG_logini_lvl,
    CFG_logini_lvl, CFG_logini_lvl, CFG_logini_lvl, CFG_logini_lvl,
    CFG_logini_lvl, CFG_logini_lvl, CFG_logini_lvl, CFG_logini_lvl,
    CFG_logini_lvl, CFG_logini_lvl, CFG_logini_lvl, CFG_logini_lvl
};


static int log_header (u1_t mod_level) {
    int mod = (mod_level & MOD_ALL) >> 3;
    logbuf.pos = 0;
    str_t mod_s = slaveMod[0] ? slaveMod : mod >= SIZE_ARRAY(MODSTR) ? "???":MODSTR[mod];
    xprintf(&logbuf, "%.3T [%s:%s] ", rt_getUTC(), mod_s, LVLSTR[mod_level & 7]);
    return logbuf.pos;
}

int log_str2level (const char* level) {
    if( level[0] >= '0' && level[0] <='7' ) {
        return (level[0]-'0') | MOD_ALL;
    }
    int mod = MOD_ALL;
    if( level[0] && level[1] && level[2] && level[3] == ':' ) {
        for( int m=0; m < SIZE_ARRAY(MODSTR); m++ ) {
            if( strncasecmp(level, MODSTR[m], 3) == 0 ) {
                mod = m << 3;
                level += 4;
                goto cklevel;
            }
        }
        return -1;
    }
 cklevel:
    for( int i=0; i < SIZE_ARRAY(LVLSTR); i++ ) {
        if( strncasecmp(level, LVLSTR[i], 4) == 0 )
            return i | mod;
    }
    return -1;
}

str_t log_parseLevels (const char* levels) {
    do {
        int l = log_str2level(levels);
        if( l < 0 )
            return levels;
        log_setLevel(l);
        str_t s = strchr(levels, ',');
        if( s == NULL )
            return NULL;
        levels = s+1;
    } while(1);
}

void log_setSlaveIdx (s1_t idx) {
    slaveMod[0] = 'S';
    slaveMod[1] = idx/10 + '0';
    slaveMod[2] = idx%10 + '0';
}

int log_setLevel (int level) {
    if( level < 0 ) return -1;
    int mod = level & MOD_ALL;
    level &= 7;
    if( mod == MOD_ALL ) {
        for( int m=0; m<32; m++ ) {
            logLevels[m] = level;
        }
        return -1;
    }
    int old = logLevels[mod>>3];
    logLevels[mod>>3] = level;
    return old;
}

int log_shallLog (u1_t mod_level) {
    return (mod_level&7) >= logLevels[(mod_level & MOD_ALL) >> 3];
}

void log_vmsg (u1_t mod_level, const char* fmt, va_list args) {
    if( !log_shallLog(mod_level) )
        return;
    int n = log_header(mod_level);
    logbuf.pos = n;
    vxprintf(&logbuf, fmt, args);
    log_flush();
}

void log_msg (u1_t mod_level, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vmsg(mod_level, fmt, ap);
    va_end(ap);
}

void log_hal (u1_t level, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    log_vmsg(MOD_HAL|level, fmt, ap);
    va_end(ap);
}

int log_special (u1_t mod_level, dbuf_t* b) {
    if( !log_shallLog(mod_level) )
        return 0;
    b->buf = logbuf.buf;
    b->bufsize = logbuf.bufsize;
    b->pos = log_header(mod_level);
    return 1;
}

void log_specialFlush (int len) {
    assert(len < logbuf.bufsize);
    logbuf.pos = len;
    log_flush();
}

void log_flush () {
    xeol(&logbuf);
    xeos(&logbuf);
    sys_addLog(logbuf.buf, logbuf.pos);
    logbuf.pos = 0;
}

void log_flushIO () {
    log_flush();
    sys_addLog(logbuf.buf, 0);
}

