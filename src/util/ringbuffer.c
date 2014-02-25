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
#include "ringbuffer.h"

#define _RB_SIZE	(0x80)
#define _RB_MASK	(0x7F)

#pragma pack(1)

struct ringbuffer{
	volatile int	cursor;
	volatile int	pd;
	volatile int	real;
	volatile int	cs;
};

#pragma pack()

static void put_ordered_char(volatile char * buffer, uint offset, char flag){
	volatile char * addr = buffer+offset;
	*addr = flag;
}

static int get_int_volatitle(volatile int * buffer, uint offset){
	volatile int * addr = buffer+offset;
	int result = *addr;
	rmb();
	return result;
}

static uint ringbuffer_number_cas(volatile int * key, uint _old, uint _new){
#ifdef EMC_WINDOWS
	return InterlockedCompareExchange((long*)key, _new, _old);
#else
	return __sync_val_compare_and_swap(key, _old, _new);
#endif
}

static uint ringbuffer_write_next(struct ringbuffer * rb){
	uint current=0, next=0;
	do{
		current = get_int_volatitle(&rb->pd, 0);
		next = current+1;
	}while(current != ringbuffer_number_cas(&rb->pd, current, next));
	return next;
}

static int ringbuffer_read_next(struct ringbuffer * rb, int * cursor){
	int current=0, next=0;
	do{
		current = get_int_volatitle(&rb->cs, 0);
		next = current+1;
		if(next > get_int_volatitle(&rb->cursor, 0)){
			return -1;
		}
	}while(current != ringbuffer_number_cas(&rb->cs, current, next));
	*cursor = next;
	return 0;
}

static uint ringbuffer_check_consumer(struct ringbuffer * rb){
	if(get_int_volatitle(&rb->pd, 0)-get_int_volatitle(&rb->real, 0)+1 >= _RB_SIZE){
		return 1;
	}
	return 0;
}

void init_ringbuffer(struct ringbuffer * rb){
	rb->cursor = -1;
	rb->cs = -1;
	rb->pd = -1;
	rb->real = -1;
}

int push_ringbuffer(struct ringbuffer * rb, void * data, uint len){
	uint current=0, cursor=0, index=0;

	if(len>MAX_DATA_SIZE || ringbuffer_check_consumer(rb) > 0) return -1;
	cursor = ringbuffer_write_next(rb);
	memcpy((char *)rb+sizeof(struct ringbuffer)+MAX_DATA_SIZE * (cursor&_RB_MASK), data, len);
	//commit
	do{
		current = get_int_volatitle(&rb->cursor, 0);
		if(current+1 == cursor){
			if(current == ringbuffer_number_cas(&rb->cursor, current, cursor)){
				break;
			}
		}
	}while(1);
	return 0;
}

int pop_ringbuffer(struct ringbuffer * rb, void * buffer){
	uint current = 0;
	int cursor = 0;

	if(get_int_volatitle(&rb->cursor,0)-get_int_volatitle(&rb->cs,0) <= 0){
		return -1;
	}
	if(ringbuffer_read_next(rb, &cursor) < 0){
		return -1;
	}
	if(buffer){
		memcpy(buffer, (char *)rb+sizeof(struct ringbuffer)+(cursor&_RB_MASK) * MAX_DATA_SIZE, MAX_DATA_SIZE);
	}
	//commit
	do{
		current = get_int_volatitle(&rb->real, 0);
		if(current+1 == cursor){
			if(current == ringbuffer_number_cas(&rb->real, current, cursor)){
				break;
			}
		}
	}while(1);
	return 0;
}
