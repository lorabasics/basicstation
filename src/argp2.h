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
