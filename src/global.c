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
#include "emc.h"
#include "msg.h"
#include "common.h"
#include "util/hashmap.h"
#include "util/unpack.h"
#include "util/merger.h"
#include "util/nqueue.h"
#include "util/sendqueue.h"
#include "util/map.h"
#include "util/utility.h"
#include "global.h"

#define GLOBAL_DEVICE_DEFAULT	4096

struct global{
	// Serial device, since the growth
	volatile uint		device_serial;
	// Serial plug, since the growth
	volatile uint		plug_serial;
	// Serial data, since the growth
	volatile uint		data_serial;
	// Record all the pointer device
	struct hashmap		*devices;
	// Record all the plug
	struct hashmap		*plugs;
	// Serial connection id, since the growth
	struct nqueue		*id_allocator;
	// Data consolidator
	struct merger		*mg;
	// Depacketizer
	struct unpack		*upk;
	// Send queue
	struct sendqueue	*sq;
	// Reconnect map
	struct map			*rcmq;
	// reconnect thread
	emc_result_t		treconnect;
	// exit
	volatile uint		exit;
};

// Global parameters, a process exists only one
static struct global self = {0};

static uint reconnect_map_foreach_cb(struct map * m, int64 key, void * p, void * addition){
	struct reconnect * rc = (struct reconnect *)p;
	if(rc->cb){
		if(0 == ((on_reconnect_cb *)rc->cb)(rc->client, rc->addition)){
			map_erase(m, key);
			return 1;
		}
	}
	return 0;
}

static emc_cb_t EMC_CALL  global_reconnect_cb(void * args){
	while(!self.exit){
		map_foreach(self.rcmq, reconnect_map_foreach_cb, NULL);
		nsleep(100);
	}
	return (emc_cb_t)0;
}

static uint global_number_cas(volatile uint * key, uint _old, uint _new){
#ifdef EMC_WINDOWS
	return InterlockedCompareExchange((long*)key, _new, _old);
#else
	return __sync_val_compare_and_swap(key, _old, _new);
#endif
}

static uint global_number_next(volatile uint * v){
	uint current=0, next=0;
	do{
		current = *v;
		next = current + 1;
	}while(current != global_number_cas(v, current, next));
	return next;
}

static void global_init(void){
	int index = 0;
	if(!self.devices){
#if defined (EMC_WINDOWS)
		WSADATA	wsaData = {0};
		WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
		self.devices = hashmap_new(GLOBAL_DEVICE_DEFAULT);
		self.plugs = hashmap_new(EMC_SOCKETS_DEFAULT);
		self.mg = merger_new(EMC_SOCKETS_DEFAULT);
		self.upk = unpack_new(EMC_SOCKETS_DEFAULT);
		self.id_allocator = create_nqueue();
		for(index=0; index<EMC_SOCKETS_DEFAULT; index++){
			nqueue_push(self.id_allocator, index);
		}
		self.sq = create_sendqueue();
		self.rcmq = create_map(EMC_SOCKETS_DEFAULT);
		srand((uint)time(NULL));
		self.treconnect = emc_thread(global_reconnect_cb, NULL);
	}
}

void global_term(void){
	self.exit = 1;
	emc_thread_join(self.treconnect);
#if defined (EMC_WINDOWS)
	WSACleanup();
#endif
	hashmap_delete(self.devices);
	self.devices = NULL;
	hashmap_delete(self.plugs);
	self.plugs = NULL;
	unpack_delete(self.upk);
	self.upk = NULL;
	delete_nqueue(self.id_allocator);
	self.id_allocator = NULL;
	delete_sendqueue(self.sq);
	self.sq = NULL;
	delete_map(self.rcmq);
	self.rcmq = NULL;
}

int global_add_device(void * device_){
	int id = -1;
	global_init();
	id = global_number_next(&self.device_serial);
	if(hashmap_insert(self.devices, id, device_) < 0){
		return -1;
	}
	return id;
}

void * global_get_device(int id){
	if(id < 0) return NULL;
	global_init();
	return hashmap_search(self.devices,id);
}

void global_erase_device(int id){
	if(id >= 0 && id < GLOBAL_DEVICE_DEFAULT){
		global_init();
		hashmap_erase(self.devices,id);
	}
}

int global_add_plug(void * plug){
	int id = -1;
	global_init();
	id = global_number_next(&self.plug_serial);
	if(hashmap_insert(self.plugs, id, plug) < 0){
		return -1;
	}
	return id;
}

void * global_get_plug(int id){
	if(id < 0) return NULL;
	global_init();
	return hashmap_search(self.plugs,id);
}

void global_erase_plug(int id){
	if(id >= 0 && id < GLOBAL_DEVICE_DEFAULT){
		global_init();
		hashmap_erase(self.plugs,id);
	}
}

void * global_alloc_merger(){
	return merger_alloc(self.mg);
}

void global_free_merger(void * unit){
	merger_free(self.mg, unit);
}

void * global_alloc_unapck(void){
	return unpack_alloc(self.upk);
}

void global_free_unpack(void * unit){
	unpack_free(self.upk, unit);
}

unsigned int global_get_data_serial(){
	return global_number_next(&self.data_serial);
}

int global_get_connect_id(){
	int id = -1;
	if(nqueue_pop(self.id_allocator, &id) < 0){
		return -1;
	}
	return id;
}

void global_idle_connect_id(int id){
	nqueue_push(self.id_allocator, id);
}

int global_push_sendqueue(int id, void * p){
	return sendqueue_push(self.sq, id, p);
}

int global_push_head_sendqueue(int id, void * p){
	return sendqueue_push_head(self.sq, id, p);
}

int global_pop_sendqueue(int id, void ** p){
	return sendqueue_pop(self.sq, id, p);
}

int global_add_reconnect(int id, on_reconnect_cb * cb, void * client, void * addition){
	struct reconnect * rc = (struct reconnect *)malloc(sizeof(struct reconnect));
	if(!rc)return -1;
	rc->cb = cb;
	rc->client = client;
	rc->addition = addition;
	if(map_add(self.rcmq, id, rc) < 0){
		free(rc);
		return -1;
	}
	return 0;
}

void global_free_reconnect(int id){
	struct reconnect * rc = NULL;
	if(0==map_get(self.rcmq, id, (void **)&rc)){
		if(rc){
			free(rc);
		}
		map_erase(self.rcmq, id);
	}
}

void * global_alloc_monitor(){
	return malloc(sizeof(struct monitor_data));
}

void global_free_monitor(void *data){
	if(data){
		free(data);
	}
}

int global_rand_number(){
	return rand();
}
