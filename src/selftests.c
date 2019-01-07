// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "selftests.h"

#if defined(CFG_selftests)

static int fails;
static jmp_buf onfail;

static void (*const selftest_fns[])() = {
    selftest_txq,
    selftest_rxq,
    selftest_lora,
    selftest_rt,
    selftest_ujdec,
    selftest_ujenc,
    selftest_xprintf,
    selftest_fs,
    NULL
};


// LCOV_EXCL_START
void selftest_fail (const char* expr, const char* file, int line) {
    fprintf(stderr, "TEST FAILED: %s at %s:%d\n", expr, file, line);
    longjmp(onfail, 1);
}
// LCOV_EXCL_STOP


void selftests () {
    int i=-1;
    while( selftest_fns[++i] ) {
        if( setjmp(onfail) ) {
            fails += 1;                  // LCOV_EXCL_LINE
        } else {
            selftest_fns[i]();
        }
    }
    if( fails == 0 ) {
        fprintf(stderr,"ALL %d SELFTESTS PASSED\n", i);
        exit(0);
    }
    fprintf(stderr,"TESTS FAILED: %d of %d\n", fails, i); // LCOV_EXCL_LINE
    exit(70);                                             // LCOV_EXCL_LINE
}

#else // !defined(CFG_selftests)

void selftests () {}

#endif // !defined(CFG_selftests)
