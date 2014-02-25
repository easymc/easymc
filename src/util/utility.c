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

#include "../config.h"
#include "../emc.h"
#include "thread.h"
#include "utility.h"


#if !defined (EMC_WINDOWS)
unsigned int timeGetTime(){
	unsigned int uptime = 0;
	struct timespec on;
	if(clock_gettime(CLOCK_MONOTONIC, &on) == 0)
		uptime = on.tv_sec*1000 + on.tv_nsec/1000000;
	return uptime;
}
#endif

int create_thread(emc_thread_cb * cb, void * args){
	thread_t thrd = 0;
	return thread_create(&thrd, NULL, cb, args);
}

int emc_thread(emc_thread_cb * cb, void * args){
	return create_thread(cb, args);
}

unsigned int get_thread_id(){
#if defined (EMC_WINDOWS)
	return GetCurrentThreadId();
#else
	return pthread_self();
#endif
}

unsigned int check_local_machine(int ip){
	char name[PATH_LEN] = {0};
	struct hostent * ent = NULL;
	int index = 0;

	if(LOOPBACK == ip) return 1;
	if(gethostname(name, PATH_LEN) < 0){
		return 0;
	}
	ent = gethostbyname(name);
	if(!ent){
		return 0;
	}
	for(index=0; ent->h_addr_list[index]; index++){
		if(ip == ((struct sockaddr_in *)ent->h_addr_list[index])->sin_addr.s_addr){
			return 1;
		}
	}
	return 0;
}

//获取服务器的cpu个数
uint get_cpu_num(){
#if defined (EMC_WINDOWS)
	SYSTEM_INFO sysInfo = {0};
	GetSystemInfo(&sysInfo);
	return sysInfo.dwNumberOfProcessors/2;
#else
	return 2*sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

//微妙级等待
void micro_wait(int64 microseconds){
#if defined (EMC_WINDOWS)
	LARGE_INTEGER  timestop; 
	LARGE_INTEGER  timestart; 
	LARGE_INTEGER  freq; 
	ULONG  timeout;

	if(!QueryPerformanceFrequency(&freq))
		return;
	timeout = freq.QuadPart*microseconds/1000/1000; 
	QueryPerformanceCounter(&timestart); 
	timestop = timestart; 
	while(timestop.QuadPart-timestart.QuadPart < timeout){ 
		QueryPerformanceCounter(&timestop); 
	}
#else
	struct timespec ts = {0,microseconds};
	nanosleep(&ts, NULL);
#endif
}
