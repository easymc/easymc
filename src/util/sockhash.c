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
#include "lock.h"
#include "queue.h"

#pragma pack(1)
struct node {
	int						fd;
	int						id;
	struct node *			next;
	struct emc_queue		queue;
};

struct sockhash {
	int size;
	struct node **			hash;
	struct node *			data;
	struct emc_queue		queue;
	volatile unsigned int	lock;                    
};
#pragma pack()

struct sockhash * sockhash_new(int max){
	int sz = 1;
	int i = 0;
	struct sockhash * m = NULL;
	while (sz <= max) {
		sz *= 2;
	}
	m = (struct sockhash *)malloc(sizeof(*m));
	if(!m) return NULL;
	memset(m, 0, sizeof(struct sockhash));
	m->size = max;
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
	return m;
}

void sockhash_delete(struct sockhash * m) {
	free(m->hash);
	free(m->data);
	free(m);
}

int sockhash_search(struct sockhash * m, int fd) {
	int hash = -1;
	struct node * n = NULL;
	emc_lock(&m->lock);
	hash = fd & (m->size-1);
	n = m->hash[hash];
	while(n){
		if (n->fd == fd){
			emc_unlock(&m->lock);
			return n->id;
		}
		n = n->next;
	}
	emc_unlock(&m->lock);
	return -1;
}

int sockhash_insert(struct sockhash * m, int fd, int id) {
	int hash = -1;
	struct node * n = NULL;
	struct node * o = NULL;
	struct emc_queue* head=NULL;
	emc_lock(&m->lock);
	hash = fd & (m->size-1);
	n = m->hash[hash];
	head = emc_queue_head(&m->queue);
	if(!head){
		emc_unlock(&m->lock);
		return -1;
	}
	emc_queue_remove(head);
	o = emc_queue_data(head,struct node,queue);
	o->fd = fd;
	o->id = id;
	o->next = NULL;
	if(n){
		while(n){
			if(!n->next){
				n->next = o;
				emc_unlock(&m->lock);
				return 0;
			}
			else{
				n = n->next;
			}
		}
	}
	else{
		m->hash[hash] = o;
		emc_unlock(&m->lock);
		return 0;
	}
	emc_unlock(&m->lock);
	return -1;
}

void sockhash_erase(struct sockhash * m, int fd)
{
	int hash = -1;
	struct node * n = NULL, * prev = NULL;
	emc_lock(&m->lock);
	hash = fd & (m->size-1);
	n = m->hash[hash];
	while(n) {
		if (n->fd == fd) {
			n->fd = -1;
			n->id = -1;
			if(n == m->hash[hash]){
				m->hash[hash] = n->next;
			}else{
				prev->next = n->next;
			}
			n->next = NULL;
			emc_queue_init(&n->queue);
			emc_queue_insert_tail(&m->queue, &n->queue);
			break;
		}
		prev = n;
		n = n->next;
	}
	emc_unlock(&m->lock);
}
