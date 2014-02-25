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
#include "ringarray.h"

#define _RA_SIZE	(0x8)
#define _RA_MASK	(0x7)

#pragma pack(1)

struct ringarray{
	int				node[_RA_SIZE];
	// Pointing the cursor ringarray next available element
	volatile int	cursor;
	// Pointing the cursor producer next available element
	volatile int	pd;
	// Cursor consumer elements already submitted
	volatile int	real;
	// Pointing the cursor next available element in consumer
	volatile int	cs;
};

#pragma pack()

static void put_ordered_char(volatile char * buffer, uint offset, char flag){
	volatile char *addr = buffer+offset;
	*addr = flag;
}

static int get_int_volatitle(volatile int * buffer, uint offset){
	volatile int * addr = buffer+offset;
	int result = *addr;
	rmb();
	return result;
}

static uint ringarray_number_cas(volatile int * key, uint _old, uint _new){
#ifdef EMC_WINDOWS
	return InterlockedCompareExchange((long*)key, _new, _old);
#else
	return __sync_val_compare_and_swap(key, _old, _new);
#endif
}

static uint ringarray_write_next(struct ringarray * rb){
	uint current=0, next=0;
	do{
		current = get_int_volatitle(&rb->pd, 0);
		next = current+1;
	}while(current != ringarray_number_cas(&rb->pd, current, next));
	return next;
}

static int ringarray_read_next(struct ringarray * rb, int * cursor){
	int current=0, next=0;
	do{
		current = get_int_volatitle(&rb->cs, 0);
		next = current+1;
		if(next > get_int_volatitle(&rb->cursor, 0)){
			return -1;
		}
	}while(current != ringarray_number_cas(&rb->cs,current,next));
	*cursor = next;
	return 0;
}

static uint ringarray_check_consumer(struct ringarray * rb){
	if(get_int_volatitle(&rb->pd,0)-get_int_volatitle(&rb->real,0)+1 >= _RA_SIZE){
		return 1;
	}
	return 0;
}

void init_ringarray(struct ringarray * rb){
	memset(rb->node, 0, sizeof(int) * _RA_SIZE);
	rb->cursor = -1;
	rb->cs = -1;
	rb->pd = -1;
	rb->real = -1;
}

int push_ringarray(struct ringarray * rb, int v){
	uint current=0, cursor=0, index=0;

	if(ringarray_check_consumer(rb) > 0) return -1;
	cursor = ringarray_write_next(rb);
	rb->node[cursor&_RA_MASK] = v;
	//commit
	do{
		current = get_int_volatitle(&rb->cursor, 0);
		if(current+1 == cursor){
			if(current == ringarray_number_cas(&rb->cursor, current, cursor)){
				break;
			}
		}
	}while(1);
	return 0;
}

int pop_ringarray(struct ringarray * rb, void * buf){
	uint current = 0;
	int	cursor = 0;

	if(get_int_volatitle(&rb->cursor,0)-get_int_volatitle(&rb->cs,0) <= 0){
		return -1;
	}
	if(ringarray_read_next(rb,&cursor) < 0){
		return -1;
	}
	if(buf){
		*(int *)buf = rb->node[cursor&_RA_MASK];
	}
	//commit
	do{
		current = get_int_volatitle(&rb->real, 0);
		if(current+1 == cursor){
			if(current == ringarray_number_cas(&rb->real, current, cursor)){
				break;
			}
		}
	}while(1);
	return 0;
}

int get_ringarray_size(){
	return sizeof(struct ringarray);
}
