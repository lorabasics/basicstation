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

#include <stdarg.h>
#include <stdio.h>
#include "uj.h"
#include "xq.h"     // %J - txjob only
#include "kwcrc.h"


static int nextChar (ujdec_t* dec) {
    if( dec->read_pos >= dec->json_end ) {
        dec->read_pos++;
        return 0;
    }
    return *dec->read_pos++;
}

static void backChar (ujdec_t* dec) {
    dec->read_pos -= 1;
}

static int skipWsp (ujdec_t* dec) {
    while(1) {
        int c = nextChar(dec);
        switch( c ) {
        case '\t':
        case '\r':
        case '\n':
        case ' ':
            break;
        case '/': {
            if( nextChar(dec) != '*' )
                uj_error(dec, "Bad start of comment");
            int d = 0;
            while(1) {
                c = nextChar(dec);
                if( c == 0 ) {  // or limit to a single line: || c == '\n'
                    uj_error(dec, "Unterminated /*.. comment");
                    // NOT REACHED
                }
                if( d == '*' && c == '/' )
                    break;
                d = c;
            }
            break;
        }
        default:
            return c;
        }
    }
}


static void nextLit (ujdec_t* dec, const char* s) {
    while( nextChar(dec) == s[0] ) {
        if( *++s == 0 )
            return;
    }
    uj_error(dec, "Expecting literal (null,true,false)");
    // NOT REACHED
}


static void parseString (ujdec_t* dec) {
    ujcrc_t crc = 0;
    char* wp = (dec->mode & UJ_MODE_SKIP) ? NULL : dec->read_pos;
    dec->str.beg = dec->read_pos;
    assert(dec->read_pos[-1] == '"');
    while(1) {
        int c = nextChar(dec);
        switch( c ) {
        case 0: {
            uj_error(dec, "Malformed string - no closing quote");
            // NOT REACHED
        }
        case '"': {
            // Make it a C string - assumes no \0 in the middle
            if( wp ) *wp = 0;
            dec->str.crc = UJ_FINISH_CRC(crc);
            dec->str.len = wp - dec->str.beg;
            return;
        }
        case '\\': {
            switch( c = nextChar(dec) ) {
            case '"':
            case '\\':
            case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                c = ((rt_hexDigit(nextChar(dec)) << 12) |
                     (rt_hexDigit(nextChar(dec)) <<  8) |
                     (rt_hexDigit(nextChar(dec)) <<  4) |
                     (rt_hexDigit(nextChar(dec))      ) );
                if( c < 0 ) {
                    uj_error(dec, "Malformed \\u escape sequence");
                    // NOT REACHED
                }
                if( c >= (1<<7) ) {
                    char cu[2];
                    int ci;
                    if( c < (1<<11) ) {  // 2^7 <= c < 2^11   ==> 5+6 bits
                        ci = 1;
                        cu[1] = 0xC0 | (c>>6);   // encode 5 bits
                    } else {             // 2^11 <= c < 2^16  ==> 4+6+6 bits
                        ci = 0;
                        cu[0] = 0xE0 | (c>>12);  // encode 4 bits
                        cu[1] = 0x80 | ((c>>6)&0x3F);   // 6 bits
                    }
                    for(; ci < 2; ci++ ) {
                        if( wp ) *wp++ = cu[ci];
                        crc = UJ_UPDATE_CRC(crc,cu[ci]);
                    }
                    c = 0x80|(c&0x3F);  // encode 6 bits
                }
                break;
            }
            default:
                uj_error(dec, "Illegally escaped character");
                // NOT REACHED
            }
        }
        }
        if( wp ) *wp++ = c;
        crc = UJ_UPDATE_CRC(crc,c);
    }
}

static int decDigits (ujdec_t* dec, uL_t* pv, int* pn) {
    uL_t v = 0;
    int c, n = 0;
    while( (c = nextChar(dec)) >= '0' && c <= '9' ) {
        v = v*10 + c - '0';
        n++;
    }
    if( n == 0 ) {
        uj_error(dec, "Expecting some decimal digits");
        // NOT REACHED
    }
    *pn = n;
    *pv = v;
    return c;
}


static void parseNumber (ujdec_t* dec, int signum) {
    uL_t num;
    int  dummy;
    uL_t frac = 0;
    int  nFrac = 0;
    sL_t exp;
    int  expSign = 0;  // no exponent
    int  c;

    c = decDigits(dec, &num, &dummy);
    if( c=='.' ) {
        c = decDigits(dec, &frac, &nFrac);
    }
    if( c=='e' || c=='E' ) {
        expSign = 1;
        c = nextChar(dec);
        /**/ if( c=='-' ) expSign = -1;
        else if( c!='+' ) backChar(dec);
        decDigits(dec, (uL_t*)&exp, &dummy);
    }
    backChar(dec);
    if( nFrac == 0 && expSign == 0 ) {
        dec->type = signum < 0 ? UJ_SNUM: UJ_UNUM;
        dec->snum = signum * num;
        return;
    }
    double f = frac;
    while( --nFrac >= 0 )
        f /= 10;
    f += (double)num;
    if( expSign != 0 ) {
        while( --exp >= 0 )
            f = expSign > 0 ? f*10 : f/10;
    }
    dec->fnum = signum * f;
    dec->type = UJ_FNUM;
}


static void do_enter (ujdec_t* dec, int what, char brace, const char* err) {
    if( skipWsp(dec) != brace ) {
        uj_error(dec, err);
        // NOT REACHED
    }
    if( dec->nest_level == UJ_MAX_NEST )
        uj_error(dec, "JSON nested too deeply");
    int nl = dec->nest_level;
    if( nl >= 0 ) {
        if( (dec->nest_type & 1) == UJ_N_OBJ ) {
            // Inside an object - remember the offset to the field key
            dec->nest_stack[nl] = dec->field.name - dec->json_beg;
        } else {
            // Inside an array - remember the array slot
            assert(dec->index >= 0);
            dec->nest_stack[nl] = dec->index;
        }
        dec->nest_type <<= 1;
    }
    dec->nest_type |= what;
    dec->nest_level = nl + 1;
}

static void do_exit (ujdec_t* dec, int what, char brace, const char* err) {
    if( dec->nest_level < 0 || (dec->nest_type & 1) != what ) {
        uj_error(dec, "Internal parser error - do_exit");
        // NOT REACHED
    }
    if( skipWsp(dec) != brace ) {
        uj_error(dec, err);
        // NOT REACHED
    }
    dec->nest_level -= 1;
    dec->nest_type >>= 1;
    dec->type = UJ_UNDEF;
    if( dec->nest_level < 0 ) {
        dec->field.name = NULL;
        dec->field.crc = 0;
        dec->index = -1;
    }
    else if( (dec->nest_type & 1) == UJ_N_OBJ ) {
        dec->field.name = &dec->json_beg[dec->nest_stack[dec->nest_level]];
        dec->field.crc = 0;
    } else {
        dec->index = dec->nest_stack[dec->nest_level];
    }
}

void uj_enterObject (ujdec_t* dec) {
    do_enter(dec, UJ_N_OBJ, '{', "Expecting an object");
    dec->field.name = NULL;
    dec->field.crc = 0;
}

void uj_enterArray (ujdec_t* dec) {
    do_enter(dec, UJ_N_ARY, '[', "Expecting an array");
    dec->index = -1;
}

void uj_exitObject (ujdec_t* dec) {
    do_exit(dec, UJ_N_OBJ, '}', "Expecting a closing }");
}

void uj_exitArray (ujdec_t* dec) {
    do_exit(dec, UJ_N_ARY, ']', "Expecting a closing ]");
}


ujcrc_t uj_nextField (ujdec_t* dec) {
    dec->type = UJ_UNDEF;
    int c = skipWsp(dec);
    if( c == '}' ) {
        backChar(dec);
        return 0;                    // no more object fields
    }
    if( dec->field.name != NULL ) {  // very first field?
        if( c != ',' ) {
            uj_error(dec, "Expecting a comma");
            // NOT REACHED
        }
        c = skipWsp(dec);
    }
    if( c != '"' ) {
        uj_error(dec, "Expecting a field");
        // NOT REACHED
    }
    parseString(dec);
    dec->field.name = dec->str.beg;
    dec->field.crc  = dec->str.crc;
    if( skipWsp(dec) != ':' ) {
        uj_error(dec, "Expecting a colon");
        // NOT REACHED
    }
    return dec->field.crc;
}


int uj_nextSlot (ujdec_t* dec) {
    dec->type = UJ_UNDEF;
    int c = skipWsp(dec);
    if( c == ']' ) {
        backChar(dec);
        return -1;          // no more array slots
    }
    if( dec->index >= 0 ) {
        // any slot after first
        if( c != ',' ) {
            uj_error(dec, "Expecting a comma");
            // NOT REACHED
        }
    } else {
        // very first slot?
        backChar(dec);
    }
    dec->index += 1;
    return dec->index;
}


int uj_indexedField (ujdec_t* dec, str_t prefix) {
    if( dec->field.name == NULL || dec->field.crc == 0 )
        return -1;
    int n = strlen(prefix);
    if( strncmp(dec->field.name, prefix, n) != 0 )
        return -1;
    str_t s, p;
    s = p = dec->field.name + n;
    sL_t idx = rt_readDec(&s);
    if( s==p || s[0] ) return -1;
    return idx;
}


sL_t uj_intRange (ujdec_t* dec, sL_t minval, sL_t maxval) {
    sL_t v = uj_int(dec);
    if( v < minval || v > maxval )
        uj_error(dec, "Field value not in range [%ld..%ld]: %ld", minval, maxval, v);
    return v;
}


sL_t uj_intRangeOr (ujdec_t* dec, sL_t minval, sL_t maxval, sL_t orval) {
    sL_t v = uj_int(dec);
    if( v != orval && (v < minval || v > maxval) )
        uj_error(dec, "Field value not %ld or in range [%ld..%ld]: %ld", orval, minval, maxval, v);
    return v;
}


void uj_error (ujdec_t* dec, const char* fmt, ...) {
    ujbuf_t b;
    if( log_special(MOD_JSN|ERROR, &b) ) {
        int nl = dec->nest_level;
        if( nl >= 0 ) {
            xprintf(&b, "@");
            int li = -1;
            while( ++li <= nl ) {
                int what = (dec->nest_type >> (nl-li)) & 1;
                ujoff_t off = dec->nest_stack[li];
                if( what == UJ_N_OBJ ) {
                    char* f = li==nl ? dec->field.name : (char*)&dec->json_beg[off];
                    xprintf(&b, ".%.20s", f);
                } else {
                    xprintf(&b, "[%d]", li==nl ? dec->index : off);
                }
            }
        }
        xprintf(&b, ": ");
        va_list ap;
        va_start(ap, fmt);
        vxprintf(&b, fmt, ap);
        va_end(ap);
        log_specialFlush(b.pos);
    }
    longjmp(dec->on_err, 1);
}


void uj_assertEOF (ujdec_t* dec) {
    if( skipWsp(dec) != 0 ) {
        uj_error(dec, "Expecting EOF but found garbage: %.20s", dec->read_pos-1);
        // NOT REACHED
    }
}


ujtype_t uj_nextValue (ujdec_t* dec) {
    if( dec->type != UJ_UNDEF )
        return dec->type;

    int c = skipWsp(dec);
    dec->val = dec->read_pos - 1;

    switch( c ) {
    case 0: {
        uj_error(dec, "Unexpected EOF");
        // NOT REACHED
    }
    case '"': {
        dec->type = UJ_STRING;
        parseString(dec);
        break;
    }
    case '-': {
        parseNumber(dec, -1);
        break;
    }
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': {
        backChar(dec);
        parseNumber(dec, 1);
        break;
    }
    case 't': {
        nextLit(dec,"true"+1);
        dec->type = UJ_BOOL;
        dec->snum = 1;
        break;
    }
    case 'f': {
        nextLit(dec,"false"+1);
        dec->type = UJ_BOOL;
        dec->snum = 0;
        break;
    }
    case 'n': {
        nextLit(dec,"null"+1);
        dec->type = UJ_NULL;
        dec->snum = 0;
        break;
    }
    case '{':
    case '[': {
        dec->type = c=='{' ? UJ_OBJECT : UJ_ARRAY;
        backChar(dec);  // enter expects to find opening brace
        break;
    }
    default: {
        uj_error(dec, "Syntax error");
        // NOT REACHED
    }
    }
    return dec->type;
}


static void skipValue (ujdec_t* dec) {
    uj_nextValue(dec);
    if( dec->type == UJ_OBJECT ) {
        uj_enterObject(dec);
        while( uj_nextField(dec) )
            skipValue(dec);
        uj_exitObject(dec);
    }
    else if( dec->type == UJ_ARRAY ) {
        uj_enterArray(dec);
        while( uj_nextSlot(dec) >= 0 )
            skipValue(dec);
        uj_exitArray(dec);
    }
}


ujbuf_t uj_skipValue (ujdec_t* dec) {
    // Skip leading WSP - this is for code that uses skipped value as text (e.g. s2conf)
    skipWsp(dec);
    backChar(dec);
    ujbuf_t buf = { .buf = dec->read_pos, .bufsize = 0, .pos = 0 };
    dec->mode |= UJ_MODE_SKIP;
    skipValue(dec);
    buf.bufsize = dec->read_pos - buf.buf;
    dec->mode &= ~UJ_MODE_SKIP;
    return buf;
}


void uj_iniDecoder (ujdec_t* dec, char* json, ujoff_t jsonlen) {
    memset(dec, 0, sizeof(*dec));
    dec->json_beg = dec->read_pos = json;
    dec->json_end = json+jsonlen;
    dec->nest_level = -1;
}



int uj_null (ujdec_t* dec) {
    ujtype_t t = uj_nextValue(dec);
    return t == UJ_NULL;
}

int uj_bool (ujdec_t* dec) {
    ujtype_t t = uj_nextValue(dec);
    if( t != UJ_BOOL )
        uj_error(dec,"Expecting a bool value");
    return dec->unum;
}

sL_t uj_int (ujdec_t* dec) {
    ujtype_t t = uj_nextValue(dec);
    if( t != UJ_SNUM && t != UJ_UNUM )
        uj_error(dec,"Expecting an integer value");
    
    return dec->snum;
}

uL_t uj_uint (ujdec_t* dec) {
    ujtype_t t = uj_nextValue(dec);
    if( t != UJ_UNUM )
        uj_error(dec,"Expecting a positive integer value");
    return dec->unum;
}

double uj_num (ujdec_t* dec) {
    ujtype_t t = uj_nextValue(dec);
    if( t == UJ_SNUM )
        return (double)dec->snum;
    if( t == UJ_UNUM )
        return (double)dec->unum;
    if( t != UJ_FNUM )
        uj_error(dec,"Expecting a number");
    return dec->fnum;
}

char* uj_str (ujdec_t* dec) {
    ujtype_t t = uj_nextValue(dec);
    if( t != UJ_STRING )
        uj_error(dec,"Expecting a string value");
    return dec->str.beg;
}

ujcrc_t uj_keyword (ujdec_t* dec) {
    ujtype_t t = uj_nextValue(dec);
    if( t != UJ_STRING )
        uj_error(dec,"Expecting a string value");
    return dec->str.crc;
}

int uj_hexstr (ujdec_t* dec, u1_t* buf, int bufsiz) {
    ujtype_t t = uj_nextValue(dec);
    if( t != UJ_STRING )
        uj_error(dec,"Expecting a string value with hex digits");
    char* s = dec->str.beg;
    int len = dec->str.len;
    if( (len & 1) != 0 )
        uj_error(dec,"Hex string has odd number of characters");
    if( len/2 > bufsiz )
        uj_error(dec,"Hex string too long: %d bytes, buffer is %d", len/2, bufsiz);
    for( int i=0; i<len; i+=2 ) {
        int b = (rt_hexDigit(s[i])<<4) | rt_hexDigit(s[i+1]);
        if( b < 0 )
            uj_error(dec,"Hex string contains illegal characters: %c%c", s[i], s[i+1]);
        buf[i/2] = b;
    }
    return len/2;
}

uL_t uj_eui (ujdec_t* dec) {
    ujtype_t t = uj_nextValue(dec);
    if( t == UJ_SNUM || t == UJ_UNUM )
        return dec->unum;
    if( t != UJ_STRING )
        uj_error(dec,"Expecting a string value with an EUI");
    char* s = dec->str.beg;
    int len = dec->str.len;
    uL_t eui = 0;
    int i;
    for( i=0; i<len; i+=2 ) {
        int b = (rt_hexDigit(s[i])<<4) | rt_hexDigit(s[i+1]);
        if( b < 0 )
            uj_error(dec,"EUI contains illegal hex characters: %c%c", s[i], s[i+1]);
        i += (s[i+2] == '-');   // - is optional
        eui = (eui << 8) | b;   // fewer/more hex bytes would be ok too
    }
    return eui;
}

//
//  map=0
//  back = 0
//  i = 0
//  for c in 'msgtype':
//      i += 1
//      b = c.encode('ascii')[0]
//      print('1F=%02X  /3=%02X' % (b&0x1F, (b&0x1F)//3))
//      map |= 1<<(b & 0x1F)
//      back |= i << (4*(b & 0xF))
//  print('0x1F map: 0x%X' % map);
//  print('backtrack 16*4: 0x%X' % back);
//
// This function will never fail and use dec->on_err
// It returns 0 if no msgtype is being found
//
ujcrc_t uj_msgtype(ujdec_t* dec) {
    char* s = dec->json_beg-7;
    char* e = dec->json_end;
    while( (s+=7) < e ) {
        int c = s[0];
        if( (c & 0xE0) != 0x60 || ((1 << (c & 0x1F)) & 0x21920A0) == 0 )
            continue;
        char* beg = s - ((0x10005030742006ULL >> ((c&0xF)<<2)) & 0xF);
        if( beg < dec->json_beg || beg + 10 > e )
            continue;
        if( strncmp(beg,"\"msgtype\"", 9) != 0 )
            continue;
        dec->read_pos = beg+9;
        if( skipWsp(dec) != ':' || skipWsp(dec) != '"' ) {
          nomatch:
            s = dec->read_pos-7;
            dec->read_pos = dec->json_beg;
            continue;
        }
        dec->str.beg = dec->read_pos;
        ujcrc_t crc = 0;
        while( (c = nextChar(dec)) != '"' ) {
            if( c==0 || c=='\\' )
                goto nomatch;
            crc = UJ_UPDATE_CRC(crc, c);
        }
        dec->str.len = dec->read_pos - dec->str.beg - 1;
        dec->str.crc = UJ_FINISH_CRC(crc);
        dec->read_pos = dec->json_beg;
        return dec->str.crc;
    }
    return 0;
}


// --------------------------------------------------------------------------------
//
// Encoder
//
// --------------------------------------------------------------------------------

#define snXp(b, fmt, ...) ((b)->pos += snprintf((b)->buf + (b)->pos, (b)->bufsize - (b)->pos, fmt, ##__VA_ARGS__))

static int lastChar (ujbuf_t* b) {
    if( !b->pos )
        return -1;
    return b->buf[b->pos-1];
}

static void addChar (ujbuf_t* b, char c) {
    if( b->pos < b->bufsize )
        b->buf[b->pos++] = c;
}

static void addHex2 (ujbuf_t* b, int v) {
    if( b->pos < b->bufsize )
        b->buf[b->pos++] = "0123456789ABCDEF"[(v>>4)&0xF];
    if( b->pos < b->bufsize )
        b->buf[b->pos++] = "0123456789ABCDEF"[v&0xF];
}

// Add string - n<=0 add string until \0
//            - n>0  at most n chars
void xputs (ujbuf_t* b, const char* s, int n) {
    while( *s && n != 0 ) {
        if( b->pos >= b->bufsize )
            break;
        b->buf[b->pos++] = *s++;
        n--;
    }
}

static const char* B64 = ("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                          "abcdefghijklmnopqrstuvwxyz"
                          "0123456789+/");
static void b64Encode (ujbuf_t* b, const u1_t* d, int len) {
    int di=0, spos=b->pos;
    while( di < len ) {
        u4_t v = ((d[di] << 16) |
                  ((di+1 < len ? d[di+1] : 0) << 8) |
                  ((di+2 < len ? d[di+2] : 0)     ) );
        di += 3;
        addChar(b, B64[(v >> 18) & 0x3f]);
        addChar(b, B64[(v >> 12) & 0x3f]);
        addChar(b, B64[(v >> 6)  & 0x3f]);
        addChar(b, B64[(v >> 0)  & 0x3f]);
    }
    if( di > len ) {
        b->pos = spos + len/3*4 + 4 - (di-len);
        while( --di >= len )
            addChar(b, '=');
    }
}

static void anotherValue(ujbuf_t* b) {
    int c = lastChar(b);
    if( c == -1 || c == ',' || c== ':' || c == '[' || c == '{' )
        return;
    addChar(b, ',');
}

static void anotherString(ujbuf_t* b) {
    if( b->pos >= 2 && b->buf[b->pos-1] == '\b' && b->buf[b->pos-2] == '"' ) {
        b->pos -= 2;
        return;
    }
    anotherValue(b);
    addChar(b, '"');
}

// Ensure termination with \0
int xeos(ujbuf_t* b) {
    if( b->pos < b->bufsize ) {
        b->buf[b->pos] = 0;
        return 1; // ok - no overflow
    }
    // Make sure it's always terminated
    b->buf[b->bufsize-1] = 0;
    return 0;  // overflow
}

// Ensure last char is \n
int xeol(ujbuf_t* b) {
    if( b->pos < b->bufsize ) {
        if( b->pos > 0 && b->buf[b->pos-1] != '\n' )
            b->buf[b->pos++] = '\n';
        return 1; // ok - no overflow
    }
    // Make sure it's always terminated with c
    int i = b->pos = b->bufsize;
    b->buf[i-1] = '\n';
    return 0;  // overflow
}

void uj_mergeStr(ujbuf_t* b) {
    // Next encoded string is merged with previous one
    addChar(b, '\b');
}


void uj_encOpen(ujbuf_t* b, char brace) {
    anotherValue(b);
    addChar(b, brace);
}

void uj_encClose(ujbuf_t* b, char brace) {
    addChar(b, brace);
}

void uj_encNull(ujbuf_t* b) {
    anotherValue(b);
    xputs(b, "null", -1);
}

void uj_encBool(ujbuf_t* b, int val) {
    anotherValue(b);
    xputs(b, val ? "true" : "false", -1);
}

void uj_encInt(ujbuf_t* b, sL_t val) {
    anotherValue(b);
    xprintf(b, "%ld", val);
}

void uj_encUint(ujbuf_t* b, uL_t val) {
    anotherValue(b);
    xprintf(b, "%lu", val);
}

void uj_encNum(ujbuf_t* b, double val) {
    anotherValue(b);
    int n = snprintf(b->buf + b->pos, b->bufsize - b->pos, "%g", val);
    b->pos += n;
}

void uj_encTime(ujbuf_t* b, double val) {
    anotherValue(b);
    int n = snprintf(b->buf + b->pos, b->bufsize - b->pos, "%.6f", val);
    b->pos += n;
}

void uj_encStr (ujbuf_t* b, const char* s) {
    if( s == NULL ) {
        uj_encNull(b);
        return;
    }
    anotherString(b);
    int c;
    while( (c=*s++) && b->pos < b->bufsize ) {
        switch(c) {
        case '\\':
        case '"': break;
        case '\b': c = 'b'; break;
        case '\f': c = 'f'; break;
        case '\n': c = 'n'; break;
        case '\r': c = 'r'; break;
        case '\t': c = 't'; break;
        default:
            if( c >=0 && c < 0x20 ) {
                addChar(b,'\\');
                addChar(b,'u');
                addHex2(b,0);
                addHex2(b,c);
                continue;
            }
            addChar(b, c);
            continue;
        }
        addChar(b, '\\');
        addChar(b, c);
    }
    addChar(b, '"');
}

void uj_encHex(ujbuf_t* b, const u1_t* d, int len) {
    if( d == NULL ) {
        uj_encNull(b);
        return;
    }
    anotherString(b);
    for( int i=0; i<len; i++ )
        addHex2(b, d[i]);
    addChar(b, '"');
}

static void encMac(ujbuf_t* b, uL_t mac) {
    addHex2(b,mac>>40);
    for( int i=40-8; i>=0; i-=8 ) {
        addChar(b,':');
        addHex2(b,mac>>i);
    }
}

void uj_encMac(ujbuf_t* b, uL_t mac) {
    anotherString(b);
    encMac(b, mac);
    addChar(b, '"');
}

static void encDate(ujbuf_t* b, uL_t tm) {
    struct datetime dt = rt_datetime(tm);
    snXp(b, "%04d-%02d-%02d ", dt.year, dt.month, dt.day);
    snXp(b, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
}

void uj_encDate(ujbuf_t* b, uL_t tm) {
    anotherString(b);
    encDate(b, tm);
    addChar(b, '"');
}

static void encEui(ujbuf_t* b, uL_t eui, int nlsb) {
    if( nlsb == 0 || nlsb >= 8 ) { // render all bytes
        addHex2(b,eui>>56);
        nlsb = 7;
    }
    for( int i=nlsb*8-8; i>=0; i-=8 ) {
        addChar(b,'-');
        addHex2(b,eui>>i);
    }
}

void uj_encEui(ujbuf_t* b, uL_t eui) {
    anotherString(b);
    encEui(b, eui, 0);
    addChar(b, '"');
}

static void encId6 (ujbuf_t* b, uL_t eui) {
    u2_t g[4] = { eui>>48, eui>>32, eui>>16, eui };

    if( !g[0] && !g[1] ) {
        if( !g[2] )
            xprintf(b, "::%x", g[3]);
        else
            xprintf(b, "::%x:%x", g[2], g[3]);
        return;
    }
    if( !g[2] && !g[3] ) {
        if( !g[1] )
            xprintf(b, "%x::", g[0]);
        else
            xprintf(b, "%x:%x::", g[0], g[1]);
        return;
    }
    if( !g[1] && !g[2] ) {
        xprintf(b, "%x::%x", g[0], g[3]);
        return;
    }
    xprintf(b, "%x:%x:%x:%x", g[0], g[1], g[2], g[3]);
}

void uj_encId6(ujbuf_t* b, uL_t eui) {
    anotherString(b);
    encId6(b, eui);
    addChar(b, '"');
}


void uj_encKey (ujbuf_t* b, const char* key) {
    uj_encStr(b, key);
    addChar(b, ':');
}

static int encArg(ujbuf_t* b, int type, va_list* args) {
    switch(type) {
    case 'b': uj_encBool(b, va_arg(*args,      int)); break;
    case 'i': uj_encInt (b, va_arg(*args,      int)); break;
    case 'I': uj_encInt (b, va_arg(*args,     sL_t)); break;
    case 'u': uj_encUint(b, va_arg(*args, unsigned)); break;
    case 'U': uj_encUint(b, va_arg(*args,     uL_t)); break;
    case 'D': uj_encDate(b, va_arg(*args,     uL_t)); break;
    case 'g': uj_encNum (b, va_arg(*args,   double)); break;
    case 'T': uj_encTime(b, va_arg(*args,   double)); break;
    case 's': uj_encStr (b, va_arg(*args,    char*)); break;
    case 'E': uj_encEui (b, va_arg(*args,     uL_t)); break;
    case 'M': uj_encMac (b, va_arg(*args,     uL_t)); break;
    case '6': uj_encId6 (b, va_arg(*args,     uL_t)); break;
    case 'H': {
        int dl = va_arg(*args, int);
        const u1_t* d = va_arg(*args, const u1_t*);
        uj_encHex(b, d, dl);
        break;
    }
    default: return 0;
    }
    return 1;
}

void uj_encKV (ujbuf_t* b, const char* key, char type, ...) {
    va_list ap;
    uj_encKey(b, key);
    va_start(ap, type);
    encArg(b, type, &ap);
    va_end(ap);
}

void uj_encKVn (ujbuf_t* b, ...) {
    va_list ap;
    va_start(ap, b);
    do {
        const char* key = va_arg(ap, const char*);
        if( key == NULL )
            break;
        if( key[0] == '}' && key[1] == 0 ) {
            uj_encClose(b, '}');
            continue;
        }
        uj_encKey(b, key);
        int type = va_arg(ap, int);
        if( type == '{' ) {
            uj_encOpen(b, '{');
            continue;
        }
        if( type == '[' ) {
            uj_encOpen(b, '[');
            while(1) {
                type = va_arg(ap, int);
                if( type == ']' ) {
                    uj_encClose(b, ']');
                    break;
                }
                if( !encArg(b, type, &ap) )
                    goto stop;
            }
            continue;
        }
        if( !encArg(b, type, &ap) )
            break;
    } while(1);
 stop:
    va_end(ap);
}

// --------------------------------------------------------------------------------
//
// Special print stuff
//
// --------------------------------------------------------------------------------


#if defined(CFG_surrogate_snprintf_64bit)
// Minihub's snprintf cannot deal with 64bit integers - we provide a surrogate
// impl here which does not cover all formatting features (padding, truncation etc.)
// but which is good enough for now
int surrogate_snprintf_64bit(char* buf, int len, uL_t val, char fmt) {
    if( val == 0 ) {
        if( len > 0 )
            buf[0] = '0';
        return 1;
    }
    if( fmt == 'X' || fmt == 'x' ) {
        int m = 0, n = __builtin_clzll(val) / 4;
        val <<= n*4;
        while( n+m < 16 ) {
            if( m < len )
                buf[m] = "0123456789ABCDEF"[(val>>60)];
            val <<= 4;
            m += 1;
        }
        return m;
    }
    int n = 0;
    if( fmt == 'd' ) {
        sL_t sval = val;
        if( sval < 0 ) {
            if( len > 0 )
                buf[0] = '-';
            buf += 1;
            len -= 1;
            n = 1;
            val = -sval;
        }
    }
    // 3011/10000 >~ log(2)/log(10)
    int di, nd = ((64-__builtin_clzll(val)) * 3011 + /*ceil*/9999) / 10000;
    uL_t v;
 again:
    di = nd-1;
    v = val;
    while( v ) {
        assert(di >= 0);
        if( di < len )
            buf[di] = (v % 10) + '0';
        v /= 10;
        di -= 1;
    }
    if( di >= 0 ) {
        nd -= 1;   // guess was wrong by one digit
        goto again;
    }
    return nd+n;
}
#endif // defined(CFG_surrogate_snprintf_64bit)


static const char* TSPAN_UNITS_NAME[] = {
    "d","h","m","s","ms","us", NULL
};

static const ustime_t TSPAN_UNITS_VAL[] = {
    rt_seconds(24*3600), // d
    rt_seconds(3600),    // h
    rt_seconds(60),      // m
    1000000,  // s
    1000,     // ms
    1,        // us
    0         // EOL
};

static void approxTimeSpan (ujbuf_t* b, ustime_t span) {
    if( span == 0 ) {
        addChar(b, '0');
        return;
    }
    if( span < 0 ) {
        addChar(b, '-');
        span = -span;
    }
    int k = 0;
    int ui=0;
    while( span < TSPAN_UNITS_VAL[ui] ) ui++;
    do {
        int v = span / TSPAN_UNITS_VAL[ui+k];
        snXp(b, "%d%s", v, TSPAN_UNITS_NAME[ui+k]);
        span %= TSPAN_UNITS_VAL[ui];
        ++k;
    } while ( k < 2 && span != 0 && TSPAN_UNITS_VAL[ui+k] != 0 );
}


static void hexData(ujbuf_t* b, const u1_t* d, int len, int leftbytes, int rightbytes) {
    int dbeg, dend;
    if( (leftbytes || rightbytes) &&  leftbytes+rightbytes < len ) {
        dbeg = leftbytes;
        dend = len-rightbytes;
    } else {
        dbeg = dend = -1;
    }
    for( int i=0; i<len; i++ ) {
        if( i == dbeg ) {
            addChar(b, '.');
            addChar(b, '.');
            i = dend-1;
        } else {
            addHex2(b, d[i]);
        }
    }
}


int xprintf(ujbuf_t* b, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ovfl = vxprintf(b, fmt, ap);
    va_end(ap);
    return ovfl;
}


static void padField (ujbuf_t* b, ujoff_t beg, int width, int padding) {
    int w = b->pos - beg;
    int d = width - w;
    if( d <= 0 || b->pos + d > b->bufsize ) return;
    int p1, p2;
    /**/ if( padding == '^' ) { p1 = d/2; p2 = d-p1; } // centered
    else if( padding == '>' ) { p1 = d;   p2 = 0;    } // right aligned
    else /* '<' || '-' */     { p1 = 0;   p2 = d;    } // left aligned
    if( p1 ) {
        memmove(&b->buf[beg+p1], &b->buf[beg], w);
        for( int i=0; i<p1; i++ )
            b->buf[beg+i] = ' ';
        b->pos += p1;
    }
    if( p2 ) {
        for( int i=0; i<p2; i++ )
            b->buf[b->pos+i] = ' ';
        b->pos += p2;
    }
}


// Max size of a format element passed along to snprintf
enum { MAX_FMT_SIZE = 16 };

int vxprintf(ujbuf_t* b, const char* fmt, va_list args) {
    if( b == NULL ) {
        return 0;
    }
    if( b->pos >= b->bufsize ) {
        return xeos(b);
    }
    while( 1 ) {
        int c = *fmt++;
        if( c != '%' ) {
            if( c == 0 )
                break;
            addChar(b, c);
            continue;
        }
        if( fmt[0] == '%' ) {
            fmt++;
        literalPercent:
            addChar(b, '%');
            continue;
        }
        u1_t fmtoff=-1, stars=0;
        char extra=0;
        int width=0, frac=0;
        int* pi = &width;
        char longFlag=0, padding=0;

        while(1) {
            c = fmt[++fmtoff];
            if( c == 0 || fmtoff >= MAX_FMT_SIZE )
                goto literalPercent;
            switch(c) {
            default: {
                extra = c;
                break;
            }
            case '*':
                stars |= (pi==&width ? 2 : 1);
                *pi = va_arg(args, int);
                break;

            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9': {
                *pi = *pi*10 + c - '0';
                break;
            }
            case '<':
            case '>':
            case '^':
            case '-': {
                padding = c;
                break;
            }
            case '.': {
                pi = &frac;
                break;
            }
            case 'l': {
                longFlag += 1;
                break;
            }
            case 'c':
            case 'd':
            case 'u':
            case 'x':
            case 'X':
            case 'f':
            case 'g':
            case 's':
            case 'p': {
                
                
                char fmt2[MAX_FMT_SIZE+3];
                memcpy(fmt2, fmt-1, fmtoff+2);
                if( sizeof(long) == 4 && fmt2[fmtoff] == 'l' ) {
                    // Portable code uses %..ld to pop u8 from stack but this platform
                    // would just pop a long (4 bytes) - fix this by rewriting format element to %..lld.
                    fmt2[fmtoff+1] = 'l';
                    fmt2[fmtoff+2] = c;
                    fmt2[fmtoff+3] = 0;
                } else {
                    fmt2[fmtoff+2] = 0;
                }
                char* pb = b->buf + b->pos;
                int n = 0, bl = b->bufsize - b->pos;
                switch(c) {
                case 'c':
                case 'd':
                case 'u':
                case 'x':
                case 'X': {
                    if( longFlag ) {
#if defined(CFG_surrogate_snprintf_64bit)
                        n = surrogate_snprintf_64bit(pb, bl, va_arg(args, uL_t), c);
#else
                        n = snprintf(pb, bl, fmt2, va_arg(args, uL_t));
#endif
                    } else {
                        n = snprintf(pb, bl, fmt2, va_arg(args, int));
                    }
                    break;
                }
                case 'p': {
                    n = snprintf(pb, bl, fmt2, va_arg(args, void*));
                    break;
                }
                case 's': {
                    if( stars == 3 ) {
                        const char* p = va_arg(args, const char*);
                        n = snprintf(pb, bl, fmt2, width, frac, p);
                    }
                    else if( stars ) {
                        const char* p = va_arg(args, const char*);
                        n = snprintf(pb, bl, fmt2, stars==2 ? width : frac, p);
                    }
                    else {
                        n = snprintf(pb, bl, fmt2, va_arg(args, const char*));
                    }
                    break;
                }
                case 'f':
                case 'g': {
                    n = snprintf(pb, bl, fmt2, va_arg(args, double));
                    break;
                }
                }
                b->pos += max(0,n);
                goto doneElem;
            }
            case 'H': {
                int n = va_arg(args, int);
                const u1_t* p = va_arg(args, const u1_t*);
                hexData(b, p, n, width, frac);
                goto doneElem;
            }
            case 'B': {
                int n = va_arg(args, int);
                const u1_t* p = va_arg(args, const u1_t*);
                b64Encode(b, p, n);
                goto doneElem;
            }
            case 'M': {
                uL_t mac = va_arg(args, uL_t);
                encMac(b, mac);
                goto doneElem;
            }
            case 'E': {
                uL_t eui = va_arg(args, uL_t);
                if( extra == ':' )
                    encId6(b, eui);
                else
                    encEui(b, eui, frac);
                goto doneElem;
            }
            case 'T': {
                ustime_t tm = va_arg(args, ustime_t);
                if( extra == '~' ) {
                    doff_t beg = b->pos;
                    approxTimeSpan(b, tm);
                    padField(b, beg, width, padding);
                    goto doneElem;
                }
                struct datetime dt = rt_datetime(tm);
                if( padding != '>' )
                    snXp(b, "%04d-%02d-%02d", dt.year, dt.month, dt.day);
                if( padding == 0 )
                    addChar(b, extra==0 ? ' ' : extra);
                if( padding != '<' ) {
                    snXp(b, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
                    if( frac ) {
                        addChar(b, '.');
                        while( --frac >= 0 ) {
                            addChar(b, '0' + (dt.usec / 100000 % 10));
                            dt.usec = (dt.usec % 100000) * 10;
                        }
                    }
                }
                goto doneElem;
            }
            case 'F': {
                unsigned freq = va_arg(args, unsigned);
                unsigned rem = freq%1000000;
                if( frac == 0 ) {
                    frac = 6;
                    while( frac > 0 && rem % 10 == 0 ) {
                        frac -= 1;
                        rem /= 10;
                    }
                }
                snXp(b, "%*.*f", width, max(1,frac), freq/1e6);
                if( extra != '~' )
                    xputs(b, "MHz", -1);
                goto doneElem;
            }
            case 'R': {
                doff_t beg = b->pos;
                int rps = va_arg(args, int);
                if( (rps&7) == 7 || (rps&0x18) == 0x18 ) {
                    xputs(b, "SF??", -1);
                }
                else if( (rps&7) == 6 ) {
                    xputs(b, "FSK", -1);
                }
                else {
                    u1_t sf = 12-(rps&7);
                    u2_t bw = 125 * "\x01\x02\x04\x00"[(rps>>3)&3];
                    snXp(b, "SF%d/BW%d", sf, bw);
                }
                padField(b, beg, width, padding);
                goto doneElem;
            }
            case 'J': {
                txjob_t* txjob = va_arg(args, txjob_t*);
                encId6(b, txjob->deveui);
                xprintf(b, " diid=%ld [ant#%d]", txjob->diid, txjob->txunit);
                goto doneElem;
            }
            } // switch
        } // while(1)
    doneElem:
        fmt += fmtoff+1;
    }
    return xeos(b);
}
