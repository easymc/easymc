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

#include "config.h"
#include "global.h"
#include "util/memory/jemalloc.h"
#include "emc.h"
#include "msg.h"

#pragma pack(1)
struct message{
	// Whether the message deletion(0xdeaddead)
	volatile uint	flag;
	// Serial Number
	int				serial;
	// Message mode
	ushort			mode;
	// Both the server sends data to the client or the client sends data to the server, id values are using the client's id number
	int				id;
	// Message length
	uint			len;
	// After sending a message when the reference count is reduced to 0 when the release of the message buffer
	volatile uint	ref;
	// Whether it is deleted
	volatile uint	del;
	// Additional data, when the monitor is used to return to the upper application
	void			*addition;
};
#pragma pack()

static uint msg_number_cas(volatile uint *key,uint _old,uint _new){
#ifdef EMC_WINDOWS
	return InterlockedCompareExchange((unsigned long*)key,_new,_old);
#else
	return __sync_val_compare_and_swap(key,_old,_new);
#endif
}

static uint msg_check_live(const void *msg){
	return EMC_LIVE==((struct message *)msg)->flag;
}

void *emc_msg_alloc(void *data,uint size){
	struct message *msg_=(struct message *)malloc_impl(sizeof(struct message)+size);
	if(!msg_)return NULL;
	memset(msg_,0,sizeof(struct message)+size);
	msg_->flag=EMC_LIVE;
	msg_->serial=global_get_data_serial();
	msg_->id=-1;
	msg_->len=size;
	if(data && size){
		memcpy(msg_+1,data,size);
	}
	return msg_;
}

void emc_msg_build(void *msg,const void *old){
	if(msg && old && msg_check_live(msg) && msg_check_live(old)){
		((struct message *)msg)->mode=((struct message *)old)->mode;
		((struct message *)msg)->id=((struct message *)old)->id;
		((struct message *)msg)->len=((struct message *)old)->len;
		((struct message *)msg)->addition=((struct message *)old)->addition;
	}
}

int emc_msg_getid(void *msg_){
	int id=-1;
	if(!msg_ || !msg_check_live(msg_))return -1;
	id=((struct message *)msg_)->id;
	return id;
}

ushort emc_msg_get_mode(void *msg_){
	ushort mode=-1;
	if(!msg_ || !msg_check_live(msg_))return -1;
	mode=((struct message *)msg_)->mode;
	return mode;
}

int emc_msg_length(void *msg_){
	int length=0;
	if(!msg_ || !msg_check_live(msg_))return 0;
	length=((struct message *)msg_)->len;
	return length;
}

void emc_msg_set_mode(void *msg_,ushort mode){
	if(msg_ && msg_check_live(msg_)){
		((struct message *)msg_)->mode=mode;
	}
}

void emc_msg_setid(void *msg_,int id){
	if(msg_ && msg_check_live(msg_)){
		((struct message *)msg_)->id=id;
	}
}

void emc_msg_set_addition(void *msg_,void *addition){
	if(msg_ && msg_check_live(msg_)){
		((struct message *)msg_)->addition=addition;
	}
}

void *emc_msg_get_addition(void *msg_){
	if(msg_ && msg_check_live(msg_)){
		return ((struct message *)msg_)->addition;;
	}
	return NULL;
}

int emc_msg_free (void *msg_){
	if(!msg_) return -1;
	if(!msg_check_live(msg_)) return -1;
	do{}while(0!=msg_number_cas(&((struct message *)msg_)->del,0,1));
	if(!msg_check_live(msg_)) return -1;
	((struct message *)msg_)->flag=EMC_DEAD;
	free_impl(msg_);
	return 0;
}

void *emc_msg_buffer(void *msg_){
	if(!msg_ || (msg_ && !msg_check_live(msg_)))return NULL;
	return (struct message *)msg_+1;
}

int emc_msg_zero_ref(void *msg){
	if(msg && msg_check_live(msg)){
		return 0==((struct message *)msg)->ref?1:0;
	}
	return -1;
}

int emc_msg_ref_add(void *msg){
	uint current=0,next=0;
	if(msg_check_live(msg)){
		do{
			current=((struct message *)msg)->ref;
			next=current+1;
		}while(current!=msg_number_cas(&((struct message *)msg)->ref,current,next));
		return 0;
	}
	return -1;
}

int emc_msg_ref_dec(void *msg){
	uint current=0,next=0;
	if(msg_check_live(msg)){
		do{
			current=((struct message *)msg)->ref;
			next=current-1;
		}while(current!=msg_number_cas(&((struct message *)msg)->ref,current,next));
		return 0;
	}
	return -1;
}

int emc_msg_serial(void *msg){
	if(!msg_check_live(msg)){
		return -1;
	}
	return ((struct message *)msg)->serial;
}

int emc_msg_struct_size(){
	return sizeof(struct message);
}
