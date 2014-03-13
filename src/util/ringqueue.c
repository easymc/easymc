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
#include "ringqueue.h"

#define _RQ_SIZE	(0x10000)
#define _RQ_MASK	(0xFFFF)
#define _RQ_SHIFT	(16)	//2^16=65536
#define _RQ_PADDING_SIZE	(15)

#define _RQ_CONSUMER (1024)

#pragma pack(1)
// Producer
struct producer{
	// State flag
	volatile char	mark[_RQ_SIZE];	
	// Pointing the cursor producer next available element
	volatile int	cursor;
};
// Multiple threads share a consumer, like producer
struct consumer{
	volatile char	mark[_RQ_SIZE];
	// Cursor consumer elements already submitted
	volatile int	real;
	// Pointing the cursor next available element in consumer
	volatile int	cursor;
};
struct ringqueue{
	uchar				mode;
	void				*node[_RQ_SIZE];
	// Point to the next available element ringqueue cursor
	volatile int		cursor;
	struct producer		pd;
	// consumers
	volatile int		cs[_RQ_CONSUMER];
	volatile int		count;
	// Multi-threaded shared consumers
	struct consumer		mcs;
	struct event		*wait;
};
#pragma pack()

static void put_ordered_char(volatile char * buffer, uint offset, char flag){
	volatile char * addr = buffer+offset;
	*addr = flag;
}

static int get_int_volatitle(volatile int * buffer, uint offset){
	volatile int * addr = buffer+offset;
	int result = *addr;
	emc_mb();
	return result;
}

static uint ringqueue_number_cas(volatile int * key, uint _old, uint _new){
#ifdef EMC_WINDOWS
	return InterlockedCompareExchange((long*)key, _new, _old);
#else
	return __sync_val_compare_and_swap(key, _old, _new);
#endif
}

static uint ringqueue_write_next(struct ringqueue * rb){
	uint current=0, next=0;
	do{
		current=get_int_volatitle(&rb->pd.cursor, 0);
		next = current+1;
	}while(current != ringqueue_number_cas(&rb->pd.cursor, current, next));
	return next;
}

static int ringqueue_read_next(struct ringqueue * rb, int * cursor){
	int current=0, next=0;
	do{
		current = get_int_volatitle(&rb->mcs.cursor, 0);
		next = current+1;
		if(next > get_int_volatitle(&rb->cursor, 0)){
			return -1;
		}
	}while(current != ringqueue_number_cas(&rb->mcs.cursor, current, next));
	*cursor = next;
	return 0;
}

static uint ringqueue_check_consumer(struct ringqueue * rb){
	int index = 0;
	if(_RQ_S == rb->mode){
		for(index=0; index<rb->count; index++){
			if(get_int_volatitle(&rb->pd.cursor,0) >= get_int_volatitle(rb->cs,index)){
				if(get_int_volatitle(&rb->pd.cursor,0)-get_int_volatitle(rb->cs,index)+1 >= _RQ_SIZE){
					return 1;
				}
			}
		}
	}
	else if(_RQ_M == rb->mode){
		if(get_int_volatitle(&rb->pd.cursor,0) >= get_int_volatitle(&rb->mcs.real,0)){
			if(get_int_volatitle(&rb->pd.cursor,0)-get_int_volatitle(&rb->mcs.real,0)+1 >= _RQ_SIZE){
				return 1;
			}
		}
	}
	return 0;
}

struct ringqueue * create_ringqueue(uchar mode){
	uint index = 0;
	struct ringqueue * rb = (struct ringqueue *)malloc(sizeof(struct ringqueue));
	if(!rb) return NULL;
	memset(rb, 0, sizeof(struct ringqueue));
	rb->mode = mode;
	rb->cursor = -1;
	rb->count = -1;
	rb->pd.cursor = -1;
	rb->mcs.cursor = -1;
	rb->mcs.real = -1;
	for(index=0; index<_RQ_SIZE; index++){
		rb->pd.mark[index]  = -1;
		rb->mcs.mark[index] = -1;
	}
	for(index=0; index<_RQ_CONSUMER; index++){
		rb->cs[index] = -1;
	}
	rb->wait = create_event();
	return rb;
}

void delete_ringqueue(struct ringqueue * rb){
	delete_event(rb->wait);
	free(rb);
}

int get_single_consumer(struct ringqueue * rb){
	int cursor=-1, next=-1;
	do{
		cursor = rb->count;
		next = cursor+1;
	}while(cursor != ringqueue_number_cas(&rb->count, cursor, next));
	return next;
}

int wait_ringqueue(struct ringqueue * rb){
	return wait_event(rb->wait, -1);
}

int post_ringqueue(struct ringqueue * rb){
	return post_event(rb->wait);
}

int push_ringqueue(struct ringqueue * rb, void * p){
	uint current=0, cursor=0, index=0;

	if(ringqueue_check_consumer(rb) > 0) return -1;
	cursor = ringqueue_write_next(rb);
	rb->node[cursor&_RQ_MASK] = p;
	//commit
	do{
		current=get_int_volatitle(&rb->cursor, 0);
		if(current+1 == cursor){
//			put_ordered_char(rb->pd.mark,cursor&_RQ_MASK,cursor>>_RQ_SHIFT);
			if(current == ringqueue_number_cas(&rb->cursor, current, cursor)){
				break;
			}
		}
	}while(1);
	post_event(rb->wait);
	return 0;
}

int pop_ringqueue_single(struct ringqueue * rb, int index, void ** p){
	uint current=-1, cursor=-1;
	if(rb->cursor == rb->cs[index]) return -1;
	do{
		current = get_int_volatitle(rb->cs, index);
		cursor = current+1;
	}while(current != ringqueue_number_cas(&rb->cs[index], current, cursor));
	if(p){
		*p = rb->node[cursor&_RQ_MASK];
	}
	return 0;
}

int pop_ringqueue_multiple(struct ringqueue * rb, void ** p){
	uint current = 0;
	int cursor = 0;

	if(get_int_volatitle(&rb->cursor,0)-get_int_volatitle(&rb->mcs.cursor, 0) <= 0){
		return -1;
	}
	if(ringqueue_read_next(rb, &cursor) < 0){
		return -1;
	}
	if(p){
		*p = rb->node[cursor&_RQ_MASK];
	}
	//commit
	do{
		current = get_int_volatitle(&rb->mcs.real, 0);
		if(current+1 == cursor){
//			put_ordered_char(rb->mcs.mark,cursor&_RQ_MASK,cursor>>_RQ_SHIFT);
			if(current == ringqueue_number_cas(&rb->mcs.real, current, cursor)){
				break;
			}
		}
	}while(1);
	return 0;
}

uint check_ringqueue_single(struct ringqueue * rq, int index){
	if(_RQ_S == rq->mode){
		if(rq->cursor == rq->cs[index]){
			return 0;
		}
	}
	return 1;
}

uint check_ringqueue_multiple(struct ringqueue * rq){
	if(_RQ_M == rq->mode){
		if(get_int_volatitle(&rq->cursor, 0)-get_int_volatitle(&rq->mcs.cursor, 0) <= 0){
			return 0;
		}
	}
	return 1;
}
