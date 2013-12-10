/* 
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

#ifndef __THREAD_H__
#define __THREAD_H__

#include "trackd.h"

#include <event.h>

/* ignore time(sec) when error occur */
//如果client在最近2秒内发生过错误，则跳过，选取其他的client
#define TRK_ERR_IGNORE_TIME 2

/* pool's capacity, MUST less than pipe's max size 64K(65536) */
#define TRK_POOL_CAPACITY  10240

struct trk_client_node{
	void		*conn;

	char		host[64];
	in_port_t	port;

	const char	*user;
	const char	*pass;
	const char	*db;
	
	int (*proc)(void* n,void* item);
	int (*finalizer)(void* n);

	time_t	last_err_time;   /* last err occur time, 0 for no err */
};

struct trk_sink_client {
	//array of real clients
	struct trk_client_node *nodes;

	int op;
	int          num_clients;
	unsigned int client_idx;
};


struct trk_thread {
	struct event_base *base;    /* libevent handle this thread uses */
	struct event notify_event;  /* listen event for notify pipe */
	int notify_receive_fd;      /* receiving end of notify pipe */
	int notify_send_fd;         /* sending end of notify pipe   */

	struct pool *pool;          /* buffer pool */

	struct trk_sink_client *trk_r_clients[DDTRACK_OP_MAX];/* */
};



int trk_thread_init(int trk_msg_len);

int trk_thread_setup(struct trk_thread *me,
				int trk_msg_len, 
				const char *gearman_servers);

struct trk_thread *trk_thread_choose_one(int idx);

#endif
