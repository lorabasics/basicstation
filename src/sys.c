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

#include <stdio.h>
#include <fcntl.h>


#if defined(CFG_linux) || defined(CFG_flashsim)
#include <sys/stat.h>
#include <errno.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#endif

#include "sys.h"
#include "uj.h"
#include "s2conf.h"
#include "fs.h"
#include "net.h" // uri_checkHostPortUri

str_t  homeDir;
str_t  tempDir;
str_t  webDir;

uL_t   protoEUI     = 0;
uL_t   prefixEUI    = 1;           // 1 -> *:ffe:* MAC => EUI scheme
s1_t   sys_slaveIdx = -1;

// Standard config files:
//   {tc,cups}{,-bootstrap,-bak}.{uri,key,crt,trust}
// Temporary files:
//   {tc,cups}-temp.{uri,key,crt,trust}
// Extra marker files for forward recovery:
//   {tc,cups}-temp.upd   -- rename temp set into regular set - created at end of temp set write
//   {tc,cups}-temp.cpy   -- copy regular set into bak files, create before 1st copy, deleted after last
//   {tc,cups}-done.bak   -- copy is valid
//
enum { FN_TRUST,FN_CRT , FN_KEY , FN_URI , nFN_EXT };  // ext - extension
enum { FN_REG , FN_BAK , FN_BOOT, FN_TEMP, nFN_SET };  // set - set
enum { FN_UPD , FN_CPY , FN_DON          , nFN_TAF };  // taf - transaction files
enum { FN_CUPS, FN_TC                    , nFN_CAT };  // cat - category

static const char sFN_CAT[] = "cups\0" "tc\0.."                         "?";  // 2 x 5
static const char sFN_SET[] = "\0....." "-bak\0." "-boot\0" "-temp\0"   "?";  // 4 x 6
static const char sFN_EXT[] = "trust\0" "crt\0.." "key\0.." "uri\0.."   "?";  // 4 x 6
static const char sFN_TAF[] = "-temp.upd\0" "-temp.cpy\0" "-bak.done\0" "?";  // 3 x 10

static char* CFNS[nFN_CAT*(nFN_SET * nFN_EXT + nFN_TAF)];
static u1_t  bakDone[nFN_CAT];
static char  uriCache[nFN_SET-1][MAX_URI_LEN];
static char* pendData;

enum { UPD_CUPS=1<<FN_CUPS, UPD_TC=1<<FN_TC, UPD_ERROR=0xFF };;
static u1_t updateState;

#define categoryName(cat) (&sFN_CAT[cat*5])
#define configFilename(cat,set,ext) (CFNS[(cat)*(nFN_SET*nFN_EXT + nFN_TAF)+((set)*nFN_EXT)+(ext)])
#define transactionFilename(cat,taf) (CFNS[(cat)*(nFN_SET*nFN_EXT + nFN_TAF)+(nFN_SET*nFN_EXT)+(taf)])

str_t sys_credcat2str (int cred_cat) { return categoryName(cred_cat); }
str_t sys_credset2str (int cred_set) { return &sFN_SET[cred_set*6]; }


static int sizeFile (str_t file) {
    struct stat st;
    if( fs_stat(file, &st) == -1 )
        return -1; // No such file / cannot stat
    return st.st_size;
}

char* makeFilepath (const char* prefix, const char* suffix, char** pCachedFile, int isReadable) {
    if( pCachedFile )
        rt_free(*pCachedFile);
    char filepath[MAX_FILEPATH_LEN];
    dbuf_t b = dbuf_ini(filepath);
    if( strncmp(prefix, "~temp/", 6) == 0 ) {
        prefix += 6;
        xputs(&b, tempDir, -1);
    }
    else if( prefix[0] != '/' && (prefix[0] != '.' || prefix[1] != '/') ) {
        if( prefix[0] == '~' && prefix[1] == '/' )
            prefix += 2;
        xputs(&b, homeDir, -1);
    }
    for( int fnx=0; fnx < 2; fnx++ ) {
        str_t fni = fnx==0 ? prefix : suffix;
        char c;
        while( (c=*fni++) != 0 ) {
            if( c == '#' ) {
                if( sys_slaveIdx >= 0 )
                    xprintf(&b, "-%d", sys_slaveIdx);
            }
            else if( c == '?' ) {
                xprintf(&b, "%d", sys_slaveIdx >= 0 ? sys_slaveIdx : 0);
            } else {
                xputs(&b, &c, 1);
            }
        }
    }
    if( !xeos(&b) )
        rt_fatal("File path too big: %s", b.buf);
    if( isReadable && fs_access(b.buf, R_OK) != 0 )
        b.buf[0] = b.pos = 0;
    char* cachedFile = b.buf[0] ? rt_strdup(b.buf) : NULL;
    if( pCachedFile )
        *pCachedFile = cachedFile;
    return cachedFile;
}

dbuf_t readFile (str_t file, int complain) {
    dbuf_t b = { .buf=NULL, .bufsize=0, .pos=0 };
    int fsize;
    int fd, n;

    if( file == NULL )
        return b;
    // NOTE: we use this to read and /sys/class/net/*/address
    // For the latter stat reports a file size of 4K but read returns a smaller number!
    if( (fd = fs_open(file, O_RDONLY)) == -1 ||
        (fsize = sizeFile(file)) == -1 ||
        fsize > MAX_DOFF ||
        (n = fs_read(fd, b.buf = rt_mallocN(char, fsize+1), fsize)) == -1 ) {
        if( complain )
            LOG(MOD_SYS|ERROR, "Failed to read '%s': %s", file, strerror(errno));
        rt_free(b.buf);
        b.buf = NULL;
    } else {
        b.bufsize = b.pos = n;
        b.buf[b.bufsize] = 0;  // make it zero terminated if used as a string
    }
    if (fd != -1)
        fs_close(fd);
    return b;
}

static int trimEnd (char* s) {
    int n = strlen(s);
    while( n>0 && strchr(" \t\r\n", s[n-1]) ) --n;
    s[n] = 0;
    return n;
}


str_t readFileAsString (str_t basename, str_t suffix, str_t* pCachedValue) {
    // Read file every time - should not happen that often anyway
    // Cached value is only used to free memory - so caller has not to deal with it
    str_t value = *pCachedValue;
    if( value ) {
        rt_free((void*)value);
        *pCachedValue = NULL;
    }
    str_t file = makeFilepath(basename, suffix, NULL, 0);
    dbuf_t b = readFile(file, 0);
    rt_free((void*)file);
    if( !b.buf )
        return NULL;
    trimEnd(b.buf);
    value = rt_strdup(b.buf); // copy over - might be smaller
    *pCachedValue = value;
    rt_free(b.buf);
    return value;
}

int writeFile (str_t file, const char* data, int datalen) {
    int fd, err= 1;
    if( (fd = fs_open(file, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP)) == -1 ||
        fs_write(fd, data, datalen) == -1 ) {
        LOG(MOD_SYS|CRITICAL, "Failed to write file '%s': %s", file, strerror(errno));
        err = 0;
    }
    fs_close(fd);
    return err;
}

dbuf_t sys_webFile (str_t filename) {
    if( !webDir )
        goto err;
    char filepath[MAX_FILEPATH_LEN];
    dbuf_t b = dbuf_ini(filepath);
    xputs(&b, webDir, -1);
    
    xputs(&b, filename[0]=='/' ? filename+1 : filename, -1);
    if( !xeos(&b) ) {
      err:
        b.buf = NULL;
        b.pos = b.bufsize = 0;
        return b;
    }
    return readFile(b.buf, 0);
}

dbuf_t sys_readFile (str_t filename) {
    str_t fpath = makeFilepath(filename,"",NULL,1);
    dbuf_t b = readFile(fpath, 1);
    rt_free((void*)fpath);
    return b;
}

dbuf_t sys_checkFile (str_t filename) {
    str_t fpath = makeFilepath(filename,"",NULL,1);
    dbuf_t b = readFile(fpath, 0);
    rt_free((void*)fpath);
    return b;
}


str_t sys_makeFilepath (str_t filename, int complain) {
    return makeFilepath(filename,"",NULL,complain);
}

void sys_writeFile (str_t filename, dbuf_t* b) {
    str_t fn = makeFilepath(filename,"",NULL,0);
    writeFile(fn, b->buf, b->pos);
    rt_free((void*)fn);
}

uL_t sys_eui () {
    if( (protoEUI >> 48) != 0 )
        return protoEUI;
    if( (prefixEUI & 0xFFFFffffFFFF) != 0 ) {
        // Expand MAC to EUI
        return ((protoEUI & 0xFFFFFF000000) << 16) | 0xFFFE000000 | (protoEUI & 0xFFFFFF);
    }
    return prefixEUI | protoEUI;
}

str_t sys_uri (int cred_cat, int cred_set) {
    str_t uri_fn = configFilename(cred_cat, cred_set, FN_URI);
    dbuf_t dbuf = readFile(uri_fn, 0);
    if( dbuf.buf == NULL )
        return NULL;
    dbuf.bufsize = trimEnd(dbuf.buf);
    if( dbuf.bufsize+1 > MAX_URI_LEN ) {
        LOG(MOD_SYS|ERROR, "URI in '%s' too long (max %d): %s", uri_fn, MAX_URI_LEN, dbuf.buf);
        rt_free(dbuf.buf);
        return NULL;
    }
    char* p = &uriCache[cred_cat][cred_set];
    strcpy(p, dbuf.buf);
    rt_free(dbuf.buf);
    return p;
}

void sys_saveUri (int cred_cat, str_t uri) {
    str_t uri_fn = configFilename(cred_cat, FN_TEMP, FN_URI);
    if( !writeFile(uri_fn, uri, strlen(uri)) )
        updateState |= UPD_ERROR;
    updateState |= (1<<cred_cat);
}


int checkUris () {
    int errs=0, nuris=0;

    for( int cat=0; cat < nFN_CAT; cat++ ) {
        str_t scheme = cat == FN_CUPS ? "http" : "ws";
        if( cat == FN_TC && sys_noTC ) continue;
        int nuriscat = nuris;
        for( int set=FN_REG; set <= FN_BOOT; set++ ) {
            char host[MAX_HOSTNAME_LEN];
            char port[MAX_PORT_LEN];
            str_t uri = sys_uri(cat, set);
            if( !uri ) continue;
            if( !uri_checkHostPortUri(uri, scheme, host, sizeof(host), port, sizeof(port)) ) {
                printf("%s: Misconfigured URI - expecting scheme %s: %s\n", configFilename(cat, set, FN_URI), scheme, uri);
                errs++;
            } else {
                nuris++;
            }
        }
        if( nuriscat == nuris && cat == FN_CUPS ) {
            sys_noCUPS = 1;
        }
    }
    if( nuris == 0 ) {
        printf("No server URIs configured - expecting at least one of the following files to exist:\n");
        for( int cat=0; cat < nFN_CAT; cat++ ) {
            if( cat == FN_TC && sys_noTC ) continue;
            for( int set=FN_REG; set <= FN_BOOT; set++ ) {
                printf("   %s\n", configFilename(cat, set, FN_URI));
            }
        }
    }
    return errs==0 && nuris > 0;
}


static int updateConfigFiles (int cat, int rollFwd) {
    // Rename temp setup files to regular files.
    str_t taf_upd = transactionFilename(cat, FN_UPD);
    if( !rollFwd && !writeFile(taf_upd, "", 0) ) {
        fs_unlink(taf_upd);
        LOG(MOD_SYS|CRITICAL, "Failed to create '%s': %s", taf_upd);
        return 0;
    }
    fs_sync();
    for( int ext=0; ext < nFN_EXT; ext++ ) {
        str_t fn_temp = configFilename(cat, FN_TEMP, ext);
        str_t fn_reg  = configFilename(cat, FN_REG,  ext);
        if( fs_access(fn_temp, F_OK) == 0 ) {
            if( fs_rename(fn_temp, fn_reg) == -1 )
                rt_fatal("Failed to rename '%s' -> '%s': %s", fn_temp, fn_reg, strerror(errno));
        }
    }
    fs_sync();
    fs_unlink(taf_upd);
    return 1;
}


static int backupConfigFiles (int cat, int rollFwd) {
    // Copy a set of config files to a backup set.
    if( bakDone[cat] )
        return 1;   // already did a copy

    str_t taf_cpy = transactionFilename(cat, FN_CPY);
    if( !rollFwd && !writeFile(taf_cpy, "", 0) ) {
        fs_unlink(taf_cpy);
        LOG(MOD_SYS|CRITICAL, "Failed to create '%s': %s", taf_cpy);
        return 0;
    }
    fs_sync();
    str_t unlink_fn = transactionFilename(cat, FN_DON);
    if( fs_unlink(unlink_fn) == -1 && errno != ENOENT ) {
      unlink_fail:
        LOG(MOD_SYS|CRITICAL, "Failed to unlink '%s': %s", unlink_fn, strerror(errno));
        return 0;  // just walk away and keep ta file - copying will be continued with next station restart
    }
    for( int ext=0; ext < nFN_EXT; ext++ ) {
        unlink_fn = configFilename(cat, FN_BAK, ext);
        if( fs_unlink(unlink_fn) == -1 && errno != ENOENT )
            goto unlink_fail;
    }
    for( int ext=0; ext < nFN_EXT; ext++ ) {
        str_t fn_bak = configFilename(cat, FN_BAK, ext);
        str_t fn_reg = configFilename(cat, FN_REG, ext);
        dbuf_t dbuf = readFile(fn_reg, /*no complaints*/0);
        if( dbuf.buf ) {
            if( !writeFile(fn_bak, dbuf.buf, dbuf.bufsize) ) {
                LOG(MOD_SYS|CRITICAL, "Failed to write '%s': %s", fn_bak, strerror(errno));
                return 0;  // just walk away and keep taf file - copying will be continued with next station restart
            }
            rt_free(dbuf.buf);
        }
    }
    str_t taf_don = transactionFilename(cat, FN_DON);
    if( !writeFile(taf_don, "", 0) ) {
        LOG(MOD_SYS|CRITICAL, "Failed to write '%s': %s", taf_don, strerror(errno));
        return 0;  // just walk away and keep taf file - copying will be continued with next station restart
    }
    fs_sync();
    fs_unlink(taf_cpy);
    fs_sync();
    bakDone[cat] = 1;
    return 1;
}

void setupConfigFilenames () {
    assert((int)SYS_CRED_CUPS  == (int)FN_CUPS  && (int)SYS_CRED_TC     == (int)FN_TC);
    assert((int)SYS_CRED_REG   == (int)FN_REG   && (int)SYS_CRED_BAK    == (int)FN_BAK && (int)SYS_CRED_BOOT  == (int)FN_BOOT);
    assert((int)SYS_CRED_TRUST == (int)FN_TRUST && (int)SYS_CRED_MYCERT == (int)FN_CRT && (int)SYS_CRED_MYKEY == (int)FN_KEY);
    char filepath[MAX_FILEPATH_LEN];
    dbuf_t b = dbuf_ini(filepath);

    for( int cat=0; cat < nFN_CAT; cat++ ) {
        b.pos = 0;
        xputs(&b, homeDir, -1);
        xputs(&b, categoryName(cat), -1);
        int p0 = b.pos;
        for( int set=0; set < nFN_SET; set++ ) {
            xputs(&b, &sFN_SET[set*6], -1);
            int p1 = b.pos;
            for( int ext=0; ext < nFN_EXT; ext++ ) {
                xputs(&b, ".", 1);
                xputs(&b, &sFN_EXT[ext*6], -1);
                if( !xeos(&b) )
                    rt_fatal("File path too big: %s", b.buf);
                configFilename(cat,set,ext) = rt_strdup(b.buf);
                b.pos = p1;
            }
            b.pos = p0;
        }
        for( int taf=0; taf < nFN_TAF; taf++ ) {
            xputs(&b, &sFN_TAF[taf*10], -1);
            xeos(&b);
            transactionFilename(cat,taf) = rt_strdup(b.buf);
            b.pos = p0;
        }
    }
}


void checkRollForward () {
    int ok = 1;
    str_t taf_file;
    for( int cat=0; cat < nFN_CAT; cat++ ) {
        taf_file = transactionFilename(cat, FN_UPD);
        if( fs_access(taf_file, F_OK) == 0 ) {
            // A new set of config files got created and replacing the regular ones was interrupted.
            // Pick up replacing and run to completion.
            ok &= updateConfigFiles(cat, 1);
        }
        taf_file = transactionFilename(cat, FN_CPY);
        if( fs_access(taf_file, F_OK) == 0 ) {
            // Making a backup copy of a set of config files was interrupted.
            // Rerun the copy process and clear transaction marker.
            ok &= backupConfigFiles(cat, 1);
        }
        taf_file = transactionFilename(cat, FN_DON);
        if( fs_access(taf_file, F_OK) == 0 ) {
            bakDone[cat] = 1;
        }
    }
    if( !ok )
        rt_fatal("Forward recovery of some station config files failed");
}


int sys_cred (int cred_cat, int cred_set, str_t* elems, int* elemslen) {
    memset(elems,    0, sizeof(elems[0]   ) * SYS_CRED_NELEMS);
    memset(elemslen, 0, sizeof(elemslen[0]) * SYS_CRED_NELEMS);
    for( int ext=FN_TRUST; ext < FN_URI; ext++ ) {
        str_t fn = configFilename(cred_cat, cred_set, ext);
        int sz = sizeFile(fn);
        if( sz > 0 ) { // Empty file (sz==0) is treated as absent
            elems[ext] = fn;
        }
    }
    if( elems[SYS_CRED_TRUST] == NULL ) {
        return SYS_AUTH_NONE;
    }
    if( elems[SYS_CRED_MYCERT] == NULL && elems[SYS_CRED_MYKEY] != NULL ) {
        return SYS_AUTH_TOKEN;
    }
    if( elems[SYS_CRED_MYCERT] == NULL || elems[SYS_CRED_MYKEY] == NULL ) {
        return SYS_AUTH_SERVER;
    }
    return SYS_AUTH_BOTH;
}


u4_t sys_crcCred (int cred_cat, int cred_set) {
    u4_t crc = 0;
    for( int ext=FN_TRUST; ext < FN_URI; ext++ ) {
        dbuf_t data = readFile(configFilename(cred_cat, cred_set, ext), 0);
        if( data.buf && data.bufsize != 0 )
            crc = rt_crc32(crc, data.buf, data.bufsize);
        else
            crc = rt_crc32(crc, &(u1_t[]){0,0,0,0}, 4);
        rt_free(data.buf);
    }
    return crc;
}


void sys_resetConfigUpdate () {
    updateState = 0;
    for( int cat=0; cat < nFN_CAT; cat++ ) {
        str_t fn = transactionFilename(cat, FN_UPD);
        if( fn ) fs_unlink(fn);
        for( int ext=0; ext < nFN_EXT; ext++ ) {
            fn = configFilename(cat, FN_TEMP, ext);
            if( fn ) fs_unlink(fn);
        }
    }
    fs_sync();
}

void sys_commitConfigUpdate () {
    if( updateState == UPD_ERROR )
        return;
    for( int cat=0; cat < nFN_CAT; cat++ ) {
        if( updateState & (1<<cat) ) {
            updateConfigFiles(cat, 0);
        }
    }
    updateState = 0;
}

void sys_backupConfig (int cred_cat) {
    backupConfigFiles(cred_cat, 0);
}


void sys_credStart (int cred_cat, int len) {
    rt_free(pendData);
    pendData = rt_mallocN(char, len+1);
}

void sys_credWrite (int cred_cat, u1_t* data, int off, int len) {
    memcpy(pendData+off, data, len);
    updateState |= 1<<cred_cat;
}

#define ASN1_ISSEQ(PTR) ( (PTR)[0] == 0x30 )
//#define ALIGN(V,A) (((V)+(A-1)) & ~(A-1))

static inline int asn1_seqlen(char * ptr) {
    if( ptr[1] & 0x80 ) {
        return (( ((u2_t) ptr[2] & 0xff) << 8) | ( (u2_t) ptr[3] & 0xff)) + 4;
    } else {
        return ptr[1] + 2;
    }
}

void sys_credComplete (int cred_cat, int len) {
    pendData[len] = 0;
    char* data[SYS_CRED_NELEMS];
    int datalen[SYS_CRED_NELEMS];
    u4_t to, co, ko, tl, cl, kl;

    // Trust
    if( !ASN1_ISSEQ(pendData) ) {
        LOG(MOD_SYS|ERROR, "Failed to parse %s credentials: ASN.1 SEQ expected for trust (0x%02x)", categoryName(cred_cat), pendData[0]);
        goto parsing_failed;
    }
    to = 0;
    tl = asn1_seqlen(pendData + to);

    // Client Certificate
    co = to + tl;
    if( pendData[co] == 00 ) {
        // No certificate
        cl = 0;
        ko = co + 4;
    } else {
        if( !ASN1_ISSEQ(pendData+co) ) {
            LOG(MOD_SYS|ERROR, "Failed to parse %s credentials: ASN.1 SEQ expected for cert (0x%02x)", categoryName(cred_cat), pendData[co]);
            goto parsing_failed;
        }
        cl = asn1_seqlen(pendData + co);
        ko = co + cl;
    }

    if (ko > len) {
        LOG(MOD_SYS|ERROR, "Failed to parse %s credentials: expecting more data (key_offset=%d, total_len=%d)", categoryName(cred_cat), ko, len);
        goto parsing_failed;
    }

    if( ASN1_ISSEQ(&pendData[ko]) ) { // Key
        kl = asn1_seqlen(pendData + ko);
    } else if( pendData[ko] == 0 ) {
        kl = 0;
    } else { // Token
        kl = len - ko;
    }

    data[SYS_CRED_TRUST]  = pendData+to; datalen[SYS_CRED_TRUST]  = tl;
    data[SYS_CRED_MYCERT] = pendData+co; datalen[SYS_CRED_MYCERT] = cl;
    data[SYS_CRED_MYKEY]  = pendData+ko; datalen[SYS_CRED_MYKEY]  = kl;

    u1_t * p = (u1_t*)pendData;
    LOG(MOD_SYS|INFO, " credComplete - trust_off=%4u, trust_len=%4u               %02x %02x %02x %02x  %02x %02x %02x %02x",
        to, tl,                                     p[to+0], p[to+1], p[to+2], p[to+3],p[to+4], p[to+5], p[to+6], p[to+7]
    );
    LOG(MOD_SYS|INFO, " credComplete - cert_off =%4u, cert_len =%4u  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x",
        co, cl, p[co-4], p[co-3], p[co-2], p[co-1], p[co+0], p[co+1], p[co+2], p[co+3], p[co+4], p[co+5], p[co+6], p[co+7]
    );
    LOG(MOD_SYS|INFO, " credComplete - key_off  =%4u, key_len  =%4u  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x",
        ko, kl, p[ko-4], p[ko-3], p[ko-2], p[ko-1], p[ko+0], p[ko+1], p[ko+2], p[ko+3], p[ko+4], p[ko+5], p[ko+6], p[ko+7]
    );
    if( datalen[SYS_CRED_TRUST] + datalen[SYS_CRED_MYCERT] + datalen[SYS_CRED_MYKEY] > len ) {
        LOG(MOD_SYS|ERROR, "Failed to parse %s credentials! Lengths do not align segment_len=%d parsed_len=%d. Ignoring.",
            categoryName(cred_cat),
            len, datalen[SYS_CRED_TRUST] + datalen[SYS_CRED_MYCERT] + datalen[SYS_CRED_MYKEY]);
        goto parsing_failed;
    }
    for( int ext=FN_TRUST; ext < FN_URI; ext++ ) {
        str_t fn = configFilename(cred_cat, FN_TEMP, ext);
        // Note: unset credential files are create as empty files.
        // This makes creating bak set of files easier as if files would be absent.
        if( !writeFile(fn, data[ext], datalen[ext]) )
            goto parsing_failed;
    }
    goto parsing_done;
parsing_failed:
    // updateState |= UPD_ERROR;
    (void) updateState;

parsing_done:
    rt_free(pendData);
    pendData = NULL;
}

u4_t sys_crcSigkey (int key_id) {
    u4_t crc = 0;
    dbuf_t data = sys_sigKey(key_id);
    if( data.buf )
        crc = rt_crc32(crc, data.buf, data.bufsize);
    sys_sigKey(-1); // Clear buffer
    return crc;
}


dbuf_t sys_sigKey (int key_id) {
    static dbuf_t b;
    if( key_id < 0 && b.buf ) {
        rt_free(b.buf);
        b.buf = NULL;
    }
    if( b.buf )
        rt_free(b.buf);
    char path[20];
    snprintf(path, sizeof(path), "~/sig-%d.key", key_id);
    b = sys_readFile(path);
    return b;
}

void sys_keepAlive (int fd) {
    str_t tag = "SO_KEEPALIVE";
    int v = TCP_KEEPALIVE_EN;
    if( setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,  &v, sizeof(v)) == -1 ) goto err;
    if( v == 0 ) return;
    tag = "TCP_KEEPCNT";
    v = TCP_KEEPALIVE_CNT;
    if( setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &v, sizeof(v)) == -1 ) goto err;
    tag = "TCP_KEEPIDLE";
    v = TCP_KEEPALIVE_IDLE;
    if( setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &v, sizeof(v)) == -1 ) goto err;
    tag = "TCP_KEEPINTVL";
    v = TCP_KEEPALIVE_INTVL;
    if( setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &v, sizeof(v)) == -1 ) goto err;
    return;
 err:
    LOG(MOD_AIO|ERROR, "Failed to set %s=%d: %s", tag, v, strerror(errno));
}
