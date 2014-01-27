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

#include <stdio.h>   
#include <stdlib.h>
#include <memory.h>
#include "../src/emc.h"

static uint send_count=0,recv_count=0;

struct para{
	int device;
	int exit;
};

static emc_result_t EMC_CALL OnRecvMsg(void *p){
	struct para *pa=(struct para *)p;
	int device=pa->device;
	void *msg=NULL;
	while(!pa->exit){
		if(0==emc_recv(device,(void **)&msg)){
			recv_count++;
			printf("recv length=%ld\n",emc_msg_length(msg));
			emc_msg_set_mode(msg,EMC_REQ);
			if(0==emc_send(device,msg,EMC_NOWAIT)){
				send_count++;
			}
			emc_msg_free(msg);
		}
	}
	return 0;
}

static emc_result_t EMC_CALL OnMonitorDevice(void *p){
	struct para *pa=(struct para *)p;
	int device=pa->device;
	struct monitor_data data={0};
	while(!pa->exit){
		if(0==emc_monitor(device,&data)){
			switch(data.events){
			case EMC_EVENT_CONNECT:
				printf("client connected server,ip=%s,port=%ld,id=%ld\n",data.ip,data.port,data.id);
				break;
			case EMC_EVENT_CLOSED:
				printf("client disconnected server,ip=%s,port=%ld,id=%ld\n",data.ip,data.port,data.id);
				break;
			case EMC_EVENT_SNDSUCC:
//				printf("server send successful,ip=%s,port=%ld,id=%ld\n",data.ip,data.port,data.id);
				break;
			case EMC_EVENT_SNDFAIL:
				printf("server send failed,ip=%s,port=%ld,id=%ld\n",data.ip,data.port,data.id);
				break;
			}
		}
	}

	return 0;
}

int main(int argc, char* argv[]){
	int ch=0;int device=-1;
	char ip[16]={0};
	char buffer[1024*7]={0};
	int monitor=1,length=0,port=0;
	void *msg=NULL;void *msg_=NULL;
	struct para pa={0};

	device=emc_device();
	pa.device=device;
	pa.exit=0;
	printf("Input serve ip:");
	scanf("%s",ip);
	printf("Input server port:");
	scanf("%ld",&port);
	emc_thread(OnMonitorDevice,(void *)&pa);
	emc_set(device,EMC_OPT_MONITOR,&monitor,sizeof(int));
	emc_connect(device,EMC_REQ,ip,port);
	emc_thread(OnRecvMsg,(void *)&pa);
	printf("Input send data length[Bytes]:");
	scanf("%ld",&length);
	while(1){
		printf("Type S or s to send data and type Q or q to quit,type P or p to display the data length transceiver\n");
		ch=getchar();
		if('S'==ch || 's'==ch){
			msg=emc_msg_alloc(buffer,length);
			emc_msg_set_mode(msg,EMC_REQ);
			if(emc_send(device,msg,0)==0){
				send_count++;
			}
			emc_msg_free(msg);
		}else if('Q'==ch || 'q'==ch){
			pa.exit=1;
			break;
		}else if('P'==ch || 'p'==ch){
			printf("send=%ld,recv=%ld\n",send_count,recv_count);
		}
	}
// 	msg=emc_msg_alloc(buffer,1024*7);
// 	emc_msg_set_mode(msg,EMC_REQ);
// 	while(1){
// 		emc_send(device,msg,EMC_NOWAIT);
// 		Sleep(1);
// 	}
	getchar();
	getchar();
	pa.exit=1;
	emc_destory(device);
	return 0;
}
