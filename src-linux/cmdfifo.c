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
                    LOG(INFO, "CMD sent: %.40s%s", cmdline, i>40?"..":"");
                    (*TC->s2ctx.sendText)(&TC->s2ctx, &sendbuf);
                } else {
                    err = "Not enough WS space to sent command";
                }
            } else {
                err = "Command dropped - not connected right now";
            }
            if( err ) {
                LOG(ERROR,"%s: %.20s%s", err, cmdline, i>20?"..":"");
            }
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
    if( (fd = open(fifo, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) == -1 ) {
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
