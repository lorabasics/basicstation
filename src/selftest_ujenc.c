// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

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
    uj_encStr (&B,  "-\"\\\b\f\n\r\t\x01-");
    uj_encHex (&B,  (const u1_t*)"ABC", 3);
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
    T = "[null,false,true,-1,1,1.5,21.500000,"
        "\"-\\\"\\\\\\b\\f\\n\\r\\t\\u0001-\","
        "\"414243\","
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
              "F", 's', "abc",
              "G", 'H', 3, (const u1_t*)"ABC",
              "H", 'E', 0x91A2B3C4D5E6F708,
              "I", '6', (uL_t)0xB000A,
              NULL);
    uj_encClose(&B, '}');
    TCHECK(xeos(&B) == 1 );
    T = "{\"A\":true,\"B\":-1,\"C\":-1,\"D\":1,\"E\":1,\"G1\":1.25,\"G2\":21.250000,"
        "\"F\":\"abc\",\"G\":\"414243\",\"H\":\"91-A2-B3-C4-D5-E6-F7-08\",\"I\":\"::b:a\"}";
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
