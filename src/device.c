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

struct device{
	// Device id
	int					id;
	// Device mode
	ushort				mode;
	// The device is monitoring events
	uint				monitor;
	struct ipc			*ipc_;
	struct tcp			*tcp_;
	//Message queue
	struct ringqueue	*mq;
	//Monitor message queue
	struct ringqueue	*mmq;
};

int emc_device(void){
	int id = -1;
	struct device *device_=(struct device *)malloc(sizeof(struct device));
	if(!device_) return -1;
	memset(device_,0,sizeof(struct device));
	device_->mq=create_ringqueue(_RQ_M);
	if(!device_->mq){
		free(device_);
		return -1;
	}
	device_->mmq=create_ringqueue(_RQ_M);
	if(!device_->mmq){
		delete_ringqueue(device_->mq);
		free(device_);
		return -1;
	}
	id=global_add_device(device_);
	if(id < 0){
		delete_ringqueue(device_->mq);
		delete_ringqueue(device_->mmq);
		free(device_);
		return -1;
	}
	return id;
}

void emc_destory(int id){
	void *msg=NULL;
	struct device *device_=(struct device *)global_get_device(id);
	if(device_){
		post_ringqueue(device_->mq);
		post_ringqueue(device_->mmq);
		if(device_->ipc_){
			delete_ipc(device_->ipc_);
		}
		if(device_->tcp_){
			delete_tcp(device_->tcp_);
		}
		while(1){
			if(pop_ringqueue_multiple(device_->mq,(void**)&msg) < 0){
				break;
			}else{
				emc_msg_free(msg);
			}
		}
		delete_ringqueue(device_->mq);
		delete_ringqueue(device_->mmq);
		free(device_);
	}
	global_erase_device(id);
}

 int emc_set(int id,int opt,void *optval,int optlen){
	 struct device *device_=(struct device *)global_get_device(id);
	 if(!device_){
		 return -1;
	 }
	switch(opt){
	case EMC_OPT_MONITOR:
		if(sizeof(char)==optlen){
			device_->monitor=*(uchar *)optval;
		}else if(sizeof(short)==optlen){
			device_->monitor=*(ushort *)optval;
		}else if(sizeof(int)==optlen){
			device_->monitor=*(uint *)optval;
		}else if(sizeof(int64)==optlen){
			device_->monitor=*(uint64 *)optval;
		}
		break;
	}
	return 0;
}

int emc_bind(int id,const char *ip,const ushort port){
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	if(device_->ipc_ || device_->tcp_){
		return -1;
	}
	device_->tcp_=create_tcp(ip?inet_addr(ip):0,port,id,EMC_NONE,EMC_LOCAL);
	if(!device_->tcp_){
		return -1;
	}
	device_->ipc_=create_ipc(ip?inet_addr(ip):0,port,id,EMC_NONE,EMC_LOCAL);
	if(!device_->ipc_){
		return -1;
	}
	return 0;
}

int emc_connect(int id,ushort mode,const char *ip,const ushort port){
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	if(device_->ipc_ || device_->tcp_){
		return -1;
	}
	if(EMC_PUB==mode)mode=EMC_SUB;
	if(EMC_REP==mode)mode=EMC_REQ;
	device_->mode=mode;
	if(check_local_machine(inet_addr(ip))){
		device_->ipc_=create_ipc(inet_addr(ip),port,id,mode,EMC_REMOTE);
		if(!device_->ipc_){
			return -1;
		}
	}else{
		device_->tcp_=create_tcp(inet_addr(ip),port,id,mode,EMC_REMOTE);
		if(!device_->tcp_){
			delete_tcp(device_->tcp_);
			device_->tcp_=NULL;
			return -1;
		}
	}
	return 0;
}

int emc_close(int id){
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	if(device_->ipc_){
		delete_ipc(device_->ipc_);
	}
	if(device_->tcp_){
		delete_tcp(device_->tcp_);
	}
	return 0;
}

int emc_send(int id, void *msg,int flag){
	int result=-1;
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	if(device_->ipc_){
		if(0==send_ipc(device_->ipc_,msg,flag)){
			result=0;
		}
	}
	if(device_->tcp_){
		if(0==send_tcp(device_->tcp_,msg,flag)){
			result=0;
		}
	}
	return result;
}

int emc_recv(int id,void **msg){
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	if(!check_ringqueue_multiple(device_->mq)){
		if(0!=wait_ringqueue(device_->mq)){
			return -1;
		}
	}
	if(pop_ringqueue_multiple(device_->mq,msg) < 0){
		return -1;
	}
	return 0;
}

int emc_monitor(int id,struct monitor_data *data){
	struct monitor_data *md=NULL;
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	if(!check_ringqueue_multiple(device_->mmq)){
		if(0!=wait_ringqueue(device_->mmq)){
			return -1;
		}
	}
	if(pop_ringqueue_multiple(device_->mmq,(void **)&md) < 0){
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

ushort get_device_mode(int id){
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	return device_->mode;
}

int push_device_message(int id,void *msg){
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	if(push_ringqueue(device_->mq,msg) < 0){
		return -1;
	}
	return 0;
}

int push_device_event(int id,void *data){
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return -1;
	}
	if(push_ringqueue(device_->mmq,data) < 0){
		return -1;
	}
	return 0;
}

uint get_device_monitor(int id){
	struct device *device_=(struct device *)global_get_device(id);
	if(!device_){
		return 0;
	}
	return device_->monitor;
}
