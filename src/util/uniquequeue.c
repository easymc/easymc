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

#include "../config.h"
#include "../emc.h"
#include "event.h"
#include "uniquequeue.h"

#define MAX_ARRAY_SIZE	(0x10000)

#pragma pack(1)
struct uniquequeue{
	volatile int		_map[MAX_ARRAY_SIZE];
	int					_array[MAX_ARRAY_SIZE];
	volatile uint		used;
	struct event		*wait;
#if defined (EMC_WINDOWS)
	CRITICAL_SECTION	lock;                    
#else
	pthread_mutex_t		lock;
#endif
};
#pragma pack()


static __inline void _lock_queue(struct uniquequeue* queue){
#if defined (EMC_WINDOWS)
	EnterCriticalSection(&queue->lock);
#else
	pthread_mutex_lock(&queue->lock);
#endif
}

static __inline void _unlock_queue(struct uniquequeue* queue){
#if defined (EMC_WINDOWS)
	LeaveCriticalSection(&queue->lock);
#else
	pthread_mutex_unlock(&queue->lock);
#endif
}
struct uniquequeue* create_uqueue(){
	struct uniquequeue *uq=(struct uniquequeue *)malloc(sizeof(struct uniquequeue));
	if(!uq)return NULL;
	memset(uq,0,sizeof(struct uniquequeue));
	uq->wait=create_event();
#if defined (EMC_WINDOWS)
	InitializeCriticalSection(&uq->lock);
#else
	pthread_mutex_init(&uq->lock,NULL);		
#endif
	return uq;
}

void delete_uqueue(struct uniquequeue* uq){
#if defined (EMC_WINDOWS)
	DeleteCriticalSection(&uq->lock);
#else
	pthread_mutex_destroy(&uq->lock);		
#endif
	delete_event(uq->wait);
	free(uq);
}

uint push_uqueue(struct uniquequeue* uq,int v){
	_lock_queue(uq);
	if(uq->_map[v] > 0){
		_unlock_queue(uq);
		return 0;
	}
	if(uq->used>=MAX_ARRAY_SIZE){
		_unlock_queue(uq);
		return -1;
	}
	uq->_array[uq->used++]=v;
	uq->_map[v]=1;
	post_event(uq->wait);
	_unlock_queue(uq);
	return 0;
}

int pop_uqueue(struct uniquequeue* uq){
	int v=-1;
	_lock_queue(uq);
	if(!uq->used){
		_unlock_queue(uq);
		return -1;
	}
	v=uq->_array[0];
	if(uq->used > 1){
		memmove(uq->_array,uq->_array+1,sizeof(int)*(uq->used-1));
	}
	uq->_map[v]=0;
	uq->used--;
	_unlock_queue(uq);
	return v;
}

int wait_uqueue(struct uniquequeue* uq){
	return wait_event(uq->wait,1);
}

void post_uqueue(struct uniquequeue* uq){
	post_event(uq->wait);
}
