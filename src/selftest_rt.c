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
#include "rt.h"


void selftest_rt () {
    TCHECK(rt_seconds(2) == rt_millis(2000));
    u1_t b[] = { 1,2,3,4,5,6,7,8 };
    TCHECK(rt_rlsbf2(b) == 0x0201);
    TCHECK(rt_rmsbf2(b) == 0x0102);
    TCHECK(rt_rlsbf4(b) == 0x04030201);
    TCHECK(rt_rlsbf8(b) == (uL_t)0x0807060504030201);
    TCHECK(rt_hexDigit('1') == 1);
    TCHECK(rt_hexDigit('a') == 10);
    TCHECK(rt_hexDigit('f') == 15);
    TCHECK(rt_hexDigit('A') == 10);
    TCHECK(rt_hexDigit('F') == 15);
    TCHECK(rt_hexDigit('g') == -1);
    TCHECK(rt_hexDigit(  0) == -1);

    str_t p;
    str_t s1 = "12345";
    p = s1;
    TCHECK(rt_readDec(&p) == 12345);
    TCHECK(p==s1+5);

    str_t s2 = "12345  ";
    p = s2;
    TCHECK(rt_readDec(&p) == 12345);
    TCHECK(p==s2+5);

    str_t s3 = "x12345  ";
    p = s3;
    TCHECK(rt_readDec(&p) == -1);
    TCHECK(p==s3);


    str_t eui1 = "123456  ";
    p = eui1;
    TCHECK(rt_readEui(&p, 0) == 0x123456);
    TCHECK(p==eui1+6);

    str_t eui2 = "12-34-56-78-9a-bc-de-f0  ";
    p = eui2;
    TCHECK(rt_readEui(&p, 0) == (uL_t)0x123456789abcdef0);
    TCHECK(p==eui2+23);

    str_t eui3 = "12:34:56:78:9a";
    p = eui3;
    TCHECK(rt_readEui(&p, 0) == (uL_t)0x123456789a);
    TCHECK(p==eui3+14);

    str_t eui4 = "12::34  ";
    p = eui4;
    uL_t e4 = rt_readEui(&p, 0);
    TCHECK(e4 == (uL_t)0x0012000000000034);
    TCHECK(p==eui4+6);

    str_t eui5 = "::12:34";
    p = eui5;
    uL_t e5 = rt_readEui(&p, 0);
    TCHECK(e5 == (uL_t)0x0000000000120034);
    TCHECK(p==eui5+7);

    str_t eui6 = "12:34::";
    p = eui6;
    uL_t e6 = rt_readEui(&p, 0);
    TCHECK(e6 == (uL_t)0x0012003400000000);
    TCHECK(p==eui6+7);

    str_t eui7 = "1:2:3:4";
    p = eui7;
    uL_t e7 = rt_readEui(&p, 0);
    TCHECK(e7 == (uL_t)0x0001000200030004);
    TCHECK(p==eui7+7);



    str_t eui20 = "12:::34";
    p = eui20;
    TCHECK(rt_readEui(&p, 0) == 0);
    TCHECK(p==eui20);

    str_t eui21 = "1:2:3:4:5:6:7:8:9:0";
    p = eui21;
    TCHECK(rt_readEui(&p, 0) == 0);
    TCHECK(p==eui21);

    str_t eui22 = ":12:34";
    p = eui22;
    TCHECK(rt_readEui(&p, 0) == 0);
    TCHECK(p==eui22);


    str_t sp1 = "1d2h3m4s5ms---";
    p = sp1;
    TCHECK(rt_readSpan(&p, 1) == ((((((ustime_t)1*24)+2)*60+3)*60+4)*1000+5)*1000);
    TCHECK(p[0]=='-');

    str_t sp2 = "123ms400---";
    p = sp2;
    TCHECK(rt_readSpan(&p, 1) == 123400);
    TCHECK(p[0]=='-');

    str_t sp3 = "123ms400---";
    p = sp3;
    TCHECK(rt_readSpan(&p, 0) == -1);

    str_t sp4 = "ms400---";
    p = sp4;
    TCHECK(rt_readSpan(&p, 0) == -1);
}
