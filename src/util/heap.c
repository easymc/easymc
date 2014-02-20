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

#include "queue.h"
#include "../config.h"
#include "heap.h"

#define HEAP_DEFAULT_COUNT	(1024)

#pragma pack(1)
struct heap{
	char				*mem[HEAP_DEFAULT_COUNT];
	int					count;
	int					block;
	struct emc_queue	idle;
	int					used;
#if defined (EMC_WINDOWS)
	CRITICAL_SECTION	lock;
#else
	pthread_mutex_t		lock;
#endif
};
#pragma pack()

static __inline void _lock_heap(struct heap* hp){
#if defined (EMC_WINDOWS)
	EnterCriticalSection(&hp->lock);
#else
	pthread_mutex_lock(&hp->lock);
#endif
}

static __inline void _unlock_heap(struct heap* hp){
#if defined (EMC_WINDOWS)
	LeaveCriticalSection(&hp->lock);
#else
	pthread_mutex_unlock(&hp->lock);
#endif
}

struct heap *heap_new(int block,int count){
	int index=0;
	struct heap *hp=(struct heap *)malloc(sizeof(struct heap));
	if(!hp)return NULL;
	memset(hp,0,sizeof(struct heap));
	hp->block=block;
	hp->mem[0]=(char*)malloc((block+sizeof(struct emc_queue))*count);
	memset(hp->mem[0],0,(block+sizeof(struct emc_queue))*count);
	emc_queue_init(&hp->idle);
	for(index=0;index<count;index++){
		emc_queue_init((struct emc_queue *)(hp->mem[0]+index*(sizeof(struct emc_queue)+block)));
		emc_queue_insert_tail(&hp->idle,(struct emc_queue *)(hp->mem[0]+index*(sizeof(struct emc_queue)+block)));
	}
	hp->count=count;
#if defined (EMC_WINDOWS)
	InitializeCriticalSection(&hp->lock);
#else
	pthread_mutex_init(&hp->lock,NULL);
#endif
	return hp;
}

void heap_delete(struct heap * hp){
	int index=0;
#if defined (EMC_WINDOWS)
	DeleteCriticalSection(&hp->lock);
#else
	pthread_mutex_destroy(&hp->lock);
#endif
	for(index=0;index<HEAP_DEFAULT_COUNT;index++){
		if(hp->mem[index]){
			free(hp->mem[index]);
		}
	}
	free(hp);
}

void *heap_alloc(struct heap *hp){
	char *buffer=NULL;
	struct emc_queue *head=NULL;
	_lock_heap(hp);
	head=emc_queue_head(&hp->idle);
	if(!head || hp->used>=hp->count){
		int index=0;
		buffer=(char*)malloc(hp->count*(hp->block+sizeof(struct emc_queue)));
		if(!buffer){
			_unlock_heap(hp);
			return NULL;
		}
		for(index=0;index<HEAP_DEFAULT_COUNT;index++){
			if(!hp->mem[index]){
				hp->mem[index]=buffer;
				break;
			}
		}
		memset(buffer,0,hp->count*(hp->block+sizeof(struct emc_queue)));
		for(index=0;index<hp->count;index++){
			emc_queue_init((struct emc_queue *)(buffer+index*(hp->block+sizeof(struct emc_queue))));
			emc_queue_insert_tail(&hp->idle,(struct emc_queue *)(buffer+index*(hp->block+sizeof(struct emc_queue))));
		}
		hp->count *= 2;
		head=emc_queue_head(&hp->idle);
	}
	if(!head){
		_unlock_heap(hp);
		return NULL;
	}
	emc_queue_remove(head);
	buffer=(char *)head+sizeof(struct emc_queue);
	hp->used++;
	_unlock_heap(hp);
	return buffer;
}

int heap_free(struct heap *hp,void* buf){
	struct emc_queue *head=(struct emc_queue *)((char *)buf-sizeof(struct emc_queue));
	_lock_heap(hp);
	if(!hp->used){
		_unlock_heap(hp);
		return -1;
	}
	emc_queue_init(head);
	emc_queue_insert_tail(&hp->idle,head);
	hp->used--;
	_unlock_heap(hp);
	return 0;
}

int heap_size(struct heap *hp){
	int size=0;
	_lock_heap(hp);
	size=hp->used;
	_unlock_heap(hp);
	return size;
}

void heap_reset(struct heap *hp){
	int index=0,index2=0,count=0,num=0;
	_lock_heap(hp);
	for(index=0;index<HEAP_DEFAULT_COUNT;index++){
		if(hp->mem[index]){
			num++;
		}else break;
	}
	count=hp->count/num;
	for(index=0;index<num;index++){
		if(hp->mem[index]){
			for(index2=0;index2<count;index2++){
				emc_queue_init((struct emc_queue *)(hp->mem[index]+index2*(sizeof(struct emc_queue)+hp->block)));
				emc_queue_insert_tail(&hp->idle,(struct emc_queue *)(hp->mem[index]+index2*(sizeof(struct emc_queue)+hp->block)));
			}
		}
	}
	hp->used=0;
	_unlock_heap(hp);
}
