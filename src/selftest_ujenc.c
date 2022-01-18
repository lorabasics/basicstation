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

#include "selftests.h"
#include "uj.h"

#define BUFSZ (2*1024)


static void test_simple_values() {
    char* jsonbuf = rt_mallocN(char, BUFSZ);

    ujbuf_t B = { .buf = jsonbuf, .bufsize = BUFSZ, .pos = 0 };
    const char* T;

    for( int k=1; k<=3; k++ ) {
        B.pos = 0;
        for( int i=0; i<k; i++ )
            uj_encOpen(&B, '[');
        for( int i=0; i<k; i++ )
            uj_encClose(&B, ']');
        TCHECK(xeos(&B) == 1 );
        T = "[[[]]]";
        TCHECK(strncmp(T+3-k, B.buf, 2*k) == 0 && B.pos == 2*k);
    }

    B.pos = 0;
    uj_encOpen(&B, '[');
    uj_encNull(&B);
    uj_encBool(&B,  0);
    uj_encBool(&B,  1);
    uj_encInt (&B, -1);
    uj_encUint(&B,  1);
    uj_encNum (&B,  1.5);
    uj_encTime(&B, 21.5);
    uj_encDate(&B, 1451649600L*1000000LL);
    uj_encStr (&B,  "-\"\\\b\f\n\r\t\x01\x02\xc2\xbf-");
    uj_encHex (&B,  (const u1_t*)"ABC", 3);
    uj_encMac (&B,  0x1A2B3C4DA1B2C3D4);
    uj_encEui (&B,  0x91A2B3C4D5E6F708);
    uj_encId6 (&B,  0x0000000000000000);
    uj_encId6 (&B,  0x0000000000000001);
    uj_encId6 (&B,  0x0000000000020001);
    uj_encId6 (&B,  0x0004000000000001);
    uj_encId6 (&B,  0x0004000300000000);
    uj_encId6 (&B,  0x0004000000000000);
    uj_encId6 (&B,  0x0000000300020000);
    uj_encClose(&B, ']');
    TCHECK(xeos(&B) == 1 );
    T = "[null,false,true,-1,1,1.5,21.500000,\"2016-01-01 12:00:00\","
        "\"-\\\"\\\\\\b\\f\\n\\r\\t\\u0001\\u0002\xc2\xbf-\","
        "\"414243\","
        "\"3C:4D:A1:B2:C3:D4\","
        "\"91-A2-B3-C4-D5-E6-F7-08\","
        "\"::0\",\"::1\",\"::2:1\",\"4::1\",\"4:3::\",\"4::\",\"0:3:2:0\""
        "]";
    TCHECK(strcmp(T, B.buf) == 0);

    B.pos = 0;
    uj_encOpen (&B, '{');
    uj_encKey  (&B, "msgtype");
    uj_encOpen (&B, '[');
    uj_encStr  (&B, "A" ); uj_mergeStr(&B);
    uj_encStr  (&B, ""  ); uj_mergeStr(&B);
    uj_encStr  (&B, "BC"); uj_mergeStr(&B);
    uj_encStr  (&B, "DE");
    uj_encClose(&B, ']');
    uj_encKey  (&B, "data");
    uj_encOpen (&B, '[');
    uj_encStr  (&B,  NULL);
    uj_encHex  (&B,  NULL, 0);
    uj_encClose(&B, ']');
    uj_encClose(&B, '}');
    TCHECK(xeos(&B) == 1 );
    T = "{\"msgtype\":[\"ABCDE\"],\"data\":[null,null]}";
    TCHECK(strcmp(T, B.buf) == 0);


    B.pos = 0;
    uj_encOpen (&B, '{');
    uj_encKV(&B, "A", 'b', 1);
    uj_encKVn(&B,
              "B", 'i',       -1,
              "C", 'I', (sL_t)-1,
              "D", 'u',        1U,
              "E", 'U', (uL_t) 1,
              "G1",'g',  1.25,
              "G2",'T', 21.25,
              "D", 'D', 1451649600L*1000000LL,
              "F", 's', "abc",
              "G", 'H', 3, (const u1_t*)"ABC",
              "M", 'M', 0x1A2B3C4DA1B2C3D4,
              "H", 'E', 0x91A2B3C4D5E6F708,
              "I", '6', (uL_t)0xB000A,
              NULL);
    uj_encClose(&B, '}');
    TCHECK(xeos(&B) == 1 );
    T = "{\"A\":true,\"B\":-1,\"C\":-1,\"D\":1,\"E\":1,\"G1\":1.25,\"G2\":21.250000,\"D\":\"2016-01-01 12:00:00\","
        "\"F\":\"abc\",\"G\":\"414243\",\"M\":\"3C:4D:A1:B2:C3:D4\",\"H\":\"91-A2-B3-C4-D5-E6-F7-08\",\"I\":\"::b:a\"}";
    TCHECK(strcmp(T, B.buf) == 0);

    B.pos = 0;
    uj_encOpen (&B, '{');
    uj_encKVn(&B,
              "A", '{', "B", 'I', (sL_t)-1,
              /**/      "C", '[', 's', "a1",
              /**/                's', "a2",
              /**/                ']',
              /**/      "D", 'u', 1U,
              /**/      "}",
              "D", 'b', 0,
              NULL);
    uj_encClose(&B, '}');
    TCHECK(xeos(&B) == 1 );
    T = "{\"A\":{\"B\":-1,\"C\":[\"a1\",\"a2\"],\"D\":1},\"D\":false}";
    TCHECK(strcmp(T, B.buf) == 0);

    B.pos = 0;
    uj_encOpen (&B, '{');
    uj_encKVn(&B, "X", 0, NULL, NULL);
    uj_encClose(&B, '}');
    TCHECK(xeos(&B) == 1 );
    TCHECK(strcmp("{\"X\":}", B.buf) == 0);

    B.pos = 0;
    uj_encOpen (&B, '{');
    uj_encKVn(&B, "X", '[', 0, NULL, NULL);
    TCHECK(xeos(&B) == 1 );
    TCHECK(strcmp("{\"X\":[", B.buf) == 0);

    // xprintf
    {
        B.pos = 0;
        uj_encInt(&B, 1234567);
        xprintf(&B, "abc%d", 123);
    }

    // Buffer overflow
    {
        B.pos = 0;
        B.bufsize = 2;
        uj_encInt(&B, 1234567);
        TCHECK(0 == xeos(&B));
        TCHECK(strcmp("1", B.buf) == 0);
        xprintf(&B, "abc"); // buffer overflow
    }

    free(jsonbuf);
}


void selftest_ujenc () {
    test_simple_values();
}
