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

#include "nqueue.h"
#include "../config.h"
#include "lock.h"
#include <stdio.h>
#include <errno.h>
#include <assert.h>

/* Pointer queue, do not allocate memory, only save the pointer */

#pragma pack(1)
struct nqueue
{
	 int						ids[EMC_SOCKETS_DEFAULT];
	 unsigned int				used;
	 unsigned int				size;
	 volatile unsigned int		lock;
};
#pragma pack()

struct nqueue * create_nqueue()
{
	struct nqueue * queue = (struct nqueue *)malloc(sizeof(struct nqueue));
	if(!queue) return NULL;
	memset(queue, 0, sizeof(struct nqueue));
	queue->size = EMC_SOCKETS_DEFAULT;
	memset(queue->ids, -1, sizeof(int) * EMC_SOCKETS_DEFAULT);
	return queue;
}

void delete_nqueue(struct nqueue * queue){
	free(queue);
}

int nqueue_push(struct nqueue * queue, int id){
	emc_lock(&queue->lock);
	if(queue->used >= queue->size){
		emc_unlock(&queue->lock);
		return -1;
	}
	queue->ids[queue->used ++] = id;
	emc_unlock(&queue->lock);
	return 0;
}

int nqueue_pop(struct nqueue * queue, int * id){
	emc_lock(&queue->lock);
	if(!queue->used){
		emc_unlock(&queue->lock);
		return -1;
	}
	*id = queue->ids[0];
	queue->used --;
	if(queue->used){
		memmove(&queue->ids[0], &queue->ids[1], sizeof(int) * queue->used);
	}
	emc_unlock(&queue->lock);
	return 0;
}
