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
#include "util/ringqueue.h"
#include "util/utility.h"

struct emc_device{
	// Device id
	int					id;
	// Device mode
	ushort				mode;
	// operation type device can accept
	uint				operate;
	struct ipc			*ipc_;
	struct tcp			*tcp_;
	//Message queue
	struct ringqueue	*mq;
	//Monitor message queue
	struct ringqueue	*mmq;
};

int emc_device(void){
	int id = -1;
	struct emc_device *ed=(struct emc_device *)malloc(sizeof(struct emc_device));
	if(!ed) return -1;
	memset(ed,0,sizeof(struct emc_device));
	ed->mq=create_ringqueue(_RQ_M);
	if(!ed->mq){
		free(ed);
		return -1;
	}
	ed->mmq=create_ringqueue(_RQ_M);
	if(!ed->mmq){
		delete_ringqueue(ed->mq);
		free(ed);
		return -1;
	}
	id=global_add_device(ed);
	if(id < 0){
		delete_ringqueue(ed->mq);
		delete_ringqueue(ed->mmq);
		free(ed);
		return -1;
	}
	return id;
}

void emc_destory(int device){
	void *msg=NULL;
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(ed){
		post_ringqueue(ed->mq);
		post_ringqueue(ed->mmq);
		if(ed->ipc_){
			delete_ipc(ed->ipc_);
		}
		if(ed->tcp_){
			delete_tcp(ed->tcp_);
		}
		while(1){
			if(pop_ringqueue_multiple(ed->mq,(void**)&msg) < 0){
				break;
			}else{
				emc_msg_free(msg);
			}
		}
		delete_ringqueue(ed->mq);
		delete_ringqueue(ed->mmq);
		free(ed);
	}
	global_erase_device(device);
}

 int emc_set(int device,int opt,void *optval,int optlen){
	 int add=0;
	 struct emc_device *ed=(struct emc_device *)global_get_device(device);
	 if(!ed){
		 return -1;
	 }
	if(sizeof(char)==optlen){
		add=*(char *)optval;
	}else if(sizeof(short)==optlen){
		add=*(short *)optval;
	}else if(sizeof(int)==optlen){
		add=*(int *)optval;
	}else if(sizeof(int64)==optlen){
		add=*(int64 *)optval;
	}
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
	return 0;
}

int emc_bind(int device,const char *ip,const ushort port){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(ed->ipc_ || ed->tcp_){
		return -1;
	}
	ed->tcp_=create_tcp(ip?inet_addr(ip):0,port,device,EMC_NONE,EMC_LOCAL);
	if(!ed->tcp_){
		return -1;
	}
	ed->ipc_=create_ipc(ip?inet_addr(ip):0,port,device,EMC_NONE,EMC_LOCAL);
	if(!ed->ipc_){
		return -1;
	}
	return 0;
}

int emc_connect(int device,ushort mode,const char *ip,const ushort port){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(ed->ipc_ || ed->tcp_){
		return -1;
	}
	if(EMC_PUB==mode)mode=EMC_SUB;
	if(EMC_REP==mode)mode=EMC_REQ;
	ed->mode=mode;
	if(!ip || check_local_machine(inet_addr(ip))){
		ed->ipc_=create_ipc(inet_addr(ip),port,device,mode,EMC_REMOTE);
		if(!ed->ipc_){
			return -1;
		}
	}else{
		ed->tcp_=create_tcp(inet_addr(ip),port,device,mode,EMC_REMOTE);
		if(!ed->tcp_){
			delete_tcp(ed->tcp_);
			ed->tcp_=NULL;
			return -1;
		}
	}
	return 0;
}

int emc_control(int device,int id,int ctl){
	int result=-1;
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(!(ed->operate & EMC_OPT_CONTROL)) return -1;
	if(ctl & EMC_CTL_CLOSE){
		if(ed->ipc_){
			if(0==close_ipc(ed->ipc_,id)){
				result=0;
			}
		}
		if(ed->tcp_){
			if(0==close_tcp(ed->tcp_,id)){
				result=0;
			}
		}
	}
	return result;
}

int emc_close(int device){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(ed->ipc_){
		delete_ipc(ed->ipc_);
	}
	if(ed->tcp_){
		delete_tcp(ed->tcp_);
	}
	return 0;
}

int emc_send(int device, void *msg,int flag){
	int result=-1;
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(ed->ipc_){
		if(0==send_ipc(ed->ipc_,msg,flag)){
			result=0;
		}
	}
	if(ed->tcp_){
		if(0==send_tcp(ed->tcp_,msg,flag)){
			result=0;
		}
	}
	return result;
}

int emc_recv(int device,void **msg){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(!check_ringqueue_multiple(ed->mq)){
		if(0!=wait_ringqueue(ed->mq)){
			return -1;
		}
	}
	if(pop_ringqueue_multiple(ed->mq,msg) < 0){
		return -1;
	}
	return 0;
}

int emc_monitor(int device,struct monitor_data *data){
	struct monitor_data *md=NULL;
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(!check_ringqueue_multiple(ed->mmq)){
		if(0!=wait_ringqueue(ed->mmq)){
			return -1;
		}
	}
	if(pop_ringqueue_multiple(ed->mmq,(void **)&md) < 0){
		return -1;
	}
	if(data && md){
		memcpy(data,md,sizeof(struct monitor_data));
	}
	if(md){
		global_free_monitor(md);
	}
	return 0;
}

ushort get_device_mode(int device){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	return ed->mode;
}

int push_device_message(int device,void *msg){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(push_ringqueue(ed->mq,msg) < 0){
		return -1;
	}
	return 0;
}

int push_device_event(int device,void *data){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return -1;
	}
	if(push_ringqueue(ed->mmq,data) < 0){
		return -1;
	}
	return 0;
}


uint get_device_monitor(int device){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return 0;
	}
	return (ed->operate & EMC_OPT_MONITOR);
}

uint get_device_control(int device){
	struct emc_device *ed=(struct emc_device *)global_get_device(device);
	if(!ed){
		return 0;
	}
	return (ed->operate & EMC_OPT_CONTROL);
}
