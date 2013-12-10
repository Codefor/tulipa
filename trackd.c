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

#include "inifile.h"
#include "thread.h"
#include "trackd.h"
#include "util.h"
#include "md5.h"
#include "sha1.h"
#include "log.h"
#include "pool.h"

#include <assert.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <mysql/mysql.h>

#include <event.h>
#include <evhtp.h>





/*
   -------------------------------------------------------------------------------
   Function Defination
   -------------------------------------------------------------------------------
   */
static int create_udp_server_socket(const char *host, int port);
static void libevent_cb_udp_recv(int fd, short which, void *arg);

static int config_init(const char *config_file, struct inifile **ini);
static int settings_init(struct settings **settings, struct inifile *ini);
static int running_init(struct running **running);
static void parse_arguments(int argc, char *argv[]);
static void print_help();

static void create_reset_counter();
static void *reset_counter(void *arg);

static struct trk_thread *pickup_trk_thread();
static inline void push_ele_to_pool(struct trk_item *trkitem);

static void *listener_udp(void *arg);
static void *listener_tcp(void *arg);
static void run_udp(pthread_t *thread);
static void run_tcp(pthread_t *thread);

static int verify_request_arg(evhtp_request_t *req,struct trk_item *trk_item);

static void htp_add_common_headers(evhtp_request_t *req);
static evhtp_res htpcb_pre   (evhtp_connection_t *req, void *arg);
static void      htpcb_track (evhtp_request_t *req, void *arg);
static void      htpcb_status(evhtp_request_t *req, void *arg);
static void      htpcb_ison(evhtp_request_t *req, void *arg);

static void worker_run();

static void shutdown_server();
static void sigtermHandler(int sig);
static void sigtermHandlerChild(int sig);
static void settings_free(struct settings *settings);
/*
   -------------------------------------------------------------------------------
   Global Vars
   -------------------------------------------------------------------------------
   */

struct settings *g_settings = NULL;
struct inifile  *g_ini      = NULL;
struct running  *g_running  = NULL;


/* which thread we assigned a connection to most recently. */
static int g_last_thread = -1;


/* server options */
int trkd_do_daemonize = 0;
int  trkd_do_silence = 0;

char trkd_config_ini[256] = {0};



/*
   -------------------------------------------------------------------------------
   Entry Point
   -------------------------------------------------------------------------------
   */
int main (int argc, char * argv[])
{
	running_init(&g_running);

	//parse command line
	parse_arguments(argc, argv);
	//parse ini file
	if (config_init(trkd_config_ini, &g_ini) != 0) {
		fprintf(stderr, "init config.ini file failure, file: %s\n",
				trkd_config_ini);
		exit(1);
	}
	//setup g_settings
	settings_init(&g_settings, g_ini);

	if(trkd_do_daemonize) g_settings->daemonize = 1;

	if (g_settings->daemonize) {
		daemonize(0);
	}

	if (trkd_do_silence) {
		silence();
	}
	
	g_settings->pid = getpid();
	trackdLog(TRACKD_DEBUG,"Trackd Master started,pid:%d",g_settings->pid);

	/* fork */
	pid_t pid = fork();
	if (pid == -1) {
		fprintf(stderr,"failed to fork\n");
		exit(1);
	} else if (pid == 0) {  /* child process */
		worker_run();
	} else {                /* parent process */
		g_settings->worker_pid = pid;
		g_settings->worker_pid_alive = 1;

		setupSignalHandlers(sigtermHandler);

		pid_t exit_child_pid;
		while (1) {
			/* wait for process termination */
			exit_child_pid = wait(NULL);
			if (exit_child_pid == -1){
				trackdLog(TRACKD_WARNING,"wait error!");
				continue;
			}

			g_settings->worker_pid_alive = 0;
			trackdLog(TRACKD_WARNING,"Trackd Worker exit,pid:%d",exit_child_pid);

			if (g_settings->shutdown_asap){
				shutdown_server();
			}

			/* fork again */
			pid_t pid = fork();
			if (pid == -1) {
				exit(1);
			} else if (pid > 0) {
				g_settings->worker_pid = pid;
				g_settings->worker_pid_alive = 1;

				//sleep for 1 second
				sleep(1);
			} else {
				worker_run();
			}
		}
	}

	return 0;
}


static void worker_run()
{
	trackdLog(TRACKD_DEBUG,"new Trackd Worker started,pid:%d",getpid());
	setupSignalHandlers(sigtermHandlerChild);

	trk_thread_init(sizeof(struct trk_item));

	// Creates a thread for reset counter
	create_reset_counter();

	pthread_t threads[2];

	run_udp(&threads[0]);
	run_tcp(&threads[1]);

	int i = 0;
	for (; i < sizeof(threads)/sizeof(threads[0]); ++i) {
		pthread_join(threads[i], NULL);
	}

	mysql_library_end();

	trackdLog(TRACKD_DEBUG,"Trackd Worker exit,pid:%d",getpid());
}


/*
   -------------------------------------------------------------------------------
   UDP
   -------------------------------------------------------------------------------
   */
static void run_udp(pthread_t *thread)
{
	pthread_attr_t  attr;
	int             ret;

	pthread_attr_init(&attr);
	if ((ret = pthread_create(thread, &attr, listener_udp, NULL)) != 0) {
		fprintf(stderr, "can't create thread: %s\n", strerror(ret));
		exit(1);
	}
}

static void *listener_udp(void *arg)
{
	struct event udp_event;
	struct event_base *main_base;

	main_base = event_init();
	if (!main_base) {
		fprintf(stderr, "can't allocate event base\n");
		exit(1);
	}

	int trk_udpser_shd = create_udp_server_socket(g_settings->host,
			g_settings->port);
	if (trk_udpser_shd == -1) {
		fprintf(stderr, "can't create udp server socket\n");
		exit(1);
	}

	event_set(&udp_event, trk_udpser_shd,
			EV_READ | EV_PERSIST, libevent_cb_udp_recv, NULL);
	event_base_set(main_base, &udp_event);
	event_add(&udp_event, NULL);

	event_base_dispatch(main_base);
	event_base_free(main_base);

	return NULL;
}


/*
   -------------------------------------------------------------------------------
   TCP HTTP
   -------------------------------------------------------------------------------
   */
static void run_tcp(pthread_t *thread)
{
	//  pthread_t       thread;
	pthread_attr_t  attr;
	int             ret;

	pthread_attr_init(&attr);
	if ((ret = pthread_create(thread, &attr, listener_tcp, NULL)) != 0) {
		fprintf(stderr, "can't create thread: %s\n", strerror(ret));
		exit(1);
	}
}

static evhtp_res htpcb_pre(evhtp_connection_t *req, void *arg)
{
	__sync_fetch_and_add(&g_running->today_req_num, 1);
	__sync_fetch_and_add(&g_running->total_req_num, 1);
	//g_running->today_req_num += 1;
	//g_running->total_req_num += 1;
	

	return EVHTP_RES_OK;
}

static void htp_add_common_headers(evhtp_request_t *req)
{
	evhtp_header_key_add(req->headers_out, "Server", 0);
	evhtp_header_val_add(req->headers_out, DDTRACK_SERVER_NAME, 0);
	evhtp_header_key_add(req->headers_out, "Connection", 0);
	evhtp_header_val_add(req->headers_out, "close", 0);
}

static int verify_request_arg(evhtp_request_t *req,struct trk_item *trk_item)
{
	/* find http key */
	const char *q_op, *q_date, *q_data, *q_trk_id, *q_salt, *p;

	q_op     = evhtp_kv_find(req->uri->query, "op");
	q_date   = evhtp_kv_find(req->uri->query, "date");
	q_data   = evhtp_kv_find(req->uri->query, "data");
	q_trk_id = evhtp_kv_find(req->uri->query, "trk_id");
	q_salt	 = evhtp_kv_find(req->uri->query, "salt");

	if (!q_op || !q_date || !q_data || !q_trk_id || !q_salt) {
		evbuffer_add_printf(req->buffer_out, "%d\t%s",
				HTP_MISSING_ARG, "missing arguments");
		return TRACKD_ERR;
	}

	/* check op */
	int op = atoi(q_op);
	if (op < 0 || op >= DDTRACK_OP_MAX) {
		evbuffer_add_printf(req->buffer_out, "%d\top must in [0, %d)",
				HTP_OP_ERR, DDTRACK_OP_MAX);
		return TRACKD_ERR;
	}

	(*trk_item).op = op;

	/* check date */
	int date_len = strlen(q_date);
	if (date_len != DATESTR_LEN) {
		evbuffer_add_printf(req->buffer_out, "%d\t%s",
				HTP_DATELEN_ERR, "datelen error");
	}

	int date = atoi(q_date);
	if (date < MIN_DATE) {
		evbuffer_add_printf(req->buffer_out, "%d\t%s",
				HTP_DATE_ERR, "date error");
		return TRACKD_ERR;
	}

	/* check track id */
	p = q_trk_id;
	while (isdigit(*p)) { p++; }
	if (*p != '\0') {
		evbuffer_add_printf(req->buffer_out, "%d\t%s",
				HTP_TRK_ID_ERR, "track id error");
		return TRACKD_ERR;
	}
	int trk_id = atoi(q_trk_id);
	(*trk_item).trk_id = trk_id;

	int data = atoi(q_data);

	/* check salt */
	const char *token = NULL;
	get_token(op,trk_id,&token,&g_settings);

	struct dict d[3]; 
	d[0].p = "trk_id";
	d[0].v = trk_id;

	d[1].p = "data";
	d[1].v = data;

	d[2].p = "date";
	d[2].v = date;

	qsort(d,3,sizeof(struct dict),cmp);

	int written;

	char cryptstr[41];

	written = snprintf((*trk_item).query_str, sizeof((*trk_item).query_str),
			"%s=%d&%s=%d&%s=%d",
			d[0].p,d[0].v,d[1].p,d[1].v,d[2].p,d[2].v);
	md5((*trk_item).query_str,written,cryptstr);

	written = snprintf((*trk_item).query_str,sizeof((*trk_item).query_str),
			"%s%s",
			cryptstr,token);
	sha1((*trk_item).query_str,written,cryptstr);

	if (memcmp(cryptstr,q_salt,SHA1_LEN) != 0) {
		evbuffer_add_printf(req->buffer_out, "%d\t%s",
				HTP_SALT_ERR, "salt error");
		return TRACKD_ERR;
	}

	snprintf((*trk_item).query_str, sizeof((*trk_item).query_str),
			"t=%ld&data=%s&date=%s",
			time(NULL), q_data, q_date);

	return TRACKD_OK;
}

static void htpcb_track(evhtp_request_t *req, void *arg)
{

	//log trace
	struct sockaddr_in *p = (struct sockaddr_in *)req->conn->saddr;
	trackdLog(TRACKD_NOTICE,"ip:%s,uri:%s?%s",
			inet_ntoa((*p).sin_addr),
			req->uri->path->full,
			req->uri->query_raw);

	struct trk_item trk_item;
	memset(&trk_item, 0, sizeof(struct trk_item));

	if (verify_request_arg(req,&trk_item) != TRACKD_OK) goto finish;
	push_ele_to_pool(&trk_item);

	evbuffer_add_printf(req->buffer_out, "%d\t%s", HTP_OK, "ok");
	goto finish;


finish:
	htp_add_common_headers(req);
	evhtp_send_reply(req, EVHTP_RES_OK);
}

static void htpcb_status(evhtp_request_t *req, void *arg)
{
	/* request number */
	evbuffer_add_printf(req->buffer_out, "req num: %llu/%llu (today/total)\n",
			g_running->today_req_num, g_running->total_req_num);

	/* pool size */
	struct trk_thread *t;
	int i;
	size_t total_size     = 0;
	size_t total_capacity = 0;
	for (i = 0; i < g_settings->num_worker_threads; ++i) {
		t = trk_thread_choose_one(i);
		size_t size = pool_size(t->pool);
		evbuffer_add_printf(req->buffer_out, "pool[%02d]: %6ld/%6ld (cur/max)\n",
				i, size, t->pool->capacity);
		total_size     += size;
		total_capacity += t->pool->capacity;
	}
	evbuffer_add_printf(req->buffer_out, "  ptotal: %6ld/%6ld (cur/max)\n",
			total_size, total_capacity);

	/* up time */
	int up_time = (int)(time(NULL) - g_running->start_time);
	evbuffer_add_printf(req->buffer_out, 
			"\nup: %dd, %dh, %dm, %ds\n",
			up_time / 86400,
			up_time % 86400 / 3600,
			up_time % 3600  / 60,
			up_time % 60);

	htp_add_common_headers(req);
	evhtp_send_reply(req, EVHTP_RES_OK);
}

static void htpcb_ison(evhtp_request_t *req, void *arg)
{
	evbuffer_add_printf(req->buffer_out, "1");

	htp_add_common_headers(req);
	evhtp_send_reply(req, EVHTP_RES_OK);
}

static void *listener_tcp(void *arg)
{
	evbase_t *evbase = event_base_new();
	evhtp_t  *htp    = evhtp_new(evbase, NULL);

	/* set callback func */
	//增加计数信息
	evhtp_set_pre_accept_cb(htp, htpcb_pre, NULL);

	evhtp_set_gencb(htp, htpcb_track, NULL);             /* track */
	evhtp_set_cb(htp, "/?", htpcb_track, NULL);
	evhtp_set_cb(htp, "/_status", htpcb_status, NULL);   /* status */
	evhtp_set_cb(htp, "/_ison", htpcb_ison, NULL);   /* ison */

	struct timeval timeo;
	timeo.tv_sec  = 0;
	timeo.tv_usec = 500000; // 0.5 sec
	evhtp_set_timeouts(htp, &timeo, &timeo);             /* set timeout */

	if (EVHTP_THREAD_NUM > 0) {
		evhtp_use_threads(htp, NULL, EVHTP_THREAD_NUM, NULL);
	}
	evhtp_bind_socket(htp, g_settings->host, g_settings->port, 1024);

	trackdLog(TRACKD_DEBUG,"Tulipa is now listening %s:%d",g_settings->host, g_settings->port);

	event_base_loop(evbase, 0);

	//evhtp_free(htp);
	//event_base_free(evbase);
	return NULL;
}




/*
   -------------------------------------------------------------------------------
   Functions
   -------------------------------------------------------------------------------
   */



/**
 * Create a socket and bind it to a specific port number
 *
 * @param host the host to bind to
 * @param port the port number to bind to
 */
static int create_udp_server_socket(const char *host, int port)
{
	int nfd;

	nfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (nfd < 0) return -1;

	int flags = 1;
	setsockopt(nfd, SOL_SOCKET, SO_REUSEADDR, (char *)&flags, sizeof(int));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = inet_addr(host);
	addr.sin_port        = htons(port);

	if (bind(nfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		return -1;
	}

	if ((flags = fcntl(nfd, F_GETFL, 0)) < 0 ||
			fcntl(nfd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -1;

	return nfd;
}



/**
 * libevent callback function for main_base to receive udp data
 */
static void libevent_cb_udp_recv(int fd, short which, void *arg)
{
	__sync_fetch_and_add(&g_running->today_req_num, 1);
	__sync_fetch_and_add(&g_running->total_req_num, 1);

	struct trk_item trk_item;
	memset(&trk_item, 0, sizeof(struct trk_item));

	sprintf(trk_item.query_str, "t=%d&", (int)time(NULL));
	size_t buf_len = strlen(trk_item.query_str);

	struct sockaddr_in client;
	socklen_t len = sizeof(struct sockaddr_in);  
	recvfrom(fd, trk_item.query_str + buf_len,
			sizeof(trk_item.query_str) - buf_len - 1,
			0, (struct sockaddr *)&client, &len);

	int dtlen = 0;
	size_t data_real_len = 0;
	size_t salt_len = 0;
	const char *p = NULL;
	int is_trk_id_ok = 0;
	/* check data len */
	static const char *delimiter = "&";
	char *brkt;
	char *str_cpy = strdup(trk_item.query_str);
	char *pch = strtok_r(str_cpy, delimiter, &brkt);
	while (pch != NULL) {
		if (memcmp(pch, "dtlen=", 6) == 0) {
			dtlen = atoi(pch + 6);
		} else if (memcmp(pch, "op=", 3) == 0) {
			trk_item.op = atoi(pch + 3);
		} else if (memcmp(pch, "data=", 5) == 0) {
			data_real_len = strlen(pch+5);
		} else if (memcmp(pch, "trk_id=", 7) == 0) {
			p = pch + 7;
			while (isdigit(*p)) { p++; }
			if (*p == '\0') {
				is_trk_id_ok = 1;
			}
		} else if (memcmp(pch, "salt=", 5) == 0) {
			salt_len = strlen(pch+5);
		}
		pch = strtok_r(NULL, delimiter, &brkt);
	}
	free(str_cpy);

	// error data, do nothing
	if (salt_len != SHA1_LEN || !is_trk_id_ok ||
			dtlen != data_real_len ||
			(trk_item.op < 0 || trk_item.op >= DDTRACK_OP_MAX)) {
		return;
	}

	push_ele_to_pool(&trk_item);
}


static struct trk_thread *pickup_trk_thread()
{
	int old_last_thread;
	int tid;
	while (1) {
		old_last_thread = g_last_thread;
		tid = (old_last_thread + 1) % g_settings->num_worker_threads;
		// CAS
		if (__sync_bool_compare_and_swap(&g_last_thread, old_last_thread, tid)) {
			break;
		}
	}

	return trk_thread_choose_one(tid);
}


/*
 * Creates a thread for reset counter
 */
static void create_reset_counter()
{
	pthread_t       thread;
	pthread_attr_t  attr;
	int             ret;

	pthread_attr_init(&attr);
	if ((ret = pthread_create(&thread, &attr, reset_counter, NULL)) != 0) {
		fprintf(stderr, "can't create reset counter thread: %s\n", strerror(ret));
		exit(1);
	}
}


static void *reset_counter(void *arg)
{
	int64_t old_counter;
	time_t t, now, tomorrow;
	char buf[32];
	struct tm tm;

	while (1) {
		memset(buf, 0, sizeof(buf));
		memset(&tm, 0, sizeof(struct tm));

		t = time(NULL) + 86400;
		strftime(buf, sizeof(buf), "%Y-%m-%d", localtime(&t));
		strptime(buf, "%Y-%m-%d", &tm);
		tomorrow = mktime(&tm);

		while (1) {
			now = time(NULL);
			if (now < tomorrow) {
				int s = (int)(tomorrow-now)/2;
				sleep(s > 1 ? s : 1);
				continue;
			}

			/* reset counter to 0 */
			while (1) {
				old_counter = g_running->today_req_num;
				if (__sync_bool_compare_and_swap(&g_running->today_req_num, old_counter, 0)) {
					break;
				}
			}
			break;
		}
	}
	//return void*,or it will issuse a warning here. However,useless
	//it should never run to here!
	return NULL; 
}


/**
 * create inifile and initinate
 */
static int config_init(const char *config_file, struct inifile **ini)
{
	*ini = inifile_new();
	if (*ini == NULL) return TRACKD_ERR;

	if (inifile_parse_file(*ini, config_file) != 0) {
		return TRACKD_ERR;
	}
	return TRACKD_OK;
}

static int running_init(struct running **running)
{
	*running = calloc(1, sizeof(struct running));
	if (!*running) {
		return -1;
	}

	(*running)->start_time    = time(NULL);
	(*running)->today_req_num = 0;
	(*running)->total_req_num = 0;

	return 0;
}


/**
 * create settings and initinate
 * host
 * port
 * num_worker_threads
 * trk_msg_len
 * job_servers
 * job_func_lists
 */
static int settings_init(struct settings **settings, struct inifile *ini)
{
	assert(ini);
	
	*settings = calloc(1, sizeof(struct settings));
	if (!*settings) {
		return -1;
	}

	/* [trackd] */
	if (inifile_fetch_str(ini, "trackd", "listen_host",
				&(*settings)->host) != 0 ||
			inifile_fetch_int(ini, "trackd", "listen_port",
				&(*settings)->port) != 0 ||
			inifile_fetch_int(ini, "trackd", "num_worker_threads",
				&(*settings)->num_worker_threads) != 0) {
		return -2;
	}

	inifile_fetch_str(ini, "trackd", "pidfile",&(*settings)->pidfile);
	inifile_fetch_str(ini, "trackd", "logfile",&(*settings)->logfile);

	const char *p = NULL;
	inifile_fetch_str(ini, "trackd", "loglevel",&p);
	if(memcmp(p,"debug",5) == 0){
		(*settings)->verbosity = TRACKD_DEBUG;
	}else if(memcmp(p,"verbose",7) == 0){
		(*settings)->verbosity = TRACKD_VERBOSE;
	}else if(memcmp(p,"notice",6) == 0){
		(*settings)->verbosity = TRACKD_NOTICE;
	}else if(memcmp(p,"warning",7) == 0){
		(*settings)->verbosity = TRACKD_WARNING;
	}else{
		return TRACKD_ERR;
	}

	/* [op_func_list] */
	const char *k_op,*k_trk_id,*v_func,*v_token;
	struct iniitem *iniitem = NULL;
	struct iniitem *iniitem_token = NULL;
	struct token_item *token_item = NULL;

	while ((iniitem = inifile_foreach_group(ini, "op_func_list", iniitem,
					&k_op, &v_func))) {
		int op = atoi(k_op);
		if (op < 0 || op >= DDTRACK_OP_MAX) {
			continue;
		}

		//init func
		(*settings)->op_funcs[op] = calloc(1,sizeof(struct func));

		struct func *f = (*settings)->op_funcs[op];
		f->op = op;
		f->func = v_func;

		char groupname[32];
		snprintf(groupname,sizeof(groupname),"op_func_%d_sinkserver",op);
		inifile_fetch_str(ini, groupname, "sink_servers",&(f->sink_servers));
		inifile_fetch_str(ini, groupname, "sink_type",&(f->sink_type));

		inifile_fetch_str(ini, groupname, "user",&(f->user));
		inifile_fetch_str(ini, groupname, "pass",&(f->pass));
		inifile_fetch_str(ini, groupname, "db",&(f->db));

		//check tokens
		snprintf(groupname,sizeof(groupname),"op_func_%d_token",op);

		struct token_item *tail = f->tokens;
		while ((iniitem_token = inifile_foreach_group(ini, groupname, iniitem_token,
						&k_trk_id, &v_token))) {
			token_item = calloc(1,sizeof(struct token_item));

			if (!token_item){
				return TRACKD_ERR;
			}

			token_item->trk_id = atoi(k_trk_id);
			token_item->token  = v_token;
			token_item->next = NULL;

			if( tail == NULL ){
				f->tokens= token_item;
				tail = f->tokens;
			}else{
				tail->next = token_item;
				tail = tail->next;
			}
		}
	}

	/* check settings value */
	if ((*settings)->num_worker_threads < 1 ||
			(*settings)->num_worker_threads > 64) {
		fprintf(stderr, "'num_worker_threads' must in range [1, 64]\n");
		exit(1);
	}

	/* check op func list */
	int i = DDTRACK_OP_MAX - 1;
	for (; i >= 0; --i) {
		if((*settings)->op_funcs[i] && (*settings)->op_funcs[i]->func != NULL){
			break;
		}
	}

	if (i < 0) {
		fprintf(stderr, "[op_func_list] is empty, op range: [1, %d)\n",
				DDTRACK_OP_MAX);
		exit(1);
	}

	return 0;
}


static void print_help()
{
	const static char *h = "ctrackd - UDP & HTTP Track system .\n"
		"Options:\n"
		"\t-c <config.ini> file to config.ini\n"
		"\t-d run as daemon\n"
		"\t-h print help\n"
		"Usage:\n"
		"\tstart: ./ctrackd -c config.ini -d\n"
		"\tstop : killall ddtrackd\n"
		;
	fprintf(stdout, "%s", h);
}

/**
 * getopt(argc, argv, "ab:c:de::");
 * 1.单个字符，表示选项，（如上例中的abcde各为一个选项）
 * 2.单个字符后接一个冒号：表示该选项后必须跟一个参数。参数紧跟在选项后或者以空格隔开。该参数的指针赋给optarg。（如上例中的b:c:）
 * 3 单个字符后跟两个冒号，表示该选项后必须跟一个参数。参数必须紧跟在选项后不能以空格隔开。该参数的指针赋给optarg。(如上例中的e::)
 * */
static void parse_arguments(int argc, char *argv[])
{
	int optchr = 0;
	while ((optchr = getopt(argc, argv, "c:l:t:dsh")) != -1) {
		switch (optchr) {
			case 'c':
				snprintf(trkd_config_ini, sizeof(trkd_config_ini), "%s", optarg);
				break;
			case 'd':
				trkd_do_daemonize = 1;
				break;
			case 's':
				trkd_do_silence = 1;
				break;
			case 'h':
			default:
				print_help();
				exit(1);
		}
	}
}


static inline void push_ele_to_pool(struct trk_item *trkitem)
{
	struct trk_thread *t;
	int res;
	while (1) {
		t = pickup_trk_thread();
		res = pool_push(t->pool, trkitem);
		if (res == 0/*success*/ || res == -2/*system overload ignore*/) {
			break;
		}
		usleep(1);
	}

	/* write notify to pipe */
	if (res == 0 && write(t->notify_send_fd, "", 1) != 1) {
		fprintf(stderr, "write thread notify pipe failure\n");
	}
}

static void settings_free(struct settings *settings){
	assert(settings);

	int i;
	struct func *f;
	struct token_item *p,*q;
	for(i=0;i<DDTRACK_OP_MAX;i++){
		f = settings->op_funcs[i];
		if(f == NULL){
			continue;
		}

		p = f->tokens;
		while(p){
			q = p->next;
			free(p);
			p = q;
		}

		free(f);
	}
	free(settings);
}

static void shutdown_server() {
	if(g_settings->daemonize) unlink(g_settings->pidfile); 

	trackdLogFromHandler(TRACKD_WARNING,"all is well, I'm going down...byebye");

	inifile_free(g_ini);
	free(g_running);
	settings_free(g_settings);
	exit(0);
}

static void sigtermHandler(int sig) {
	trackdLogFromHandler(TRACKD_WARNING,"Main Process Received SIGTERM, scheduling shutdown...");
	g_settings->shutdown_asap = 1;

	if(g_settings->worker_pid_alive){
		kill(g_settings->worker_pid,SIGTERM);
	}
}

static void sigtermHandlerChild(int sig) {
	trackdLogFromHandler(TRACKD_WARNING,"Worker Process Received SIGTERM, scheduling shutdown...");

	struct trk_thread *thread;
	int i,j,k;
	//check pool
	while(1){
		size_t total_size     = 0;

		for (i = 0; i < g_settings->num_worker_threads; ++i) {
			thread = trk_thread_choose_one(i);
			size_t size = pool_size(thread->pool);
			total_size     += size;
		}

		if(total_size == 0) {
			trackdLogFromHandler(TRACKD_WARNING,"empty pool, really going down...");
			break;
		}
		trackdLogFromHandler(TRACKD_WARNING,
				"trying shutdown but there are still items in pool");
		sleep(1);
	}

	//iter every worker thread,
	//close sink server connection;free pool
	struct trk_sink_client *client;
	struct trk_client_node *node;
	for (i = 0; i < g_settings->num_worker_threads; ++i) {
		thread = trk_thread_choose_one(i);

		pool_free(thread->pool);
		event_base_free(thread->base);

		for(j=0;j < DDTRACK_OP_MAX;j++){
			client = thread->trk_r_clients[j];

			if(client == NULL){
				continue;
			}

			for(k=0;k < client->num_clients;k++){
				node = client->nodes + k;
				node->finalizer(node->conn);
			}
		}
	}

	inifile_free(g_ini);
	free(g_running);
	settings_free(g_settings);

	//clean mysql library
	//mysql_library_end();
	trackdLogFromHandler(TRACKD_WARNING,"child byebye.");
}
