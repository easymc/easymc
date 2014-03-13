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
#include "ipc.h"
#include "tcp.h"
#include "emc.h"
#include "msg.h"
#include "util/hashmap.h"
#include "util/ringqueue.h"
#include "util/utility.h"

struct emc_device{
	// Device id
	int					id;
	// operation type device can accept
	uint				operate;
	// thread number
	uint				thread;
	// tcp manager
	struct tcp_mgr	 *	mgr;
	//Monitor message queue
	struct ringqueue *	mmq;
	// plug map
	struct hashmap	 *	plug_map;
};

static uint plug_delete_cb(struct hashmap * m, int id, void * p, void * addition){
	emc_close(id);
	return 1;
}

int emc_device(void){
	int id = -1;
	struct emc_device * ed = (struct emc_device *)malloc(sizeof(struct emc_device));
	if(!ed) {
		errno = ENOMEM;
		return -1;
	}
	memset(ed, 0, sizeof(struct emc_device));
	ed->mmq = create_ringqueue(_RQ_M);
	if(!ed->mmq){
		free(ed);
		errno = ENOMEM;
		return -1;
	}
	ed->plug_map = hashmap_new(EMC_SOCKETS_DEFAULT);;
	id = global_add_device(ed);
	if(id < 0){
		delete_ringqueue(ed->mmq);
		free(ed);
		errno = EUSERS;
		return -1;
	}
	return id;
}

void emc_destory(int device){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(ed){
		hashmap_foreach(ed->plug_map, plug_delete_cb, NULL);
		post_ringqueue(ed->mmq);
		nsleep(100);
		delete_ringqueue(ed->mmq);
		ed->mmq = NULL;
		if(ed->mgr){
			delete_tcp_mgr(ed->mgr);
			ed->mgr = NULL;
		}
		hashmap_delete(ed->plug_map);
		ed->plug_map = NULL;
		free(ed);
	}
	global_erase_device(device);
}

 int emc_set(int device, int opt, void * optval, int optlen){
	 int add=0;
	 struct emc_device * ed = (struct emc_device *)global_get_device(device);
	 if(!ed){
		 errno = ENODEVICE;
		 return -1;
	 }
	if(sizeof(char) == optlen){
		add = *(char *)optval;
	}else if(sizeof(short) == optlen){
		add = *(short *)optval;
	}else if(sizeof(int) == optlen){
		add = *(int *)optval;
	}else if(sizeof(int64) == optlen){
		add = *(int64 *)optval;
	}
	if(opt & EMC_OPT_THREAD){
		if(ed->mgr) return -1;
		if(add <= 0) add = 1;
		ed->thread = add;
	}else{
		if(add > 0){
			if(opt & EMC_OPT_MONITOR){
				ed->operate |= EMC_OPT_MONITOR;
			}
			if(opt & EMC_OPT_CONTROL){
				ed->operate |= EMC_OPT_CONTROL;
			}
		}else{
			if(opt & EMC_OPT_MONITOR){
				ed->operate &= ~EMC_OPT_MONITOR;
			}
			if(opt & EMC_OPT_CONTROL){
				ed->operate &= ~EMC_OPT_CONTROL;
			}
		}
	}
	return 0;
}

int emc_monitor(int device, struct monitor_data * data){
	struct monitor_data * md = NULL;
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(!ed || !ed->mmq){
		errno = ENODEVICE;
		return -1;
	}
	if(!check_ringqueue_multiple(ed->mmq)){
		if(0 != wait_ringqueue(ed->mmq)){
			return -1;
		}
	}
	if(pop_ringqueue_multiple(ed->mmq, (void **)&md) < 0){
		errno = EQUEUE;
		return -1;
	}
	if(data && md){
		memcpy(data, md, sizeof(struct monitor_data));
	}
	if(md){
		global_free_monitor(md);
	}
	return 0;
}

int push_device_event(int device, void * data){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(!ed){
		errno = ENODEVICE;
		return -1;
	}
	if(push_ringqueue(ed->mmq, data) < 0){
		errno = EQUEUE;
		return -1;
	}
	return 0;
}


uint get_device_monitor(int device){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(!ed){
		errno = ENODEVICE;
		return 0;
	}
	return (ed->operate & EMC_OPT_MONITOR);
}

uint get_device_control(int device){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(!ed){
		errno = ENODEVICE;
		return 0;
	}
	return (ed->operate & EMC_OPT_CONTROL);
}

struct tcp_mgr * get_device_tcp_mgr(int device){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(!ed){
		errno = ENODEVICE;
		return NULL;
	}
	return ed->mgr;
}

void set_device_tcp_mgr(int device, struct tcp_mgr *mgr){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(ed){
		errno = ENODEVICE;
		ed->mgr = mgr;
	}
}

uint get_device_thread(int device){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(!ed){
		errno = ENODEVICE;
		return 0;
	}
	return ed->thread;
}

int add_device_plug(int device, int plug, void * p){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(!ed){
		errno = ENODEVICE;
		return -1;
	}
	return hashmap_insert(ed->plug_map, plug, p);
}

int del_device_plug(int device, int plug){
	struct emc_device * ed = (struct emc_device *)global_get_device(device);
	if(!ed) return -1;
	return hashmap_erase(ed->plug_map, plug);
}
