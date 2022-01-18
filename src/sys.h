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

#ifndef _sys_h_
#define _sys_h_

#include "rt.h"

enum { COMTYPE_SPI=0, COMTYPE_USB };
enum { PPS_GPS=0, PPS_FUZZY, PPS_TESTPIN };
extern u1_t   sys_modePPS; // special PPS mode?
extern u1_t   sys_deviceMode; // special mode used for GW regr testing
extern u1_t   sys_noTC;
extern u1_t   sys_noCUPS;
extern u2_t   sys_webPort;
extern s1_t   sys_slaveIdx;   // -1=master, >=0 slave process
extern uL_t   protoEUI;
extern uL_t   prefixEUI;

void  sys_ini ();
void  sys_fatal (int code);
void  sys_addLog (str_t line, int len);     // output/store one log line - *is* always \n treminated
#if defined(CFG_sysrandom)
int  sys_random (u1_t* buf, int len);
#else
void  sys_seed (u1_t* seed, int len);       // sys provides high quality seeds to grow randomness
#endif
sL_t  sys_time ();
sL_t  sys_utc  ();  // native UTC time - return 0 if not avail.
uL_t  sys_eui  ();
void  sys_usleep(sL_t us);

enum {
    SYSIS_TC_CONNECTED    = 1,
    SYSIS_TC_DISCONNECTED = 2,
    SYSIS_STATION_DEAD    = 3,  // process not started
    SYSIS_CUPS_INTERACT   = 4,
    SYSIS_CUPS_DONE       = 5,
};
void sys_inState (int state);

// Categories of credentials/config
enum { SYS_CRED_CUPS, SYS_CRED_TC, SYS_CRED_MAX };  // cat - category
enum { SYS_CRED_REG, SYS_CRED_BAK, SYS_CRED_BOOT }; // set - set of configs
enum { SYS_CRED_TRUST, SYS_CRED_MYCERT, SYS_CRED_MYKEY, SYS_CRED_NELEMS };
enum { SYS_AUTH_NONE, SYS_AUTH_SERVER, SYS_AUTH_BOTH, SYS_AUTH_TOKEN };
str_t sys_credcat2str (int cred_cat);
str_t sys_credset2str (int cred_set);
int   sys_cred (int cred_cat, int cred_set, str_t* elems, int* elemslen);
void  sys_credStart (int cred_cat, int len);
void  sys_credWrite  (int cred_cat, u1_t* data, int off, int len);
void  sys_credComplete (int cred_cat, int len);

void  sys_updateStart  (int len);
void  sys_updateWrite  (u1_t* data, int off, int len);
int   sys_updateCommit (int len);
void  sys_resetConfigUpdate ();
void  sys_commitConfigUpdate ();
void  sys_backupConfig (int cred_cat);

str_t sys_version ();
str_t sys_uri     (int cred_cat, int cred_set);
void  sys_saveUri (int cred_cat, str_t uri);
u4_t  sys_crcCred (int cred_cat, int cred_set);

void   sys_runUpdate ();
void   sys_abortUpdate ();
str_t  sys_radioDevice (str_t device, u1_t* comtype);
int    sys_runRadioInit (str_t device);
int    sys_execCommand (ustime_t max_wait, str_t* argv);

dbuf_t sys_sigKey (int key_id);
u4_t   sys_crcSigkey (int key_id);
dbuf_t sys_readFile (str_t filename);   // should this be here? - only used in sx130xconf.c
str_t  sys_makeFilepath (str_t fn, int complain);

void   sys_iniTC ();
void   sys_stopTC ();
void   sys_startTC ();
s1_t   sys_statusTC ();

void   sys_iniCUPS ();
void   sys_triggerCUPS (int delay);
void   sys_clearCUPS ();
void   sys_delayCUPS ();
s1_t   sys_statusCUPS ();

void   sys_iniWeb ();
void   sys_stopWeb ();

void   sys_keepAlive (int fd);

int    sys_getLatLon (double* lat, double* lon);

#endif // _sys_h_
