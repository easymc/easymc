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
#include "queue.h"
#include "lock.h"
#include "sendqueue.h"

#define SQ_DEFAULT_SIZE	(1024)

#pragma pack(1)
struct sendqueue_unit{
	void				*data;
	struct emc_queue	queue;
};

struct sendqueue_id_unit{
	volatile uint		count;
	struct emc_queue	queue;
};

struct sendqueue{
	struct sendqueue_id_unit	_map[EMC_SOCKETS_DEFAULT];
	struct sendqueue_unit		*units[SQ_DEFAULT_SIZE];
	struct emc_queue			idle;
	uint						total;
	volatile uint				lock;                    
};

#pragma pack()

struct sendqueue * create_sendqueue(){
	uint index=0;
	struct sendqueue * sq = (struct sendqueue *)malloc(sizeof(struct sendqueue));
	if(!sq) return NULL;
	memset(sq, 0, sizeof(struct sendqueue));
	emc_queue_init(&sq->idle);
	for(index=0; index<EMC_SOCKETS_DEFAULT; index++){
		emc_queue_init(&sq->_map[index].queue);
	}
	sq->units[sq->total++] = (struct sendqueue_unit*)malloc(sizeof(struct sendqueue_unit) * EMC_SOCKETS_DEFAULT);
	memset(sq->units[sq->total-1], 0, sizeof(struct sendqueue_unit) * EMC_SOCKETS_DEFAULT);
	for(index=0; index<EMC_SOCKETS_DEFAULT; index++){
		emc_queue_init(&(sq->units[sq->total-1]+index)->queue);
		emc_queue_insert_tail(&sq->idle, &(sq->units[sq->total-1]+index)->queue);
	}
	return sq;
}

void delete_sendqueue(struct sendqueue * sq){
	uint index = 0;
	for(index=0; index<sq->total; index++){
		free(sq->units[index]);
	}
	free(sq);
}

int sendqueue_push(struct sendqueue * sq, int id, void * data){
	uint index = 0;
	struct sendqueue_unit * unit = NULL;
	struct emc_queue * head = NULL;
	if(id<0 || id >= EMC_SOCKETS_DEFAULT) return -1;
	emc_lock(&sq->lock);
	if(sq->_map[id].count >= SQ_DEFAULT_SIZE){
		emc_unlock(&sq->lock);
		return -1;
	}
	if(emc_queue_empty(&sq->idle)){
		sq->units[sq->total++] = (struct sendqueue_unit*)malloc(sizeof(struct sendqueue_unit) * EMC_SOCKETS_DEFAULT);
		memset(sq->units[sq->total-1], 0, sizeof(struct sendqueue_unit) * EMC_SOCKETS_DEFAULT);
		for(index=0; index<EMC_SOCKETS_DEFAULT; index++){
			emc_queue_init(&(sq->units[sq->total-1]+index)->queue);
			emc_queue_insert_tail(&sq->idle, &(sq->units[sq->total-1]+index)->queue);
		}
	}
	head = emc_queue_head(&sq->idle);
	if(!head){
		emc_unlock(&sq->lock);
		return -1;
	}
	emc_queue_remove(head);
	emc_queue_init(head);
	unit = emc_queue_data(head, struct sendqueue_unit,queue);
	unit->data = data;
	emc_queue_insert_tail(&sq->_map[id].queue, head);
	sq->_map[id].count ++;
	emc_unlock(&sq->lock);
	return 0;
}

int sendqueue_push_head(struct sendqueue * sq, int id, void * data){
	uint index = 0;
	struct sendqueue_unit * unit = NULL;
	struct emc_queue * head = NULL;
	if(id < 0 || id >= EMC_SOCKETS_DEFAULT) return -1;
	emc_lock(&sq->lock);
	if(sq->_map[id].count >= SQ_DEFAULT_SIZE){
		emc_unlock(&sq->lock);
		return -1;
	}
	if(emc_queue_empty(&sq->idle)){
		sq->units[sq->total++] = (struct sendqueue_unit*)malloc(sizeof(struct sendqueue_unit) * EMC_SOCKETS_DEFAULT);
		memset(sq->units[sq->total-1], 0, sizeof(struct sendqueue_unit) * EMC_SOCKETS_DEFAULT);
		for(index=0; index<EMC_SOCKETS_DEFAULT; index++){
			emc_queue_init(&(sq->units[sq->total-1]+index)->queue);
			emc_queue_insert_tail(&sq->idle, &(sq->units[sq->total-1]+index)->queue);
		}
	}
	head = emc_queue_head(&sq->idle);
	if(!head){
		emc_unlock(&sq->lock);
		return -1;
	}
	emc_queue_remove(head);
	emc_queue_init(head);
	unit = emc_queue_data(head, struct sendqueue_unit, queue);
	unit->data = data;
	emc_queue_insert_head(&sq->_map[id].queue, head);
	sq->_map[id].count ++;
	emc_unlock(&sq->lock);
	return 0;
}

int sendqueue_pop(struct sendqueue * sq, int id, void ** data){
	struct emc_queue * head = NULL;
	struct sendqueue_unit * unit = NULL;
	if(id < 0 || id >= EMC_SOCKETS_DEFAULT) return -1;
	emc_lock(&sq->lock);
	head = emc_queue_head(&sq->_map[id].queue);
	if(!head){
		emc_unlock(&sq->lock);
		return -1;
	}
	emc_queue_remove(head);
	sq->_map[id].count --;
	unit = emc_queue_data(head, struct sendqueue_unit, queue);
	if(data){
		*data = unit->data;
	}
	emc_queue_init(head);
	emc_queue_insert_tail(&sq->idle, head);
	emc_unlock(&sq->lock);
	return 0;
}

uint sendqueue_size(struct sendqueue * sq, int id){
	uint size = 0;
	if(id < 0 || id >= EMC_SOCKETS_DEFAULT) return 0;
	emc_lock(&sq->lock);
	size = sq->_map[id].count;
	emc_unlock(&sq->lock);
	return size;
}
