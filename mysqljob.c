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

#include "mysqljob.h"
#include "log.h"
#include "thread.h"

#include<time.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

static int dashboard_job(MYSQL *conn,struct trk_item* trk_item);

int mysql_proc(void* n,void* item){
	if (n == NULL || item == NULL) {
		return TRACKD_ERR;
	}

	struct trk_client_node *node = (struct trk_client_node *)n;
	struct trk_item *trk_item = (struct trk_item *)item;

	switch(trk_item->op){
		case 2:
			//dashboard
			return dashboard_job((MYSQL *)node->conn,trk_item);
		default:
			return TRACKD_OK;
			//do nothing
	}

	return TRACKD_OK;
}

int mysql_finalizer(void* n){
	if( n == NULL ){
		return TRACKD_OK;
	}

	mysql_close((MYSQL *)n);

	return TRACKD_OK;
}

static int dashboard_job(MYSQL *conn,struct trk_item* trk_item){
	//check where the conn is alive
	if(mysql_ping(conn) != 0){
		return TRACKD_ERR;
	}

	const char *delimiter = "&";

	int data = 0,date = 0;
	time_t t;
	char timestr[32];

	char *brkt;
	char *str_cpy = strdup(trk_item->query_str);
	char *pch = strtok_r(str_cpy, delimiter, &brkt);
	while (pch != NULL) {
		if (memcmp(pch, "data=", 5) == 0) {
			data = atoi(pch+5);
		} else if (memcmp(pch, "date=", 5) == 0){
			date = atoi(pch+5);
		} else if (memcmp(pch, "t=", 2) == 0) {
			t = atoi(pch+2);
			int n = strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&t));
			timestr[n] = '\0';
		}
		pch = strtok_r(NULL, delimiter, &brkt);
	}
	free(str_cpy);

	if (date < MIN_DATE){
		return TRACKD_ERR;
	}

	char sql[MAX_SQL_LEN];
	snprintf(sql,sizeof(sql),"REPLACE INTO T_Zhixin_Stat values(NULL,%d,'%d',%d,'%s')",trk_item->trk_id,date,data,timestr);
	if(mysql_query(conn,sql) != 0){
		trackdLog(TRACKD_NOTICE,"replace into mysql error:(%d)%s",mysql_errno(conn),mysql_error(conn));
		return TRACKD_ERR;
	}

	return TRACKD_OK;
}
