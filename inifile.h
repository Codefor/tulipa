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

#ifndef __INIFILE_H__
#define __INIFILE_H__

#include "trackd.h"
/*
 -------------------------------------------------------------------------------
 how to use
 -------------------------------------------------------------------------------
 struct inifile *ini;
 int i;
 
 ini = inifile_new();
 inifile_parse_file(ini, "tmp.ini");
 
 inifile_print(ini);
 
 int b;
 if (0 != inifile_fetch_bool(ini, "2", "f1", &b)) {
 // error
 }
 printf("b: %d\n", b);
 
 double d;
 if (0 != inifile_fetch_double(ini, "2", "d", &d)) {
 // error
 }
 printf("d: %f\n", d);
 
 const char *event_id_key;
 const char *event_id_val;
 struct iniitem *iniitem = NULL;
 while ((iniitem = inifile_foreach_group(ini, "evid_list", iniitem,
                                         &event_id_key, &event_id_val))) {
   printf("key: %*s, val: %*s\n", 8, event_id_key, 20, event_id_val);
 }
 
 inifile_free(ini);
*/


struct iniitem {
  const char *group;
  const char *name;
  const char *val;
};

struct inifile {
  struct iniitem *items;
  char           *content;   /* parsed ini file txt content */
};


struct inifile *inifile_new();
void            inifile_free(struct inifile *ini);

int inifile_parse_file(struct inifile *ini, const char *file);

int inifile_fetch_int   (struct inifile *ini,
                         const char *group, const char *name, int   *val);
int inifile_fetch_double(struct inifile *ini,
                         const char *group, const char *name, double *val);
int inifile_fetch_str   (struct inifile *ini,
                         const char *group, const char *name, const char **val);
/*
 * True:  "1", "yes", "true", "on"
 * False: "0", "no", "false", "off"
 * other str return: atoi(const char *) == 0 ? 0 : 1
 */
int inifile_fetch_bool  (struct inifile *ini,
                         const char *group, const char *name, int *val);

/**
 * foreach group
 * first time set now_itme=NULL
 * @return NULL is not found
 */
struct iniitem *inifile_foreach_group(struct inifile *ini, const char *group,
                                      struct iniitem *now_item,
                                      const char **name, const char **val);

/* print all items */
void inifile_print(struct inifile *ini);

void get_token(int op,int trk_id,const char **token,struct settings **settings);

#endif
