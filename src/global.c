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
#include "util/heap.h"
#include "util/pqueue.h"
#include "util/sendqueue.h"
#include "util/map.h"
#include "util/utility.h"
#include "global.h"

#define GLOBAL_DEVICE_DEFAULT	4096

struct global{
	// Serial device, since the growth
	volatile uint		device_serial;
	// Serial data, since the growth
	volatile uint		data_serial;
	// Record all the pointer device
	struct hashmap		*devices;
	// Serial connection id, since the growth
	struct pqueue		*id_allocator;
	// Data consolidator
	struct merger		*mg;
	// Depacketizer
	struct unpack		*upk;
	// Send queue
	struct sendqueue	*sq;
	// Reconnect map
	struct map			*rcmq;
	// reconnect struct heap
	struct heap			*rcheap;
	// monitor heap
	struct heap			*mtheap;
	// exit
	volatile uint		exit;
};

// Global parameters, a process exists only one
static struct global self = {0};

static uint reconnect_map_foreach_cb(struct map * m, int64 key, void * p, void * addition){
	struct reconnect * rc = (struct reconnect *)p;
	if(rc->cb){
		if(0 == ((on_reconnect_cb *)rc->cb)(rc->client, rc->addition)){
			map_erase_nonlock(m, key);
			return 1;
		}
	}
	return 0;
}

static emc_result_t EMC_CALL  global_reconnect_cb(void * args){
	while(!self.exit){
		map_foreach(self.rcmq, reconnect_map_foreach_cb, NULL);
		nsleep(100);
	}
#if defined (EMC_WINDOWS)
	return 0;
#else
	return NULL;
#endif
}

static void global_init(void){
	int64 index = 0;
	if(!self.devices){
#if defined (EMC_WINDOWS)
		WSADATA	wsaData = {0};
		WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
		self.devices = hashmap_new(GLOBAL_DEVICE_DEFAULT);
		self.mg = merger_new(EMC_SOCKETS_DEFAULT);
		self.upk = unpack_new(EMC_SOCKETS_DEFAULT);
		self.id_allocator = create_pqueue();
		for(index=0; index<EMC_SOCKETS_DEFAULT; index++){
			pqueue_push(self.id_allocator, (void*)index);
		}
		self.sq = create_sendqueue();
		self.rcmq = create_map(EMC_SOCKETS_DEFAULT);
		self.rcheap = heap_new(sizeof(struct reconnect), EMC_SOCKETS_DEFAULT);
		self.mtheap = heap_new(sizeof(struct monitor_data), EMC_SOCKETS_DEFAULT);
		srand((uint)time(NULL));
		create_thread(global_reconnect_cb, NULL);
	}
}

void global_term(void){
	self.exit = 1;
#if defined (EMC_WINDOWS)
	WSACleanup();
#endif
	hashmap_delete(self.devices);
	self.devices = NULL;
	unpack_delete(self.upk);
	self.upk = NULL;
	delete_pqueue(self.id_allocator);
	self.id_allocator = NULL;
	delete_sendqueue(self.sq);
	self.sq = NULL;
	delete_map(self.rcmq);
	self.rcmq = NULL;
	heap_delete(self.rcheap);
	self.rcheap = NULL;
	heap_delete(self.mtheap);
	self.mtheap = NULL;
}

int global_add_device(void * device_){
	int id = self.device_serial;
	global_init();
	if(hashmap_insert(self.devices, id, device_) < 0){
		return -1;
	}
	self.device_serial ++;
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
	return ++ self.data_serial;
}

int global_get_connect_id(){
	int64 serial = -1;
	if(pqueue_pop(self.id_allocator, (void**)&serial) < 0){
		return -1;
	}
	return (int)serial;
}

void global_idle_connect_id(int id){
	int64 serial = id;
	pqueue_push(self.id_allocator, (void *)serial);
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

unsigned int global_sendqueue_size(int id){
	return sendqueue_size(self.sq, id);
}

int global_add_reconnect(int id, on_reconnect_cb * cb, void * client, void * addition){
	struct reconnect * rc = (struct reconnect *)heap_alloc(self.rcheap);
	if(!rc)return -1;
	rc->cb = cb;
	rc->client = client;
	rc->addition = addition;
	if(map_add(self.rcmq, id, rc) < 0){
		heap_free(self.rcheap, rc);
		return -1;
	}
	return 0;
}

void *global_alloc_monitor(){
	return heap_alloc(self.mtheap);
}

void global_free_monitor(void *data){
	if(data){
		heap_free(self.mtheap,data);
	}
}

int global_rand_number(){
	return rand();
}
