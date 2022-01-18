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

#define _GNU_SOURCE  // syncfs(fd)
#include <limits.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "argp2.h"
#include "s2conf.h"
#include "kwcrc.h"
#include "rt.h"
#include "uj.h"
#include "s2e.h"
#include "ral.h"
#include "timesync.h"
#include "sys.h"
#include "sys_linux.h"
#include "fs.h"
#include "selftests.h"

#include "mbedtls/version.h"

extern char* makeFilepath (const char* prefix, const char* suffix, char** pCachedFile, int isReadable); // sys.c
extern int writeFile (str_t file, const char* data, int datalen);
extern dbuf_t readFile (str_t file, int complain);
extern str_t readFileAsString (str_t basename, str_t suffix, str_t* pCachedValue);
extern void setupConfigFilenames ();
extern int checkUris ();
extern void checkRollForward ();



#if defined(CFG_ral_master_slave)
static const char* const SLAVE_ENVS[] = {
    "SLAVE_IDX",
    "SLAVE_WRFD",
    "SLAVE_RDFD",
    NULL
};
#endif // defined(CFG_ral_master_slave)

static struct logfile logfile;
static char* gpsDevice    = NULL;
static tmr_t startupTmr;

str_t sys_slaveExec;
u1_t  sys_deviceMode;
u1_t  sys_modePPS;      // special mode?
u2_t  sys_webPort;
u1_t  sys_noTC;
u1_t  sys_noCUPS;

extern str_t  homeDir;
extern str_t  tempDir;
extern str_t  webDir;
static str_t  homeDirSrc;
static str_t  tempDirSrc;
static str_t  webDirSrc;

static int    daemonPid;
static int    workerPid;
static str_t  radioInit;
static str_t  radioDevice;
static str_t  versionTxt;
static char*  updfile;
static char*  temp_updfile;
static int    updfd = -1;

static str_t  protoEuiSrc;
static str_t  prefixEuiSrc;
static str_t  radioInitSrc;


static void handle_signal (int signum) {
    // Calling exit() in a signal handler is unsafe
    // exit() will run atexit functions! + triggers writting of gcda files (gcov/lcov)
    // but might interrupt pending IO operations somewhere in libc...
    // Quite unlikely - nevertheless we should use pselect in aio_loop
    // and enable signals only when hanging in pselect
    // Termination code throughout station tries first SIGTERM and after a while SIGKILL, thus,
    // we should not end up with a station process lingering.
    exit(128+signum);
    // Signal safe but less convenient
    //_exit(128+signum);
}



static int updateDirSetting (str_t path, str_t source, str_t* pdir, str_t* psrc) {
    int l = strlen(path);
    char* p = rt_mallocN(char, l+5);  // more space for optional "./" and/or "/" + "\0"
    if( path[0] ) {
        strcpy(p,path);
    } else {
        strcpy(p,"./");
        l = 2;
    }
    if( p[l-1] != '/' ) {
        p[l++] = '/';
    }
    if( p[0] != '/' && (p[0] != '.' || p[1] != '/') ) {
        memmove(p+2, p, l+1);
        p[0] = '.';
        p[1] = '/';
    }
    struct stat st;
    if( stat(p, &st) == -1 ) {
        fprintf(stderr, "%s - Cannot access directory '%s': %s\n", source, p, strerror(errno));
        goto err;
    }
    if( !S_ISDIR(st.st_mode) ) {
        fprintf(stderr, "%s - Not a directory: %s\n", source, p);
  err:
        rt_free(p);
        return 0;
    }

    rt_free((void*)*pdir);
    rt_free((void*)*psrc);
    *pdir = p;
    *psrc = rt_strdup(source);
    return 1;
}

static int setWebDir (str_t path, str_t source) {
    return updateDirSetting(path, source, &webDir, &webDirSrc);
}

static int setHomeDir (str_t path, str_t source) {
    return updateDirSetting(path, source, &homeDir, &homeDirSrc);
}

static int setTempDir (str_t path, str_t source) {
    return updateDirSetting(path, source, &tempDir, &tempDirSrc);
}


static dbuf_t stripTrailingWsp (dbuf_t b) {
    while( b.bufsize > 0 && strchr(" \t\r\n", b.buf[b.bufsize-1]) ) {
        b.buf[--b.bufsize] = 0;
    }
    return b;
}


static str_t parseEui (str_t s, int n, uL_t* peui, int nonzero) {
    str_t p = s;
    uL_t eui = rt_readEui(&p, n);
    if( p==s || (n==0 ? (p[0] != 0) : (p != s+n)) )
        return "syntax error";
    if( nonzero && eui == 0 )
        return "must not be zero";
    *peui = eui;
    return NULL;
}


static void findDefaultEui () {
    str_t dirname = "/sys/class/net";
    DIR* D = opendir(dirname);
    if( D==NULL )
        return;
    char path[NAME_MAX+32];
    char ifc[64] = {0};
    uL_t eui = 0;
    struct dirent* de;
    while( (de = readdir(D)) ) {
        char* dname = de->d_name;
        if( strlen(dname) > sizeof(ifc)-1 )
            continue;
        if( strcmp("lo", dname) == 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s/address", dirname, dname);
        dbuf_t b = stripTrailingWsp(readFile(path, 0));
        if( b.buf == NULL )
            continue;
        uL_t mac = 0;
        str_t err = parseEui(b.buf, b.bufsize, &mac, 1);
        rt_free(b.buf);
        if( err != NULL )
            continue;
        // Prefer ethX devices
        if( ifc[0] != 0 ) {
            if( strncmp(ifc, "eth", 3) == 0 && strncmp(dname, "eth", 3) != 0 )
                continue; // eth trumps other devices
            // Otherwie choose alphabetically lowest - unless eth replaces something else
            if( !((strncmp(ifc, "eth", 3) == 0) ^ (strncmp(dname, "eth", 3) == 0))
                && strcmp(ifc, dname) <= 0 )
                continue; // not lower
        }
        strcpy(ifc, dname);
        eui = mac;
        continue;
    }
    closedir(D);
    if( eui ) {
        snprintf(path, sizeof(path), "%s/%s/address", dirname, ifc);
        protoEUI = eui;
        rt_free((void*)protoEuiSrc);
        protoEuiSrc = rt_strdup(path);
    }
}


static int setEui (str_t spec, str_t source) {
    str_t err;
    if( access(spec, R_OK) == 0 ) {
        dbuf_t b = stripTrailingWsp(sys_readFile(spec));
        if( b.buf && (err = parseEui(b.buf, b.bufsize, &protoEUI, 1)) == NULL ) {
            char sbuf[strlen(source)+strlen(spec)+32];
            snprintf(sbuf, sizeof(sbuf), "%s file %s", source, spec);
            rt_free((void*)protoEuiSrc);
            protoEuiSrc = rt_strdup(sbuf);
            rt_free(b.buf);
            return 1;
        }
        if( b.buf == NULL ) {
            LOG(MOD_SYS|ERROR, "Station proto EUI %s (%s): Cannot read file", spec, source);
        } else {
            LOG(MOD_SYS|ERROR, "Station proto EUI '%s' (%s file %s): %s", b.buf, source, spec, err);
        }
        rt_free(b.buf);
        return 0;
    }
    if( (err = parseEui(spec, strlen(spec), &protoEUI, 1)) == NULL ) {
        rt_free((void*)protoEuiSrc);
        protoEuiSrc = rt_strdup(source);
        return 1;
    }
    LOG(MOD_SYS|ERROR, "Station proto EUI: '%s' (%s): %s", spec, source, err);
    return 0;
}


// Check if there is a process that has an open file handle to a specific device/file
int sys_findPids (str_t device, u4_t* pids, int n_pids) {
    if( device[0] != '/' )
        return 0;

    char path[NAME_MAX+64];
    struct dirent* de;
    DIR *D, *DD;
    int dlen, cnt = 0;

    dlen = strlen(device);
    assert(dlen < NAME_MAX);
    strcpy(path, "/proc");
    if( (D = opendir("/proc")) == NULL )
        return 0;
    while( (de = readdir(D)) ) {
        const char* pid_s = de->d_name;
        sL_t pid = rt_readDec(&pid_s);
        if( pid < 0 )
            continue;
        int n_prefix = snprintf(path, sizeof(path), "/proc/%s/fd", de->d_name);
        if( (DD = opendir(path)) == NULL )
            continue;
        while( (de = readdir(DD)) ) {
            if( de->d_type != DT_LNK )
                continue;
            char linkpath[NAME_MAX];
            snprintf(path+n_prefix, sizeof(path)-n_prefix, "/%s", de->d_name);
            int err = readlink(path, linkpath, sizeof(linkpath));
            if( err != dlen )
                continue;
            linkpath[err] = 0;
            if( strcmp(device, linkpath) == 0 ) {
                if( cnt < n_pids )
                    pids[cnt] = pid;
                cnt++;
            }
        }
        closedir(DD);
    }
    closedir(D);
    return cnt;
}


str_t sys_radioDevice (str_t device, u1_t* comtype) {
    str_t f = device==NULL ? radioDevice : device;
    if( f == NULL )
        f = RADIODEV;
    // check for comtype prefix
    if( comtype )
	*comtype = COMTYPE_SPI;
    char *colon = index(f, ':');
    if( colon ) {
	if( strncmp(f, "spi:", 4) == 0 ) {
	    if( comtype )
		*comtype = COMTYPE_SPI;
	} else if( strncmp(f, "usb:", 4) == 0 ) {
	    if( comtype )
		*comtype = COMTYPE_USB;
	} else {
	    LOG(MOD_SYS|ERROR, "Unknown device comtype '%.*s' (using SPI)", colon-f, f);
	}
	f = colon + 1;
    }
    // Caller must free result
    return sys_makeFilepath(f, 0);
}


void sys_fatal (int code) {
    exit(code==0 ? FATAL_GENERIC : code);
}

static char* makePidFilename() {
    return makeFilepath("~temp/station",".pid",NULL,0);
}

static int readPid() {
    char* pidfile = makePidFilename();
    dbuf_t b = readFile(pidfile,0);
    const char* s = b.buf;
    int pid = rt_readDec(&s);
    rt_free(pidfile);
    rt_free(b.buf);
    return max(0,pid);
}

static void writePid () {
    char buf[16];
    dbuf_t b = dbuf_ini(buf);
    xprintf(&b, "%d", daemonPid ? daemonPid : getpid());
    char* pidsfile = makePidFilename();
    writeFile(pidsfile, b.buf, b.pos);
    rt_free(pidsfile);
}


static void killOldPid () {
    int pid = readPid();
    if( daemonPid && pid == daemonPid )
        return;  // worker started under daemon
    if( pid > 0 ) {
        pid_t pgid = getpgid(pid);
        if( pgid == pid ) {
            fprintf(stderr, "Killing process group %d\n", pid);
            kill(-pid, SIGINT);  // if process group leader
            rt_usleep(2000);
            kill(-pid, SIGKILL);  // if process group leader
        } else {
            fprintf(stderr, "Killing process %d\n", pid);
            kill( pid, SIGINT);  // if just a simple process
            rt_usleep(2000);
            kill( pid, SIGKILL);  // if just a simple process
        }
    }
}



static void leds_off () {
    sys_inState(SYSIS_STATION_DEAD);
}

void sys_ini () {
    LOG(MOD_SYS|INFO, "Logging     : %s (maxsize=%d, rotate=%d)\n",
        logfile.path==NULL ? "stderr" : logfile.path, logfile.size, logfile.rotate);
    LOG(MOD_SYS|INFO, "Station Ver : %s",  CFG_version " " CFG_bdate);
    LOG(MOD_SYS|INFO, "Package Ver : %s",  sys_version());
    LOG(MOD_SYS|INFO, "mbedTLS Ver : %s",  MBEDTLS_VERSION_STRING);
    LOG(MOD_SYS|INFO, "proto EUI   : %:E\t(%s)", protoEUI, protoEuiSrc);
    LOG(MOD_SYS|INFO, "prefix EUI  : %:E\t(%s)", prefixEUI, prefixEuiSrc);
    LOG(MOD_SYS|INFO, "Station EUI : %:E", sys_eui());
    LOG(MOD_SYS|INFO, "Station home: %s\t(%s)",  homeDir, homeDirSrc);
    LOG(MOD_SYS|INFO, "Station temp: %s\t(%s)",  tempDir, tempDirSrc);
    if( sys_slaveIdx >= 0 ) {
        LOG(MOD_SYS|INFO, "Station slave: %d", sys_slaveIdx);
    } else {
        if( gpsDevice )
            LOG(MOD_SYS|INFO, "GPS device: %s", gpsDevice);
    }
    if( sys_noTC || sys_noCUPS ) {
        LOG(MOD_SYS|WARNING, "Station in NO-%s mode", sys_noTC ? "TC" : "CUPS");
    }
    int seed;
    sys_seed((u1_t*)&seed, sizeof(seed));
    srand(seed);
}


void sys_seed (unsigned char* seed, int len) {
    int fd;
    if( (fd = open("/dev/urandom", O_RDONLY)) == -1 ) {
        if( (fd = open("/dev/random", O_RDONLY)) == -1 ) {
            // Some fallback
        fail:
            LOG(MOD_SYS|CRITICAL, "Unable to properly seed cryptographic random number generator!");
            ustime_t t = sys_time();
            for( int i=0; i<8 && i<len; i++, t>>=8 )
                seed[i] ^= t;
            uL_t p = (ptrdiff_t)seed;
            for( int i=8; i<16 && i <len; i++, p>>=8 )
                seed[i] ^= p;
            return;
        }
    }
    int n = read(fd, seed, len);
    if( n != len )
        goto fail;
    close(fd);
}


void sys_usleep (sL_t us) {
    if( us <= 0 )
        return;
    struct timespec slp, rem = { .tv_sec = us/1000000, .tv_nsec = us%1000000*1000 }; // 200ms
    while( rem.tv_sec > 0 || rem.tv_nsec > 0 ) {
        slp = rem;
        if( nanosleep(&slp, &rem) == 0 )
            break;
    }
}


sL_t sys_time () {
    struct timespec tp;
    int err = clock_gettime(CLOCK_MONOTONIC, &tp);
    if( err == -1 )
        rt_fatal("clock_gettime(2) failed: %s\n", strerror(errno));      // LCOV_EXCL_LINE
    return tp.tv_sec*(sL_t)1000000 + tp.tv_nsec/1000;
}


sL_t sys_utc () {
    struct timespec tp;
    int err = clock_gettime(CLOCK_REALTIME, &tp);
    if( err == -1 )
        rt_fatal("clock_gettime(2) failed: %s\n", strerror(errno));      // LCOV_EXCL_LINE
    return (tp.tv_sec*(sL_t)1000000 + tp.tv_nsec/1000);
}


str_t sys_version () {
    return readFileAsString("version", ".txt", &versionTxt);
}

/* FW Update ************************************ */

void sys_updateStart (int len) {
    close(updfd);
    if( len == 0 ) {
        updfd = -1;
        return;
    }
    makeFilepath("/tmp/update", ".bi_", &temp_updfile, 0);
    updfd = open(temp_updfile, O_CREAT|O_TRUNC|O_WRONLY, S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP);
    if( updfd == -1 )
        LOG(MOD_SYS|ERROR, "Failed to open '%s': %s", temp_updfile, strerror(errno));
}

void sys_updateWrite (u1_t* data, int off, int len) {
    if( updfd == -1 ) return;
    int err = write(updfd, data, len);
    if( err == -1 ) {
        LOG(MOD_SYS|ERROR, "Failed to write '%s': %s", temp_updfile, strerror(errno));
        close(updfd);
        updfd = -1;
    }
}

int sys_updateCommit (int len) {
    // Rename file and start a process
    if( len == 0 )
        return 1;
    if( updfd == -1 ) {
        if( temp_updfile )
            unlink(temp_updfile);
        return 0;
    }
    close(updfd);
    sync();
    updfd = -1;
    makeFilepath("/tmp/update", ".bin", &updfile, 0);
    if( rename(temp_updfile, updfile) == -1 ) {
        LOG(MOD_SYS|ERROR, "Rename of update file failed '%s': %s", temp_updfile, strerror(errno));
    }
    sync();
    return 1;
}


void sys_runUpdate () {
    makeFilepath("/tmp/update", ".bin", &updfile, 0);
    if( access(updfile, X_OK) != 0 )
        return; // no such file or not executable
    str_t argv[2] = { updfile, NULL };
    sys_execCommand(0, argv);  // 0=detach, don't wait for update to finish
}

void sys_abortUpdate () {
    unlink("/tmp/update.bin");
    sync();
}

int sys_runRadioInit (str_t device) {
    setenv("LORAGW_SPI", device, 1);   // for libloragw (SPI module)
    if( !radioInit )
        return 1;
    char buf[16];
    str_t argv[4] = { radioInit, device, NULL, NULL };
    if( sys_slaveIdx >= 0 ) {
        snprintf(buf, sizeof(buf), "%d", sys_slaveIdx);
        argv[2] = buf;
    }
    return sys_execCommand(RADIO_INIT_WAIT, argv) == 0;
}


int sys_execCommand (ustime_t max_wait, str_t* argv) {
    int argc = 0;
    while( argv[argc] ) argc++;
    if( argc == 0 || (argc==1 && argv[0][0]==0) )
        return 0;
    sys_flushLog();
    pid_t pid1;
    if( (pid1 = fork()) == 0 ) {
        pid_t pid2 = 0;
        if( max_wait!=0 || (pid2 = fork()) == 0 ) {
            if( access(argv[0], X_OK) != 0 ) {
                // Not an executable file
                str_t* argv2 = rt_mallocN(str_t, argc+4);
                memcpy(&argv2[3], &argv[0], sizeof(argv[0])*(argc+1)); // also copy trailing NULL
                if( access(argv[0], F_OK) == -1 ) {
                    // Not even a file - assume shell statements
                    argv2[0] = "/bin/sh";
                    argv2[1] = "-c";
                    argv2[2] = argv[0];
                    argv = argv2;
                } else {
                    // Assume file with Shell statements
                    argv2[1] = "/bin/bash";
                    argv2[2] = argv[0];
                    argv = &argv2[1];
                }
            }
            for( int i=0; argv[i]; i++ )
                LOG(MOD_SYS|DEBUG, "%s argv[%d]: <%s>\n", i==0?"execvp":"      ", i, argv[i]);
            log_flushIO();

            if( execvp(argv[0], (char*const*)argv) == -1 ) {
                LOG(MOD_SYS|ERROR, "%s: Failed to exec: %s", argv[0], strerror(errno));
                log_flushIO();
                exit(9);
            }
        } else if( pid2 < 0 ) {
            LOG(MOD_SYS|ERROR, "%s: Fork(2) failed: %s", argv[0], strerror(errno));
            log_flushIO();
            exit(8);
        }
        exit(0);
    }
    if( pid1 < 0 ) {
        LOG(MOD_SYS|ERROR, "%s: Fork failed: %s", argv[0], strerror(errno));
        return -1;
    }
    LOG(MOD_SYS|VERBOSE, "%s: Forked, waiting...", argv[0]);
    log_flushIO();
    int wmode = WNOHANG;
    if( max_wait == 0 ) {       // detached child forks a grand child - child exits
        max_wait = USTIME_MAX;  // basically forever
        wmode = 0;
    }
    for( ustime_t u=0; u < max_wait; u+=rt_millis(1) ) {
        int status = 0;
        int err = waitpid(pid1, &status, wmode);
        if( err == -1 ) {
            LOG(MOD_SYS|ERROR, "Process %s (pid=%d) - waitpid failed: %s", argv[0], pid1, strerror(errno));
            return -1;
        }
        if( err == pid1 ) {
            if( WIFEXITED(status) ) {
                int xcode = WEXITSTATUS(status);
                if( xcode == 0 ) {
                    LOG(MOD_SYS|INFO, "Process %s (pid=%d) completed", argv[0], pid1);
                    log_flushIO();
                    return 0;
                }
                LOG(MOD_SYS|ERROR, "Process %s (pid=%d) failed with exit code %d", argv[0], pid1, xcode);
                return xcode;
            }
            if( WIFSIGNALED(status) ) {
                int signo = WTERMSIG(status);
                LOG(MOD_SYS|ERROR, "Process %s (pid=%d) terminated by signal %d", argv[0], pid1, signo);
                return -2;
            }
            LOG(MOD_SYS|ERROR, "Process %s (pid=%d) with strange exit state 0x%X", argv[0], pid1, status);
            return -4;
        }
        rt_usleep(rt_millis(2));
    }
    kill(pid1, SIGTERM);
    LOG(MOD_SYS|ERROR, "Process %s (pid=%d) did not terminate within %ldms - killing it (SIGTERM)",
        argv[0], pid1, max_wait/1000);
    return -3;
}

static int setLogLevel (str_t arg, str_t source) {
    str_t err = log_parseLevels(arg);
    if( err  ) {
        int n = strlen(err);
        fprintf(stderr, "%s: Failed to parse log level: %.*s%s\n", source, 8, err, n>8 ? ".." : "");
        return 0;
    }
    return 1;
}

static int setLogFile (str_t logdef, str_t source) {
    // override  builtin defaults
    if( strcmp(logdef, "stderr") == 0 || strcmp(logdef, "-") == 0 ) {
        logfile.path = NULL;
        return 1;
    }
    free((void*)logfile.path);
    str_t spec = strchr(logdef,',');
    if( spec != NULL ) {
        str_t path = rt_strdupn(logfile.path, spec-logfile.path);
        logfile.path = sys_makeFilepath(path,1);
        rt_free((void*)path);
        sL_t logsz = rt_readDec((str_t*)&spec);
        if( logsz > 0 )
            logfile.size = min(max(logsz, 10000), (sL_t)100e6);
        if( spec[0] == ',' ) {
            int logrot = rt_readDec((str_t*)&spec);
            if( logrot > 0 )
                logfile.rotate = min(max(logrot, 0), (sL_t)100);
        }
        if( !spec[0] ) {
            fprintf(stderr, "%s: Illegal log file spec: %s\n", source, logdef);
            return 0;
        }
    } else {
        logfile.path = sys_makeFilepath(logdef,0);
    }
    return 1;
}

static int parseStationConf () {
    str_t filename = "station.conf";
    dbuf_t jbuf = sys_readFile(filename);
    if( jbuf.buf == NULL ) {
        LOG(MOD_SYS|ERROR, "No such file (or not readable): %s", filename);
        return 0;
    }
    ujdec_t D;
    uj_iniDecoder(&D, jbuf.buf, jbuf.bufsize);
    if( uj_decode(&D) ) {
        LOG(MOD_SYS|ERROR, "Parsing of JSON failed - '%s' ignored", filename);
        free(jbuf.buf);
        return 0;
    }
    u1_t ccaDisabled=0, dcDisabled=0, dwellDisabled=0;   // fields not present
    ujcrc_t field;
    uj_enterObject(&D);
    while( (field = uj_nextField(&D)) ) {
        switch(field) {
        case J_station_conf: {
            uj_enterObject(&D);
            while( (field = uj_nextField(&D)) ) {
                switch(field) {
                case J_routerid: {
                    if( !setEui(uj_str(&D), filename) )
                        uj_error(&D, "Illegal EUI");
                    break;
                }
                case J_euiprefix: {
                    str_t err = parseEui(uj_str(&D), 0, &prefixEUI, 0);
                    if( err != NULL )
                        uj_error(&D, "Illegal EUI: %s", err);
                    free((void*)prefixEuiSrc);
                    prefixEuiSrc = rt_strdup(filename);
                    break;
                }
                case J_log_file: {
                    if( !setLogFile(uj_str(&D), filename) )
                        uj_error(&D, "Illegal log file spec: %s", D.str.beg);
                    break;
                }
                case J_log_size: {
                    logfile.size = uj_num(&D);
                    break;
                }
                case J_log_rotate: {
                    logfile.rotate = uj_int(&D);
                    break;
                }
                case J_log_level: {
                    if( !setLogLevel(uj_str(&D), filename) )
                        uj_error(&D, "Illegal log level: %s", D.str.beg);
                    break;
                }
                case J_gps: {
                    makeFilepath(uj_str(&D),"",&gpsDevice,0);
                    break;
                }
                case J_pps: {
                    str_t mode = uj_str(&D);
                    if( strcmp(mode,"gps") == 0 ) {
                        sys_modePPS = PPS_GPS;
                    }
                    else if( strcmp(mode,"fuzzy") == 0 ) {
                        sys_modePPS = PPS_FUZZY;
                    }
                    else if( strcmp(mode,"testpin") == 0 ) {
                        sys_modePPS = PPS_TESTPIN;
                    }
                    else {
                        uj_error(&D, "Illegal pps mode: %s", mode);
                    }
                    break;
                }
                case J_radio_init: {
                    free((void*)radioInit);
                    radioInit = rt_strdup(uj_str(&D));
                    radioInitSrc = filename;
                    break;
                }
#if defined(CFG_prod)
                case J_nocca:
                case J_nodc:
                case J_nodwell:
                case J_device_mode: {
                    LOG(MOD_S2E|WARNING, "Feature not supported in production level code (station.conf) - ignored: %s", D.field.name);
                    uj_skipValue(&D);
                    break;
                }
#else // !defined(CFG_prod)
                case J_nocca: {
                    ccaDisabled = uj_bool(&D) ? 2 : 1;
                    break;
                }
                case J_nodc: {
                    dcDisabled = uj_bool(&D) ? 2 : 1;
                    break;
                }
                case J_nodwell: {
                    dwellDisabled = uj_bool(&D) ? 2 : 1;
                    break;
                }
                case J_device_mode: {
                    sys_deviceMode = uj_bool(&D) ? 1 : 0;
                    break;
                }
#endif // !defined(CFG_prod)
                case J_device: {
                    free((void*)radioDevice);
                    radioDevice = rt_strdup(uj_str(&D));
                    break;
                }
                case J_web_port: {
                    sys_webPort = uj_intRange(&D, 1, 65535);
                    break;
                }
                case J_web_dir: {
                    setWebDir(uj_str(&D), filename);
                    break;
                }
                default: {
                    dbuf_t b = uj_skipValue(&D);
                    int err = s2conf_set(filename, D.field.name, rt_strdupn(b.buf, b.bufsize));
                    if( err == -1 )
                        LOG(MOD_SYS|WARNING, "Ignoring field: %s", D.field.name);
                    break;
                }
                }
            }
            uj_exitObject(&D);
            break;
        }
        default: {
            uj_skipValue(&D);
            break;
        }
        }
    }
    uj_exitObject(&D);
    uj_assertEOF(&D);
    free(jbuf.buf);
    if( ccaDisabled   ) s2e_ccaDisabled   = ccaDisabled   & 2;
    if( dcDisabled    ) s2e_dcDisabled    = dcDisabled    & 2;
    if( dwellDisabled ) s2e_dwellDisabled = dwellDisabled & 2;
    return 1;
}


static struct opts {
    str_t logLevel;
    str_t logFile;
    str_t homeDir;
    str_t tempDir;
    str_t radioInit;
    str_t euiprefix;
    int   slaveMode;
    str_t slaveExec;
    u1_t  params;
    u1_t  daemon;
    u1_t  force;
    u1_t  kill;
    u1_t  notc;
} *opts;


static struct argp_option options[] = {
    { "log-file", 'L', "FILE[,SIZE[,ROT]]", 0,
      ("Write log entries to FILE. If FILE is '-' then write to stderr. "
       "Optionally followed by a max file SIZE and a number of rotation files. "
       "If ROT is 0 then keep only FILE. If ROT is 1 then keep one more old "
       "log file around. "
       "Overrides environment STATION_LOGFILE.")
    },
    { "log-level", 'l', "LVL|0..7", 0,
      ("Set a log level LVL=#loglvls# or use a numeric value. "
       "Overrides environment STATION_LOGLEVEL.")
    },
    { "home", 'h', "DIR", 0,
      ("Home directory for configuration files. "
       "Default is the current working directory. "
       "Overrides environment STATION_DIR.")
    },
    { "temp", 't', "DIR", 0,
      ("Temp directory for frequently written files. "
       "Default is /tmp. "
       "Overrides environment STATION_TEMPDIR.")
    },
    { "radio-init", 'i', "cmd", 0,
      ("Program/script to run before reinitializing radio hardware. "
       "By default nothing is being executed. "
       "Overrides environment STATION_RADIOINIT.")
    },
    { "eui-prefix", 'x', "id6", 0,
      ("Turn MAC address into EUI by adding this prefix. If the argument has value "
       "ff:fe00:0 then the EUI is formed by inserting FFFE in the middle. "
       "If absent use MAC or routerid as is. "
       "Overrides environment STATION_EUIPREFIX.")
    },
    { "params", 'p', NULL, 0,
      ("Print current parameter settings.")
    },
    { "version", 'v', NULL, 0,
      ("Print station version."),
    },
    { "daemon", 'd', NULL, 0,
      ("First check if another process is still alive. If so do nothing and exit. "
       "Otherwise fork a worker process to operate the radios and network protocols. "
       "If the subprocess died respawn it with an appropriate back off.")
    },
    { "force", 'f', NULL, 0,
      ("If a station process is already running, kill it before continuing with requested operation mode.")
    },
    { "kill", 'k', NULL, 0,
      ("Kill a currently running station process.")
    },
    { "no-tc", 'N', NULL, 0,
      ("Do not connect to a LNS. Only run CUPS functionality.")
    },
    { "slave", 'S', NULL, OPTION_HIDDEN,
      ("Station process is slave to a master process. For internal use only."),
    },
    { "exec", 'X', "CMD", OPTION_HIDDEN,
      ("Template for exec of slave processes. For internal/test use only."),
    },
    { "selftests", 256, NULL, OPTION_HIDDEN,
      ("If compiled with builtin selftests run them. For internal/test use only."),
    },
    { "fscmd", 257, "cmdline", OPTION_HIDDEN,
      ("Run a command on the simulated flash."),
    },
    { "fskey", 258, "hex", OPTION_HIDDEN,
      ("Specify an encryption key for the simulated flash."),
    },
    { "fscd",  259, "dir", OPTION_HIDDEN,
      ("Specify an current working dir for the simulated flash."),
    },
    { 0 }
};


static int parse_opt (int key, char* arg, struct argp_state* state) {
    switch(key) {
    case 259: {
        int err = fs_chdir(arg);
        if( err != 0 ) {
            fprintf(stderr, "Failed --fscd: %s\n", strerror(errno));
            exit(8);
        }
        return 0;
    }
    case 258: {
        u4_t key[4] = {0};
        for( int ki=0; ki<16; ki++ ) {
            int b = (rt_hexDigit(arg[2*ki])<<4) | rt_hexDigit(arg[2*ki+1]);
            if( b < 0 ) {
                fprintf(stderr, "Illegal --fskey argument - expecting 32 hex digits\n");
                exit(7);
            }
            key[ki/4] |= b<<(24 - ki%4*8);
        }
        fs_ini(key);
        return 0;
    }
    case 257: {
        fs_ini(NULL);
        exit(fs_shell(arg));
    }
    case 256: {
        setenv("STATION_SELFTESTS", "1", 1);
        return 0;
    }
    case 'S': {
        opts->slaveMode = 1;
        return 0;
    }
    case 'X': {
        free((void*)sys_slaveExec);
        sys_slaveExec = rt_strdup(arg);
        return 0;
    }
    case 'x': {
        opts->euiprefix = arg;
        return 0;
    }
    case 'l': {
        opts->logLevel = arg;
        return 0;
    }
    case 'L': {
        opts->logFile = arg;
        return 0;
    }
    case 'h': {
        opts->homeDir = arg;
        return 0;
    }
    case 't': {
        opts->tempDir = arg;
        return 0;
    }
    case 'i': {
        opts->radioInit = arg;
        return 0;
    }
    case 'p': {
        opts->params = 1;
        return 0;
    }
    case 'd': {
        opts->daemon = 1;
        return 0;
    }
    case 'f': {
        opts->force = 1;
        return 0;
    }
    case 'k': {
        opts->kill = 1;
        return 0;
    }
    case 'N': {
        opts->notc = 1;
        return 0;
    }
    case 'v': {
        fputs("Station: " CFG_version " " CFG_bdate "\n", stdout);
        readFileAsString("version", ".txt", &versionTxt);
        fprintf(stdout, "Package: %s\n", versionTxt);
        exit(0);
    }
    case ARGP_KEY_END: {
        return 0;
    }
    case ARGP_KEY_ARG: {
        break;
    }
    }
    return ARGP_ERR_UNKNOWN;
}

struct argp argp = { options, parse_opt, "", NULL, NULL, NULL, NULL };


static void startupMaster2 (tmr_t* tmr) {
#if !defined(CFG_no_rmtsh)
    rt_addFeature("rmtsh");
#endif
#if defined(CFG_prod)
    rt_addFeature("prod");  // certain development/test/debug features not accepted
#endif
    sys_enableCmdFIFO(makeFilepath("~/cmd",".fifo",NULL,0));
    if( gpsDevice ) {
        rt_addFeature("gps");
        sys_enableGPS(gpsDevice);
    }
    sys_iniTC();
    sys_startTC();
    sys_iniCUPS();
    sys_triggerCUPS(0);
    sys_iniWeb();
}

static void startupMaster (tmr_t* tmr) {
    sys_startLogThread();
    if( getenv("STATION_SELFTESTS") ) {
        selftests();
        // NOT REACHED
    }
    // Kill off any old processes - create a file with my pid
    writePid();
    // If there is an update pending - run it
    sys_runUpdate();
    ral_ini();
    atexit(leds_off);
    // Wait until slaves are up
    //startupMaster2(tmr);
    rt_setTimerCb(tmr, rt_millis_ahead(200), startupMaster2); 
}


// Fwd decl
static void startupDaemon (tmr_t* tmr);

// We poll here because using SIGCHLD would require pselect in aio which
// is less portable (e.g. LWIP on FreeRTOS). Polling is not a problem because
// we also would like to slow down restart to avoid blocking the system in a tight restart cycle.
static void waitForWorker (tmr_t* tmr) {
    int wstatus;
    pid_t wpid = waitpid(workerPid, &wstatus, WNOHANG);
    //NOT-NEEDED sys_inState(SYSIS_STATION_DEAD);
    if( wpid < 0 || wpid == workerPid ) {
        LOG(MOD_SYS|ERROR, "DAEMON: Station process %d died (exit code 0x%X)", workerPid, wstatus);
        workerPid = 0;
        startupDaemon(&startupTmr);
    } else {
        rt_setTimer(&startupTmr, rt_millis_ahead(500));
    }
}


static void startupDaemon (tmr_t* tmr) {
    int subprocPid;
    // Respawn station worker process
    sys_inState(SYSIS_STATION_DEAD);
    sys_flushLog();
    if( (subprocPid = fork()) == -1 )
        rt_fatal("DAEMON: Failed to fork station: %s", strerror(errno));
    if( subprocPid == 0 ) {
        // Child
        sys_iniLogging(&logfile, 1);
        LOG(MOD_SYS|INFO, "DAEMON: Station process %d started...", getpid());
        rt_yieldTo(&startupTmr, startupMaster);
    } else {
        // Parent
        workerPid = subprocPid;
        rt_yieldTo(&startupTmr, waitForWorker);
    }
}


int sys_main (int argc, char** argv) {
    // Because we log even before rt_ini()...
    rt_utcOffset = sys_utc() - rt_getTime();

    signal(SIGHUP,  SIG_IGN);
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    char cwd[MAX_FILEPATH_LEN];
    if( getcwd(cwd, sizeof(cwd)) != NULL )
        fs_chdir(cwd);

    s2conf_ini();
    logfile.size = LOGFILE_SIZE;
    logfile.rotate = LOGFILE_ROTATE;
    setHomeDir(".", "builtin");
    // setWebDir("./web", "builtin");
    setTempDir(access("/var/tmp", W_OK) < 0 ? "/tmp" : "/var/tmp", "builtin");
    prefixEuiSrc = rt_strdup("builtin");
    findDefaultEui();

    opts = rt_malloc(struct opts);
    int err = argp_parse (&argp, argc, argv, 0, NULL, NULL);
    if( err != 0 )
        return err;

#if defined(CFG_ral_master_slave)
    int slave_rdfd = -1, slave_wrfd = -1;
    if( opts->slaveMode ) {
        str_t const* sn = SLAVE_ENVS;
        while( *sn ) {
            str_t sv = getenv(*sn);
            if( sv == NULL )
                rt_fatal("Missing mandatory env var: %s", *sn);
            str_t sve = sv;
            sL_t v = rt_readDec(&sve);
            if( v < 0 )
                rt_fatal("Env var %s has illegal value: %s", *sn, sv);
            switch(sn[0][6]) {
            case 'I': log_setSlaveIdx(sys_slaveIdx = v); break;
            case 'R': slave_rdfd = v; break;
            case 'W': slave_wrfd = v; break;
            }
            sn++;
        }
    }
    if( sys_slaveExec == NULL ) {
        sys_slaveExec = rt_strdup("/proc/self/exe -S");
    }
#endif // defined(CFG_ral_master_slave)

    {
        str_t prefix = opts->euiprefix;
        str_t source = "--eui-prefix";
        if( prefix == NULL ) {
            source = "STATION_EUIPREFIX";
            prefix = getenv(source);
        } else {
            setenv("STATION_EUIPREFIX", prefix, 1);
        }
        if( prefix ) {
            str_t err = parseEui(prefix, 0, & prefixEUI, 0);
            if( err )
                rt_fatal("%s has illegal EUI value: %s", source, err);
            free((void*)prefixEuiSrc);
            prefixEuiSrc = rt_strdup(source);
        }
    }
    if( opts->tempDir ) {
        if( !setTempDir(opts->tempDir, "--temp") )
            return 1;
        setenv("STATION_TEMPDIR", opts->tempDir, 1);
    } else {
        str_t source = "STATION_TEMPDIR";
        str_t v = getenv(source);
        if( v && !setTempDir(v, source) )
            return 1;
    }

    if( opts->homeDir ) {
        if( !setHomeDir(opts->homeDir, "--home") )
            return 1;
        setenv("STATION_HOME", opts->homeDir, 1);
    } else {
        str_t source = "STATION_HOME";
        str_t v = getenv(source);
        if( v && !setHomeDir(v, source) )
            return 1;
    }

    if( !parseStationConf() )
        return 1;
    if( opts->params ) {
        s2conf_printAll();
    }

    if( opts->logFile ) {
        if( !setLogFile(opts->logFile, "--log-file") )
            return 1;
        setenv("STATION_LOGFILE", opts->logFile, 1);
    } else {
        str_t source = "STATION_LOGFILE";
        str_t v = getenv(source);
        if( v && !setLogFile(v, source) )
            return 1;
    }
    if( opts->radioInit ) {
        radioInitSrc = "--radio-init";
        free((char*)radioInit);
        radioInit = rt_strdup(opts->radioInit);
        setenv("STATION_RADIOINIT", radioInit, 1);
    } else {
        str_t s = "STATION_RADIOINIT";
        str_t v = getenv(s);
        if( v ) {
            radioInitSrc = s;
            free((char*)radioInit);
            radioInit = rt_strdup(v);
        }
    }
    if( opts->logLevel ) {
        if( !setLogLevel(opts->logLevel, "--log-level") )
            return 1;
        setenv("STATION_LOGLEVEL", opts->logLevel, 1);
    } else {
        str_t source = "STATION_LOGLEVEL";
        str_t v = getenv(source);
        if( v && !setLogLevel(v, source) )
            return 1;
    }
    {
        str_t source = "STATION_TLSDBG";
        str_t v = getenv(source);
        if( v && (v[0]&0xF0) == '0' )
            tls_dbgLevel = v[0] - '0';
    }

    if( opts->kill ) {
        if( opts->daemon || opts->force ) {
            fprintf(stderr, "Option -k is incompatible with -d/-f\n");
            return 1;
        }
        killOldPid();
        return 0;
    }
    sys_noTC = opts->notc;

    int daemon = opts->daemon;
    int force = opts->force;
    free(opts);
    opts = NULL;

#if defined(CFG_ral_master_slave)
    int isSlave = (sys_slaveIdx >= 0);
#else
    int isSlave = 0;
#endif

    if( !isSlave ) {
        if( !force ) {
            int pid = readPid();
            if( pid && kill(pid, 0) == 0 ) {
                // Some process is still running
                fprintf(stderr, "A station with pid=%d is still running (use -f to take over)\n", pid);
                exit(EXIT_NOP);
            }
        } else {
            killOldPid();
        }
    }

    setupConfigFilenames();
    checkRollForward();
    if( !checkUris() )
        return 1;

    if( daemon ) {
        if( logfile.path == NULL ) {
            setLogFile("~temp/station.log", "builtin");  // change default stderr to a file
            setenv("STATION_TEMPDIR", tempDir, 1);
        }
        int subprocPid;
        if( (subprocPid = fork()) == -1 )
            rt_fatal("Daemonize fork failed: %s\n", strerror(errno));
        if( subprocPid != 0 ) {
            fprintf(stderr, "Daemon pid=%d running...\n", subprocPid);
            daemonPid = subprocPid;
            writePid();
            exit(0); // parent exit
        }
        // child is the daemon process
        daemonPid = getpid();
        setsid();
    }

    aio_ini();
    sys_iniLogging(&logfile, !isSlave && !daemon);
    sys_ini();
    rt_ini();
    ts_iniTimesync();

#if defined(CFG_ral_master_slave)
    if( isSlave ) {
        sys_startupSlave(slave_rdfd, slave_wrfd);
        // NOT REACHED
        assert(0);
    }
#endif // defined(CFG_ral_master_slave)

    rt_yieldTo(&startupTmr, daemon ? startupDaemon : startupMaster);
    aio_loop();
    // NOT REACHED
    assert(0);
}
