// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _selftests_h_
#define _selftests_h_


#define TCHECK(cond) do {                                               \
        if(!(cond)) {                                                   \
            selftest_fail(#cond, __FILE__, __LINE__);                   \
        }                                                               \
    } while (0)

#define TFAIL(expr) do {                                                \
        selftest_fail(#expr, __FILE__, __LINE__);                       \
    } while (0)


extern void selftest_txq ();
extern void selftest_rxq ();
extern void selftest_lora ();
extern void selftest_rt ();
extern void selftest_ujdec ();
extern void selftest_ujenc ();
extern void selftest_xprintf ();
extern void selftest_fs ();

void selftest_fail (const char* expr, const char* file, int line);
void selftests ();


#endif // _selftests_h_
