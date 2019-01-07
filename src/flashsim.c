// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

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
