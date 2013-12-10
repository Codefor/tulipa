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

#include "pool.h"
#include "thread.h"
#include "redisjob.h"
#include "mysqljob.h"

#include <pthread.h>
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

extern struct settings *g_settings;

static void create_worker(void *(*func)(void *), void *arg);
static void *worker_libevent_loop(void *arg);

static void libevent_cb_worker_notify(int fd, short which, void *arg);

static void trk_thread_setup_sink_client(struct trk_thread *me);
static void setup_client_node(struct trk_client_node *n,struct func *f,char *host,char *port);
/*
 * Number of worker threads that have finished setting themselves up.
 */
static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;


/*
 * threads begin pointer
 */
static struct trk_thread *threads;


/**
 * init track worker thread
 */
int trk_thread_init(int trk_msg_len)
{
	pthread_mutex_init(&init_lock, NULL);
	pthread_cond_init(&init_cond, NULL);

	//init mysql library
	//mysql_library_init(0, NULL, NULL);
	//实例化nthreads个线程
	threads = calloc(g_settings->num_worker_threads, sizeof(struct trk_thread));
	if (!threads) {
		fprintf(stderr, "can't allocate thread descriptors");
		exit(1);
	}

	int i;
	struct trk_thread *me = NULL;
	int nthreads = g_settings->num_worker_threads;
	for (i = 0; i < nthreads; i++) {
		me = &threads[i];

		int fds[2];
		if (pipe(fds)) {
			fprintf(stderr, "can't create notify pipe");
			exit(1);
		}
#ifdef _GNU_SOURCE
		fcntl(fds[0], F_SETFL, O_NOATIME);
#endif

		me->notify_receive_fd = fds[0];
		me->notify_send_fd    = fds[1];

		me->base = event_init();
		if (!me->base) {
			fprintf(stderr, "can't allocate event base\n");
			exit(1);
		}

		/* listen for notifications from other threads */
		event_set(&me->notify_event, me->notify_receive_fd,
				EV_READ | EV_PERSIST, libevent_cb_worker_notify, me);
		event_base_set(me->base, &me->notify_event);

		if (event_add(&me->notify_event, NULL) == -1) {
			fprintf(stderr, "can't monitor libevent notify pipe\n");
			exit(1);
		}

		/* init buffer pool */
		me->pool = pool_new(TRK_POOL_CAPACITY, trk_msg_len);
		if (!me->pool) {
			fprintf(stderr, "init buffer pool failure\n");
			exit(1);
		}

		trk_thread_setup_sink_client(me);
	}

	/* Create threads after we've done all the libevent setup. */
	/* worker_libevent_loop will incr init_count. */
	for (i = 0; i < nthreads; i++) {
		create_worker(worker_libevent_loop, &threads[i]);
	}

	/* Wait for all the threads to set themselves up before returning. */
	pthread_mutex_lock(&init_lock);
	while (init_count < nthreads) {
		pthread_cond_wait(&init_cond, &init_lock);
	}
	pthread_mutex_unlock(&init_lock);

	return 0;
}


static void trk_thread_setup_sink_client(struct trk_thread *me)
{
	assert(me);

	int j;
	struct func *f = NULL;
	struct trk_sink_client *client = NULL;

	for(j=0;j<DDTRACK_OP_MAX;j++){
		f = g_settings->op_funcs[j];
		if(f == NULL) continue;

		const char *p = f->sink_servers;
		if(p == NULL) continue;

		me->trk_r_clients[j] = calloc(1,sizeof(struct trk_sink_client));
		client = me->trk_r_clients[j];
		if(!client){
			fprintf(stderr, "calloc struct trk_sink_client failed");
			exit(-1);
		}

		client->op = j;

		client->num_clients = 1;
		while (*p) {
			if (*p == ',') {
				client->num_clients++;
			}
			p++;
		}

		//init client's node,which is an array of connections.
		client->nodes = calloc(client->num_clients, sizeof(struct trk_client_node));
		if(!client->nodes){
			fprintf(stderr, "calloc struct trk_client_node failed");
			exit(-1);
		}

		/*
		 * The format for the server list is: SERVER[:PORT][,SERVER[:PORT]]
		 * @example: 10.0.0.1:4730,10.0.0.2,10.0.0.3:4731
		 */
		const char *ptr = f->sink_servers;
		char host[NI_MAXHOST] = {0};
		char port[NI_MAXSERV] = {0};
		size_t x;
		int client_idx = 0;

		while (1) {
			x = 0;
			while (*ptr != 0 && *ptr != ',' && *ptr != ':') {
				if (x < (NI_MAXHOST - 1)) {
					host[x++] = *ptr;
				}
				ptr++;
			}
			host[x] = 0;

			if (*ptr == ':') {
				ptr++;
				x = 0;
				while (*ptr != 0 && *ptr != ',') {
					if (x < (NI_MAXSERV - 1)) {
						port[x++] = *ptr;
					}
					ptr++;
				}
				port[x] = 0;
			} else {
				port[0] = 0;
			}

			setup_client_node(client->nodes+client_idx,f,host,port);

			client_idx++;

			if (*ptr == 0) {
				break;
			}
			ptr++;
		}//end while
	}
}

static void setup_client_node(struct trk_client_node *n,struct func *f,char *host,char *port){
	assert(n);

	strncpy(n->host, host, sizeof(n->host));
	n->port = (in_port_t)atoi(port);
	n->user = f->user;
	n->pass = f->pass;
	n->db = f->db;

	if(memcmp(f->sink_type,"mysql",5) == 0){
		MYSQL *conn = mysql_init(NULL);
		if(conn == NULL){
			fprintf(stderr, "init MYSQL conn failed\n");
			exit(1);
		}

		if(!mysql_real_connect(conn,n->host,n->user,n->pass,n->db,n->port,NULL,0)){
			mysql_close(conn);
			fprintf(stderr, "create mysql client failed\n");
			exit(1);
		}

		n->conn = conn;
		n->proc = mysql_proc;
		n->finalizer = mysql_finalizer;
	}else if(memcmp(f->sink_type,"redis",5) == 0){
		/* create redis clients */
		redisContext *conn = redisConnect(n->host,n->port);
		if(conn->err) {
			redisFree(conn);  
			fprintf(stderr, "create redis client failed\n");
			exit(1);
		}

		n->conn = conn;
		n->proc = redis_proc;
		n->finalizer = redis_finalizer;
	}else{
		fprintf(stderr, "init sink server failure,unknown sink_type\n");
		exit(1);
	}
}

struct trk_thread *trk_thread_choose_one(int idx)
{
	return threads + idx;
}


/*
 * Creates a worker thread.
 */
static void create_worker(void *(*func)(void *), void *arg)
{
	pthread_t       thread;
	pthread_attr_t  attr;
	int             ret;

	pthread_attr_init(&attr);
	if ((ret = pthread_create(&thread, &attr, func, arg)) != 0) {
		fprintf(stderr, "can't create thread: %s\n", strerror(ret));
		exit(1);
	}
}


/*
 * Worker thread: main event loop
 */
static void *worker_libevent_loop(void *arg)
{
	//pthread_detach(pthread_self());

	struct trk_thread *me = arg;

	/* Any per-thread setup can happen here; trk_thread_init() will block until
	 * all threads have finished initializing.
	 */

	pthread_mutex_lock(&init_lock);
	init_count++;
	pthread_cond_signal(&init_cond);
	pthread_mutex_unlock(&init_lock);

	event_base_loop(me->base, 0);
	return NULL;
}



/**
 * Processes an incoming track message. This is called when
 * input arrives on the libevent wakeup pipe.
 */
static void libevent_cb_worker_notify(int fd, short which, void *arg)
{
	struct trk_thread *me = arg;
	char buf[1];

	if (read(fd, buf, 1) != 1) {
		fprintf(stderr, "read thread notify pipe failure\n");
	}

	struct trk_item trk_item;
	memset(&trk_item, 0, sizeof(struct trk_item));

	if (pool_pop(me->pool, &trk_item) != 0) {
		fprintf(stderr, "pool_pop() failure\n");
		return;
	}

	const char *func_name = g_settings->op_funcs[trk_item.op]->func;
	if (func_name == NULL) {
		return;
	}

	int try_times = 0;
	struct trk_client_node *node;
	struct trk_sink_client *client = me->trk_r_clients[trk_item.op];

	//return value of sink data
	int ret;

	while (1) {
		if (++try_times > client->num_clients) { break; }

		/* 加1求余轮询 */
		int client_idx = (client->client_idx + 1) % client->num_clients;
		client->client_idx = client_idx;

		node = client->nodes + client_idx;

		if (node->last_err_time > 0) {   /* 最近发生过错误 */
			if (time(NULL) - node->last_err_time <= TRK_ERR_IGNORE_TIME) {
				continue;  /* N秒内刚发生，排除之，选择下一个 */
			} else {
				node->last_err_time = time(NULL);    /* 立刻更新错误时间 */

				const char* sink_type = g_settings->op_funcs[trk_item.op]->sink_type;

				if(memcmp(sink_type,"mysql",5) == 0){
					//mysql_close(node->conn);
					if(!mysql_real_connect(node->conn,
								node->host,node->user,node->pass,node->db,node->port,NULL,0)){
						fprintf(stderr, "create mysql client failed\n");
						exit(1);
					}
				}else if(memcmp(sink_type,"redis",5) == 0){
					/* create redis clients */

					if(node->conn != NULL){
						redisFree((redisContext*)node->conn);
					}

					node->conn = redisConnect(node->host,node->port);
					if(((redisContext*)(node->conn))->err) {
						redisFree((redisContext*)node->conn);  
						continue;
					}
				}
			}
		}

		ret = node->proc(node,&trk_item);

		/* if failure, try n times. still failure, ignore this data item */
		if (ret == TRACKD_OK) {
			node->last_err_time = 0;         /* clear last err time */
			break;
		} else {
			node->last_err_time = time(NULL);  /* set last err time */
		}
	} /* /while */
}

