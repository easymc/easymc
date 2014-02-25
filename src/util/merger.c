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
#include "merger.h"
#include "queue.h"
#include "memory/jemalloc.h"

#define MERGER_DEFAULT_COUNT	(1024)

#pragma pack(1)
struct merger_unit{
	// Data pointer
	char				*buffer;
	// Total length
	int					total;
	// Length has been received
	int					len;
	// The last time the data is received
	volatile uint		time;

	struct emc_queue	queue;
};

struct merger{
	struct merger_unit	*	units;
	struct emc_queue		idle;
	// Represents the number of units assigned to the elements
	volatile unsigned int	count;
#if defined (EMC_WINDOWS)
	CRITICAL_SECTION	lock;
#else
	pthread_mutex_t		lock;
#endif
};
#pragma pack()

static void _merger_lock(struct merger * un){
#if defined (EMC_WINDOWS)
	EnterCriticalSection(&un->lock);
#else
	pthread_mutex_lock(&un->lock);
#endif
}

static void _merger_unlock(struct merger * un){
#if defined (EMC_WINDOWS)
	LeaveCriticalSection(&un->lock);
#else
	pthread_mutex_unlock(&un->lock);
#endif
}

struct merger *merger_new(unsigned int count){
	struct merger * un = (struct merger *)malloc(sizeof(struct merger));
	int index = 0;
	
	if(!un) return NULL;
	if(!count)count = MERGER_DEFAULT_COUNT;
	memset(un, 0, sizeof(struct merger));
	un->count = count;
	emc_queue_init(&un->idle);
	un->units = (struct merger_unit *)malloc(sizeof(struct merger_unit) * count);
	memset(un->units, 0, sizeof(struct merger_unit) * count);
	for(index=0; index<count; index++){
		emc_queue_init(&un->units[index].queue);
		emc_queue_insert_tail(&un->idle, &un->units[index].queue);
	}
#if defined (EMC_WINDOWS)
	InitializeCriticalSection(&un->lock);
#else
	pthread_mutex_init(&un->lock, NULL);
#endif
	return un;
}

void merger_delete(struct merger * un){
#if defined (EMC_WINDOWS)
	DeleteCriticalSection(&un->lock);
#else
	pthread_mutex_destroy(&un->lock);
#endif
	free(un->units);
	free(un);
}

void* merger_alloc(struct merger * un){
	struct emc_queue * head = NULL;
	
	_merger_lock(un);
	head = emc_queue_head(&un->idle);
	if(!head){
		_merger_unlock(un);
		return NULL;
	}
	emc_queue_remove(head);
	_merger_unlock(un);
	return emc_queue_data(head, struct merger_unit, queue);
}

void merger_init(void * block, int len){
	struct merger_unit * unit = (struct merger_unit *)block;
	if(unit->buffer){
		if(unit->total < len){
			free_impl(unit->buffer);
			unit->buffer = NULL;
			unit->total = 0;
			unit->len = 0;
		}
	}
	if(!unit->buffer){
		unit->buffer = (char *)malloc_impl(len);
		memset(unit->buffer, 0, len);
		unit->total = len;
	}
	unit->time = timeGetTime();
}

int merger_add(void * block, int start, char * data, int len){
	struct merger_unit * unit = (struct merger_unit *)block;
	
	memcpy(unit->buffer+start, data, len);
	unit->len += len;
	unit->time = timeGetTime();
	return len;
}

int merger_get(void * block, merger_get_cb * cb, int id, void * addition){
	struct merger_unit * unit = (struct merger_unit *)block;

	if(unit->buffer && unit->total==unit->len){
		if(cb){
			cb(unit->buffer, unit->total, id, addition);
		}
		free_impl(unit->buffer);
		unit->buffer = NULL;
		unit->total = 0;
		unit->len = 0;
		return 0;
	}
	return -1;
}

uint merger_time(void * block){
	return ((struct merger_unit *)block)->time;
}

void merger_free(struct merger * un, void * block){
	struct merger_unit * unit = (struct merger_unit *)block;
	_merger_lock(un);
	if(unit->buffer){
		free_impl(unit->buffer);
		unit->buffer = NULL;
		unit->total = 0;
		unit->len = 0;
	}
	emc_queue_init(&unit->queue);
	emc_queue_insert_tail(&un->idle, &unit->queue);
	_merger_unlock(un);
}
