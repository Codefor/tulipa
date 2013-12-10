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


#include "md5.h"
#include "sha1.h"
#include "trackd.h"
#include "mysqljob.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

void testMySQL(){
	MYSQL my_connection;
	MYSQL_RES *result;
	MYSQL_ROW sql_row;
	MYSQL_FIELD *fd;
	char column[MAX_COLUMN_LEN][MAX_COLUMN_LEN];
	int res;
	mysql_init(&my_connection);
	if(mysql_real_connect(&my_connection,"cq01-dr-peercenterbak01.cq01","root","test123","zhixin_dashboard",5145,NULL,0))
	{
		perror("connect");
		res=mysql_query(&my_connection,"select * from T_Zhixin_Stat limit 1");//查询
		if(!res)
		{
			result=mysql_store_result(&my_connection);//保存查询到的数据到result
			if(result)
			{
				int i,j;
				printf("the result number is %lu\n ",(unsigned long)mysql_num_rows(result));
				for(i=0;fd=mysql_fetch_field(result);i++)//获取列名
				{
					bzero(column[i],sizeof(column[i]));
					strcpy(column[i],fd->name);
				}
				j=mysql_num_fields(result);
				for(i=0;i<j;i++)
				{
					printf("%s\t",column[i]);
				}
				printf("\n");
				while(sql_row=mysql_fetch_row(result))//获取具体的数据
				{
					for(i=0;i<j;i++)
					{
						printf("%s\t",sql_row[i]);
					}
					printf("\n");
				}

			}
		}
		else
		{
			perror("select");
		}
	}
	else
	{
		perror("connect:error");
	}
	mysql_free_result(result);//释放结果资源
	mysql_close(&my_connection);//断开连接
	mysql_library_end();
}

int main(){
	//test md5
	char md5str[33];
	char *p = "test";
	md5(p,strlen(p),md5str);

	assert(memcmp(md5str,"098f6bcd4621d373cade4e832627b4f6",32) == 0);

	//test sha1
	char sha1str[41];
	char *q = "apple";
	sha1(q,strlen(q),sha1str);
	assert(memcmp(sha1str,"d0be2dc421be4fcd0172e5afceea3970e2f3d940",40) == 0);

	testMySQL();
	return 0;
}
