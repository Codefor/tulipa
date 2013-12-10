/* 
 * Tulipa:Yet Another Track Server
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

#ifndef __TRACKD_H__
#define __TRACKD_H__

#include<time.h>

#define DDTRACK_OP_MAX    32 /* max op */
#define TRK_MAX_MSG_LEN 1472 /* 1500(MTU) - 20(IP) - 8(UDP) */
#define EVHTP_THREAD_NUM  4 /* number of evhtp threads, set 0 will NOT use multi thread */
#define DDTRACK_SERVER_NAME "Tulipa 1.0"

#define SHA1_LEN 40

#define DATESTR_LEN 8 /* 20131203 */
#define MIN_DATE 19700101 

#define TRACKD_OK 0
#define TRACKD_ERR -1 

/* trk item in pool */
struct trk_item {
	int op:5;
	int trk_id:27;
	char query_str[TRK_MAX_MSG_LEN];
};


/* track server http return code */
typedef enum {
	HTP_OK          = 1,
	HTP_MISSING_ARG,
	HTP_DATALEN_ERR,
	HTP_DATELEN_ERR,
	HTP_DATE_ERR,
	HTP_TRK_ID_ERR,
	HTP_TIME_ERR,
	HTP_OP_ERR,
	HTP_SALT_ERR
} track_htp_code_t;


/* running info */
struct running {
	time_t start_time;
	unsigned long long today_req_num;
	unsigned long long total_req_num;
};

struct settings {
	const char *host;
	const char *pidfile;
	const char *logfile; /* Path of log file */

	int port;

	int verbosity; /* Loglevel in conf */
	int daemonize; /* True if running as a daemon */

	int pid;
	int worker_pid;
	int worker_pid_alive;
	int shutdown_asap; 

	int num_worker_threads;

	/* funcs */
	struct func *op_funcs[DDTRACK_OP_MAX];
};

/*
 * A func is a complete track work flow,
 * which has a single-linked sink_servers list
 */
struct func{
	//backend sink server
	const char *sink_type;
	const char *sink_servers;

	//auth,optional
	const char *user;
	const char *pass;
	const char *db;

	//args
	//const char *args;

	//single linked list
	struct token_item *tokens;

	const char *func;
	int op;
};

/* 
 * every trk_id has a single-linked token-items list
 */
struct token_item{
	int trk_id;
	const char *token;
	struct token_item *next;
};

struct func_arg{
	int op;
	struct arg_item *args[10];
};

struct arg_item{
	const char *name;
	const char *type;
	int len;
};

#endif
