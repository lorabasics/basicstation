// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#if defined(CFG_linux)

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "s2conf.h"
#include "rt.h"
#include "tc.h"


static str_t  fifo;
static aio_t* aio;
static int    fill;
static char   cmdline[PIPE_BUF];
static tmr_t  reopen_tmr;


// Fwd decl
static int fifo_reopen ();

static void reopen_timeout (tmr_t* tmr) {
    if( tmr == NULL || !fifo_reopen() )
        rt_setTimer(&reopen_tmr, rt_micros_ahead(CMD_REOPEN_FIFO_INTV));
}


static void fifo_read(aio_t* _aio) {
    assert(aio == _aio);
    int n;
    while(1) {
        n = read(aio->fd, cmdline+fill, sizeof(cmdline)-fill);
        if( n == -1 ) {
            if( errno == EAGAIN )
                return;
            LOG(ERROR, "Failed to read CMD from '%s': %s", fifo, strerror(errno));
            n = 0;
        }
        if( n == 0 ) {
            // EOF
            aio_close(aio);
            aio = NULL;
            reopen_timeout(NULL);
            return;
        }
        n = fill += n;
        int i=0;
        while( i<n ) {
            if( cmdline[i] != '\n' ) {
                i++;
                continue;
            }
            cmdline[i] = 0;  // make it 0 terminated - don't need the new line
            str_t err = NULL;

            if( cmdline[0] != '{' ) {
                // Not a json object - check for some builtin commands
                int lvl = log_str2level(cmdline);
                if( lvl >= 0 ) {
                    log_setLevel(lvl);
                } else {
                    
                    err = "Unknown fifo command";
                }
            }
            else if( TC ) {
                ujbuf_t sendbuf = (*TC->s2ctx.getSendbuf)(&TC->s2ctx, i);
                if( sendbuf.buf != NULL ) {
                    memcpy(sendbuf.buf, cmdline, i);
                    sendbuf.pos = i;
                    LOG(INFO, "CMD sent: %.40s", cmdline);
                    (*TC->s2ctx.sendText)(&TC->s2ctx, &sendbuf);
                } else {
                    err = "Not enough WS space to sent command";
                }
            } else {
                err = "Command dropped - not connected right now";
            }
            if( err )
                LOG(ERROR,"%s: %.20s", err, cmdline);

            fill = n -= i+1;
            memcpy(cmdline, cmdline+i+1, n);
            i = 0;
        }
    }
}


static void fifo_close () {
    if( aio == NULL )
        return;
    aio_close(aio);
    aio = NULL;
}


static int fifo_reopen () {
    struct stat st;
    int fd;

    if( aio ) {
        aio_close(aio);
        aio = NULL;
    }
    if( stat(fifo, &st) == -1  || (st.st_mode & S_IFMT) != S_IFIFO )
        return 0;  // no such file or not a FIFO
    if( (fd = open(fifo, O_RDONLY | O_NONBLOCK)) == -1 ) {
        LOG(ERROR, "Failed to open cmd FIFO '%s': %s", fifo, strerror(errno));
        return 0;
    }
    // use device as dummy context
    aio = aio_open(&fifo, fd, fifo_read, NULL);
    atexit(fifo_close);
    fifo_read(aio);
    return 1;
}


void sys_enableCmdFIFO (str_t file) {
    fifo = file;
    rt_iniTimer(&reopen_tmr, reopen_timeout);
    reopen_timeout(&reopen_tmr);
}

#endif // defined(CFG_linux)
