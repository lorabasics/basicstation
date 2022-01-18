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

#define TSTR(s) TCHECK(strcmp(s, B.buf) == 0); B.pos=0

void selftest_xprintf () {
    char* outbuf = rt_mallocN(char, BUFSZ);
    ujbuf_t B = {.buf=outbuf, .bufsize=BUFSZ, .pos=0 };

    xprintf(&B, "Hello!");                TSTR("Hello!");
    xprintf(&B, "%");                     TSTR("%");
    xprintf(&B, "%%");                    TSTR("%");
    xprintf(&B, "%d", 123);               TSTR("123");
    xprintf(&B, "%ld", (uL_t)123);        TSTR("123");
    xprintf(&B, "[%012X]", 1<<31);        TSTR("[000080000000]");
    xprintf(&B, "[%lX]", (uL_t)1<<32);    TSTR("[100000000]");
    xprintf(&B, "% lg", 123E6);           TSTR(" 1.23e+08");
    xprintf(&B, "%-7.1f", 123.456);       TSTR("123.5  ");
    xprintf(&B, "%c%c%c", 'a','b','c');   TSTR("abc");
    xprintf(&B, "%10.3s", "abcdef");      TSTR("       abc");
    xprintf(&B, "%p", NULL);              TSTR("(nil)");


    xprintf(&B, "%M"    ,0x1A2B3C4DA1B2C3D4);            TSTR("3C:4D:A1:B2:C3:D4");
    xprintf(&B, "%E"    ,0x1A2B3C4DA1B2C3D4);            TSTR("1A-2B-3C-4D-A1-B2-C3-D4");
    xprintf(&B, "%.4E"  ,0x1A2B3C4DA1B2C3D4);            TSTR("-A1-B2-C3-D4");
    xprintf(&B, "%~T"   ,rt_seconds(7200));              TSTR("2h");
    xprintf(&B, "%~T"   ,rt_seconds(0));                 TSTR("0");
    xprintf(&B, "%~T"   ,(ustime_t)-3500);               TSTR("-3ms500us");
    xprintf(&B, "%~<12T",(ustime_t)-3500);               TSTR("-3ms500us   ");
    xprintf(&B, "%~>12T",(ustime_t)-3500);               TSTR("   -3ms500us");
    xprintf(&B, "%~^12T",(ustime_t)-3500);               TSTR(" -3ms500us  ");
    xprintf(&B, "%H"    ,6, "ABCDEF"  );                 TSTR("414243444546");
    xprintf(&B, "%2.2H" ,6, "ABCDEF"  );                 TSTR("4142..4546");
    xprintf(&B, "%.4H"  ,6, "ABCDEF"  );                 TSTR("..43444546");
    xprintf(&B, "%4H"   ,6, "ABCDEF"  );                 TSTR("41424344..");
    xprintf(&B, "%B"    ,6, "ABCDEF"  );                 TSTR("QUJDREVG");
    xprintf(&B, "%B"    ,7, "ABCDEFG" );                 TSTR("QUJDREVGRw==");
    xprintf(&B, "%B"    ,8, "ABCDEFGH");                 TSTR("QUJDREVGR0g=");

    ustime_t t0 = (ustime_t)1522068206421865L;
    xprintf(&B, "%T"     ,t0);                   TSTR("2018-03-26 12:43:26");
    xprintf(&B, "%<T"    ,t0);                   TSTR("2018-03-26");
    xprintf(&B, "%>.6T"  ,t0);                   TSTR("12:43:26.421865");
    xprintf(&B, "%_.3T"  ,t0);                   TSTR("2018-03-26_12:43:26.421");
    ustime_t t1;
    t1 = 1451649600L*1000000LL;
    xprintf(&B, "%T"     ,t1);                   TSTR("2016-01-01 12:00:00");
    t1 -= 24*3600*1000000LL;
    xprintf(&B, "%T"     ,t1);                   TSTR("2015-12-31 12:00:00");
    t1 = 1456657200L*1000000LL;
    xprintf(&B, "%T"     ,t1);                   TSTR("2016-02-28 11:00:00");
    t1 += 24*3600*1000000LL;
    xprintf(&B, "%T"     ,t1);                   TSTR("2016-02-29 11:00:00");
    t1 += 24*3600*1000000LL;
    xprintf(&B, "%T"     ,t1);                   TSTR("2016-03-01 11:00:00");
    ustime_t t2 = (ustime_t)-1;
    xprintf(&B, "%T"     ,t2);                   TSTR("0000-00-00 00:00:00");

    xprintf(&B, "%R"     ,0);                    TSTR("SF12/BW125");
    xprintf(&B, "%R"     ,6);                    TSTR("FSK");
    xprintf(&B, "%R"     ,(1<<3)|5);             TSTR("SF7/BW250");
    xprintf(&B, "%R"     ,(2<<3)|4);             TSTR("SF8/BW500");
    xprintf(&B, "%^8R"   ,0xFF);                 TSTR("  SF??  ");
    xprintf(&B, "%F"     ,868300000);            TSTR("868.3MHz");
    xprintf(&B, "%~F"    ,868300000);            TSTR("868.3");

    xprintf(&B, "%s"     ,"0123456789");         TSTR("0123456789");
    xprintf(&B, "%*s"    ,10,"01234");           TSTR("     01234");
    xprintf(&B, "%.*s"   , 5,"0123456789");      TSTR("01234");
    xprintf(&B, "%-*.*s" ,10,5,"0123456789");    TSTR("01234     ");

    char bufsmall[10];
    ujbuf_t B2 = dbuf_ini(bufsmall);
    xputs(&B2, "123456", -1);
    TCHECK(1==xeos(&B2));
    TCHECK(strcmp(B2.buf, "123456")==0);
    xputs(&B2, "123456", -1);
    TCHECK(0==xeos(&B2));
    TCHECK(strcmp(B2.buf, "123456123")==0);

    B2.pos = 0;
    xputs(&B2, "123456", -1);
    TCHECK(1==xeol(&B2));
    TCHECK(strncmp(B2.buf, "123456\n", B2.pos)==0);
    xputs(&B2, "123456", -1);
    TCHECK(0==xeol(&B2));
    TCHECK(strncmp(B2.buf, "123456\n12\n", B2.bufsize)==0);

    free(outbuf);
}
