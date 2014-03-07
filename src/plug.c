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
#include "plug.h"
#include "device.h"
#include "util/ringqueue.h"
#include "util/utility.h"

struct easymc_plug{
	// Device id
	int					device;
	// Plug id
	int					id;
	// Device mode
	ushort				mode;
	struct ipc			*ipc_;
	struct tcp			*tcp_;
	//Message queue
	struct ringqueue	*mq;
};

int emc_plug(int device){
	int id = -1;
	struct easymc_plug * pg = (struct easymc_plug *)malloc(sizeof(struct easymc_plug));
	if(!pg) {
		errno = ENOMEM;
		return -1;
	}
	memset(pg, 0, sizeof(struct easymc_plug));
	pg->device = device;
	pg->mq = create_ringqueue(_RQ_M);
	if(!pg->mq){
		free(pg);
		errno = ENOMEM;
		return -1;
	}
	id = global_add_plug(pg);
	if(id < 0){
		delete_ringqueue(pg->mq);
		free(pg);
		errno = EUSERS;
		return -1;
	}
	add_device_plug(device, id, pg);
	pg->id = id;
	return id;
}

int emc_bind(int plug, const char * ip, const ushort port){
	struct easymc_plug * pg = (struct easymc_plug *)global_get_plug(plug);
	if(!pg){
		errno = ENOPLUG;
		return -1;
	}
	if(pg->ipc_ || pg->tcp_){
		errno = EREBIND;
		return -1;
	}
	if(!get_device_tcp_mgr(pg->device)){
		set_device_tcp_mgr(pg->device, create_tcp_mgr(pg->device, get_device_thread(pg->device)));
	}
	if(!get_device_tcp_mgr(pg->device)){
		return -1;
	}
	pg->tcp_ = add_tcp(ip?inet_addr(ip):0, port, EMC_NONE, EMC_LOCAL, plug, get_device_tcp_mgr(pg->device));
	if(!pg->tcp_){
		return -1;
	}
	pg->ipc_ = create_ipc(ip?inet_addr(ip):0, port, pg->device, plug, EMC_NONE, EMC_LOCAL);
	if(!pg->ipc_){
		return -1;
	}
	return 0;
}

int emc_connect(int plug, ushort mode, const char * ip, const ushort port){
	struct easymc_plug * pg = (struct easymc_plug *)global_get_plug(plug);
	if(!pg){
		errno = ENOPLUG;
		return -1;
	}
	if(pg->ipc_ || pg->tcp_){
		errno = EREBIND;
		return -1;
	}
	if(EMC_PUB == mode)mode = EMC_SUB;
	if(EMC_REP == mode)mode = EMC_REQ;
	pg->mode = mode;
	if(!get_device_tcp_mgr(pg->device)){
		set_device_tcp_mgr(pg->device, create_tcp_mgr(pg->device, get_device_thread(pg->device)));
	}
	if(!get_device_tcp_mgr(pg->device)){
		return -1;
	}
	if(!ip || check_local_machine(inet_addr(ip))){
		pg->ipc_ = create_ipc(inet_addr(ip), port, pg->device, plug, mode, EMC_REMOTE);
		if(!pg->ipc_){
			return -1;
		}
	}else{
		pg->tcp_ = add_tcp(inet_addr(ip), port, mode, EMC_REMOTE, plug, get_device_tcp_mgr(pg->device));
		if(!pg->tcp_){
			delete_tcp(pg->tcp_);
			pg->tcp_ = NULL;
			return -1;
		}
	}
	return 0;
}

int emc_control(int plug, int id, int ctl){
	int result=-1;
	struct easymc_plug * pg = (struct easymc_plug *)global_get_plug(plug);
	if(!pg){
		errno = ENOPLUG;
		return -1;
	}
	if(!get_device_control(pg->device)) return -1;
	if(ctl & EMC_CTL_CLOSE){
		if(pg->ipc_){
			if(0 == close_ipc(pg->ipc_, id)){
				result = 0;
			}
		}
		if(pg->tcp_){
			if(0 == close_tcp(pg->tcp_, id)){
				result = 0;
			}
		}
	}
	return result;
}

int emc_close(int plug){
	void * msg = NULL;
	struct easymc_plug * pg = (struct easymc_plug *)global_get_plug(plug);
	if(!pg){
		errno = ENOPLUG;
		return -1;
	}
	del_device_plug(pg->device, plug);
	post_ringqueue(pg->mq);
	if(pg->ipc_){
		delete_ipc(pg->ipc_);
	}
	if(pg->tcp_){
		delete_tcp(pg->tcp_);
	}
	while(1){
		if(pop_ringqueue_multiple(pg->mq, (void **)&msg) < 0){
			break;
		}else{
			emc_msg_free(msg);
		}
	}
	delete_ringqueue(pg->mq);
	free(pg);
	global_erase_plug(plug);
	return 0;
}

int emc_send(int plug, void * msg, int flag){
	int result=-1;
	struct easymc_plug * pg = (struct easymc_plug *)global_get_plug(plug);
	if(!pg){
		errno = ENOPLUG;
		return -1;
	}
	if(pg->ipc_){
		if(0 == send_ipc(pg->ipc_, msg, flag)){
			result = 0;
		}
	}
	if(pg->tcp_){
		if(0 == send_tcp(pg->tcp_, msg, flag)){
			result = 0;
		}
	}
	return result;
}

int emc_recv(int plug, void ** msg){
	struct easymc_plug * pg = (struct easymc_plug *)global_get_plug(plug);
	if(!pg){
		errno = ENOPLUG;
		return -1;
	}
	if(!check_ringqueue_multiple(pg->mq)){
		if(0!=wait_ringqueue(pg->mq)){
			return -1;
		}
	}
	if(pop_ringqueue_multiple(pg->mq, msg) < 0){
		errno = EQUEUE;
		return -1;
	}
	return 0;
}

int push_plug_message(int plug, void * msg){
	struct easymc_plug * pg = (struct easymc_plug *)global_get_plug(plug);
	if(!pg){
		errno = ENOPLUG;
		return -1;
	}
	if(push_ringqueue(pg->mq, msg) < 0){
		errno = EQUEUE;
		return -1;
	}
	return 0;
}

ushort get_plug_mode(int plug){
	struct easymc_plug * pg = (struct easymc_plug *)global_get_plug(plug);
	if(!pg){
		errno = ENOPLUG;
		return -1;
	}
	return pg->mode;
}
