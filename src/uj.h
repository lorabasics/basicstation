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

#ifndef _uj_h_
#define _uj_h_

#include <stdarg.h>
#include <setjmp.h>
#include "rt.h"

typedef dbuf_t ujbuf_t;
typedef doff_t ujoff_t;
typedef u4_t   ujcrc_t;

enum { UJ_MAX_NEST = 8 };
enum { UJ_N_ARY = 0, UJ_N_OBJ=1 };  // type of nesting
enum { UJ_MODE_SKIP = 1 };

typedef enum {
    UJ_UNDEF,
    UJ_NULL,
    UJ_BOOL,
    UJ_SNUM,   // signed number (there was a -)
    UJ_UNUM,   // unsigned number
    UJ_FNUM,   // floating point number (. or E present)
    UJ_STRING,
    UJ_ARRAY,
    UJ_OBJECT,
} ujtype_t;

typedef struct ujdec {
    jmp_buf on_err;
    char*   json_beg;
    char*   json_end;
    char*   read_pos;
    u2_t    nest_type;    // sequence of UJ_N_x
    s2_t    nest_level;
    ujoff_t nest_stack[UJ_MAX_NEST];
    u1_t    mode;
    // Currently parsed value if type != UJ_UNDEF
    union {
        sL_t   snum;
        uL_t   unum;
        double fnum;
        struct {
            char*   beg;
            ujoff_t len;
            ujcrc_t crc;
        } str;
    };
    char *val;
    ujtype_t type;
    // Current field/array slot context
    union {
        int index;
        struct {
            char* name;
            ujcrc_t crc;        } field;
    };
} ujdec_t;

#define uj_decode(dec) setjmp((dec)->on_err)

void      uj_iniDecoder (ujdec_t*, char* json, ujoff_t jsonlen);
ujtype_t  uj_nextValue  (ujdec_t*);
ujbuf_t   uj_skipValue  (ujdec_t*);
int       uj_nextSlot   (ujdec_t*);
ujcrc_t   uj_nextField  (ujdec_t*);
void      uj_enterObject(ujdec_t*);
void      uj_enterArray (ujdec_t*);
void      uj_exitObject (ujdec_t*);
void      uj_exitArray  (ujdec_t*);
void      uj_assertEOF  (ujdec_t*);

int       uj_null   (ujdec_t*);
int       uj_bool   (ujdec_t*);
sL_t      uj_int    (ujdec_t*);
uL_t      uj_uint   (ujdec_t*);
double    uj_num    (ujdec_t*);
char*     uj_str    (ujdec_t*);
uL_t      uj_eui    (ujdec_t*);
ujcrc_t   uj_keyword(ujdec_t*);
int       uj_hexstr (ujdec_t*, u1_t* buf, int bufsiz);
ujcrc_t   uj_msgtype(ujdec_t*);
void      uj_error  (ujdec_t*, str_t msg, ...);
int       uj_indexedField (ujdec_t*, str_t prefix);  // common case in radio config files
sL_t      uj_intRange     (ujdec_t*, sL_t minval, sL_t maxval);  // convenience - check value range
sL_t      uj_intRangeOr   (ujdec_t*, sL_t minval, sL_t maxval, sL_t orval);  // convenience - check value range


void uj_mergeStr(ujbuf_t* buf);
void uj_encOpen (ujbuf_t* buf, char brace);
void uj_encClose(ujbuf_t* buf, char brace);
void uj_encNull (ujbuf_t* buf);
void uj_encBool (ujbuf_t* buf, int val);
void uj_encInt  (ujbuf_t* buf, sL_t val);
void uj_encUint (ujbuf_t* buf, uL_t val);
void uj_encNum  (ujbuf_t* buf, double val);
void uj_encTime (ujbuf_t* buf, double val);
void uj_encDate (ujbuf_t* buf, uL_t date);
void uj_encKey  (ujbuf_t* buf, const char* key);
void uj_encStr  (ujbuf_t* buf, const char* s);
void uj_encHex  (ujbuf_t* buf, const u1_t* d, int len);
void uj_encEui  (ujbuf_t* buf, uL_t eui);
void uj_encMac  (ujbuf_t* buf, uL_t mac);
void uj_encId6  (ujbuf_t* buf, uL_t eui);

void uj_encKV   (ujbuf_t* buf, const char* key, char type, ...);
void uj_encKVn  (ujbuf_t* buf, ...);

int  xeos   (ujbuf_t* buf);
int  xeol   (ujbuf_t* buf);
void xputs  (ujbuf_t* buf, const char* s, int n);
int  xprintf(ujbuf_t* buf, const char* fmt, ...);
int vxprintf(ujbuf_t* buf, const char* fmt, va_list args);

#endif // _uj_h_
