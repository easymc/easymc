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
#include "unpack.h"
#include "queue.h"
#include "lock.h"
#include "memory/jemalloc.h"

#define UNPACK_BUFFER_SIZE		(16392)
#define UNPACK_DEFAULT_COUNT	(1024)

#pragma pack(1)
struct unpack_unit{
	char			*	buffer;
	int					len;
	struct emc_queue	queue;
};

struct unpack{
	struct unpack_unit *units;
	struct emc_queue	idle;
	// The number of units assigned to elements
	volatile uint		count;
	volatile uint		lock;
};
#pragma pack()

struct unpack *unpack_new(unsigned int count){
	struct unpack * un = (struct unpack *)malloc(sizeof(struct unpack));
	int index = 0;
	
	if(!un) return NULL;
	if(!count)count = UNPACK_DEFAULT_COUNT;
	memset(un, 0, sizeof(struct unpack));
	un->count = count;
	emc_queue_init(&un->idle);
	un->units = (struct unpack_unit *)malloc(sizeof(struct unpack_unit) * count);
	memset(un->units, 0, sizeof(struct unpack_unit) * count);
	for(index=0; index<count; index++){
		emc_queue_init(&un->units[index].queue);
		emc_queue_insert_tail(&un->idle, &un->units[index].queue);
	}
	return un;
}

void unpack_delete(struct unpack * un){
	free(un->units);
	free(un);
}

void* unpack_alloc(struct unpack * un){
	struct emc_queue * head = NULL;
	
	emc_lock(&un->lock);
	head = emc_queue_head(&un->idle);
	if(!head){
		emc_unlock(&un->lock);
		return NULL;
	}
	emc_queue_remove(head);
	emc_unlock(&un->lock);
	return emc_queue_data(head, struct unpack_unit, queue);
}

int unpack_add(void* block, char * data, int len){
	struct unpack_unit * unit = (struct unpack_unit *)block;
	if(!unit->buffer){
		unit->buffer = (char*)malloc_impl(UNPACK_BUFFER_SIZE);
		memset(unit->buffer, 0, UNPACK_BUFFER_SIZE);
	}
	memcpy(unit->buffer+unit->len, data, (unit->len+len)>UNPACK_BUFFER_SIZE?(UNPACK_BUFFER_SIZE-unit->len):len);
	unit->len += (unit->len+len)>UNPACK_BUFFER_SIZE?(UNPACK_BUFFER_SIZE-unit->len):len;
	return (unit->len+len)>UNPACK_BUFFER_SIZE?(UNPACK_BUFFER_SIZE-unit->len):len;
}

static ushort unpack_get_peer(struct unpack_unit * unit, char * buffer){
	ushort length=0, len=0;
	char *rpos = unit->buffer;
	while(unit->len && EMC_HEAD != *(ushort*)rpos){
		rpos ++;
		unit->len --;
	}
	if(!unit->len) return 0;
	len = length = *(ushort*)(rpos+sizeof(ushort));
	if(0x8000 < length){// After compression
		length ^= 0x8000;
	}
	if(length > MAX_DATA_SIZE){
		memmove(unit->buffer, rpos+sizeof(ushort), unit->len-sizeof(ushort));
		unit->len -= sizeof(ushort);
		return 0;
	}
	if(unit->len < (length+sizeof(uint))){
		if(rpos > unit->buffer){
			memmove(unit->buffer, rpos, unit->len);
		}
		return 0;
	}
	memcpy(buffer, rpos+sizeof(uint), length);
	rpos += (length+sizeof(uint));
	unit->len -= (length+sizeof(uint));
	if(unit->len > 0 && unit->len < UNPACK_BUFFER_SIZE && rpos > unit->buffer && rpos < unit->buffer+UNPACK_BUFFER_SIZE){
		memmove(unit->buffer, rpos, unit->len);
	}
	return len;
}

void unpack_get(void * block, unpack_get_data * cb, int id, void * args, char * buffer){
	struct unpack_unit * unit = (struct unpack_unit *)block;
	unsigned short length = 0;

	if(unit->buffer){
		while((length = unpack_get_peer(unit, buffer))){
			if(cb){
				cb(buffer, length, id, args);
			}
		}
		if(!unit->len){
			free_impl(unit->buffer);
			unit->buffer = NULL;
		}
	}
}

void unpack_free(struct unpack * un, void * block){
	struct unpack_unit * unit = (struct unpack_unit *)block;
	emc_lock(&un->lock);
	if(unit->buffer){
		free_impl(unit->buffer);
		unit->buffer = NULL;
	}
	emc_queue_init(&unit->queue);
	emc_queue_insert_tail(&un->idle, &unit->queue);
	emc_unlock(&un->lock);
}
