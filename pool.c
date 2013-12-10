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

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


static inline void *pool_next_ele(struct pool *p, void *ptr);


struct pool *pool_new(size_t capacity, size_t element_size)
{
  struct pool *p;
  if ((p = calloc(1, sizeof(struct pool))) == NULL) {
    return NULL;
  }
  //请求capacity个element_size大小的空间
  //calloc 分配内存后自动0化
  if ((p->data = calloc(capacity, element_size)) == NULL) {
    free(p);
    return NULL;
  }
  p->capacity     = capacity;
  p->element_size = element_size;
  p->tail = p->data;
  //注意，p->data + p->element_size 不是指针+size
  //p->head指向下一个元素
  p->head = p->data + p->element_size;
  
  pthread_mutex_init(&p->insert_lock, NULL);
  
  return p;
}


void pool_free(struct pool *p)
{
  assert(p);
  free(p->data);
  free(p);
}


/**
 * insert element into pool
 * If successful, return zero
 */
int pool_push(struct pool *p, void *ele)
{
  assert(p);
  assert(ele);
  
  /* If successful, pthread_mutex_trylock() will return zero. */
  if (pthread_mutex_trylock(&p->insert_lock) != 0) {
    return -1;
  }
  
  void *next_head = pool_next_ele(p, p->head);
  if (next_head == p->tail) {
    /* 系统过载pool已满, 丢弃数据 */
    pthread_mutex_unlock(&p->insert_lock);
    return -2;
  }
  //void *memcpy(void *dest, const void *src, size_t n);
  memcpy(p->head, ele, p->element_size);
  p->head = next_head;
  
  pthread_mutex_unlock(&p->insert_lock);
  return 0;
}


/**
 * pop an element
 * 由于出队列是单线程的，这里无需加锁
 */
int pool_pop(struct pool *p, void *ele)
{
  void *next_tail = pool_next_ele(p, p->tail);
  if (next_tail == p->head) {
    return -1;
  }
  p->tail = next_tail;
  memcpy(ele, p->tail, p->element_size);
  return 0;
}


/**
 * pool size, 非线程安全
 */
size_t pool_size(struct pool *p)
{
  void *tmp_tail = p->tail;
  void *tmp_head = p->head;
  
  long diff = (tmp_head - tmp_tail) / (int)p->element_size;
  if (diff > 0) {
    return (size_t)(diff - 1);
  } else if (diff < 0) {
    assert(p->capacity + diff - 1 >= 0);
    return (size_t)(p->capacity + diff - 1);
  }
  return 0; // shouldn't be here
}

//p,p->head
//p,p->tail
//如果ptr指向最后一个元素，next_ele回卷到p->data
static inline void *pool_next_ele(struct pool *p, void *ptr)
{
  if (ptr == p->data + p->element_size*(p->capacity - 1)) {
    return p->data;
  }
  return ptr + p->element_size;
}

