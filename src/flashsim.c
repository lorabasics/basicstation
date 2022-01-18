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

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "s2conf.h"
#include "sys.h"
#include "fs.h"


static int   fd;
static u1_t* mem;


u4_t* sys_ptrFlash () {
    return (u4_t*)mem;
}

void sys_eraseFlash (u4_t faddr, uint pagecnt) {
    assert((faddr&(FLASH_PAGE_SIZE-1)) == 0);
    memset(&mem[faddr-FLASH_ADDR], FLASH_ERASED&0xFF, pagecnt*FLASH_PAGE_SIZE);
    if( msync(mem, FLASH_SIZE, MS_SYNC) == -1 )
        LOG(MOD_SYS|ERROR, "Flash simulation - msync failed: %s", strerror(errno));
}

void sys_writeFlash (u4_t faddr, u4_t* data, uint u4cnt) {
    assert((faddr&3) == 0 && faddr >= FLASH_ADDR && faddr+u4cnt*4 <= FLASH_ADDR+FLASH_SIZE);
    memcpy(&mem[faddr-FLASH_ADDR], data, u4cnt*4);
    if( msync(mem, FLASH_SIZE, MS_SYNC) == -1 )
        LOG(MOD_SYS|ERROR, "Flash simulation - msync failed: %s", strerror(errno));
}

void sys_readFlash  (u4_t faddr, u4_t* data, uint u4cnt) {
    assert((faddr&3) == 0 && faddr >= FLASH_ADDR && faddr+u4cnt*4 <= FLASH_ADDR+FLASH_SIZE);
    memcpy(data, &mem[faddr-FLASH_ADDR], u4cnt*4);
}

void sys_iniFlash () {
    if( mem ) return;
    str_t fsimFn = sys_makeFilepath("./station.flash", 0);
    str_t op = "";
    fd = open(fsimFn, O_CREAT|O_APPEND|O_RDWR, S_IRUSR|S_IWUSR);
    if( fd == -1 ) {
      err:
        op = "open";
        rt_fatal("Cannot %s flash file '%s': %s", op, fsimFn, strerror(errno));
    }
    off_t flen = lseek(fd, 0, SEEK_END);
    if( flen == -1 ) {
        op = "lseek";
        goto err;
    }
    if( flen < FLASH_SIZE ) {
        u1_t pg[FLASH_PAGE_SIZE];
        memset(pg, FLASH_ERASED&0xFF, sizeof(pg));
        while( flen < FLASH_SIZE ) {
            if( write(fd, pg, FLASH_PAGE_SIZE) == -1 ) {
                op = "write";
                goto err;
            }
            flen += FLASH_PAGE_SIZE;
        }
        fsync(fd);
    }
    mem = mmap(NULL, FLASH_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if( mem==MAP_FAILED ) {
        op = "mmap";
        goto err;
    }
    free((void*)fsimFn);
}

#endif // defined(CFG_flashsim)
