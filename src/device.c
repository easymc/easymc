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

void emc_destory(int dev){
	void *msg=NULL;
	struct device *device_=(struct device *)global_get_device(dev);
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
	global_erase_device(dev);
}

 int emc_set(int dev,int opt,void *optval,int optlen){
	 int add=0;
	 struct device *device_=(struct device *)global_get_device(dev);
	 if(!device_){
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
			device_->operate |= EMC_OPT_MONITOR;
		}
		if(opt & EMC_OPT_CONTROL){
			device_->operate |= EMC_OPT_CONTROL;
		}
	}else{
		if(opt & EMC_OPT_MONITOR){
			device_->operate &= ~EMC_OPT_MONITOR;
		}
		if(opt & EMC_OPT_CONTROL){
			device_->operate &= ~EMC_OPT_CONTROL;
		}
	}
	return 0;
}

int emc_bind(int dev,const char *ip,const ushort port){
	struct device *device_=(struct device *)global_get_device(dev);
	if(!device_){
		return -1;
	}
	if(device_->ipc_ || device_->tcp_){
		return -1;
	}
	device_->tcp_=create_tcp(ip?inet_addr(ip):0,port,dev,EMC_NONE,EMC_LOCAL);
	if(!device_->tcp_){
		return -1;
	}
	device_->ipc_=create_ipc(ip?inet_addr(ip):0,port,dev,EMC_NONE,EMC_LOCAL);
	if(!device_->ipc_){
		return -1;
	}
	return 0;
}

int emc_connect(int dev,ushort mode,const char *ip,const ushort port){
	struct device *device_=(struct device *)global_get_device(dev);
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
		device_->ipc_=create_ipc(inet_addr(ip),port,dev,mode,EMC_REMOTE);
		if(!device_->ipc_){
			return -1;
		}
	}else{
		device_->tcp_=create_tcp(inet_addr(ip),port,dev,mode,EMC_REMOTE);
		if(!device_->tcp_){
			delete_tcp(device_->tcp_);
			device_->tcp_=NULL;
			return -1;
		}
	}
	return 0;
}

int emc_control(int dev,int id,int ctl){
	int result=-1;
	struct device *device_=(struct device *)global_get_device(dev);
	if(!device_){
		return -1;
	}
	if(!(device_->operate & EMC_OPT_CONTROL)) return -1;
	if(ctl & EMC_CTL_CLOSE){
		if(device_->ipc_){
			if(0==close_ipc(device_->ipc_,id)){
				result=0;
			}
		}
		if(device_->tcp_){
			if(0==close_tcp(device_->tcp_,id)){
				result=0;
			}
		}
	}
	return result;
}

int emc_close(int dev){
	struct device *device_=(struct device *)global_get_device(dev);
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

int emc_send(int dev, void *msg,int flag){
	int result=-1;
	struct device *device_=(struct device *)global_get_device(dev);
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

int emc_recv(int dev,void **msg){
	struct device *device_=(struct device *)global_get_device(dev);
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

int emc_monitor(int dev,struct monitor_data *data){
	struct monitor_data *md=NULL;
	struct device *device_=(struct device *)global_get_device(dev);
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

ushort get_device_mode(int dev){
	struct device *device_=(struct device *)global_get_device(dev);
	if(!device_){
		return -1;
	}
	return device_->mode;
}

int push_device_message(int dev,void *msg){
	struct device *device_=(struct device *)global_get_device(dev);
	if(!device_){
		return -1;
	}
	if(push_ringqueue(device_->mq,msg) < 0){
		return -1;
	}
	return 0;
}

int push_device_event(int dev,void *data){
	struct device *device_=(struct device *)global_get_device(dev);
	if(!device_){
		return -1;
	}
	if(push_ringqueue(device_->mmq,data) < 0){
		return -1;
	}
	return 0;
}


uint get_device_monitor(int dev){
	struct device *device_=(struct device *)global_get_device(dev);
	if(!device_){
		return 0;
	}
	return (device_->operate & EMC_OPT_MONITOR);
}

uint get_device_control(int dev){
	struct device *device_=(struct device *)global_get_device(dev);
	if(!device_){
		return 0;
	}
	return (device_->operate & EMC_OPT_CONTROL);
}
