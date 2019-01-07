// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#ifndef _fs_h_
#define _fs_h_

void  sys_eraseFlash (u4_t faddr, uint pagecnt);
void  sys_writeFlash (u4_t faddr, u4_t* data, uint u4cnt);
void  sys_readFlash  (u4_t faddr, u4_t* data, uint u4cnt);
u4_t* sys_ptrFlash   ();
void  sys_iniFlash   ();

u4_t rdFlash1 (u4_t faddr);
void rdFlashN (u4_t faddr, u4_t* daddr, uint u4cnt);

void wrFlash1 (u4_t faddr, u4_t data);
void wrFlashN (u4_t faddr, u4_t* daddr, uint u4cnt, int keepData);

int fs_open   (str_t filename, int mode, ...);
int fs_read   (int fd,       void* buf, int size);
int fs_write  (int fd, const void* buf, int size);
int fs_close  (int fd);
int fs_rename (str_t from, str_t to);
int fs_unlink (str_t from);
int fs_chdir  (str_t dir);
int fs_access (str_t fn, int mode);
int fs_stat   (str_t fn, struct stat* st);
int fs_lseek  (int fd, int offset, int whence);

int  fs_fnNormalize (const char* fn, char* wb, int maxsz);

int  fs_ini   (u4_t key[4]);
int  fs_ck    ();
void fs_erase ();
void fs_gc    (int emergency);
int  fs_dump  (void (*logfn)(u1_t mod_level, const char* fmt, ...));
int  fs_shell (char* cmdline);

typedef struct fsinfo {
    void* fbasep;
    u4_t  fbase;
    u2_t  pagecnt;
    u2_t  pagesize;
    u1_t  activeSection;
    u2_t  gcCycles;
    u4_t  records;
    u4_t  used;
    u4_t  free;
    u4_t  key[4];
} fsinfo_t;

void fs_info(fsinfo_t* infop);

#endif // _fs_h_
