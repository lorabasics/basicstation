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

int  fs_open   (str_t filename, int mode, ...);
int  fs_read   (int fd,       void* buf, int size);
int  fs_write  (int fd, const void* buf, int size);
int  fs_close  (int fd);
int  fs_rename (str_t from, str_t to);
int  fs_unlink (str_t from);
int  fs_chdir  (str_t dir);
int  fs_access (str_t fn, int mode);
int  fs_stat   (str_t fn, struct stat* st);
int  fs_lseek  (int fd, int offset, int whence);
void fs_sync   ();

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
