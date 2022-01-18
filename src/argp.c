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

#include <stdio.h>
#include <string.h>
#include "argp2.h"


static struct argp_option* find_option (struct argp_option* opt, const char* long_opt, int short_opt) {
    while( opt->long_opt || opt->short_opt ) {
        if( (long_opt  && opt->long_opt  && strcmp(long_opt, opt->long_opt) == 0) ||
            (short_opt && opt->short_opt &&     short_opt == opt->short_opt ) ) {
            return opt;
        }
        opt++;
    }
    return NULL;
}

static char* check_arg (struct argp_option* opt, char* optarg, char** argv, int* p_argi) {
    if( optarg && optarg[0] ) {
        // Short option - arg follows option letter
        return &optarg[0];
    }
    int argi = *p_argi;
    char* a = argv[argi];
    if( a == NULL || strcmp(a, "--") == 0 ) {
        char sname[2] = { opt->short_opt, 0 };
        fprintf(stderr, "Expecting option %s%s to have an argument: %s\n",
                opt->long_opt ? "--" : "",
                opt->long_opt ? opt->long_opt : sname, opt->arg_spec);
        return NULL;
    }
    *p_argi += 1;
    return a;
}

static void usage (const char* prog, struct argp* argp) {
    const char* p = strrchr(prog, '/');
    fprintf(stderr, "usage: %s [OPTION...] %s\n\n", p?p+1:prog, argp->args_spec && argp->args_spec[0] ? argp->args_spec : "");
    struct argp_option* opt = argp->options;
    while( opt->long_opt || opt->short_opt ) {
        char line[80+10];
        int li = 0;
        li = snprintf(line+li, sizeof(line)-li, "  ");
        if( opt->short_opt > ' ' )
            li += snprintf(line+li, sizeof(line)-li, "-%c%s", opt->short_opt, opt->long_opt?", ":"");
        if( opt->long_opt )
            li += snprintf(line+li, sizeof(line)-li, "--%s", opt->long_opt);
        if( opt->arg_spec )
            li += snprintf(line+li, sizeof(line)-li, "%s%s  ", opt->long_opt?"=":"", opt->arg_spec);
        if( li > 19 ) {
            fprintf(stderr, "%s\n", line);
            li = 0;
        }
        li += snprintf(line+li, sizeof(line)-li, "%*s", 19-li, "");

        const char* doc = opt->doc;
        while( doc && doc[0] ) {
            const char* w = doc;
            int c;
            while( (c = w[0]) != ' ' && c != '\n' && c != 0 ) w++;
            if( c == 0 ) {
                doc = w;
                if( li > 20 )
                    fprintf(stderr, "%s\n", line);
                fprintf(stderr, "\n");
                continue;
            }
            if( c == '\n' ) {
                li = 80;
            }
            if( w > doc ) {
                if( li + 1 + (w-doc) > 80 ) {
                    fprintf(stderr, "%s\n", line);
                    li = 20;
                    for( int i=0; i<li; i++ )
                        line[i] = ' ';
                } else {
                    line[li] = ' ';
                    li += 1;
                }
                memcpy(&line[li], doc, w-doc);
                li += w-doc;
                line[li] = 0;
            }
            doc = w+1;
        }
        opt++;
    }
}


int argp_parse (struct argp* argp, int argc, char** argv, int flag, void* dummy1, void* dummy2) {
    int argi = 1;
    while( argi < argc ) {
        char* a = argv[argi];
        if( a[0] == '-' && a[1] == '-' ) {
            if( a[2] == 0 ) {
                argi++;
                break;
            }
            // Long option
            if( strcmp(a, "--help") == 0 ) {
                usage(argv[0], argp);
                return 1;
            }
            struct argp_option* opt = find_option(argp->options, a+2, 0);
            if( opt == NULL ) {
                fprintf(stderr, "Unknown option: %s\n", a);
                return 1;
            }
            argi += 1;
            char* arg = NULL;
            if( opt->arg_spec && (arg = check_arg(opt, NULL, argv, &argi)) == NULL ) {
                return 1;
            }
            if( argp->parsefn(opt->short_opt, arg, NULL) != 0 ) {
                fprintf(stderr, "Parsing of option --%s failed\n", opt->long_opt);
                return 1;
            }
        }
        else if( a[0] == '-' ) {
            int si=1, sopt;
            argi += 1;
            while( (sopt = a[si++]) ) {
                // Short option
                if( sopt == 'h' || sopt == '?' ) {
                    usage(argv[0], argp);
                    return 1;
                }
                struct argp_option* opt = find_option(argp->options, NULL, sopt);
                if( opt == NULL ) {
                    fprintf(stderr, "Unknown option: -%c\n", sopt);
                    return 1;
                }
                char* arg = NULL;
                if( opt->arg_spec && (arg = check_arg(opt, &a[si], argv, &argi)) == NULL ) {
                    return 1;
                }
                if( argp->parsefn(opt->short_opt, arg, NULL) != 0 ) {
                    fprintf(stderr, "Parsing of option -%c failed\n", sopt);
                    return 1;
                }
            }
        }
        else {
            break;
        }
    }
    while( argi < argc ) {
        char* arg = argv[argi++];
        if( argp->parsefn(ARGP_KEY_ARG, arg, NULL) != 0 ) {
            fprintf(stderr, "Illegal argument: %s\n", arg);
            return 1;
        }
    }
    argp->parsefn(ARGP_KEY_END, NULL, NULL);
    return 0;
}

#endif // defined(CFG_argp)
