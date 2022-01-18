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

#include <stdio.h>
#include "selftests.h"
#include "uj.h"
#include "kwcrc.h"

#define BUFSZ (2*1024)
static char* jsonbuf;

#define iniDecoder(D, S) (strcpy(jsonbuf, (S)), uj_iniDecoder(D, jsonbuf, strlen(jsonbuf)))

static const char* E0  = " \t\r\n ";
static const char* E1  = " nuxx ";
static const char* E2  = " -? ";
static const char* E3  = " 0. ";
static const char* E4  = " 1e ";
static const char* E5  = " 1e+ ";
static const char* E6  = " 1.2e+ ";
static const char* E7  = " \" ";
static const char* E8  = " \"\\x ";
static const char* E9  = " \"\\uA";
static const char* E10 = " rubbish ";
static const char* EA  = " 123 garbage ";
static const char* EB  = " true ";
static const char* EC  = " \"Ax-\" ";

#define Texpr(Ex, expr)                                 \
    iniDecoder(&D, Ex);                                 \
    if( !uj_decode(&D) ) {                              \
        expr;                                           \
        TFAIL(#Ex " did not fail as expected");         \
    }                                                   \
    fprintf(stderr, #Ex " failed as expected\n");

#define Tnextvalue(Ex) Texpr(Ex, uj_nextValue(&D))
#define Teui(Ex)       Texpr(Ex, uj_eui(&D))


static void test_simple_errors() {
    ujdec_t D;
    ujtype_t t;

    Tnextvalue(E0);
    Tnextvalue(E1);
    Tnextvalue(E2);
    Tnextvalue(E3);
    Tnextvalue(E4);
    Tnextvalue(E5);
    Tnextvalue(E6);
    Tnextvalue(E7);
    Tnextvalue(E8);
    Tnextvalue(E9);
    Tnextvalue(E10);

    iniDecoder(&D, EA);
    if( !uj_decode(&D) ) {
        t = uj_nextValue(&D);
        TCHECK(t == UJ_UNUM);
        t = uj_nextValue(&D);
        TCHECK(t == UJ_UNUM);
        uj_assertEOF(&D);
        TFAIL("EA did not fail as expected");    // LCOV_EXCL_LINE
    }
    fprintf(stderr, "EA failed as expected\n");

    Teui(EB);
    Teui(EC);
}


static const char* T1 = " 123 ";
static const char* T2 = " -123 ";
static const char* T3 = " true ";
static const char* T4 = " false ";
static const char* T5 = " null ";
static const char* T6 = "\r\n\t 123.456 \r\n\t";
static const char* T7 = " 123e3 ";
static const char* T8 = " 125e-2 ";
static const char* T9 = " 128e+2 ";
static const char* T10= " -125.68e-2 ";
static const char* T11= " -0.00128e+4 ";
static const char* T12= " \" \\\\-\\b-\\f-\\t-\\n-\\r-\\\"-\\/-\\u0041\\u00c2\\u00bf\\uc2bf \" ";
static const char* T13= "  7654321 ";
static const char* T14= " -7654321 ";
static const char* T15= " \"91-A2-B3-C4-D5-E6-F7-08\" ";
static const char* T16= " \"AA-BB\" ";
static const char* T17= " \"AA-BB-AA-BB-AA-BB-AA-BB-00\" ";

#define SETUP(Tx)                             \
    iniDecoder(&D, Tx);                       \
    if( uj_decode(&D) ) TCHECK(#Tx "failed"); \
    t = uj_nextValue(&D);                     \
    fprintf(stderr, #Tx " JSON parsing ok\n");


static void test_simple_values() {
    ujdec_t D;
    ujtype_t t;

    SETUP(T1);
    TCHECK(t == UJ_UNUM);
    TCHECK(D.unum == 123);
    uj_assertEOF(&D);

    SETUP(T2);
    TCHECK(t == UJ_SNUM);
    TCHECK(D.snum == -123);
    uj_assertEOF(&D);

    SETUP(T3);
    TCHECK(t == UJ_BOOL);
    TCHECK(D.snum == 1);
    uj_assertEOF(&D);

    SETUP(T4);
    TCHECK(t == UJ_BOOL);
    TCHECK(D.snum == 0);
    uj_assertEOF(&D);

    SETUP(T5);
    TCHECK(t == UJ_NULL);
    TCHECK(D.snum == 0);
    uj_assertEOF(&D);

    SETUP(T6);
    TCHECK(t == UJ_FNUM);
    TCHECK(abs(D.fnum -123.456) < 1e-13);
    uj_assertEOF(&D);

    SETUP(T7);
    TCHECK(t == UJ_FNUM);
    TCHECK(D.fnum == 123000.0);
    uj_assertEOF(&D);

    SETUP(T8);
    TCHECK(t == UJ_FNUM);
    TCHECK(abs(D.fnum - 1.25) < 1e-13);
    uj_assertEOF(&D);

    SETUP(T9);
    TCHECK(t == UJ_FNUM);
    TCHECK(D.fnum == 12800);
    uj_assertEOF(&D);

    SETUP(T10);
    TCHECK(t == UJ_FNUM);
    TCHECK(abs(D.fnum + 1.2568) < 1e13);
    uj_assertEOF(&D);

    SETUP(T11);
    TCHECK(t == UJ_FNUM);
    TCHECK(abs(D.fnum + 12.8) < 1e13);
    uj_assertEOF(&D);

    SETUP(T12);
    TCHECK(t == UJ_STRING);
                                                    // \u00c2  \u00bf  \uc2bf
    TCHECK(strcmp(D.str.beg, " \\-\b-\f-\t-\n-\r-\"-/-A\xc3\x82\xc2\xbf\xec\x8a\xbf ")==0);
    uj_assertEOF(&D);

    SETUP(T13);
    TCHECK(t == UJ_UNUM);
    TCHECK(uj_eui(&D) == 7654321);
    uj_assertEOF(&D);

    SETUP(T14);
    TCHECK(t == UJ_SNUM);
    TCHECK(uj_eui(&D) == (uL_t)-7654321);
    uj_assertEOF(&D);

    SETUP(T15);
    TCHECK(t == UJ_STRING);
    TCHECK(uj_eui(&D) == (uL_t)0x91A2B3C4D5E6F708);
    uj_assertEOF(&D);

    SETUP(T16);
    TCHECK(t == UJ_STRING);
    TCHECK(uj_eui(&D) == (uL_t)0xAABB);
    uj_assertEOF(&D);

    SETUP(T17);
    TCHECK(t == UJ_STRING);
    TCHECK(uj_eui(&D) == (uL_t)0xBBAABBAABBAABB00);
    uj_assertEOF(&D);
}
#undef SETUP




static const char* S2 = "{ \
   \"AS923\": 123,         \
   \"EU868\": true         \
}";

static void test_S2() {
    ujdec_t D;
    ujtype_t t;
    ujcrc_t c;

    iniDecoder(&D, S2);
    if( uj_decode(&D) )
        TFAIL("S2 failed");     // LCOV_EXCL_LINE
    t = uj_nextValue(&D);
    TCHECK(t == UJ_OBJECT);
    uj_enterObject(&D);
    while( (c = uj_nextField(&D)) ) {
        switch(c) {
        case J_AS923: {
            TCHECK( UJ_UNUM == uj_nextValue(&D) );
            break;
        }
        case J_EU868: {
            TCHECK( UJ_BOOL == uj_nextValue(&D) );
            break;
        }
        default:                              // LCOV_EXCL_LINE
            TFAIL("Unexpected field crc");    // LCOV_EXCL_LINE
        }
    }
    uj_exitObject(&D);

    fprintf(stderr, "S2 ok\n");
}


static char* S3 = "[ \
   \"AS923\", 123, \
   \"EU868\", true \
]";

static void test_S3() {
    ujdec_t D;
    ujtype_t t;
    int i, idx = 0;

    iniDecoder(&D, S3);
    if( uj_decode(&D) )
        TFAIL("S3 failed");    // LCOV_EXCL_LINE
    t = uj_nextValue(&D);
    TCHECK(t == UJ_ARRAY);
    uj_enterArray(&D);
    while( (i = uj_nextSlot(&D)) >= 0 ) {
        TCHECK(i==idx);
        idx++;

        switch(i) {
        case 0: {
            TCHECK( UJ_STRING == uj_nextValue(&D) && D.str.crc == J_AS923 );
            break;
        }
        case 1: {
            TCHECK( UJ_UNUM == uj_nextValue(&D) );
            break;
        }
        case 2: {
            TCHECK( UJ_STRING == uj_nextValue(&D) && D.str.crc == J_EU868 );
            break;
        }
        case 3: {
            TCHECK( UJ_BOOL == uj_nextValue(&D) );
            break;
        }
        default:                                // LCOV_EXCL_LINE
            TFAIL("Unexpected array index");    // LCOV_EXCL_LINE
        }
    }
    uj_exitArray(&D);

    fprintf(stderr, "S3 ok\n");
}


static const char* F11 = ("[[[[[[[[[[[[[[[[[[[[[[["
                          "]]]]]]]]]]]]]]]]]]]]]]]");

static const char* F12 = ("[[[["
                          "]]]]");

static int test_F1x(const char* F1x) {
    ujdec_t D;
    ujtype_t t;
    int i, level = 0;

    iniDecoder(&D, F1x);
    if( uj_decode(&D) )
        return 0; // fail
    goto start;
    while(1) {
        i = uj_nextSlot(&D);
        TCHECK(i==0 || i==-1);
        if( i==-1 ) {
            uj_exitArray(&D);
            level -= 1;
            if( level == 0 )
                break;
        } else {
        start:
            t = uj_nextValue(&D);
            TCHECK(t == UJ_ARRAY);
            uj_enterArray(&D);
            level += 1;
        }
    }
    uj_assertEOF(&D);
    return 1;  // success
}

static void test_F11() {
    if( test_F1x(F11) ) {
        TFAIL("F11 did not fail");    // LCOV_EXCL_LINE
    } else {
        fprintf(stderr, "F11 ok\n");
    }
}

static void test_F12() {
    if( test_F1x(F12) ) {
        fprintf(stderr, "F12 ok\n");
    } else {
        TFAIL("F12 did not fail");    // LCOV_EXCL_LINE
    }
}

static void test_F13() {
    if( test_F1x("[[] 123 ]") ) {
        TFAIL("F13 did not fail");    // LCOV_EXCL_LINE
    } else {
        fprintf(stderr, "F13 ok\n");
    }
}


static const char* F21 = ("{\"a\":{\"b\":{\"c\":{\"d\":"
                          "{\"a\":{\"b\":{\"c\":{\"d\":"
                          "{\"a\":{\"b\":{\"c\":{\"d\":"
                          "{\"a\":{\"b\":{\"c\":{\"d\":{}"
                          "}      }      }      }"
                          "}      }      }      }"
                          "}      }      }      }"
                          "}      }      }      }");

static const char* F22 = ("{\"a\":{\"b\":{\"c\":{\"d\":{"
                          "}      }      }      }      }");

static int test_F2x(const char* F2x) {
    ujdec_t D;
    ujtype_t t;
    int i, level = 0;

    iniDecoder(&D, F2x);
    if( uj_decode(&D) )
        return 0; // fail
    goto start;
    while(1) {
        i = uj_nextField(&D);
        if( i==0 ) {
            uj_exitObject(&D);
            level -= 1;
            if( level == 0 )
                break;
        } else {
        start:
            t = uj_nextValue(&D);
            TCHECK(t == UJ_OBJECT);
            uj_enterObject(&D);
            level += 1;
        }
    }
    uj_assertEOF(&D);
    return 1;  // success
}

static void test_F21() {
    if( test_F2x(F21) ) {
        TFAIL("F21 did not fail");    // LCOV_EXCL_LINE
    } else {
        fprintf(stderr, "F21 ok\n");
    }
}

static void test_F22() {
    if( test_F2x(F22) ) {
        fprintf(stderr, "F22 ok\n");
    } else {
        TFAIL("F22 did not fail");    // LCOV_EXCL_LINE
    }
}

static void test_F23() {
    if( test_F2x("{ \"a\": { \"b\":{} } \"c\":{} }") ) {
        TFAIL("F23 did not fail");    // LCOV_EXCL_LINE
    } else {
        fprintf(stderr, "F23 ok\n");
    }
}

static void test_F24() {
    if( test_F2x("{ \"a\": { \"b\":{ 123 } } \"c\":{} }") ) {
        TFAIL("F24 did not fail");    // LCOV_EXCL_LINE
    } else {
        fprintf(stderr, "F24 ok\n");
    }
}

static void test_F25() {
    if( test_F2x("{ \"a\": { \"b\" {}, \"c\":{} }") ) {
        TFAIL("F25 did not fail");    // LCOV_EXCL_LINE
    } else {
        fprintf(stderr, "F25 ok\n");
    }
}

static void test_specials() {
    ujdec_t D;

    iniDecoder(&D, " []");
    if( !uj_decode(&D) ) {
        D.type = UJ_OBJECT;
        uj_enterObject(&D);
        TFAIL("specials 1");   // LCOV_EXCL_LINE
    }
    iniDecoder(&D, " {}");
    if( !uj_decode(&D) ) {
        D.type = UJ_ARRAY;
        uj_enterArray(&D);
        TFAIL("specials 2");   // LCOV_EXCL_LINE
    }

    iniDecoder(&D, " []");
    if( !uj_decode(&D) ) {
        uj_nextValue(&D);
        uj_enterArray(&D);
        uj_exitObject(&D);
        TFAIL("specials 3");   // LCOV_EXCL_LINE
    }

    iniDecoder(&D, " {]");
    if( !uj_decode(&D) ) {
        uj_nextValue(&D);
        uj_enterObject(&D);
        uj_exitObject(&D);
        TFAIL("specials 4");   // LCOV_EXCL_LINE
    }

    fprintf(stderr, "Specials ok\n");
}


static void test_sugar() {
    ujdec_t D;

    // ---------- uj_null
    iniDecoder(&D,"null");
    if( uj_decode(&D) )
        TFAIL("G1");            // LCOV_EXCL_LINE
    TCHECK(1 == uj_null(&D));

    iniDecoder(&D,"false");
    if( uj_decode(&D) )
        TFAIL("G2");            // LCOV_EXCL_LINE
    TCHECK(0 == uj_null(&D));

    // ---------- uj_bool
    iniDecoder(&D,"null");
    if( !uj_decode(&D) ) {
        uj_bool(&D);
        TFAIL("G3");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"true");
    if( uj_decode(&D) )
        TFAIL("G4");            // LCOV_EXCL_LINE
    TCHECK(1 == uj_bool(&D));

    // ---------- uj_int
    iniDecoder(&D,"null");
    if( !uj_decode(&D) ) {
        uj_int(&D);
        TFAIL("G5");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"-1024");
    if( uj_decode(&D) )
        TFAIL("G6");            // LCOV_EXCL_LINE
    TCHECK(-1024 == uj_int(&D));

    // ---------- uj_uint
    iniDecoder(&D,"null");
    if( !uj_decode(&D) ) {
        uj_uint(&D);
        TFAIL("G7");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"1024");
    if( uj_decode(&D) )
        TFAIL("G8");            // LCOV_EXCL_LINE
    TCHECK(1024 == uj_uint(&D));

    // ---------- uj_num
    iniDecoder(&D,"null");
    if( !uj_decode(&D) ) {
        uj_num(&D);
        TFAIL("G9");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"-1024");
    if( uj_decode(&D) )
        TFAIL("G10");            // LCOV_EXCL_LINE
    TCHECK(-1024.0 == uj_num(&D));
    iniDecoder(&D,"1024");
    if( uj_decode(&D) )
        TFAIL("G11");            // LCOV_EXCL_LINE
    TCHECK(1024.0 == uj_num(&D));
    iniDecoder(&D,"-1.024e3");
    if( uj_decode(&D) )
        TFAIL("G12");            // LCOV_EXCL_LINE
    TCHECK(-1024.0 == uj_num(&D));

    // ---------- uj_str
    iniDecoder(&D,"null");
    if( !uj_decode(&D) ) {
        uj_str(&D);
        TFAIL("G13");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"\"abc\"");
    if( uj_decode(&D) )
        TFAIL("G14");            // LCOV_EXCL_LINE
    TCHECK(strcmp("abc", uj_str(&D))==0);

    // ---------- uj_keyword
    iniDecoder(&D,"null");
    if( !uj_decode(&D) ) {
        uj_keyword(&D);
        TFAIL("G15");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"\"EU868\"");
    if( uj_decode(&D) )
        TFAIL("G16");            // LCOV_EXCL_LINE
    TCHECK(J_EU868 == uj_keyword(&D));

    // ---------- uj_hexstr
    u1_t buf[6];
    iniDecoder(&D,"null");
    if( !uj_decode(&D) ) {
        uj_hexstr(&D, buf, sizeof(buf));
        TFAIL("G17");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"\"013\"");
    if( !uj_decode(&D) ) {
        uj_hexstr(&D, buf, sizeof(buf));
        TFAIL("G18");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"\"013G\"");
    if( !uj_decode(&D) ) {
        uj_hexstr(&D, buf, sizeof(buf));
        TFAIL("G19");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"\"010203040506070809\"");
    if( !uj_decode(&D) ) {
        uj_hexstr(&D, buf, sizeof(buf));
        TFAIL("G20");            // LCOV_EXCL_LINE
    }
    iniDecoder(&D,"\"41424300\"");
    if( uj_decode(&D) )
        TFAIL("G21");            // LCOV_EXCL_LINE
    TCHECK(4 == uj_hexstr(&D, buf, sizeof(buf)));
    TCHECK(strcmp((const char*)buf, "ABC") == 0);

    // ---------- uj_msgtype
    iniDecoder(&D,"null");
    if( uj_decode(&D) )
        TFAIL("G30");            // LCOV_EXCL_LINE
    TCHECK(0 == uj_msgtype(&D));
    iniDecoder(&D,"{\"msgtype\":\"EU868\"}");
    if( uj_decode(&D) )
        TFAIL("G31");            // LCOV_EXCL_LINE
    TCHECK(J_EU868 == uj_msgtype(&D));
    iniDecoder(&D,"{\"msgtyp\":\"msgtyp\"}");
    if( uj_decode(&D) )
        TFAIL("G31");            // LCOV_EXCL_LINE
    TCHECK(0 == uj_msgtype(&D));
    iniDecoder(&D,"[\"msgtype\",\"msgtype\"]");
    if( uj_decode(&D) )
        TFAIL("G31");            // LCOV_EXCL_LINE
    TCHECK(0 == uj_msgtype(&D));
    iniDecoder(&D,"{\"msgtype\":\"EU\\\"\",\"msgtype\":\"EU868\"}");
    if( uj_decode(&D) )
        TFAIL("G31");            // LCOV_EXCL_LINE
    TCHECK(J_EU868 == uj_msgtype(&D));

}


static void test_skip() {
    ujdec_t D;

    iniDecoder(&D, "{\"a\": {\"x1\":{\"x2\":[],\"x3\":{}},\"\":[0,\"zzz\",true]},\"b\":1}");
    if( uj_decode(&D) )
        TFAIL("skip");            // LCOV_EXCL_LINE
    uj_enterObject(&D);
    TCHECK(uj_nextField(&D));
    TCHECK(strcmp(D.field.name,"a")==0);
    ujbuf_t buf = uj_skipValue(&D);
    TCHECK(strncmp(buf.buf, "{\"x1\":{\"x2\":[],\"x3\":{}},\"\":[0,\"zzz\",true]}", buf.bufsize)==0);
    TCHECK(uj_nextField(&D));
    TCHECK(strcmp(D.field.name,"b")==0);
    uj_skipValue(&D);
    uj_exitObject(&D);
    uj_assertEOF(&D);
}


static void test_comment() {
    ujdec_t D;

    iniDecoder(&D, "{\"a\": /false");
    if( !uj_decode(&D) ) {
        uj_enterObject(&D);
        TCHECK(uj_nextField(&D));
        TCHECK(uj_nextValue(&D));
        TFAIL("commnt success");            // LCOV_EXCL_LINE
    }

    iniDecoder(&D, "{\"a\": /*C*O*M*M*E*N*T*");
    if( !uj_decode(&D) ) {
        uj_enterObject(&D);
        TCHECK(uj_nextField(&D));
        TCHECK(uj_nextValue(&D));
        TFAIL("commnt success");            // LCOV_EXCL_LINE
    }

    iniDecoder(&D, "{\"a\": /*C*O*M*M*E*N*T*\n");
    if( !uj_decode(&D) ) {
        uj_enterObject(&D);
        TCHECK(uj_nextField(&D));
        TCHECK(uj_nextValue(&D));
        TFAIL("commnt success");            // LCOV_EXCL_LINE
    }

    iniDecoder(&D, "{\"a\": /*C*O*M*M*E*N*T*/ {\"x1\":{\"x2\":[],\"x3\":{}},\"\":[0,\"zzz\",true]} /**/,/*  */\"b\":1}");
    if( uj_decode(&D) )
        TFAIL("skip");            // LCOV_EXCL_LINE
    uj_enterObject(&D);
    TCHECK(uj_nextField(&D));
    TCHECK(strcmp(D.field.name,"a")==0);
    uj_skipValue(&D);
    TCHECK(uj_nextField(&D));
    TCHECK(strcmp(D.field.name,"b")==0);
    uj_skipValue(&D);
    uj_exitObject(&D);
    uj_assertEOF(&D);
}


static void test_indexedField_intRange() {
    ujdec_t D;

    iniDecoder(&D, "[0]");
    if( uj_decode(&D) ) {
        TFAIL("indexedField 1");   // LCOV_EXCL_LINE
    }
    uj_enterArray(&D);
    uj_nextSlot(&D);
    TCHECK(-1 == uj_indexedField(&D, ""));

    iniDecoder(&D, "{\"abc123\":1}");
    if( uj_decode(&D) ) {
        TFAIL("indexedField 2");   // LCOV_EXCL_LINE
    }
    uj_enterObject(&D);
    uj_nextField(&D);
    uj_nextValue(&D);
    TCHECK(-1 == uj_indexedField(&D, "ABC"));
    TCHECK(123 == uj_indexedField(&D, "abc"));

    iniDecoder(&D, "1");
    if( !uj_decode(&D) ) {
        TCHECK(1 == uj_intRange(&D, -1,1));
        TCHECK(1 == uj_intRange(&D, 1,2));
        TCHECK(1 == uj_intRangeOr(&D, 10,11,1));
        uj_intRange(&D, -1,0);
        TFAIL("intRange 1");   // LCOV_EXCL_LINE
    }
    iniDecoder(&D, "1");
    if( !uj_decode(&D) ) {
        uj_intRangeOr(&D, 10,20,0);
        TFAIL("intRange 1");   // LCOV_EXCL_LINE
    }
}


void selftest_ujdec () {
    jsonbuf = rt_mallocN(char, BUFSZ);

    test_simple_errors();
    test_simple_values();
    test_S2();
    test_S3();
    test_F11();
    test_F12();
    test_F13();
    test_F21();
    test_F22();
    test_F23();
    test_F24();
    test_F25();
    test_sugar();
    test_specials();
    test_skip();
    test_comment();
    test_indexedField_intRange();

    free(jsonbuf);
}
