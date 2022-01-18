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

#include <stdio.h>
#include "s2conf.h"
#include "rt.h"
#include "uj.h"


#undef _s2conf_x_
#undef CONF_PARAM
#define CONF_PARAM(name,type,fn,value,info) type##_t name;

#include "s2conf.h"


static int parse_bool     (struct conf_param* param);
static int parse_u4       (struct conf_param* param);
static int parse_str      (struct conf_param* param);
static int parse_tspan_h  (struct conf_param* param);
static int parse_tspan_m  (struct conf_param* param);
static int parse_tspan_s  (struct conf_param* param);
static int parse_tspan_ms (struct conf_param* param);
static int parse_size_kb  (struct conf_param* param);
static int parse_size_mb  (struct conf_param* param);


#undef _s2conf_x_
#undef CONF_PARAM
#define CONF_PARAM(name,type,fn,value,info) { #name, #type, info, "builtin", value, &name, parse_##fn },

struct conf_param conf_params[] = {
#include "s2conf.h"
    { NULL }
};


static int parse_bool (struct conf_param* param) {
    ujdec_t D;
    char* v = rt_strdup(param->value);
    uj_iniDecoder(&D, v, strlen(v));
    if( uj_decode(&D) ) {
        LOG(ERROR, "Parsing of config param '%s' (%s) as bool failed: %s",
            param->name, param->src, param->value);
        free(v);
        return 0;
    }
    *(bool*)param->pvalue = uj_bool(&D);
    uj_assertEOF(&D);
    rt_free(v);
    return 1;
}


static int parse_u4 (struct conf_param* param) {
    ujdec_t D;
    char* v = rt_strdup(param->value);
    uj_iniDecoder(&D, v, strlen(v));
    if( uj_decode(&D) ) {
        LOG(ERROR, "Parsing of config param '%s' (%s) as u4 failed: %s",
            param->name, param->src, param->value);
        free(v);
        return 0;
    }
    *(u4_t*)param->pvalue = uj_uint(&D);
    uj_assertEOF(&D);
    rt_free(v);
    return 1;
}


static int parse_str (struct conf_param* param) {
    ujdec_t D;
    char* v = rt_strdup(param->value);
    uj_iniDecoder(&D, v, strlen(v));
    if( uj_decode(&D) ) {
        LOG(ERROR, "Parsing of config param '%s' (%s) as 'str' failed: %s",
            param->name, param->src, param->value);
        free(v);
        return 0;
    }
    rt_free(*(void**)param->pvalue);
    *(str_t*)param->pvalue = rt_strdup(uj_str(&D));
    uj_assertEOF(&D);
    rt_free(v);
    return 1;
}


static int parse_tspan (struct conf_param* param, ustime_t defaultUnit) {
    ujdec_t D;
    char* v = rt_strdup(param->value);
    uj_iniDecoder(&D, v, strlen(v));
    if( uj_decode(&D) ) {
        LOG(ERROR, "Parsing of config param '%s' (%s) as 'tspan' failed: %s",
            param->name, param->src, param->value);
        free(v);
        return 0;
    }
    ustime_t tspan;
    if( uj_nextValue(&D) == UJ_STRING ) {
        char *s = uj_str(&D);
        tspan = rt_readSpan((str_t*)&s, defaultUnit);
        if( tspan < 0 || s[0] )
            uj_error(&D, "Syntax error");
    } else {
        tspan = uj_num(&D) * defaultUnit;
    }
    uj_assertEOF(&D);
    *(ustime_t*)param->pvalue = tspan;
    rt_free(v);
    return 1;
}

static int parse_tspan_h (struct conf_param* param) {
    return parse_tspan(param, rt_seconds(3600));
}

static int parse_tspan_m (struct conf_param* param) {
    return parse_tspan(param, rt_seconds(60));
}

static int parse_tspan_s (struct conf_param* param) {
    return parse_tspan(param, rt_seconds(1));
}

static int parse_tspan_ms (struct conf_param* param) {
    return parse_tspan(param, rt_millis(1));
}

static int parse_size (struct conf_param* param, u4_t defaultUnit) {
    ujdec_t D;
    char* v = rt_strdup(param->value);
    uj_iniDecoder(&D, v, strlen(v));
    if( uj_decode(&D) ) {
        LOG(ERROR, "Parsing of config param '%s' (%s) as 'size' failed: %s",
            param->name, param->src, param->value);
        free(v);
        return 0;
    }
    sL_t size;
    if( uj_nextValue(&D) == UJ_STRING ) {
        char *s = uj_str(&D);
        size = rt_readSize((str_t*)&s, defaultUnit);
        if( size < 0 || s[0] )
            uj_error(&D, "Syntax error");
    } else {
        size = uj_num(&D) * defaultUnit;
    }
    uj_assertEOF(&D);
    *(u4_t*)param->pvalue = size;
    rt_free(v);
    return 1;
}

static int parse_size_kb (struct conf_param* param) {
    return parse_size(param, 1024);
}

static int parse_size_mb (struct conf_param* param) {
    return parse_size(param, 1024*1024);
}


void s2conf_ini () {
    for( struct conf_param* p = conf_params; p->name; p++ ) {
        s2conf_set("builtin", p->name, p->value);
        str_t v = getenv(p->name);
        if( v ) {
            if (strcmp(p->type,"str")==0)
                s2conf_set("env", p->name, rt_strdupq(v));
            else
                s2conf_set("env", p->name, rt_strdup(v));
        }
    }
}


void* s2conf_get (str_t name) {
    for( struct conf_param* p = conf_params; p->name; p++ ) {
        if( strcmp(p->name, name) == 0 )
            return p;
    }
    return NULL;
}


int s2conf_set (str_t src, str_t name, str_t value) {
    struct conf_param* p = s2conf_get(name);
    if( p == NULL ) {
        assert(strcmp(src,"builtin") != 0);  // initialization values must be ok
        free((void*)value);
        return -1; // No such param name
    }
    struct conf_param n = *p;
    n.src = src;
    n.value = value;
    if( !n.parseFn(&n) ) {
        assert(p->value != value);  // initialization values must be ok
        free((void*)value);
        return 0;  // parsing failed
    }
    if( strcmp(p->src, "builtin") != 0 )
        free((void*)p->value);
    *p = n;
    return 1;
}


void s2conf_printAll () {
    for( struct conf_param* p = conf_params; p->name; p++ ) {
        fprintf(stderr, "%6s %-20s = %-10s %-12s %s\n",
                p->type, p->name, p->value, p->src, p->info);
    }
}
