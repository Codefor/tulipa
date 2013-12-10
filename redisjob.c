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

#include "redisjob.h"
#include "thread.h"

#include <string.h>
#include <stdlib.h>

static int ddtrack_job(redisContext *redis,struct trk_item* trk_item);
static int dashboard_job(redisContext *redis,struct trk_item* trk_item);
static int redis_hset(redisContext *redis,const char *key, const char *filed,long value);
static int redis_hincr(redisContext *redis,const char *key, const char *filed,long value);

int redis_proc(void* n,void* item){
	if(n == NULL || item == NULL){
		return TRACKD_ERR;
	}

	struct trk_client_node *node = (struct trk_client_node *)n;
	struct trk_item *trk_item = (struct trk_item *)item;

	switch(trk_item->op){
		case 1:
			//ddtrack
			return ddtrack_job((redisContext*)node->conn,trk_item);
		case 2:
			//dashboard
			return dashboard_job((redisContext*)node->conn,trk_item);
		default:
			return TRACKD_OK;
			//do nothing
	}

	return TRACKD_OK;
}

int redis_finalizer(void* n){
	if (n == NULL){
		return TRACKD_OK;
	}

	redisFree((redisContext *)n);
	return TRACKD_OK;
}

static int dashboard_job(redisContext *redis,struct trk_item* trk_item){
	//date=20131203&date=123&t=xx
	static const char *delimiter = "&";

	char redishkey[REDIS_HKEY_MAX_LENGTH];
	char redishfield[REDIS_HFIELD_MAX_LENGTH];

	snprintf(redishkey,REDIS_HKEY_MAX_LENGTH,"dashboard_%d",trk_item->trk_id);

	long data = 0;
	//time_t t = 0;
	//struct tm *local_time = NULL;

	char *brkt;
	char *str_cpy = strdup(trk_item->query_str);
	char *pch = strtok_r(str_cpy, delimiter, &brkt);
	while (pch != NULL) {
		if (memcmp(pch, "data=", 5) == 0) {
			data = atol(pch+5);
		} else if (memcmp(pch, "date=", 5) == 0) {
			snprintf(redishfield,REDIS_HFIELD_MAX_LENGTH,"%s",pch+5);
		} else if (memcmp(pch, "t=", 2) == 0) {
			//t = atoi(pch+2);
			//local_time = localtime(&t); 
		}
		pch = strtok_r(NULL, delimiter, &brkt);
	}
	free(str_cpy);

	redis_hset(redis,redishkey,redishfield,data);

	return TRACKD_OK;
}

static int ddtrack_job(redisContext *redis,struct trk_item *trk_item){
	static const char *delimiter = "&";
	static const char *op_1 = "ddtrack_";

	//printf("query_str:%s\n",trk_item->query_str);
	char redishkey[REDIS_HKEY_MAX_LENGTH];
	char redishfield[REDIS_HFIELD_MAX_LENGTH];

	int hkey_len = strlen(op_1);
	strncpy( redishkey, op_1 , hkey_len);

	long data = 0;
	time_t t = 0;
	struct tm *local_time = NULL;

	char *brkt;
	char *str_cpy = strdup(trk_item->query_str);
	char *pch = strtok_r(str_cpy, delimiter, &brkt);
	while (pch != NULL) {
		if (memcmp(pch, "data=", 5) == 0) {
			data = atoi(pch+5);
		} else if (memcmp(pch, "trk_id=", 7) == 0) {
			strncpy( redishkey + hkey_len, pch + 7, strlen(pch) - 7);
			hkey_len += strlen(pch) - 7;
			redishkey[hkey_len++] = '\0';
		} else if (memcmp(pch, "t=", 2) == 0) {
			t = atoi(pch+2);
			local_time = localtime(&t); 
			strftime(redishfield, sizeof(redishfield), "%Y%m%d%H%M%S", local_time);
		}
		pch = strtok_r(NULL, delimiter, &brkt);
	}
	free(str_cpy);

	//incr second
	redis_hincr(redis,redishkey,redishfield,data);

	//incr minute
	redishfield[12] = '\0';
	redis_hincr(redis,redishkey,redishfield,data);

	//incr hour
	redishfield[10] = '\0';
	redis_hincr(redis,redishkey,redishfield,data);

	//incr day
	redishfield[8] = '\0';
	redis_hincr(redis,redishkey,redishfield,data);

	//incr month
	redishfield[6] = '\0';
	redis_hincr(redis,redishkey,redishfield,data);

	//incr year
	redishfield[4] = '\0';
	redis_hincr(redis,redishkey,redishfield,data);

	return TRACKD_OK;
}

static int redis_hset(redisContext *redis,const char *key, const char *field,long value){
	redisReply *reply = NULL;
	void *ret = NULL;

	ret = redisCommand(redis,"HSET %s %s %lld", key, field,value);
	if( ret == NULL){
		return TRACKD_ERR;
	}

	reply = (redisReply *)ret;
	if(reply->integer > 0){
		freeReplyObject(reply);
		return TRACKD_OK;
	}else{
		freeReplyObject(reply);
		//something must be wrong
		return TRACKD_ERR;
	}
}

static int redis_hincr(redisContext *redis,const char *key, const char *field,long value){
	//printf("redishkey:%s;redishfield:%s;data:%ld\n",key,field,value);
	redisReply *reply = NULL;
	void *ret = NULL;

	ret = redisCommand(redis,"HINCRBY %s %s %lld", key, field,value);
	if( ret == NULL){
		return TRACKD_ERR;
	}

	reply = (redisReply *)ret;
	//printf("reply:%lld\n",reply->integer);
	if(reply->integer > 0){
		freeReplyObject(reply);
		return TRACKD_OK;
	}else{
		freeReplyObject(reply);
		//something must be wrong
		return TRACKD_ERR;
	}
}


