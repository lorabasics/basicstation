// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

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
