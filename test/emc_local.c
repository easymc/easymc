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

struct para{
	int device;
	int exit;
};

static emc_result_t EMC_CALL OnRecvMsg(void *p){
	void *msg=NULL;
	struct para *pa=(struct para *)p;
	int device=pa->device;
	while(!pa->exit){
		if(0==emc_recv(device,(void **)&msg)){
//			printf("recv length=%ld\n",emc_msg_length(msg));
			emc_msg_set_mode(msg,EMC_REQ);
			emc_send(device,msg,0);
			emc_msg_free(msg);
		}
	}
	printf("OnRecvMsg exit\n");
	return 0;
}

static emc_result_t EMC_CALL OnMonitorDevice(void *p){
	struct para *pa=(struct para *)p;
	int device=pa->device;
	struct monitor_data data={0};
	while(!pa->exit){
		if(0==emc_monitor(device,&data)){
			switch(data.events){
			case EMC_EVENT_ACCEPT:
				printf("client connected server,ip=%s,port=%ld,id=%ld\n",data.ip,data.port,data.id);
				break;
			case EMC_EVENT_CLOSED:
				printf("client disconnected server,ip=%s,port=%ld,id=%ld\n",data.ip,data.port,data.id);
				break;
			case EMC_EVENT_SNDSUCC:
//				printf("server send successful,ip=%s,port=%ld,id=%ld\n",data.ip,data.port,data.id);
				break;
			case EMC_EVENT_SNDFAIL:
//				printf("server send failed,ip=%s,port=%ld,id=%ld\n",data.ip,data.port,data.id);
				break;
			}
		}
	}

	return 0;
}

int main(int argc, char* argv[]){
	struct para pa={0};
	int monitor=1,ch=0;
	int device=emc_device();
	pa.device=device;
	pa.exit=0;
	emc_set(device,EMC_OPT_MONITOR|EMC_OPT_CONTROL,&monitor,sizeof(int));
	printf("Input a port to bind:");
	scanf("%ld",&ch);
	if(emc_bind(device,NULL,ch) < 0){
		printf("emc_bing fail\n");
	}
	emc_thread(OnRecvMsg,(void *)&pa);
	emc_thread(OnMonitorDevice,(void *)&pa);
	printf("Input C or c to close a connection,Q or q to exit\n");
	while(1){
		ch=getchar();
		if('C'==ch || 'c'==ch){
			printf("Input connection id:");
			scanf("%ld",&ch);
			emc_control(device,ch,EMC_CTL_CLOSE);
		}else if('Q'==ch || 'q'==ch){
			break;
		}
	}
	getchar();
	getchar();
	pa.exit=1;
	emc_destory(device);
	getchar();
	return 0;
}
