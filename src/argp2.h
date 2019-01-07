// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#if defined(CFG_argp)
#ifndef _argp2_h_
#define _argp2_h_

#define ARGP_KEY_ARG 1
#define ARGP_KEY_END 2
#define OPTION_HIDDEN 0x10
#define ARGP_ERR_UNKNOWN -1

struct argp_state {
    int    argc;
    char** argv;
    int    aidx;
    int    cidx;
};

struct argp_option {
    const char* long_opt;
    int         short_opt;
    const char* arg_spec;
    int         flag;
    const char* doc;
};

struct argp {
    struct argp_option* options;
    int (*parsefn) (int key, char* arg, struct argp_state* state);
    const char* args_spec;
    void* dummy1;
    void* dummy2;
    void* dummy3;
    void* dummy4;
};

int argp_parse (struct argp* argp, int argc, char** argv, int flag, void* dummy1, void* dummy2);

#endif // _argp2_h_
#else // defined(CFG_argp)
#include <argp.h>
#endif // defined(CFG_argp)
