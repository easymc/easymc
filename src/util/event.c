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
#include "event.h"

#pragma pack(1)

struct event{
#ifdef EMC_WINDOWS
	HANDLE	evt;
#else
	sem_t	evt;
	pthread_mutex_t lock;
	pthread_cond_t	cond;
#endif
	int timeout;
};

#pragma pack()

struct event* create_event(){
	struct event *evt=(struct event *)malloc(sizeof(struct event));
#ifdef EMC_WINDOWS
	evt->evt=CreateEvent(NULL,FALSE,FALSE,NULL);
#else
	sem_init(&evt->evt,0,0);
	pthread_mutex_init(&evt->lock,NULL);
	pthread_cond_init(&evt->cond,NULL);
#endif
	return evt;
}

void delete_event(struct event *evt){
#ifdef EMC_WINDOWS
	CloseHandle(evt->evt);
#else
	sem_destroy(&evt->evt);
	pthread_mutex_destroy(&evt->lock);
	pthread_cond_destroy(&evt->cond);
#endif
	free(evt);
}

int wait_event(struct event *evt,int timeout){
	int status=-1;
#ifdef EMC_WINDOWS
	status=WaitForSingleObject(evt->evt,timeout>=0?timeout:INFINITE);
	if(WAIT_OBJECT_0==status){
		ResetEvent(evt->evt);
		return 0;
	}
	else if(WAIT_TIMEOUT==status){
		ResetEvent(evt->evt);
		return 1;
	}
	return -1;
#else
	evt->timeout=timeout;
	if(timeout < 0){
		return sem_wait(&evt->evt);
	}
	pthread_mutex_lock(&evt->lock);
	if(timeout>0){
		struct timespec tp={time(NULL)+timeout/1000,(timeout%1000)*1000*1000};
		status=pthread_cond_timedwait(&evt->cond,&evt->lock,&tp);
	}
	else{
		status=pthread_cond_wait(&evt->cond,&evt->lock);
	}
	pthread_mutex_unlock(&evt->lock);
	if(0==status){
		return 0;
	}
	else if(ETIMEDOUT==status){
		return 1;
	}
	return -1;
#endif
}

int post_event(struct event *evt){
#ifdef EMC_WINDOWS
	if(!SetEvent(evt->evt))
		return -1;
	return 0;
#else
	if(evt->timeout < 0){
		sem_post(&evt->evt);
	}else{
		pthread_mutex_lock(&evt->lock);
		pthread_cond_broadcast(&evt->cond);
		pthread_mutex_unlock(&evt->lock);
	}
	return 0;
#endif
}
