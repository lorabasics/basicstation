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

#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include "rt.h"
#include "sys.h"
#include "sys_linux.h"


#define LOG_LAG         100  // millis
#define LOG_OUTSIZ     8192
#define LOG_HIGHWATER (LOG_OUTSIZ/2)
#define MAX_LOGHDR      64

static struct logfile* logfile;

static tmr_t delay;               // wait until we flush
static char  outbuf[LOG_OUTSIZ];  // buffer output
static int   outfill = 0;         // write index

static aio_t* stdout_aio;         //
static char   stdout_buf[MAX_LOGHDR+PIPE_BUF];
static int    stdout_idx = MAX_LOGHDR;


static pthread_mutex_t  mxfill  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  mxcond  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   condvar = PTHREAD_COND_INITIALIZER;
static pthread_t        thr;
static int              thrUp = 0;

static int orig_stderr = STDERR_FILENO;

// Fwd decl
static void addLog (const char *logline, int len);

// Collect stdout/stderr and turn into log messages
static void stdout_read(aio_t* aio) {
    while(1) {
        int n = read(aio->fd, &stdout_buf[stdout_idx], sizeof(stdout_buf)-stdout_idx);
        if( n == 0 ) {
            // EOF - should never happen unless this process is exiting
            LOG(ERROR, "Stdout pipe - EOF");
            return;
        }
        if( n == -1 ) {
            if( errno != EAGAIN )
                LOG(ERROR, "Stdout pipe read fail: %s", strerror(errno));
            return;
        }
        // Find \n and flush up to this point - don't tear lines apart
        stdout_idx += n;
        int end = stdout_idx;
        while( end > MAX_LOGHDR && stdout_buf[end-1] != '\n' )
            end -= 1;
        if( end == MAX_LOGHDR && stdout_idx >= sizeof(stdout_buf) )
            end = stdout_idx;   // buffer is full flush even if we have no newline
        if( end > MAX_LOGHDR ) {
            dbuf_t lbuf;
            if( log_special(MOD_SIO|INFO, &lbuf) ) {
                int n = min(MAX_LOGHDR, lbuf.pos);
                char* p = &stdout_buf[MAX_LOGHDR-n];
                char* e = &stdout_buf[end];
                memcpy(p, lbuf.buf, n);
                if( e[-1] != '\n') {
                    // make sure we have a trailing new line
                    if( e < &stdout_buf[sizeof(stdout_buf)] ) {
                        e[0] = '\n';   // enough space - append one
                        e += 1;
                    } else {
                        e[-1] = '\n';  // overwrite last char - sorry, but proper line break is important
                    }
                }
                addLog(p, e-p);
            }
            n = stdout_idx - end;
            if( n > 0 )
                memcpy(&stdout_buf[MAX_LOGHDR], &stdout_buf[end], n);
            stdout_idx = MAX_LOGHDR+n;
        }
    }
}


static void writeLogData (const char *data, int len) {
    if( !logfile || !logfile->path ) {
      log2stderr:
        if( write(orig_stderr, data, len) == -1 )
            sys_fatal(FATAL_NOLOGGING);
        return;
    }
    struct stat st = { .st_size = 0 };
    if( stat(logfile->path, &st) == -1 && errno != ENOENT ) {
        fprintf(stderr,"Failed to stat log file %s: %s\n", logfile->path, strerror(errno));
        goto log2stderr;
    }
    if( st.st_size >= logfile->size ) {
        int flen = strlen(logfile->path);
        char fn[flen + 15];
        struct timespec min_ctim;
        int logfno = -1;
        strcpy(fn, logfile->path);
        for( int i=0; i<logfile->rotate; i++ ) {
            snprintf(fn+flen, 15, ".%d", i);
            if( stat(fn, &st) == -1 ) {
                if( errno != ENOENT )
                    fprintf(stderr,"Failed to stat log file %s: %s\n", fn, strerror(errno));
                logfno = i;
                break;
            }
            if( logfno < 0 || min_ctim.tv_sec > st.st_ctim.tv_sec ) {
                min_ctim.tv_sec = st.st_ctim.tv_sec;
                logfno = i;
            }
        }
        if( unlink(fn) == -1 && errno != ENOENT )
            fprintf(stderr,"Failed to unlink log file %s: %s\n", fn, strerror(errno));
        if( rename(logfile->path, fn) == -1 ) {
            fprintf(stderr,"Failed to rename log file %s => %s: %s\n", logfile->path, fn, strerror(errno));
            if( unlink(logfile->path) == -1 )
                fprintf(stderr,"Failed to unlink log file %s: %s\n", logfile->path, strerror(errno));
        }
    }
    int fd = open(logfile->path, O_CREAT|O_APPEND|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP);
    if( fd == -1 ) {
        fprintf(stderr,"Failed to open log file %s: %s\n", logfile->path, strerror(errno));
        goto log2stderr;
    }
    int n;
    if( (n = write(fd, data, len)) != len ) {
        fprintf(stderr,"Partial write to log file %s: %s\n", logfile->path, strerror(errno));
        close(fd);
        goto log2stderr;
    }
    close(fd);
}


static void addLog (const char *logline, int len) {
    if( !thrUp ) {
        writeLogData(logline, len);
        return;
    }
    pthread_mutex_lock(&mxfill);
    int k = min(LOG_OUTSIZ-outfill, len);
    memcpy(outbuf + outfill, logline, k);
    outfill += k;
    int notify = (len == 0 || outfill >= LOG_HIGHWATER);
    pthread_mutex_unlock(&mxfill);
    if( notify ) {
        pthread_cond_signal(&condvar);
    } else if( delay.next == TMR_NIL ) {
        // Delay timer not running
        rt_setTimer(&delay, rt_millis_ahead(LOG_LAG));
    }
}


static void on_delay (tmr_t* tmr) {
    pthread_mutex_lock(&mxfill);
    if( outfill )
        pthread_cond_signal(&condvar);
    pthread_mutex_unlock(&mxfill);
}


static void thread_log (void) {
    pthread_mutex_lock(&mxcond);
    while(1) {
        pthread_cond_wait(&condvar, &mxcond);
        pthread_mutex_lock(&mxfill);
        int len = outfill;
        pthread_mutex_unlock(&mxfill);
        if( len ) {
            writeLogData(outbuf, len);
            pthread_mutex_lock(&mxfill);
            if( len < outfill ) {
                memcpy(&outbuf[0], &outbuf[len], outfill-len);
                outfill -= len;
            } else {
                outfill = 0;
            }
            pthread_mutex_unlock(&mxfill);
        }
    }
}


void sys_flushLog (void) {
    fflush(stdout);
    fflush(stderr);
    pthread_mutex_lock(&mxcond);
    pthread_mutex_lock(&mxfill);
    writeLogData(outbuf, outfill);
    outfill = 0;
    pthread_mutex_unlock(&mxfill);
    pthread_mutex_unlock(&mxcond);
}


void sys_addLog (const char *logline, int len) {
    if( len == 0 ) {
        sys_flushLog();
    } else {
        addLog(logline, len);
    }
}


void sys_iniLogging (struct logfile* lf, int captureStdio) {
    logfile = lf;
    if( logfile->path && captureStdio ) {
        // Replace stdout/stderr with a pipe and drain its data
        // into the configured log file (only on master, slaves inherit these pipes)
        if( stdout_aio == NULL ) {
            int fds[2] = { -1, -1 };
            if( pipe2(fds, O_NONBLOCK) == -1 ) {
                rt_fatal("Failed to create stdout/stderr pipe: %s", strerror(errno));
            }
            int flags = fcntl(fds[1], F_GETFL, 0);
            if( flags != -1 )
                fcntl(fds[1], F_SETFL, flags & ~O_NONBLOCK);
            orig_stderr = dup(STDERR_FILENO);
            dup2(fds[1], STDOUT_FILENO);
            dup2(fds[1], STDERR_FILENO);
            close(fds[1]);
            int fd = open("/dev/null", O_RDONLY);
            dup2(fd, STDIN_FILENO);
            close(fd);

            stdout_idx = MAX_LOGHDR;
            stdout_aio = aio_open(stdout_buf, fds[0], stdout_read, NULL);
            stdout_read(stdout_aio);
        }
    } else {
        aio_close(stdout_aio);
        stdout_aio = NULL;
    }
    atexit(sys_flushLog);

}


void sys_startLogThread () {
    if( !thrUp ) {
        if( pthread_create(&thr, NULL, (void * (*)(void *))thread_log, NULL) != 0 )
            sys_fatal(FATAL_PTHREAD);
        rt_iniTimer(&delay, on_delay);
        thrUp = 1;
    }
}
