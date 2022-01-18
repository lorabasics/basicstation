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

#ifndef _tls_h_
#define _tls_h_

#include "mbedtls/ssl.h"
#include "mbedtls/net.h"

typedef struct tlsconf tlsconf_t;
typedef struct mbedtls_ssl_context* tlsctx_p;

extern u1_t tls_dbgLevel;
void log_mbedError (u1_t mod_level, int ret, const char* fmt, ...);

tlsconf_t* tls_makeConf      ();
void       tls_freeConf      (tlsconf_t* conf);
int        tls_setMyCert     (tlsconf_t* conf, const char* cert, int certlen, const char* key, int keylen, const char* pwd);
int        tls_setTrustedCAs (tlsconf_t* conf, const char* file_or_data, int len);
tlsctx_p   tls_makeSession   (tlsconf_t* conf, const char* servername);
void       tls_freeSession   (tlsctx_p tlsctx);

int tls_read  (mbedtls_net_context* netctx, tlsctx_p tlsctx,       u1_t* p, size_t sz);
int tls_write (mbedtls_net_context* netctx, tlsctx_p tlsctx, const u1_t* p, size_t sz);

#endif // _tls_h_
