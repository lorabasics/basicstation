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

#if defined(CFG_linux) || defined(CFG_flashsim)
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "selftests.h"
#include "rt.h"
#include "fs.h"


static void  prt (u1_t mod_level, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputs("\n", stderr);
    va_end(ap);
}

static fsinfo_t printFsInfo (const char* msg, fsinfo_t* ip) {
    fsinfo_t i;
    if( ip ) {
        i = *ip;
    } else {
        fs_info(&i);
    }
    fprintf(stderr, "-- %s -- Flash Info:\n"
            "  fbase=0x%08X pagecnt=%d pagesize=0x%X\n"
            "  active: section %c\n"
            "  gc cycle: %d\n"
            "  records=%d\n"
            "  used=%d bytes\n"
            "  free=%d bytes\n"
            "  key=%08X-%08X-%08X-%08X\n",
            msg,
            i.fbase, i.pagecnt, i.pagesize,
            i.activeSection+'A',
            i.gcCycles,
            i.records, i.used, i.free,
            i.key[0], i.key[1], i.key[2], i.key[3]);
    return i;
}


#define TNORM(i, fn, expfn)                                     \
    sz = fs_fnNormalize(fn, norm, sizeof(norm));                \
    fprintf(stderr, "FN%d: (%d) %s\n", i, sz, norm);            \
    TCHECK(sz == strlen(expfn)+1);                              \
    TCHECK(strcmp(norm,expfn)==0);


void selftest_fs () {
    char norm[32];
    int sz, err, ok;

    err = fs_chdir("/s2");
    TCHECK(err==0);

    TNORM( 1, ".",  "/s2");
    TNORM( 2, "./", "/s2");
    TNORM( 3, "",   "/s2");

    TNORM( 4, "abc",           "/s2/abc");
    TNORM( 5, "abc/.",         "/s2/abc");
    TNORM( 6, "abc/..///abc",  "/s2/abc");
    TNORM( 7, "abc/d/e/f/../../..", "/s2/abc");
    TNORM( 8, "abc/d/e/f/../../../", "/s2/abc");
    TNORM( 9, "abc/d/e/f/../../../", "/s2/abc");
    TNORM(10, "../..",    "/");
    TNORM(11, "../../",   "/");
    TNORM(12, "../../.",  "/");
    TNORM(13, "../.././", "/");

    sz = fs_fnNormalize("tooloooooooooooooooooooooooooooooooog", norm, sizeof(norm));
    fprintf(stderr, "FN14: (%d) %s\n", sz, norm);
    TCHECK(sz == 0);

    err = fs_chdir("s3");
    TCHECK(err==0);
    TNORM(50, ".",  "/s2/s3");

    err = fs_chdir("/s2");
    TCHECK(err==0);
    TNORM(51, ".",  "/s2");

    err = fs_chdir("/s3");
    TCHECK(err==0);
    TNORM(52, ".",  "/s3");

    err = fs_chdir(NULL);
    TCHECK(err==0);
    TNORM(53, ".",  "/s2");


    fs_erase();
    u4_t key[4] = {0x71593cbf,0x81db1a48,0x22fc47fe,0xe8cf23ea};
    fs_ini(key);

    ok = fs_dump(prt);
    TCHECK(ok);

    char fnbuf[4];
    int fds[10];
    int nfd = 0;
    while( nfd < 10 ) {
        fnbuf[0] = 'a'+nfd;
        fnbuf[1] = 0;
        fds[nfd] = fs_open(fnbuf, O_CREAT|O_TRUNC|O_WRONLY, 0777);
        if( fds[nfd] == -1 ) {
            TCHECK(errno==ENFILE);
            break;
        }
        nfd++;
    }
    TCHECK(nfd<10);
    while( --nfd >= 0 ) {
        err = fs_close(fds[nfd]);
        TCHECK(err==0);
    }

    ok = fs_dump(prt);
    TCHECK(ok);

    int fd;
    u1_t sample[10000-8];
    u1_t buf[10000-8];
    for( int i=0; i<sizeof(sample); i++ )
        sample[i] = i;

    int sched[][3] = {
        {  0, 0, 0 },
        {  1, 1, 1 },
        {  1, 0, 0 },
        {  3, 1, 2 },
        {  3, 2, 1 },
        {  3, 2, 1 },
        {  7, 4, 4 },
        {  7, 3, 3 },
        {  7, 4, 3 },
        {  7, 3, 4 },
        {  8, 8, 8 },
        {  1024,    0,    0 },
        {  1024,  512,  512 },
        {  1024,  511,  511 },
        {  1024,  513,  513 },
        {  1024,  511,  513 },
        {  1024,  513,  511 },
        {  1024,    0,    0 },
        {  1027,  512,  512 },
        {  1027,  511,  511 },
        {  1027,  513,  513 },
        {  1027,  511,  513 },
        {  1027,  513,  511 },
    };

    for( int t=0; t<SIZE_ARRAY(sched); t++ ) {
        int L  = sched[t][0];
        int w1 = sched[t][1];
        int r1 = sched[t][2];
        assert(w1 <= L && r1 <= L);
        memset(buf, 0xAC, sizeof(buf));

        printf("FS r/w test: L=%d/w1=%d/r1=%d\n", L, w1, r1);
        fd = fs_open("test1", O_CREAT|O_TRUNC|O_WRONLY, 0777);
        TCHECK(fd>=0);
        for( int wi=0; wi<2; wi++ ) {
            int b = wi==0 ? 0 : w1;
            int l = wi==0 ? w1 : L-w1;
            int n = fs_write(fd, sample+b, l);
            TCHECK(l==n);
        }
        err = fs_close(fd);
        TCHECK(err==0);

        fd = fs_open("test1", O_RDONLY);
        TCHECK(fd>=0);
        for( int ri=0; ri<2; ri++ ) {
            int b = ri==0 ? 0 : r1;
            int l = ri==0 ? r1 : L-r1;
            int n = fs_read(fd, buf+b, l);
            TCHECK(l==n);
        }
        int n = fs_read(fd, buf, 1);
        TCHECK(n==0);
        err = fs_close(fd);
        TCHECK(err==0);

        TCHECK(memcmp(sample, buf, L)==0);
        TCHECK(buf[L+0]==0xAC);
        TCHECK(buf[L+1]==0xAC);
    }
    ok = fs_dump(prt);
    TCHECK(ok);

    err = fs_ck();
    TCHECK(err==1);


    int n;

    fd = fs_open("ino1", O_CREAT|O_TRUNC|O_WRONLY, 0777);
    n = fs_write(fd, sample, 11);
    err = fs_close(fd);
    TCHECK(fd >= 0 && err == 0 && n == 11);
    struct stat st1;
    err = fs_stat("ino1", &st1);
    TCHECK(err==0);
    fprintf(stderr, "ino1 -- ino=%d size=%d ctim=%ds ago\n",
            (int)st1.st_ino, (int)st1.st_size, (int)(rt_getUTC()/rt_seconds(1)-st1.st_ctim.tv_sec));
    TCHECK(st1.st_size == 11);

    err = fs_rename("ino1","ino1a");
    TCHECK(err==0);
    err = fs_rename("ino1a","ino1b");
    TCHECK(err==0);

    struct stat st2;
    err = fs_stat("ino1b", &st2);
    TCHECK(err==0);
    TCHECK(memcmp(&st1, &st2, sizeof(st1))==0);

    struct stat st3;
    err = fs_stat("ino1a", &st3);
    TCHECK(err==-1 && errno==ENOENT);
    err = fs_stat("ino1", &st3);
    TCHECK(err==-1 && errno==ENOENT);

    fd = fs_open("ino1b", O_CREAT|O_APPEND|O_WRONLY, 0777);
    n = fs_write(fd, sample, 17);
    err = fs_close(fd);
    TCHECK(fd >= 0 && err == 0 && n == 17);

    err = fs_stat("ino1b", &st3);
    TCHECK(err==0 && st3.st_size==28 && st3.st_ino == st1.st_ino);

    err = fs_unlink("ino1b");
    TCHECK(err==0);

    err = fs_access("ino1b", F_OK);
    TCHECK(err==-1 && errno==ENOENT);

    ok = fs_dump(prt);
    TCHECK(ok);

    printFsInfo("Before GC1", NULL);
    fs_gc(0);
    printFsInfo("After GC1", NULL);

    ok = fs_dump(prt);
    TCHECK(ok);

    err = fs_ck();
    TCHECK(err==1);

    fd = fs_open("a.log", O_CREAT|O_APPEND|O_WRONLY, 0777);
    n = fs_write(fd, sample, 17);
    err = fs_close(fd);
    TCHECK(fd >= 0 && err == 0 && n == 17);

    fd = fs_open("a.log.1", O_CREAT|O_APPEND|O_WRONLY, 0777);
    n = fs_write(fd, sample, 17);
    err = fs_close(fd);
    TCHECK(fd >= 0 && err == 0 && n == 17);

    ok = fs_dump(prt);
    TCHECK(ok);

    printFsInfo("Before GC2", NULL);
    fs_gc(1);    // emergengy GC - drop all log files
    printFsInfo("After GC2", NULL);

    ok = fs_dump(prt);
    TCHECK(ok);

    err = fs_ck();
    TCHECK(err==1);

    // ----------------------------------------
    // Fill up flash and test GC

    int fd1 = fs_open("rd", O_CREAT|O_APPEND|O_WRONLY, 0777);
    n = fs_write(fd1, sample, 64);
    err = fs_close(fd);
    TCHECK(fd1 >= 0 && err == 0 && n == 64);
    fd1 = fs_open("rd", O_RDONLY);
    n = fs_read(fd1, buf, 16);
    TCHECK(fd1 >= 0 && n == 16 && memcmp(sample, buf, 16) == 0);
    // keep this file open for read - after GC file pointer should be ok

    int fd2 = fs_open("del", O_CREAT|O_APPEND|O_WRONLY, 0777);
    int n1 = fs_write(fd2, sample, sizeof(sample));
    int n2 = fs_write(fd2, sample, sizeof(sample));
    // fd2 open will be invalidate after first GC
    TCHECK(fd2 >= 0 && err == 0 && n1 == sizeof(sample) && n2 == sizeof(sample));
    err = fs_unlink("del");
    TCHECK(err == 0);
    // if /s2/del is scrapped in first normal GC
    // it frees up:
    //   - 2 DATA records: 2 x sample + meta (20K)
    //   - FILE record: 24 bytes
    //   - DELETE record: 24 bytes
    // ==> 20048

    // /s2/a.log - can be scrapped in emergency GC
    // if will free up:
    //  - 1 DATA records: 10K = 1 sample + meta
    //  - FILE record: 16+3*4 => 28
    // ==> 10028
    fd = fs_open("a.log", O_CREAT|O_APPEND|O_WRONLY, 0777);
    n = fs_write(fd, sample, sizeof(sample));
    TCHECK(fd >= 0 && n == sizeof(sample));

    fsinfo_t i1 = printFsInfo("Flash full test begin", NULL);
    fsinfo_t i2, i3;
    fprintf(stderr, "Write size: raw=%d + meta=%d => %d bytes\n",
            (int)sizeof(sample), 8, (int)sizeof(sample)+8);

    // big is filled until flash full - first
    fd = fs_open("big", O_CREAT|O_APPEND|O_WRONLY, 0777);
    TCHECK(fd >= 0);
    n1 = 0;
    do {
        i3 = i2;  // just before GC
        n = fs_write(fd, sample, sizeof(sample));
        TCHECK(n == sizeof(sample));
        n1 += 1;
        fs_info(&i2);
        if( i2.free < 4*sizeof(sample) ) {
            fprintf(stderr, "Free before GC1 (%c) write #%-4d : %6d bytes\n",
                    i2.activeSection+'A', n1, i2.free);
        }
    } while( i2.activeSection == i1.activeSection );

    fprintf(stderr,
            "Free just before GC1     (%c): %6d bytes\n"
            "Free after GC1 + 1 write (%c): %6d bytes\n",
            i3.activeSection+'A', i3.free, i2.activeSection+'A', i2.free);

    printFsInfo("--- 1st GC due to flash full", &i2);
    err = fs_access("del", F_OK);
    TCHECK(err=-1 && errno==ENOENT);   // del is gone: 2 x sample + meta (20K) + ino (20 bytes)
    err = fs_access("a.log", F_OK);
    TCHECK(err==0);
    // del is gone now - we kept fd2 but file is gone
    // fd2 should have been invalidated
    n = fs_write(fd2, sample, 10);
    TCHECK(n==-1 && errno==EBADF);
    fs_close(fd2);
    // we should have space for this
    n = fs_write(fd, sample, sizeof(sample));
    TCHECK(n == sizeof(sample));
    i3 = printFsInfo("Just before write triggering GC + emergency GC", NULL);
    // this will trigger next GC
    // next write won't fit and will trigger GC + emergency GC wiping out a.log
    n = fs_write(fd, sample, sizeof(sample));
    TCHECK(n == sizeof(sample));
    i2 = printFsInfo("Flash after emergency GC", NULL);
    TCHECK(i2.activeSection == i3.activeSection && i2.gcCycles == i3.gcCycles+2);

    // there is nothing to free anymore - next write will trigger flash full
    n = fs_write(fd, sample, sizeof(sample));
    TCHECK(n == -1 && errno == ENOSPC);
    err = fs_close(fd);
    TCHECK(err == 0);
    i3 = printFsInfo("Flash after triggering GC + emergency GC + still not enough space", NULL);
    TCHECK(i3.activeSection == i2.activeSection && i3.gcCycles == i2.gcCycles+2);

    // Check fd1 - read file pointer should be still ok
    n = fs_read(fd1, buf, 16);
    TCHECK(n == 16 && memcmp(sample+16, buf, 16) == 0);
    err = fs_close(fd1);
    TCHECK(err == 0);

    fs_close(fd);
    fs_close(fd1);
}

#endif
