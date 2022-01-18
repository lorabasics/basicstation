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

#if !defined(CFG_no_rmtsh)
#define _XOPEN_SOURCE 600    // posix_openpt et al.
#define _DEFAULT_SOURCE      // cfmakeraw et al.
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#include "s2conf.h"
#include "s2e.h"
#include "uj.h"
#include "kwcrc.h"
#include "tc.h"


#define UPBUFSZ   4096
#define DNBUFSZ   4096
#define UPBUFHI   (UPBUFSZ/2)
#define DNBUFHI   (DNBUFSZ/2)
#define WS_CHUNKS MIN_UPJSON_SIZE


typedef struct rmtsh {
    str_t    user;
    pid_t    pid;
    aio_t*   aio;
    char     upbuf[UPBUFSZ];
    char     dnbuf[DNBUFSZ];
    int      upfill, upsink;
    int      dnfill, dnsink;
    ustime_t mtime;
} rmtsh_t;

static rmtsh_t rmtshTable[MAX_RMTSH];


// Fwd decl
static void stopRmtsh (rmtsh_t* rmtsh);


static void up_read (aio_t* aio) {
    rmtsh_t* rmtsh = aio->ctx;
    int n;
    while(1) {
        n = read(aio->fd, rmtsh->upbuf+rmtsh->upfill, UPBUFSZ-rmtsh->upfill);
        if( n == -1 ) {
            if( errno == EAGAIN ) {
                return;
            }
            LOG(ERROR, "Failed to read from rmsh#%d (pid=%d): %s", (int)(rmtsh-rmtshTable), rmtsh->pid, strerror(errno));
            n = 0;
        }
        if( n == 0 ) { // EOF
            stopRmtsh(rmtsh);
            return;
        }
        rmtsh->mtime = rt_getTime();
        rmtsh->upfill += n;
        if( TC ) {
            n = min(WS_CHUNKS, 1+rmtsh->upfill-rmtsh->upsink);
            if( n == 1 )
                continue; // do sent empty data frame - would signal EOF
            ujbuf_t sendbuf = (*TC->s2ctx.getSendbuf)(&TC->s2ctx, n);
            if( sendbuf.buf != NULL ) {
                sendbuf.buf[0] = rmtsh - rmtshTable;
                memcpy(sendbuf.buf+1, &rmtsh->upbuf[rmtsh->upsink], n-1);
                sendbuf.pos = n;
                (*TC->s2ctx.sendBinary)(&TC->s2ctx, &sendbuf);
                rmtsh->upsink += n-1;
            } else {
                LOG(WARNING, "No enough WS space to sent command");
            }
        } else {
            // No connection - drop data
            if( rmtsh->upfill >= UPBUFHI )
                rmtsh->upsink  = UPBUFHI;
        }
        // Compaction required?
        if( rmtsh->upfill >= UPBUFHI && rmtsh->upsink > 0 ) {
            memcpy(&rmtsh->upbuf[0], &rmtsh->upbuf[rmtsh->upsink], rmtsh->upfill-rmtsh->upsink);
            rmtsh->upfill -= rmtsh->upsink;
            rmtsh->upsink = 0;
        }
    }
}


static void dn_write (aio_t* aio) {
    rmtsh_t* rmtsh = aio->ctx;
    int n;
    while( rmtsh->dnfill > rmtsh->dnsink ) {
        n = write(aio->fd, rmtsh->dnbuf+rmtsh->dnsink, rmtsh->dnfill-rmtsh->dnsink);
        if( n == -1 ) {
            if( errno == EAGAIN ) {
                aio_set_wrfn(aio, dn_write);
                return;
            }
            stopRmtsh(rmtsh);
            return;
        }
        rmtsh->mtime = rt_getTime();
        rmtsh->dnsink += n;
    }
    rmtsh->dnfill = rmtsh->dnsink = 0;
    aio_set_wrfn(aio, NULL);
}


static void dn_fill (rmtsh_t* rmtsh, u1_t* data, int len) {
    if( rmtsh->dnfill + len > DNBUFSZ ) {
        // Compact
        if( rmtsh->dnsink > 0 ) {
            memcpy(&rmtsh->dnbuf[0], &rmtsh->dnbuf[rmtsh->dnsink], rmtsh->dnfill-rmtsh->dnsink);
            rmtsh->dnfill -= rmtsh->dnsink;
            rmtsh->dnsink = 0;
        }
        if( rmtsh->dnfill + len > DNBUFSZ ) {
            LOG(ERROR, "Remote shell down stream buffer overflow");
            stopRmtsh(rmtsh); // stop for safety reasons
            return;
        }
    }
    memcpy(&rmtsh->dnbuf[rmtsh->dnfill], data, len);
    rmtsh->dnfill += len;
    dn_write(rmtsh->aio);
}


static void stopRmtsh (rmtsh_t* rmtsh) {
    if( rmtsh->aio == NULL ) {
        return;  // not is use anyway
    }
    if( rmtsh->aio && TC ) {
        // Send empty data packet - EOF
        ujbuf_t sendbuf = (*TC->s2ctx.getSendbuf)(&TC->s2ctx, MIN_UPJSON_SIZE);
        if( sendbuf.buf != NULL ) {
            sendbuf.buf[0] = rmtsh - rmtshTable;
            sendbuf.pos = 1;
            (*TC->s2ctx.sendBinary)(&TC->s2ctx, &sendbuf);
        }
    }
    if( rmtsh->pid ) {
        kill(-rmtsh->pid, SIGKILL);  // kill process group
        while( waitpid(-1, NULL, WNOHANG) > 0 );
    }
    LOG(NOTICE, "Rmtsh#%d stopped (pid=%d)", (int)(rmtsh - rmtshTable), rmtsh->pid);
    rmtsh->pid = 0;
    aio_close(rmtsh->aio);
    rmtsh->aio = NULL;
    rmtsh->upfill = rmtsh->upsink = 0;
    rmtsh->dnfill = rmtsh->dnsink = 0;
}


static void startRmtsh (rmtsh_t* rmtsh, str_t user, str_t term) {
    if( rmtsh->aio != NULL ) {
        return;  // already running
    }
    int pty_master = -1;
    int pty_slave = -1;
    if( (pty_master = posix_openpt(O_RDWR|O_NONBLOCK)) == -1 ||
        grantpt(pty_master) == -1 ||
        unlockpt(pty_master) == -1 ||
        (pty_slave = open(ptsname(pty_master), O_RDWR)) == -1 ) {
        close(pty_master);
        LOG(ERROR, "Setting up pseudo terminal (%s) failed: %s", pty_master==-1 ? "master":"slave", strerror(errno));
        return;
    }
    int rc = fork();
    if( rc == -1 ) {
        LOG(ERROR, "Forking into subshell failed: %s", strerror(errno));
        return;
    }
    if( rc == 0 ) {
        // Child
        close(pty_master);
        setenv("TERM", term, 1);
        // Set RAW mode on slave side of PTY
        struct termios tsettings;
        tcgetattr(pty_slave, &tsettings);
        cfmakeraw(&tsettings);
        tcsetattr(pty_slave, TCSANOW, &tsettings);
        // Replace stdio
        if( dup2(pty_slave, STDIN_FILENO ) != STDIN_FILENO  ||
            dup2(pty_slave, STDOUT_FILENO) != STDOUT_FILENO ||
            dup2(pty_slave, STDERR_FILENO) != STDERR_FILENO ) {
                rt_fatal("Rmtsh subprocess failed to setup stdio: %s", strerror(errno));
        }
        // Make the current process a new session leader
        setsid();
        // As the child is a session leader, set the controlling terminal to be the slave side of the PTY
        // (Mandatory for programs like the shell to make them manage correctly their outputs)
        ioctl(0, TIOCSCTTY, 1);
        if( execlp("sh","sh",NULL) == -1 ) {
            rt_fatal("Rmtsh subprocess exec failed: %s", strerror(errno));
        }
    }
    // Parent
    close(pty_slave);
    free((void*)rmtsh->user);
    rmtsh->user = rt_strdup(user);
    rmtsh->mtime = rt_getTime();
    rmtsh->pid = rc;
    rmtsh->aio = aio_open(rmtsh, pty_master, up_read, NULL);
    up_read(rmtsh->aio);
    LOG(NOTICE, "Rmtsh#%d started (pid=%d)", (int)(rmtsh - rmtshTable), rmtsh->pid);
}


void s2e_handleRmtsh (s2ctx_t* s2ctx, ujdec_t* D) {
    ujcrc_t field;
    int start_idx = -1;
    int stop_idx = -1;
    str_t user = NULL;
    str_t term = "dumb";
    while( (field = uj_nextField(D)) ) {
        switch(field) {
        case J_msgtype: {
            uj_skipValue(D);
            break;
        }
        case J_user: {
            user = uj_str(D);
            break;
        }
        case J_start: {
            start_idx = uj_intRange(D, 0, MAX_RMTSH);
            break;
        }
        case J_stop: {
            stop_idx = uj_intRange(D, 0, MAX_RMTSH);
            break;
        }
        case J_term: {
            term = uj_str(D);
            break;
        }
        case J_MuxTime: {
            s2e_updateMuxtime(s2ctx, uj_num(D), 0);
            break;
        }
        default: {
            LOG(MOD_S2E|WARNING, "Unknown field in 'rmtsh' message - ignored: %s", D->field.name);
            uj_skipValue(D);
            break;
        }
        }
    }
    if( stop_idx >= 0 ) {
        LOG(DEBUG, "Rmtsh stop received idx=%d", stop_idx);
        stopRmtsh(&rmtshTable[stop_idx]);
    }
    if( start_idx >= 0 ) {
        LOG(DEBUG, "Rmtsh start received user=%s idx=%d", user, start_idx);
        startRmtsh(&rmtshTable[start_idx], user, term);
    }
    // Send back current status
    if( TC == NULL )
        return; // should not happen - we just got a 'rmtsh' request

    ujbuf_t sendbuf = (*TC->s2ctx.getSendbuf)(&TC->s2ctx, MIN_UPJSON_SIZE);
    if( sendbuf.buf == NULL ) {
        LOG(MOD_S2E|ERROR, "Failed to send 'rmtsh' response, no buffer space");
        return;
    }
    uj_encOpen(&sendbuf, '{');
    uj_encKVn(&sendbuf,
              "msgtype",   's', "rmtsh",
              "rmtsh",     '[', 0, NULL);
    for( int i=0; i<MAX_RMTSH; i++ ) {
        rmtsh_t* rmtsh = &rmtshTable[i];
        uj_encOpen(&sendbuf, '{');
        uj_encKVn(&sendbuf,
                  "user",      's', rmtsh->user==NULL ? "" : rmtsh->user,
                  "started",   'b', (rmtsh->aio != NULL),
                  "age",       'i', rmtsh->mtime==0 ? -1 : (int)((rt_getTime() - rmtsh->mtime)/1000000),  // in seconds
                  "pid",       'i', rmtsh->pid,
              NULL);
        uj_encClose(&sendbuf, '}');
    }
    uj_encClose(&sendbuf, ']');
    uj_encClose(&sendbuf, '}');
    (*s2ctx->sendText)(s2ctx, &sendbuf);
    LOG(MOD_S2E|VERBOSE, "Rmtsh response sent");
}


int s2e_onBinary (s2ctx_t* s2ctx, u1_t* data, ujoff_t len) {
    if( len == 0 ) {
        return 1;
    }
    if( data[0] >= MAX_RMTSH ) {
        LOG(MOD_S2E|ERROR, "Illegal rmtsh session: %d", data[0]);
        return 1;
    }
    rmtsh_t* rmtsh = &rmtshTable[data[0]];
    if( rmtsh->aio == NULL ) {
        LOG(MOD_S2E|ERROR, "Dropping data for stopped rmtsh#%d", data[0]);
        return 1;
    }
    dn_fill(rmtsh, data+1, len-1);
    return 1;
}


#endif // !defined(CFG_no_rmtsh)
