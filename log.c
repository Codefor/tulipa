/* log functions.
 *
 * Copyright (c) 2013, Codefor <hk dot yuhe at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "trackd.h"
#include "log.h"

#include<unistd.h>
#include<fcntl.h>
#include<stdio.h>
#include<sys/time.h>
#include<stdarg.h>
#include<string.h>

extern struct settings *g_settings;

/* Low level logging. To use only for very big messages, otherwise
 * trackLog() is to prefer. */
void trackdLogRaw(int level, const char *msg) {
    const char *c = ".-*#";
    FILE *fp;
    char buf[64];
    int rawmode = (level & TRACKD_LOG_RAW);

    level &= 0xff; /* clear flags */
    if (level < g_settings->verbosity) return;
    fp = (g_settings->logfile == NULL) ? stdout : fopen(g_settings->logfile,"a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp,"%s",msg);
    } else {
        int off;
        struct timeval tv;

        gettimeofday(&tv,NULL);
        off = strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S.",localtime(&tv.tv_sec));
        snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
        fprintf(fp,"[%5d] %s %c %s\n",(int)getpid(),buf,c[level],msg);
    }
    fflush(fp);

    if (g_settings->logfile) fclose(fp);
}

/* Like redisLogRaw() but with printf-alike support. This is the funciton that
 * is used across the code. The raw version is only used in order to dump
 * the INFO output on crash. */
void trackdLog(int level, const char *fmt, ...) {
    va_list ap;
    char msg[TRACKD_MAX_LOGMSG_LEN];

    if ((level&0xff) < g_settings->verbosity) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    trackdLogRaw(level,msg);
}

/* Log a fixed message without printf-alike capabilities, in a way that is
 * safe to call from a signal handler.
 *
 * We actually use this only for signals that are not fatal from the point
 * of view of Redis. Signals that are going to kill the server anyway and
 * where we need printf-alike features are served by redisLog(). */
void trackdLogFromHandler(int level, const char *msg) {
    int fd;
    char buf[64];

    if ((level&0xff) < g_settings->verbosity ||
        (g_settings->logfile == NULL && g_settings->daemonize)) return;
    fd = g_settings->logfile ?
        open(g_settings->logfile, O_APPEND|O_CREAT|O_WRONLY, 0644) :
        STDOUT_FILENO;
    if (fd == -1) return;
    snprintf(buf,sizeof(buf),"%d",getpid());
    if (write(fd,"[",1) == -1) goto err;
    if (write(fd,buf,strlen(buf)) == -1) goto err;
    if (write(fd," | signal handler] (",20) == -1) goto err;
    snprintf(buf,sizeof(buf),"%ld",time(NULL));
    if (write(fd,buf,strlen(buf)) == -1) goto err;
    if (write(fd,") ",2) == -1) goto err;
    if (write(fd,msg,strlen(msg)) == -1) goto err;
    if (write(fd,"\n",1) == -1) goto err;
err:
    if (g_settings->logfile) close(fd);
}


