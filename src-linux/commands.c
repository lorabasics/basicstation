// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#include <stdio.h>
#include "s2conf.h"
#include "uj.h"
#include "s2e.h"
#include "kwcrc.h"
#include "sys.h"
#include "sys_linux.h"


int s2e_handleCommands (ujcrc_t msgtype, s2ctx_t* s2ctx, ujdec_t* D) {
    switch(msgtype) {
    default: {
        return 0;
    }
    }
    return 1;
}
