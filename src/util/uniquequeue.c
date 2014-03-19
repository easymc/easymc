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
#include "lock.h"
#include "uniquequeue.h"

#define UNIQUE_SIZE	(0x10000)

#pragma pack(1)
struct uniquenode{
	int					id;
	void			*	addition;
};
struct uniquequeue{
	volatile int		ids[UNIQUE_SIZE];
	struct uniquenode	queue[UNIQUE_SIZE];
	volatile uint		used;
	struct event		*wait;
	volatile uint		lock;                    
};
#pragma pack()

struct uniquequeue * create_uqueue(){
	struct uniquequeue * uq = (struct uniquequeue *)malloc(sizeof(struct uniquequeue));
	if(!uq)return NULL;
	memset(uq, 0, sizeof(struct uniquequeue));
	uq->wait = create_event();
	return uq;
}

void delete_uqueue(struct uniquequeue * uq){
	delete_event(uq->wait);
	free(uq);
}

uint push_uqueue(struct uniquequeue * uq, int v, void * p){
	emc_lock(&uq->lock);
	if(uq->ids[v] > 0){
		emc_unlock(&uq->lock);
		return 0;
	}
	if(uq->used >= UNIQUE_SIZE){
		emc_unlock(&uq->lock);
		return -1;
	}
	uq->queue[uq->used].id = v;
	uq->queue[uq->used ++].addition = p;
	uq->ids[v] = 1;
	post_event(uq->wait);
	emc_unlock(&uq->lock);
	return 0;
}

int pop_uqueue(struct uniquequeue * uq, void ** p){
	int v = -1;
	emc_lock(&uq->lock);
	if(!uq->used){
		emc_unlock(&uq->lock);
		return -1;
	}
	v = uq->queue[0].id;
	if(p){
		*p = uq->queue[0].addition;
	}
	if(uq->used > 1){
		memmove(uq->queue, uq->queue + 1, sizeof(struct uniquenode) * (uq->used - 1));
	}
	uq->ids[v] = 0;
	uq->used --;
	emc_unlock(&uq->lock);
	return v;
}

int wait_uqueue(struct uniquequeue * uq, int timeout){
	return wait_event(uq->wait, timeout);
}

void post_uqueue(struct uniquequeue * uq){
	post_event(uq->wait);
}
