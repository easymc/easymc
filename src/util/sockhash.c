/* Copyright (c) 2014, mashka <easymc2014@gmail.com>
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
 *   * Neither the name of easymc nor the names of its contributors may be used
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

#include "sockhash.h"
#include "../config.h"
#include "queue.h"

struct node {
	int fd;
	int id;
	struct node*		next;
	struct emc_queue	queue;
};

struct sockhash {
	int size;
	struct node **	hash;
	struct node *	data;
	struct emc_queue	queue;
#if defined (EMC_WINDOWS)
	CRITICAL_SECTION	lock;                    
#else
	pthread_mutex_t		lock;
#endif
};

static __inline void lock_hash(struct sockhash * m){
#if defined (EMC_WINDOWS)
	EnterCriticalSection(&m->lock);
#else
	pthread_mutex_lock(&m->lock);
#endif
}

static __inline void unlock_hash(struct sockhash * m){
	if(m){
#if defined (EMC_WINDOWS)
		LeaveCriticalSection(&m->lock);
#else
		pthread_mutex_unlock(&m->lock);
#endif
	}
}

struct sockhash * sockhash_new(int max){
	int sz = 1;
	int i = 0;
	struct sockhash * m = NULL;
	while (sz <= max) {
		sz *= 2;
	}
	m = (struct sockhash*)malloc(sizeof(*m));
	m->size = sz;
	m->hash = (struct node**)malloc(sizeof(struct node*) * sz);
	m->data = (struct node*)malloc(sizeof(struct node) * sz);
	emc_queue_init(&m->queue);
	
	for (i=0; i<sz; i++) {
		m->hash[i] = NULL;
		m->data[i].fd = -1;
		m->data[i].id = 0;
		m->data[i].next = NULL;
		emc_queue_init(&m->data[i].queue);
		emc_queue_insert_tail(&m->queue,&m->data[i].queue);
	}
#if defined (EMC_WINDOWS)
	InitializeCriticalSection(&m->lock);
#else
	pthread_mutex_init(&m->lock,NULL);		
#endif
	return m;
}

void sockhash_delete(struct sockhash * m) {
#if defined (EMC_WINDOWS)
	DeleteCriticalSection(&m->lock);
#else
	pthread_mutex_destroy(&m->lock);		
#endif
	free(m->hash);
	free(m->data);
	free(m);
}

int sockhash_search(struct sockhash * m, int fd) {
	int hash = -1;
	struct node * n = NULL;
	lock_hash(m);
	hash = fd & (m->size-1);
	n = m->hash[hash];
	while(n){
		if (n->fd == fd){
			unlock_hash(m);
			return n->id;
		}
		n = n->next;
	}
	unlock_hash(m);
	return -1;
}

int sockhash_insert(struct sockhash * m, int fd, int id) {
	int hash = -1;
	struct node * n = NULL;
	struct node * o = NULL;
	struct emc_queue* head=NULL;
	lock_hash(m);
	hash = fd & (m->size-1);
	o = m->hash[hash];
	head = emc_queue_head(&m->queue);
	if(!head){
		unlock_hash(m);
		return -1;
	}
	emc_queue_remove(head);
	o = emc_queue_data(head,struct node,queue);
	o->fd = fd;
	o->id = id;
	if(n){
		while(n){
			if(!n->next){
				n->next = o;
				unlock_hash(m);
				return 0;
			}
			else{
				n = n->next;
			}
		}
	}
	else{
		m->hash[hash] = o;
		unlock_hash(m);
		return 0;
	}
	unlock_hash(m);
	return -1;
}

void sockhash_erase(struct sockhash * m, int fd)
{
	int hash = -1;
	struct node * next = NULL;
	struct node * n = NULL;
	lock_hash(m);
	hash = fd & (m->size-1);
	n = m->hash[hash];
	while(n)
	{
		if (n->fd == fd)
		{
			if (n->next == NULL)
			{
				n->fd = -1;
				n->id = -1;
				emc_queue_init(&n->queue);
				emc_queue_insert_tail(&m->queue, &n->queue);
				m->hash[hash] = NULL;
				unlock_hash(m);
				return;
			}
			next = n->next;
			n->fd = next->fd;
			n->id = next->id;
			n->next = next->next;
			next->fd = -1;
			next->next = NULL;
			emc_queue_init(&next->queue);
			emc_queue_insert_tail(&m->queue, &next->queue);
			unlock_hash(m);
			return;
		}
		n = n->next;
	}
	unlock_hash(m);
}
