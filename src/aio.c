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

#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include "rt.h"


enum { N_AIO_HANDLES = 10 };
static aio_t aioHandles[N_AIO_HANDLES];


// There aren't that many fd open thus recalc every time we go into select
//int aio_maxfd;
//fd_set aio_rdset;
//fd_set aio_wrset;

aio_t* aio_open(void* ctx, int fd, aiofn_t rdfn, aiofn_t wrfn) {
    assert(ctx != NULL);
    for( int i=0; i < N_AIO_HANDLES; i++ ) {
        if( NULL == aioHandles[i].ctx ) {
            aioHandles[i].ctx = ctx;
            aioHandles[i].fd  = fd;
            aioHandles[i].rdfn = rdfn;
            aioHandles[i].wrfn = wrfn;
            int flags;
            if( (flags = fcntl(fd, F_GETFD, 0)) == -1 ||
                fcntl(fd, F_SETFD, flags|FD_CLOEXEC) == -1 )
                LOG(MOD_AIO|ERROR, "fcntl(fd, F_SETFD, FD_CLOEXEC) failed: %s", strerror(errno));
            return &aioHandles[i];
        }
    }
    rt_fatal("Out of AIO handles");
    return NULL;
}


aio_t* aio_fromCtx(void* ctx) {
   for( int i=0; i < N_AIO_HANDLES; i++ ) {
        if( ctx == aioHandles[i].ctx )
            return &aioHandles[i];
   }
   return NULL;
}


void aio_close (aio_t* aio) {
    if( aio == NULL )
        return;
    if( aio->fd >= 0 ) {
        close(aio->fd);
        aio->fd = -1;
    }
    assert(aio >= aioHandles && aio < &aioHandles[N_AIO_HANDLES]);
    memset(aio, 0, sizeof(*aio));
}


void aio_set_rdfn (aio_t* aio, aiofn_t rdfn) {
    assert(aio->ctx != NULL && aio->fd >= 0);
    aio->rdfn = rdfn;
}


void aio_set_wrfn (aio_t* aio, aiofn_t wrfn) {
    assert(aio->ctx != NULL && aio->fd >= 0);
    aio->wrfn = wrfn;
}


#if defined(CFG_timerfd)
#include <sys/timerfd.h>

static int timerFD;
#endif // CFG_timerfd


void aio_loop () {
    while(1) {
        int n, maxfd;
        fd_set rdset;
        fd_set wrset;
        do {
            maxfd = -1;
            FD_ZERO(&rdset);
            FD_ZERO(&wrset);
            struct timeval *ptimeout = NULL;
#if defined(CFG_timerfd)
            ustime_t deadline = rt_processTimerQ();
            if( deadline != USTIME_MAX ) {
                struct itimerspec spec;
                memset(&spec, 0, sizeof(spec));
                spec.it_value.tv_sec = deadline / rt_seconds(1);
                spec.it_value.tv_nsec = (deadline % rt_seconds(1)) * 1000;
                if( timerfd_settime(timerFD, TFD_TIMER_ABSTIME, &spec, NULL) == -1 )
                    rt_fatal("timerfd_settime failed: %s", strerror(errno));      // LCOV_EXCL_LINE
                FD_SET(timerFD, &rdset);
                maxfd = max(maxfd, timerFD);
            }
#else // !defined(CFG_timerfd)
            struct timeval timeout;
            ustime_t ahead = rt_processTimerQ();
            if( ahead != USTIME_MAX ) {
                ptimeout = &timeout;
                timeout.tv_sec = ahead / rt_seconds(1);
                timeout.tv_usec = ahead % rt_seconds(1);
            }
#endif // !defined(CFG_timerfd)
            for( int i=0; i < N_AIO_HANDLES; i++ ) {
                aio_t* aio = &aioHandles[i];
                if( !aio->ctx )
                    continue;
                int fd = aio->fd;
                if( aio->rdfn ) FD_SET(fd, &rdset);
                if( aio->wrfn ) FD_SET(fd, &wrset);
                maxfd = max(maxfd, fd);
            }
            n = select(maxfd+1, &rdset, &wrset, NULL, ptimeout);
        } while( n == -1 && errno == EINTR );
#if defined(CFG_timerfd)
        if( FD_ISSET(timerFD, &rdset) ) {
            u1_t buf[8];
            int err;
            while( (err = read(timerFD, buf, sizeof(buf))) > 0 );
            if( err != -1 || errno != EAGAIN )
                rt_fatal("Failed to read timerfd: err=%d %s\n", err, strerror(errno));     // LCOV_EXCL_LINE
            rt_processTimerQ();
            n--;
        }
#endif // defined(CFG_timerfd)
        for( int i=0; n > 0 && i < N_AIO_HANDLES; i++ ) {
            aio_t* aio = &aioHandles[i];
            if( !aio->ctx )
                continue;
            if( FD_ISSET(aio->fd, &rdset) && aio->rdfn ) {
                aio->rdfn(aio);
                n--;
            }
            if( FD_ISSET(aio->fd, &wrset) && aio->wrfn ) {
                aio->wrfn(aio);
                n--;
            }
        }
    }
}


void aio_ini () {
   for( int i=0; i < N_AIO_HANDLES; i++ )
       aioHandles[i].fd = -1;
#if defined(CFG_timerfd)
    timerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK|TFD_CLOEXEC);
    if( timerFD == -1 )
        rt_fatal("timerfd_create failed: %s", strerror(errno));      // LCOV_EXCL_LINE
#endif // defined(CFG_timerfd)
}

