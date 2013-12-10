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
#include "trackd.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


/*
   -------------------------------------------------------------------------------
   static function
   -------------------------------------------------------------------------------
   */
static int inifile_fetch(struct inifile *ini,
		const char *group, const char *name, const char **dest);


/*
   -------------------------------------------------------------------------------
   inifile_*
   -------------------------------------------------------------------------------
   */
struct inifile *inifile_new()
{
	struct inifile *ini;
	if ((ini = calloc(1, sizeof(struct inifile))) == NULL) {
		return NULL;
	}
	return ini;
}


void inifile_free(struct inifile *ini)
{
	assert(ini);
	if (ini->items) {
		free(ini->items);
	}
	if (ini->content) {
		free(ini->content);
	}
	free(ini);
}

//parse ini file
int inifile_parse_file(struct inifile *ini, const char *file)
{
	assert(ini);
	assert(file);

	long file_size;
	FILE *f = fopen(file, "rb");
	if (!f) return TRACKD_ERR;

	fseek(f, 0, SEEK_END);
	file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	ini->content = (char *)calloc(file_size + 1, sizeof(char));

	if (!ini->content) return TRACKD_ERR;

	char *content_ptr = ini->content;

	int nitems = 0;
	//k=v\n,4 letters
	ini->items = calloc((int)(file_size/4)+1, sizeof(struct iniitem));
	if (!ini->items) return TRACKD_ERR;

	const char *now_group = NULL;
	const char *now_key   = NULL;
	const char *now_val   = NULL;

	int line_num = 0;
	while (!feof(f)) {
		char line[8192] = {0};    
		if (NULL == fgets(line, 8192, f)) {
			break;
		}
		line_num++;

		/* find line begin & end, trim */
		const char *line_begin = line;
		while (*line_begin && isspace(*line_begin)) { line_begin++; }

		char *line_end = line;
		while (*line_end) {
			if (*line_end == '\n' || (*line_end == '\r' && *(line_end+1) == '\n')) {
				*line_end = '\0';
				line_end--;
				break;
			}
			line_end++;
		}
		while (line_end > line_begin && isspace(*line_end) ) { line_end--; }
		if (line_end <= line_begin) { continue; }

		/************************ is comment line *************************/
		if (*line_begin == ';' || *line_begin == '#') {
			continue; /* this line is comment, read next line */
		}

		/************************* is group line *************************/
		if (*line_begin == '[' && *line_end == ']') {
			now_group = content_ptr;
			strncpy(content_ptr, line_begin + 1, line_end - line_begin - 1);
			content_ptr += line_end - line_begin - 1;
			*content_ptr++ = '\0';
			continue;
		}

		/************************* key value line *************************/
		/* find equal sign, '=' */
		const char *equal_sign = line;
		int found_equal_sign = 0;
		while (*equal_sign) {
			if (*equal_sign == '=') {
				found_equal_sign = 1;
				break;
			}
			equal_sign++;
		}
		if (found_equal_sign == 0) {
			continue;   /* can't find, read next line */
		}

		/* find key */
		const char *k_begin, *k_end;
		k_begin = line_begin;
		k_end   = equal_sign - 1;
		while (k_end > k_begin && isspace(*k_end)) { k_end--; }
		if (k_end < k_begin) {
			continue;   /* no key, read next line */
		}
		now_key = content_ptr;
		strncpy(content_ptr, k_begin, k_end - k_begin + 1);
		content_ptr += k_end - k_begin + 1;
		*content_ptr++ = '\0';

		/* find value */
		const char *v_begin, *v_end;
		v_begin = equal_sign + 1;
		v_end   = line_end;
		while (*v_begin && isspace(*v_begin)) { v_begin++; }
		if (v_end < v_begin) {
			continue;   /* no value, read next line */
		}
		now_val = content_ptr;
		strncpy(content_ptr, v_begin, v_end - v_begin + 1);
		content_ptr += v_end - v_begin + 1;
		*content_ptr++ = '\0';

		/* add items */
		ini->items[nitems].group = now_group;
		ini->items[nitems].name  = now_key;
		ini->items[nitems].val   = now_val;

		nitems++;
	} // while

	// resize mem
	ini->items = realloc(ini->items, sizeof(struct iniitem) * (nitems+1));
	
	fclose(f);
	return 0;
}


static int inifile_fetch(struct inifile *ini,
		const char *group, const char *name, const char **dest)
{
	assert(ini);
	struct iniitem *item = ini->items;
	for (item = ini->items; item->group && item->name; ++item) {
		if (strcmp(item->name,  name)  == 0 &&
				strcmp(item->group, group) == 0) {
			*dest = item->val;
			return 0;
		}
	}
	return -1; /* not found */
}


int inifile_fetch_int(struct inifile *ini,
		const char *group, const char *name, int *val)
{
	const char *p;
	if (-1 == inifile_fetch(ini, group, name, &p)) { return -1; }
	*val = atoi(p);
	return 0;
}


int inifile_fetch_double(struct inifile *ini,
		const char *group, const char *name, double *val)
{
	const char *p;
	if (-1 == inifile_fetch(ini, group, name, &p)) { return -1; }
	*val = atof(p);
	return 0;
}


int inifile_fetch_str(struct inifile *ini,
		const char *group, const char *name, const char **val)
{
	return inifile_fetch(ini, group, name, val);
}


int inifile_fetch_bool(struct inifile *ini,
		const char *group, const char *name, int *val)
{
	const char *p;
	if (-1 == inifile_fetch(ini, group, name, &p)) { return -1; }

	/* True */
	if (strcasecmp(p, "false") == 0 ||
			strcasecmp(p, "no"   ) == 0 ||
			strcasecmp(p, "off"  ) == 0 ||
			strcasecmp(p, "0"    ) == 0) {
		*val = 0;
		return 0;
	}

	/* False */
	if (strcasecmp(p, "true") == 0 ||
			strcasecmp(p, "yes" ) == 0 ||
			strcasecmp(p, "on"  ) == 0 ||
			strcasecmp(p, "1"   ) == 0) {
		*val = 1;
		return 0;
	}

	/* other wise */
	*val = atoi(p) == 0 ? 0 : 1;
	return 0;
}

struct iniitem *inifile_foreach_group(struct inifile *ini, const char *group,
		struct iniitem *item,
		const char **name, const char **val)
{
	if (item == NULL) {
		item = ini->items;
	}
	for (; item->group && item->name; ++item) {
		if (strcmp(item->group, group) == 0) {
			*name = item->name;
			*val  = item->val;
			return ++item;
		}
	}
	return NULL;
}

void inifile_print(struct inifile *ini)
{
	assert(ini);
	struct iniitem *item = ini->items;
	for (item = ini->items; item->group && item->name; ++item) {
		printf("-- >%s<\n-- >%s<\n-- >%s<\n\n",
				item->group, item->name, item->val);
	}
}

void get_token(int op,int trk_id,const char **token,struct settings **settings)
{
	if (op < 0 || op > DDTRACK_OP_MAX) return;

	const struct token_item *p = (*settings)->op_funcs[op]->tokens;
	while(p){
		if (p->trk_id == trk_id){
			*token = p->token;
			break;
		}
		p = p->next;
	}
}


