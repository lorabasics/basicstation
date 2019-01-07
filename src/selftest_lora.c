// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include "selftests.h"
#include "s2e.h"

#define BUFSZ (2*1024)

static const uL_t euiFilter1[] = { 0xEFCDAB8967452300, 0xEFCDAB8967452300, 0 };
static const uL_t euiFilter2[] = { 0xEFCDAB8967452300, 0xEFCDAB8967452301, 0 };


void selftest_lora () {
    char* jsonbuf = rt_mallocN(char, BUFSZ);

    ujbuf_t B = { .buf = jsonbuf, .bufsize = BUFSZ, .pos = 0 };
    const char* T;
    uL_t joineuiFilter[2*10+2] = { 0, 0 };
    s2e_joineuiFilter = joineuiFilter;

    T = "\x00_______________";  // too short
    TCHECK(!s2e_parse_lora_frame(&B, (const u1_t*)T, 1, NULL));
    T = "\x03_______________";  // bad major version
    TCHECK(!s2e_parse_lora_frame(&B, (const u1_t*)T, 16, NULL));

    B.pos = 0;
    T = "\x20_______________";  // join accept
    TCHECK(s2e_parse_lora_frame(&B, (const u1_t*)T, 16, NULL));
    xeos(&B);
    TCHECK(strcmp("\"msgtype\":\"jacc\",\"FRMPayload\":\"205F5F5F5F5F5F5F5F5F5F5F5F5F5F5F\"", B.buf) == 0);

    B.pos = 0;
    T = "\xE0_______________";  // proprietary frame
    TCHECK(s2e_parse_lora_frame(&B, (const u1_t*)T, 16, NULL));
    xeos(&B);
    TCHECK(strcmp("\"msgtype\":\"propdf\",\"FRMPayload\":\"E05F5F5F5F5F5F5F5F5F5F5F5F5F5F5F\"", B.buf) == 0);

    B.pos = 0;
    const char* Tjreq = "\x00\x01\x23\x45\x67\x89\xAB\xCD\xEF\xF1\xE3\xF5\xE7\xF9\xEB\xFD\xEF\xF0\xF1\xA0\xA1\xA2\xA3";  // jreq
    TCHECK(s2e_parse_lora_frame(&B, (const u1_t*)Tjreq, 23, NULL));
    xeos(&B);
    TCHECK(strcmp("\"msgtype\":\"jreq\",\"MHdr\":0,"
                  "\"JoinEui\":\"EF-CD-AB-89-67-45-23-01\","
                  "\"DevEui\":\"EF-FD-EB-F9-E7-F5-E3-F1\","
                  "\"DevNonce\":61936,\"MIC\":-1549622880", B.buf) == 0);
    // Too short
    B.pos = 0;
    TCHECK(!s2e_parse_lora_frame(&B, (const u1_t*)Tjreq, 22, NULL));
    // Filter enabled
    B.pos = 0;
    memcpy(s2e_joineuiFilter, euiFilter1, sizeof(euiFilter1));  // jreq removed
    TCHECK(!s2e_parse_lora_frame(&B, (const u1_t*)Tjreq, 23, NULL));
    B.pos = 0;
    memcpy(s2e_joineuiFilter, euiFilter2, sizeof(euiFilter2));  // jreq passes
    TCHECK(s2e_parse_lora_frame(&B, (const u1_t*)Tjreq, 23, NULL));
    s2e_joineuiFilter[0] = 0;

    B.pos = 0;
    const char* Tdaup1 = "\x40\xAB\xCD\xEF\xFF\x01\xF3\xF4\xFF\x20\x21\x22\xA0\xA1\xA2\xA3";  // daup
    TCHECK(s2e_parse_lora_frame(&B, (const u1_t*)Tdaup1, 12+1+3, NULL));
    xeos(&B);
    TCHECK(strcmp("\"msgtype\":\"updf\","
                  "\"MHdr\":64,\"DevAddr\":-1061461,\"FCtrl\":1,\"FCnt\":62707,"
                  "\"FOpts\":\"FF\",\"FPort\":32,\"FRMPayload\":\"2122\","
                  "\"MIC\":-1549622880", B.buf) == 0);
    // Too short
    B.pos = 0;
    TCHECK(!s2e_parse_lora_frame(&B, (const u1_t*)Tdaup1, 12, NULL));
    // Filtered
    B.pos = 0;
    s2e_netidFilter[0] = s2e_netidFilter[1] = s2e_netidFilter[2] = s2e_netidFilter[3] = 0;
    TCHECK(!s2e_parse_lora_frame(&B, (const u1_t*)Tdaup1, 12+1+3, NULL));

    free(jsonbuf);
}
