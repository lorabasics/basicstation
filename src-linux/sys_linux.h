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

#ifndef _sys_linux_h_
#define _sys_linux_h_

#include "rt.h"

#define EXIT_NOP          6
#define FATAL_GENERIC    30
#define FATAL_PTHREAD    31
#define FATAL_NOLOGGING  32
#define FATAL_MAX        40

struct logfile {
    str_t path;
    int   size;
    int   rotate;
};

extern str_t  sys_slaveExec;  // template to start slave processes

void     sys_startLogThread ();
void     sys_iniLogging (struct logfile* lf, int captureStdio);
void     sys_flushLog ();
int      sys_findPids (str_t device, u4_t* pids, int n_pids);
dbuf_t   sys_checkFile (str_t filename);
void     sys_writeFile (str_t filename, dbuf_t* data);
void     sys_startupSlave (int rdfd, int wrfd);
int      sys_enableGPS (str_t device);
void     sys_enableCmdFIFO (str_t file);

#endif // _sys_linux_h_
