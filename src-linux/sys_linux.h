// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

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
