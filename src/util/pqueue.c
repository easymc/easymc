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

#include "pqueue.h"
#include "../config.h"
#include "lock.h"
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#define PQUEUE_SIZE	(1024)

/* Pointer queue, do not allocate memory, only save the pointer */

#pragma pack(1)
struct pqueue
{
	 
	 void						**units;
	 unsigned int				used;
	 unsigned int				size;
	 volatile unsigned int		lock;                    
};
#pragma pack()

struct pqueue * create_pqueue()
{
	struct pqueue * queue = (struct pqueue *)malloc(sizeof(struct pqueue));
	if(!queue) return NULL;
	memset(queue, 0, sizeof(struct pqueue));
	queue->units = (void**)malloc(sizeof(void *) * PQUEUE_SIZE);
	queue->size = PQUEUE_SIZE;
	memset(queue->units, 0, sizeof(void *) * PQUEUE_SIZE);
	return queue;
}

void delete_pqueue(struct pqueue * queue){
	if(queue){
		free(queue->units);
		free(queue);
	}
}

int pqueue_push(struct pqueue * queue, void * data){
	emc_lock(&queue->lock);
	if(queue->used >= queue->size){
		queue->units = (void**)realloc(queue->units,sizeof(void *) * (queue->size+PQUEUE_SIZE));
		queue->size += PQUEUE_SIZE;
	}
	queue->units[queue->used] = data;
	queue->used ++;
	emc_unlock(&queue->lock);
	return 0;
}

int pqueue_push_head(struct pqueue * queue, void * data){
	emc_lock(&queue->lock);
	if(queue->used >= queue->size){
		queue->units = (void**)realloc(queue->units,sizeof(void *) * (queue->size+PQUEUE_SIZE));
		queue->size += PQUEUE_SIZE;
	}
	if(queue->used){
		memmove(&queue->units[1], &queue->units[0], sizeof(void *) * queue->used);
		queue->units[0] = data;
		queue->used ++;
	} else {
		queue->units[queue->used++] = data;
	}
	emc_unlock(&queue->lock);
	return 0;
}

int pqueue_pop(struct pqueue * queue, void ** buf){
	emc_lock(&queue->lock);
	if(!queue->used){
		emc_unlock(&queue->lock);
		return -1;
	}
	*buf = queue->units[0];
	queue->used --;
	if(queue->used){
		memmove(&queue->units[0], &queue->units[1], sizeof(void *) * queue->used);
	}
	emc_unlock(&queue->lock);
	return 0;
}

unsigned int pqueue_size(struct pqueue * queue){
	unsigned int size = 0;
	emc_lock(&queue->lock);
	size = queue->used;
	emc_unlock(&queue->lock);
	return size;
}
