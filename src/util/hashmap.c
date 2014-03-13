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

#include "hashmap.h"
#include "queue.h"
#include "lock.h"
#include "../config.h"

#pragma pack(1)
struct node {
	int						id;
	void *					addr;
	struct node *			next;
	struct emc_queue		queue;
};

struct hashmap {
	int						size;
	struct node **			hash;
	struct node *			data;
	struct emc_queue		queue;
	volatile unsigned int	lock; 
	volatile unsigned int	foreach;
};
#pragma pack()

struct hashmap* hashmap_new(int max){
	int sz=1,i=0;
	struct hashmap * m = NULL;
	while(sz <= max) {
		sz *= 2;
	}
	m = (struct hashmap *)malloc(sizeof(struct hashmap));
	if(!m) return NULL;
	memset(m, 0, sizeof(struct hashmap));
	m->size = sz;
	m->hash=(struct node **)malloc(sizeof(struct node*) * sz);
	if(!m->hash){
		free(m);
		return NULL;
	}
	memset(m->hash, 0, sizeof(struct node*) * sz);
	m->data = (struct node *)malloc(sizeof(struct node) * sz);
	if(!m->data){
		free(m->hash);
		free(m);
		return NULL;
	}
	memset(m->data, 0, sizeof(struct node) * sz);
	emc_queue_init(&m->queue);
	
	for(i=0; i<sz; i++){
		m->data[i].addr = NULL;
		m->data[i].id = 0;
		m->data[i].next = NULL;
		emc_queue_init(&m->data[i].queue);
		emc_queue_insert_tail(&m->queue, &m->data[i].queue);
	}
	return m;
}

void hashmap_delete(struct hashmap * m) {
	free(m->hash);
	free(m->data);
	free(m);
}

void * hashmap_search(struct hashmap * m, int id) {
	int hash = -1, locked = 0;
	void *addr = NULL;
	struct node * n = NULL;
	if(!m->foreach){
		emc_lock(&m->lock);
		locked = 1;
	}
	hash = id & (m->size-1);
	n = m->hash[hash];
	while(n){
		if (n->id == id){
			addr = n->addr;
			if(locked){
				emc_unlock(&m->lock);
			}
			return addr;
		}
		n = n->next;
	}
	if(locked){
		emc_unlock(&m->lock);
	}
	return NULL;
}

int hashmap_insert(struct hashmap * m, int id, void * addr) {
	int hash = -1;
	struct node * n = NULL;
	struct node * o = NULL;
	struct emc_queue * head = NULL;
	emc_lock(&m->lock);
	hash = id & (m->size-1);
	n = m->hash[hash];
	head = emc_queue_head(&m->queue);
	if(!head){
		emc_unlock(&m->lock);
		return -1;
	}
	emc_queue_remove(head);
	o = emc_queue_data(head, struct node, queue);
	o->id = id;
	o->addr = addr;
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

int hashmap_erase(struct hashmap * m, int id)
{
	int hash = -1, locked = 0;
	struct node * n = NULL, * prev = NULL;
	if(!m->foreach){
		emc_lock(&m->lock);
		locked = 1;
	}
	hash = id&(m->size-1);
	n = m->hash[hash];
	while(n){
		if (n->id == id){
			n->addr = NULL;
			n->id = -1;
			if(n == m->hash[hash]){
				m->hash[hash] = n->next;
			}else{
				prev->next = n->next;
			}
			n->next = NULL;
			emc_queue_init(&n->queue);
			emc_queue_insert_tail(&m->queue, &n->queue);
			if(locked){
				emc_unlock(&m->lock);
			}
			return 0;
		}
		prev = n;
		n = n->next;
	}
	if(locked){
		emc_unlock(&m->lock);
	}
	return -1;
}

void hashmap_foreach(struct hashmap * m, hashmap_foreach_cb * cb, void * addition){
	int i = 0;
	emc_lock(&m->lock);
	emc_lock(&m->foreach);
	for (i=0; i<m->size; i++){
		struct node * n = m->hash[i];
		while(n){
			if(cb){
				if(cb(m, n->id, n->addr, addition)){
					n = m->hash[i];
					continue;
				}
			}
			n = n->next;
		}
	}
	emc_unlock(&m->foreach);
	emc_unlock(&m->lock);
}
