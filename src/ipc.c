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
#include "util/map.h"
#include "util/merger.h"
#include "util/heap.h"
#include "util/utility.h"
#include "util/ringqueue.h"
#include "util/ringbuffer.h"
#include "util/ringarray.h"
#include "global.h"
#include "common.h"
#include "device.h"
#include "msg.h"
#include "ipc.h"

#define IPC_HEAP_SIZE		(1024)

// IPC server allows a maximum 8 ipc connection
#define IPC_MAX_REMOTE		(8)

#define IPC_BUFFER_SIZE		(0x9000E8)
#define IPC_PEER_SIZE		(0x100014)
#define IPC_DATA_SIZE		(0x100000)
#define IPC_TIMEOUT			(5000)
#define IPC_TASK_TIMEOUT	(60000)
#define IPC_CHECK_TIMEOUT	(30000)

struct ipc_server{
	struct map	*connection;
	// ipc_client distributor
	struct heap	*client_heap;
};

struct ipc_client{
	// Server-assigned id
	int				id;
	// Communication mode
	ushort			mode;
	// Locally assigned id, for reconnection
	int				inid;
	// Serial connection
	int				locate;
	// Are already connected
	volatile uint	connected;
	// Last received data time 
	volatile uint	time;

	uint			evt_flag;
	// Transfer Handle
#if defined (EMC_WINDOWS)
	HANDLE			evt;
#else
	int				evt;
#endif
	// Shared memory buffer address
	char			*buffer;
};

struct ipc_data{
	int			id;
	int			flag;
	struct ipc	*ipc_;
	void		*msg;
};

struct ipc{
	// Device ID
	int					device;
	// IPC type: local / remote
	int					type;

#if defined (EMC_WINDOWS)
	HANDLE				fd;
	// data event
	HANDLE				evt;
#else
	int					fd;
	int					evt;
#endif
	// Shared memory buffer address
	char				*buffer;
		
	uint				ip;
	ushort				port;
	volatile	uint	exit;
	// Message received task list
	struct map			*rmap;
	// Send queue
	struct ringqueue	*sq;
	// Send queue data heap
	struct heap			*send_heap;
	struct	ipc_server	*server;
	struct	ipc_client	*client;
	struct ipc_client	*reconnect;
};

static int write_ipc_data(struct ipc *ipc_,struct ipc_client *client,int id,ushort cmd,char *data,int length);
static int reopen_ipc(struct ipc *ipc_);

static int ipc_read_wait(struct ipc *ipc_){
#if defined (EMC_WINDOWS)
	if(WAIT_OBJECT_0==WaitForSingleObject(ipc_->evt,INFINITE)){
#else
	struct sembuf sem_b={0,-1,0};
	if(0==semop(ipc_->evt,&sem_b,1)){
#endif
		return 0;
	}else{
#if !defined(EMC_WINDOWS)
		if(EINTR==errno){
			return 0;
		}
#endif
	}
	return -1;
}

static int ipc_read_post(struct ipc_client *client){
#if defined (EMC_WINDOWS)
	if(ReleaseSemaphore(client->evt,1,NULL)){
#else
	struct sembuf sem_b={0,1,0};
	if(0==semop(client->evt,&sem_b,1)){
#endif
		return 0;
	}
	return -1;
}

static int ipc_self_read_post(struct ipc *ipc_){
#if defined (EMC_WINDOWS)
	if(ReleaseSemaphore(ipc_->evt,1,NULL)){
#else
	struct sembuf sem_b={0,1,SEM_UNDO};
	if(0==semop(ipc_->evt,&sem_b,1)){
#endif
		return 0;
	}
	return -1;
}

// Login packet sent to the server
static int  ipc_send_register(struct ipc *ipc_,char *data,int len){	
	return write_ipc_data(ipc_,ipc_->client,-1,EMC_CMD_LOGIN,data,len);
}

// Send registration response packet to the client
static int ipc_send_register_bc(struct ipc *ipc_,struct ipc_client *client){
	return write_ipc_data(ipc_,client,client->id,EMC_CMD_LOGIN,NULL,0);
}

// Processing of data packets received
static void ipc_complete_data(struct ipc *ipc_,int id,ushort cmd,char *data,int len){
	struct ipc_client *client=NULL;
	void *msg=NULL;

	if(EMC_CMD_LOGIN==cmd){
		if(EMC_LOCAL==ipc_->type){
			char name[PATH_LEN]={0};
			ushort mode=*(ushort *)data;

			// Process new ipc connections
			client=(struct ipc_client *)heap_alloc(ipc_->server->client_heap);
			if(client){
				client->mode=mode;
				client->id=global_get_connect_id();
				client->evt_flag=*(uint *)(data+sizeof(ushort));
#if defined (EMC_WINDOWS)
				sprintf_s(name,PATH_LEN,"event_%ld_%ld",ipc_->port,*(uint *)(data+sizeof(ushort)));
				client->evt=OpenSemaphore(SEMAPHORE_ALL_ACCESS,TRUE,name);
				if(!client->evt){
					global_idle_connect_id(client->id);
					heap_free(ipc_->server->client_heap,client);
					return;
				}
#else
				client->evt=semget(*(uint *)(data+sizeof(ushort)),0,IPC_CREAT|0600);
				if(client->evt<0){
					global_idle_connect_id(client->id);
					heap_free(ipc_->server->client_heap,client);
					return ;
				}
#endif
				client->locate=*(int *)(data+sizeof(ushort)+sizeof(uint));
				if(client->locate < 0 || client->locate >= IPC_MAX_REMOTE){
#if defined (EMC_WINDOWS)
					CloseHandle(client->evt);
#else
					semctl(client->evt,0,IPC_RMID,NULL);
#endif
					return;
				}
				client->buffer=ipc_->buffer+sizeof(uint)+get_ringarray_size()+(client->locate+1)*IPC_PEER_SIZE;
				// Send to respond to the client
				if(ipc_send_register_bc(ipc_,client) < 0){
					global_idle_connect_id(client->id);
#if defined (EMC_WINDOWS)
					CloseHandle(client->evt);
#else
					semctl(client->evt,0,IPC_RMID,NULL);
#endif
					heap_free(ipc_->server->client_heap,client);
				}else{
					client->connected=1;
					client->time=timeGetTime();
					if(map_add(ipc_->server->connection,client->id,client) < 0){
						client->connected=0;
						global_idle_connect_id(client->id);
#if defined (EMC_WINDOWS)
						CloseHandle(client->evt);
#else
						semctl(client->evt,0,IPC_RMID,NULL);
#endif
						heap_free(ipc_->server->client_heap,client);
					}
				}
				if(get_device_monitor(ipc_->device)){
					// If you set the monitor to throw on accept events
					struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
					if(md){
						md->events=EMC_EVENT_ACCEPT;
						md->id=client->id;
						strncpy(md->ip,"127.0.0.1",ADDR_LEN);
						md->port=(int)client->evt;
						push_device_event(ipc_->device,md);
					}
				}
			}
		}else if(EMC_REMOTE==ipc_->type){
			// Processing ipc server response id number
			ipc_->client->id=id;
			ipc_->client->connected=1;
			ipc_->reconnect=NULL;
			if(ipc_->client->inid >= 0){
				global_idle_connect_id(ipc_->client->inid);
				ipc_->client->inid=-1;
			}
			if(get_device_monitor(ipc_->device)){
				// If you set the monitor to throw on connect events
				struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
				if(md){
					md->events=EMC_EVENT_CONNECT;
					md->id=ipc_->client->id;
					strncpy(md->ip,"127.0.0.1",ADDR_LEN);
					md->port=(int)ipc_->fd;
					push_device_event(ipc_->device,md);
				}
			}
		}
	}else if(EMC_CMD_LOGOUT==cmd){
		if(EMC_LOCAL==ipc_->type){
			if(0==map_get(ipc_->server->connection,id,(void **)&client)){
				client->connected=0;
#if defined (EMC_WINDOWS)
				if(client->evt){
					CloseHandle(client->evt);client->evt=NULL;
				}
#else
				close(client->evt);client->evt=-1;
#endif
				if(0==map_erase(ipc_->server->connection,id)){
					if(get_device_monitor(ipc_->device)){
						// If you set the monitor to throw on disconnect events
						struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
						if(md){
							md->events=EMC_EVENT_CLOSED;
							md->id=client->id;
							strncpy(md->ip,"127.0.0.1",ADDR_LEN);
							md->port=(int)client->evt;
							push_device_event(ipc_->device,md);
						}
					}
					push_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),client->locate);
					global_idle_connect_id(id);
					heap_free(ipc_->server->client_heap,client);
				}
			}
		}else if(EMC_REMOTE==ipc_->type){
			ipc_->client->connected=0;
		}
	}else if(EMC_CMD_DATA==cmd){
		msg=emc_msg_alloc(data,len);
		if(EMC_LOCAL==ipc_->type){
			if(0==map_get(ipc_->server->connection,id,(void **)&client)){
				client->time=timeGetTime();
				if(msg){
					emc_msg_setid(msg,id);
					emc_msg_set_mode(msg,client->mode);
				}
			}
		}else if(EMC_REMOTE==ipc_->type){
			ipc_->client->time=timeGetTime();
			if(msg){
				emc_msg_setid(msg,id);
				emc_msg_set_mode(msg,get_device_mode(ipc_->device));
			}
		}
		if(msg){
			if(push_device_message(ipc_->device,msg) < 0){
				emc_msg_free(msg);
			}
		}
	}
}

// ipc data consolidation callback
static void ipc_merger_cb(char* data,int len,int id,void* addition){
	struct ipc *ipc_=(struct ipc *)addition;
	ipc_complete_data(ipc_,id,EMC_CMD_DATA,data,len);
}

static uint ipc_term_foreach_cb(struct map *m,int64 key,void* p,void* addition){
	struct ipc_client *client=(struct ipc_client *)p;
#if defined (EMC_WINDOWS)
	if(client->evt){
		CloseHandle(client->evt);
		client->evt=NULL;
	}
#else
	if(client->evt >= 0){
		close(client->evt);
	}
#endif
	return 0;
}

static void term_ipc(struct ipc *ipc_){
#if defined (EMC_WINDOWS)
	if(ipc_->buffer){
		if(EMC_LOCAL==ipc_->type){
			UnmapViewOfFile(ipc_->buffer);
		}else if(EMC_REMOTE==ipc_->type){
			UnmapViewOfFile(ipc_->client->buffer);
		}
		ipc_->buffer=NULL;
	}
	if(ipc_->fd){
		CloseHandle(ipc_->fd);ipc_->fd=NULL;
	}
	if(ipc_->evt){
		CloseHandle(ipc_->evt);ipc_->evt=NULL;
	}
#else
	struct shmid_ds mds;
	if(EMC_LOCAL==ipc_->type){
		shmdt(ipc_->buffer);ipc_->buffer=NULL;
	}else if(EMC_REMOTE==ipc_->type){
		shmdt(ipc_->client->buffer);ipc_->client->buffer=NULL;
	}
	if(0==shmctl(ipc_->fd,IPC_STAT,&mds)){
		if(mds.shm_nattch <= 0){
			shmctl(ipc_->fd,IPC_RMID,NULL);ipc_->fd=-1;
			if(EMC_REMOTE==ipc_->type){
				semctl(ipc_->client->evt,IPC_RMID,NULL);ipc_->evt=-1;
			}
		}
	}
	semctl(ipc_->evt,0,IPC_RMID,NULL);ipc_->evt=-1;
#endif
	if(EMC_LOCAL==ipc_->type){
		map_foreach(ipc_->server->connection,ipc_term_foreach_cb,ipc_);
	}else if(EMC_REMOTE==ipc_->type){
#if defined (EMC_WINDOWS)
		if(ipc_->client->evt){
			CloseHandle(ipc_->client->evt);ipc_->client->evt=NULL;
		}
#endif
	}
}

static int reopen_ipc(struct ipc *ipc_){
	char buffer[2*sizeof(uint)+sizeof(ushort)]={0};

	if(EMC_REMOTE==ipc_->type){
#if defined (EMC_WINDOWS)
		char name[PATH_LEN]={0};
		if(ipc_->client->id >= 0){
			// If you set the monitor to throw on disconnect events
			if(get_device_monitor(ipc_->device)){
				struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
				if(md){
					md->events=EMC_EVENT_CLOSED;
					md->id=ipc_->client->id;
					strncpy(md->ip,"127.0.0.1",ADDR_LEN);
					md->port=(int)ipc_->fd;
					push_device_event(ipc_->device,md);
				}
			}
			ipc_->client->id=-1;
		}
		ipc_->client->connected=0;
		if(ipc_->client->evt){
			CloseHandle(ipc_->client->evt);
			ipc_->client->evt=NULL;
		}
		if(!ipc_->fd){
			sprintf_s(name,PATH_LEN,"ipc_%ld",ipc_->port);
			ipc_->fd=OpenFileMapping(FILE_MAP_READ|FILE_MAP_WRITE,TRUE,name);
			if(!ipc_->fd){
				return -1;
			}
			ipc_->client->buffer=ipc_->buffer=(char *)MapViewOfFile(ipc_->fd,FILE_MAP_READ|FILE_MAP_WRITE,0,0,0);
			if(!ipc_->buffer){
				CloseHandle(ipc_->fd);
				ipc_->fd=NULL;
				return -1;
			}
			ipc_->client->locate=-1;
			if(pop_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),(void *)&ipc_->client->locate) < 0){
				UnmapViewOfFile(ipc_->buffer);
				CloseHandle(ipc_->fd);
				ipc_->buffer=NULL;
				ipc_->fd=NULL;
				return -1;
			}
			if(ipc_->client->locate < 0){
				UnmapViewOfFile(ipc_->buffer);
				CloseHandle(ipc_->fd);
				ipc_->buffer=NULL;
				ipc_->fd=NULL;
				return -1;
			}
			printf("locate=%ld\n",ipc_->client->locate);
			ipc_->buffer=ipc_->buffer+sizeof(uint)+get_ringarray_size()+IPC_PEER_SIZE*(ipc_->client->locate+1);
		}
		sprintf_s(name,PATH_LEN,"event_%ld",ipc_->port);
		ipc_->client->evt=OpenSemaphore(SEMAPHORE_ALL_ACCESS,TRUE,name);
		if(!ipc_->client->evt){
			return -1;
		}
		printf("locate=%ld\n",ipc_->client->locate);
		if(ipc_->buffer){
			*(uint *)ipc_->buffer=timeGetTime();
		}
		*(int *)(buffer+sizeof(ushort)+sizeof(uint))=ipc_->client->locate;
		init_ringbuffer((struct ringbuffer *)(ipc_->buffer+sizeof(uint)));
#else
		if(ipc_->client->id >= 0){
			// If you set the monitor to throw on disconnect events
			if(get_device_monitor(ipc_->device)){
				struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
				if(md){
					md->events=EMC_EVENT_CLOSED;
					md->id=ipc_->client->id;
					strncpy(md->ip,"127.0.0.1",ADDR_LEN);
					md->port=(int)ipc_->fd;
					push_device_event(ipc_->device,md);
				}
			}
			ipc_->client->id=-1;
		}
		ipc_->client->connected=0;
		if(ipc_->client->evt >= 0){
			semctl(ipc_->client->evt,0,IPC_RMID,NULL);
			ipc_->client->evt=-1;
		}
		if(ipc_->fd < 0){
			ipc_->fd=shmget(ipc_->port,0,IPC_CREAT|0666);
			if(ipc_->fd<0){
				return -1;
			}
			ipc_->client->buffer=ipc_->buffer=(char *)shmat(ipc_->fd,NULL,0);
			if(!ipc_->buffer){
				shmctl(ipc_->fd,IPC_RMID,NULL);
				ipc_->fd=-1;
				return -1;
			}
			ipc_->client->locate=-1;
			if(pop_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),(void *)&ipc_->client->locate) < 0){
				shmdt(ipc_->buffer);
				ipc_->buffer=NULL;
				close(ipc_->fd);
				semctl(ipc_->evt,IPC_RMID,NULL);
				ipc_->fd=-1;
				return -1;
			}
			if(ipc_->client->locate < 0){
				shmdt(ipc_->buffer);
				ipc_->buffer=NULL;
				close(ipc_->fd);
				semctl(ipc_->evt,IPC_RMID,NULL);
				ipc_->fd=-1;
				return -1;
			}
			printf("locate=%ld\n",ipc_->client->locate);
			ipc_->buffer=ipc_->buffer+sizeof(uint)+get_ringarray_size()+IPC_PEER_SIZE*(ipc_->client->locate+1);
		}
		ipc_->client->evt=semget(ipc_->port+0xFFFF,0,IPC_CREAT|0600);
		if(ipc_->client->evt<0){
			return -1;
		}
		if(ipc_->buffer){
			*(uint *)ipc_->buffer=timeGetTime();
		}
		*(int *)(buffer+sizeof(ushort)+sizeof(uint))=ipc_->client->locate;
		init_ringbuffer((struct ringbuffer *)(ipc_->buffer+sizeof(uint)));
#endif
		// Login packet sent to the server
		*(ushort *)buffer=ipc_->client->mode;
		*(uint *)(buffer+sizeof(ushort))=ipc_->client->evt_flag;
		*(int *)(buffer+sizeof(ushort)+sizeof(uint))=ipc_->client->locate;
		if(ipc_send_register(ipc_,buffer,2*sizeof(uint)+sizeof(ushort)) < 0){
			return -1;
		}
	}
	return 0;
}

static int process_ipc_data(struct ipc *ipc_,char *data){
	if(((struct data_unit *)data)->len==((struct data_unit *)data)->total){
		ipc_complete_data(ipc_,((struct data_unit *)data)->id,((struct data_unit *)data)->cmd,data+sizeof(struct data_unit),
			((struct data_unit *)data)->len);
	}else{
		void *mg=NULL;
		union data_serial serial={0};

		serial.id=((struct data_unit *)data)->id;
		serial.serial=((struct data_unit *)data)->serial;
		if(map_get(ipc_->rmap,serial.no,(void**)&mg) < 0){
			mg=global_alloc_merger();
			if(!mg) return -1;
			merger_init(mg,((struct data_unit *)data)->total);
			map_add(ipc_->rmap,serial.no,mg);
		}
		if(mg){
			merger_add(mg,((struct data_unit *)data)->no*(MAX_DATA_SIZE-sizeof(struct data_unit)),data+sizeof(struct data_unit),
				((struct data_unit *)data)->len);
			if(0==merger_get(mg,ipc_merger_cb,((struct data_unit *)data)->id,ipc_)){
				union data_serial serial={0};
				serial.id=((struct data_unit *)data)->id;
				serial.serial=((struct data_unit *)data)->serial;
				global_free_merger(mg);
				map_erase(ipc_->rmap,serial.no);
			}
		}
	}
	return 0;
}

static int read_ipc(struct ipc *ipc_){
	char buffer[MAX_DATA_SIZE]={0};
	if(!ipc_) return -1;
	
	if(0==ipc_read_wait(ipc_)){
		if(EMC_LOCAL==ipc_->type){
			while(0==pop_ringbuffer((struct ringbuffer *)(ipc_->buffer+2*sizeof(uint)+get_ringarray_size()),buffer)){
				process_ipc_data(ipc_,buffer);
			}
		}else{
			while(0==pop_ringbuffer((struct ringbuffer *)(ipc_->buffer+sizeof(uint)),buffer)){
				process_ipc_data(ipc_,buffer);
			}
		}
	}
	return 0;
}

static int write_ipc_data(struct ipc *ipc_,struct ipc_client *client,int id,ushort cmd,char *data,int length){
	char buffer[MAX_DATA_SIZE]={0};

	if(MAX_DATA_SIZE>=(length+sizeof(struct data_unit))){
		// As long as you can send a packet to complete
		((struct data_unit *)buffer)->cmd=cmd;
		((struct data_unit *)buffer)->id=id;
		((struct data_unit *)buffer)->no=0;
		((struct data_unit *)buffer)->serial=global_get_data_serial();
		((struct data_unit *)buffer)->total=length;
		((struct data_unit *)buffer)->len=length;
		if(data && length){
			memcpy(buffer+sizeof(struct data_unit),data,length);
		}
		if(EMC_CMD_DATA!=cmd){
			if(EMC_LOCAL==ipc_->type){
				if(push_ringbuffer((struct ringbuffer *)(client->buffer+sizeof(uint)),
					buffer,length+sizeof(struct data_unit)) < 0){
						return -1;
				}
			}else if(EMC_REMOTE==ipc_->type){
				if(push_ringbuffer((struct ringbuffer *)(client->buffer+2*sizeof(uint)+get_ringarray_size()),
					buffer,length+sizeof(struct data_unit)) < 0){
					return -1;
				}
			}
		}else{
			while(client->connected){
				if(EMC_LOCAL==ipc_->type){
					if(0==push_ringbuffer((struct ringbuffer *)(client->buffer+sizeof(uint)),
						buffer,length+sizeof(struct data_unit))){
							break;
					}
				}else if(EMC_REMOTE==ipc_->type){
					if(0==push_ringbuffer((struct ringbuffer *)(client->buffer+2*sizeof(uint)+get_ringarray_size()),
						buffer,length+sizeof(struct data_unit))){
						break;
					}
				}
				ipc_read_post(client);
				micro_wait(1);
			}
		}
		ipc_read_post(client);
	}else{
		// Multi-packet transmission
		uint package=0,index=0;

		((struct data_unit *)buffer)->cmd=EMC_CMD_DATA;
		((struct data_unit *)buffer)->id=id;
		((struct data_unit *)buffer)->serial=global_get_data_serial();
		((struct data_unit *)buffer)->total=length;
		package=length/(MAX_DATA_SIZE-sizeof(struct data_unit));
		package+=length%(MAX_DATA_SIZE-sizeof(struct data_unit))?1:0;
		for(index=0;index<package;index++){
			((struct data_unit *)buffer)->no=index;
			((struct data_unit *)buffer)->len=length>(MAX_DATA_SIZE-sizeof(struct data_unit))?(MAX_DATA_SIZE-sizeof(struct data_unit)):length;
			memcpy(buffer+sizeof(struct data_unit),data,((struct data_unit *)buffer)->len);
			while(client->connected){
				if(EMC_LOCAL==ipc_->type){
					if(0==push_ringbuffer((struct ringbuffer *)(client->buffer+sizeof(uint)),
						buffer,((struct data_unit *)buffer)->len+sizeof(struct data_unit))){
							break;
					}
				}else if(EMC_REMOTE==ipc_->type){
					if(0==push_ringbuffer((struct ringbuffer *)(client->buffer+2*sizeof(uint)+get_ringarray_size()),
						buffer,((struct data_unit *)buffer)->len+sizeof(struct data_unit))){
						break;
					}
				}
				ipc_read_post(client);
				micro_wait(1);
			}
			length-=((struct data_unit *)buffer)->len;
			data+=((struct data_unit *)buffer)->len;
		}
		ipc_read_post(client);
	}
	return 0;
}

static uint ipc_send_pub_foreach_cb(struct map *m,int64 key,void* p,void* addition){
	if(EMC_SUB==((struct ipc_client *)p)->mode){
		struct ipc_data *unit=(struct ipc_data *)addition;
		struct ipc *ipc_=unit->ipc_;
		struct ipc_data *data=NULL;

		data=(struct ipc_data *)heap_alloc(ipc_->send_heap);
		if(data){
			data->id=key;
			data->ipc_=ipc_;
			data->flag=unit->flag;
			data->msg=unit->msg;
			emc_msg_ref_add(unit->msg);
			if(push_ringqueue(ipc_->sq,data) < 0){
				emc_msg_ref_dec(unit->msg);
				heap_free(ipc_->send_heap,data);
			}
		}
	}
	return 0;
}

static int write_ipc(struct ipc *ipc_,void *msg,int flag){
	int length=0;
	ushort remain=0;
	struct ipc_client *client=NULL;

	if(!ipc_ || !msg)return -1;
	if(EMC_LOCAL==ipc_->type){
		if(EMC_REQ==emc_msg_get_mode(msg)){
			emc_msg_set_mode(msg,EMC_REP);
		}
		if(EMC_SUB==emc_msg_get_mode(msg)){
			return -1;
		}
	}
	switch(emc_msg_get_mode(msg)){
	case EMC_REQ:
	case EMC_SUB:
		{
			if(write_ipc_data(ipc_,ipc_->client,ipc_->client->id,EMC_CMD_DATA,
				(char *)emc_msg_buffer(msg),emc_msg_length(msg)) < 0){
				// If you set the monitor to throw on send failure events
				if(get_device_monitor(ipc_->device)){
					struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
					if(md){
						md->events=EMC_EVENT_SNDFAIL;
						md->id=ipc_->client->id;
						strncpy(md->ip,"127.0.0.1",ADDR_LEN);
						md->port=(int)ipc_->fd;
						md->addition=emc_msg_get_addition(msg);
						push_device_event(ipc_->device,md);
					}
				}
			}else{
				// If you set the monitor to throw on send succress events
				if(get_device_monitor(ipc_->device)){
					struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
					if(md){
						md->events=EMC_EVENT_SNDSUCC;
						md->id=ipc_->client->id;
						strncpy(md->ip,"127.0.0.1",ADDR_LEN);
						md->port=(int)ipc_->fd;
						md->addition=emc_msg_get_addition(msg);
						push_device_event(ipc_->device,md);
					}
				}
			}
		}
		break;
	case EMC_REP:
		{
			if(map_get(ipc_->server->connection,emc_msg_getid(msg),(void**)&client) < 0){
				return -1;
			}
			if(write_ipc_data(ipc_,client,emc_msg_getid(msg),EMC_CMD_DATA,
				(char *)emc_msg_buffer(msg),emc_msg_length(msg)) < 0){
				// If you set the monitor to throw on send failure events
				if(get_device_monitor(ipc_->device)){
					struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
					if(md){
						md->events=EMC_EVENT_SNDFAIL;
						md->id=client->id;
						strncpy(md->ip,"127.0.0.1",ADDR_LEN);
						md->port=(int)client->evt;
						md->addition=emc_msg_get_addition(msg);
						push_device_event(ipc_->device,md);
					}
				}
			}else{
				// If you set the monitor to throw on send success events
				if(get_device_monitor(ipc_->device)){
					struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
					if(md){
						md->events=EMC_EVENT_SNDSUCC;
						md->id=client->id;
						strncpy(md->ip,"127.0.0.1",ADDR_LEN);
						md->port=(int)client->evt;
						md->addition=emc_msg_get_addition(msg);
						push_device_event(ipc_->device,md);
					}
				}
			}
		}
		break;
	case EMC_PUB:
		{
			struct ipc_data unit={0,flag,ipc_,msg};
			map_foreach(ipc_->server->connection,ipc_send_pub_foreach_cb,&unit);
		}
		break;
	}
	return 0;
}

static uint map_foreach_logout_cb(struct map *m,int64 key,void* p,void* addition){
	write_ipc_data((struct ipc *)addition,(struct ipc_client *)p,(int)key,EMC_CMD_LOGOUT,NULL,0);
	return 0;
}

static uint map_foreach_check_cb(struct map *m,int64 key,void* p,void* addition){
	struct ipc *ipc_=(struct ipc *)addition;
	struct ipc_client *client=(struct ipc_client *)p;
	int timeout=0;
	
	if(!client->connected) return 0;
	timeout=*(uint *)client->buffer;
	timeout=timeGetTime()-timeout;
	if(timeout > IPC_TIMEOUT){
		if(timeGetTime()-client->time > IPC_TIMEOUT){
			if(get_device_monitor(ipc_->device)){
				// If you set the monitor to throw on disconnect events
				struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
				if(md){
					md->events=EMC_EVENT_CLOSED;
					md->id=client->id;
					strncpy(md->ip,"127.0.0.1",ADDR_LEN);
					md->port=(int)client->evt;
					push_device_event(ipc_->device,md);
				}
			}
#if defined (EMC_WINDOWS)
			CloseHandle(client->evt);client->evt=NULL;
#else
			semctl(client->evt,IPC_RMID,NULL);client->evt=-1;
#endif
			if(0==map_erase_nonlock(m,key)){
				push_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),client->locate);
				global_idle_connect_id(client->id);
				client->connected=0;
				heap_free(ipc_->server->client_heap,client);
				return 1;
			}
		}
	}
	return 0;
}

static uint map_foreach_task_cb(struct map *m,int64 key,void* p,void* addition){
	if(timeGetTime()-merger_time(p) > IPC_TASK_TIMEOUT){
		if(0==map_erase_nonlock(m,key)){
			global_free_merger(p);
			return 1;
		}
	}
	return 0;
}

static void check_ipc(struct ipc *ipc_,uint *reconnect_time){
	if(EMC_LOCAL==ipc_->type){
		if(ipc_->buffer){
			*(uint *)(ipc_->buffer+sizeof(uint)+get_ringarray_size())=timeGetTime();
		}
		map_foreach(ipc_->server->connection,map_foreach_check_cb,ipc_);
	}else if(EMC_REMOTE==ipc_->type){
		if(ipc_->buffer){
			*(uint *)ipc_->buffer=timeGetTime();
		}
		if(ipc_->client->connected){
			int timeout=*(uint *)(ipc_->client->buffer+sizeof(uint)+get_ringarray_size());
			timeout=timeGetTime()-timeout;
			if(timeout > IPC_TIMEOUT){
				if(timeGetTime()-ipc_->client->time > IPC_TIMEOUT){
					if(!ipc_->reconnect){
						ipc_->reconnect=ipc_->client;
						if(reopen_ipc(ipc_) < 0){
							ipc_->reconnect=NULL;
						}
						*reconnect_time=timeGetTime();
					}
				}
			}
		}else{
			if(!ipc_->reconnect || (timeGetTime()-*reconnect_time) >= IPC_TIMEOUT){
				ipc_->reconnect=ipc_->client;
				if(reopen_ipc(ipc_) < 0){
					ipc_->reconnect=NULL;
				}
				*reconnect_time=timeGetTime();
			}
		}
	}
}

static emc_result_t EMC_CALL  ipc_work_cb(void *args){
	struct ipc *ipc_=(struct ipc *)args;
	while(!ipc_->exit){
		read_ipc(ipc_);
	}
#if defined (EMC_WINDOWS)
	return 0;
#else
	return NULL;
#endif
}

static emc_result_t EMC_CALL ipc_check_cb(void *args){
	struct ipc *ipc_=(struct ipc *)args;
	uint check_time=timeGetTime(); 
	uint reconnect_time=timeGetTime();
	while(!ipc_->exit){
		check_ipc(ipc_,&reconnect_time);
		// Receive data every 30 seconds to detect whether the task timeout
		if(timeGetTime()-check_time > IPC_CHECK_TIMEOUT){
			map_foreach(ipc_->rmap,map_foreach_task_cb,ipc_);
			check_time=timeGetTime();
		}
		nsleep(100);
	}
#if defined (EMC_WINDOWS)
	return 0;
#else
	return NULL;
#endif
}

static emc_result_t EMC_CALL ipc_send_cb(void *args){
	struct ipc *ipc_=(struct ipc *)args;
	struct ipc_data *data=NULL;
	struct ipc_client *client=NULL;
	while(!ipc_->exit){
		wait_ringqueue(ipc_->sq);
		while(0==pop_ringqueue_multiple(ipc_->sq,(void **)&data)){
			if(EMC_LOCAL==ipc_->type){
				map_get(ipc_->server->connection,data->id,(void **)&client);
			}else if(EMC_REMOTE==ipc_->type){
				client=ipc_->client;
			}
			if(client){
				if(write_ipc_data(ipc_,client,client->id,EMC_CMD_DATA,(char *)emc_msg_buffer(data->msg),
					emc_msg_length(data->msg)) < 0){
						// If you set the monitor to throw on send failure events
						if(get_device_monitor(ipc_->device)){
							struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
							if(md){
								md->events=EMC_EVENT_SNDFAIL;
								md->id=client->id;
								strncpy(md->ip,"127.0.0.1",ADDR_LEN);
								md->port=(int)client->evt;
								md->addition=emc_msg_get_addition(data->msg);
								push_device_event(ipc_->device,md);
							}
						}
				}else{
					// If you set the monitor to throw on send success events
					if(get_device_monitor(ipc_->device)){
						struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
						if(md){
							md->events=EMC_EVENT_SNDSUCC;
							md->id=client->id;
							strncpy(md->ip,"127.0.0.1",ADDR_LEN);
							md->port=(int)client->evt;
							md->addition=emc_msg_get_addition(data->msg);
							push_device_event(ipc_->device,md);
						}
					}
				}
			}
			emc_msg_ref_dec(data->msg);
			if(EMC_NOWAIT==data->flag){
				emc_msg_free(data->msg);
			}
			heap_free(ipc_->send_heap,data);
		}
	}
#if defined (EMC_WINDOWS)
	return 0;
#else
	return NULL;
#endif
}

// ipc reconnection callback
static int ipc_reconnect_cb(void *p,void *addition){
	return reopen_ipc((struct ipc *)addition);
}

static int init_ipc_server(struct ipc *ipc_){
	int index=0;
#if defined (EMC_WINDOWS)
	char name[PATH_LEN]={0};
	sprintf_s(name,PATH_LEN,"ipc_%ld",ipc_->port);
	ipc_->fd=CreateFileMapping((HANDLE)-1,NULL,PAGE_READWRITE,0,IPC_BUFFER_SIZE,name);
	if(!ipc_->fd){
		return -1;
	}else{
		ipc_->buffer=(char *)MapViewOfFile(ipc_->fd,FILE_MAP_READ|FILE_MAP_WRITE,0,0,0);
		if(!ipc_->buffer){
			return -1;
		}
		if(ERROR_ALREADY_EXISTS!=GetLastError()){
			memset(ipc_->buffer,0,IPC_BUFFER_SIZE);
			init_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)));
			for(index=0;index<IPC_MAX_REMOTE;index++){
				push_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),index);
			}
		}
	}
	init_ringbuffer((struct ringbuffer *)(ipc_->buffer+get_ringarray_size()+2*sizeof(uint)));
	sprintf_s(name,PATH_LEN,"event_%ld",ipc_->port);
	ipc_->evt=CreateSemaphore(NULL,0,1,name);
	if(!ipc_->evt){
		printf("%ld\n",GetLastError());
		CloseHandle(ipc_->fd);
		return -1;
	}
#else
	ipc_->fd=shmget(ipc_->port,IPC_BUFFER_SIZE,IPC_CREAT|0666);
	if(ipc_->fd<0){
		return -1;
	}else{
		struct shmid_ds mds;
		if(0==shmctl(ipc_->fd,IPC_STAT,&mds)){
			if(mds.shm_nattch <= 0){
				printf("ipc not exist\n");
				ipc_->buffer=(char *)shmat(ipc_->fd,NULL,0);
				if(!ipc_->buffer){
					return -1;
				}
				memset(ipc_->buffer,0,IPC_BUFFER_SIZE);
				init_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)));
				for(index=0;index<IPC_MAX_REMOTE;index++){
					push_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),index);
				}
			}else{
				ipc_->buffer=(char *)shmat(ipc_->fd,NULL,0);
				if(!ipc_->buffer){
					return -1;
				}
			}
		}
	}
	init_ringbuffer((struct ringbuffer *)(ipc_->buffer+get_ringarray_size()+2*sizeof(uint)));
	ipc_->evt=semget(ipc_->port+0xFFFF,1,IPC_CREAT|0600);
	if(ipc_->evt<0){
		shmdt(ipc_->buffer);
		shmctl(ipc_->fd,IPC_RMID,NULL);
		close(ipc_->fd);
		ipc_->fd=-1;
		return -1;
	}
#endif
	ipc_->server->connection=create_map(EMC_SOCKETS_DEFAULT);
	ipc_->server->client_heap=heap_new(sizeof(struct ipc_client),IPC_HEAP_SIZE);
	return 0;
}

static int init_ipc_client(struct ipc *ipc_){
	uint flag=0;
	char buffer[2*sizeof(uint)+sizeof(ushort)]={0};
#if defined (EMC_WINDOWS)
	char name[PATH_LEN]={0};

	*(ushort *)buffer=ipc_->client->mode;
	flag=global_rand_number();
	sprintf_s(name,PATH_LEN,"event_%ld_%ld",ipc_->port,flag);
	while(!ipc_->evt){
		ipc_->evt=CreateSemaphore(NULL,0,1,name);
		if(!ipc_->evt){
			flag=global_rand_number();
			sprintf_s(name,PATH_LEN,"event_%ld_%ld",ipc_->port,flag);
		}else{
			if(ERROR_ALREADY_EXISTS==GetLastError()){
				CloseHandle(ipc_->evt);
				ipc_->evt=NULL;
				flag=global_rand_number();
				sprintf_s(name,PATH_LEN,"event_%ld_%ld",ipc_->port,flag);
			}
		}
	}
	printf("%ld\n",flag);
	ipc_->client->evt_flag=flag;
	*(uint *)(buffer+sizeof(ushort))=flag;
	// Try to open server shared memory 
	sprintf_s(name,PATH_LEN,"ipc_%ld",ipc_->port);
	ipc_->fd=OpenFileMapping(FILE_MAP_READ|FILE_MAP_WRITE,TRUE,name);
	if(ipc_->fd){
		ipc_->client->buffer=ipc_->buffer=(char *)MapViewOfFile(ipc_->fd,FILE_MAP_READ|FILE_MAP_WRITE,0,0,0);
		if(!ipc_->buffer){
			CloseHandle(ipc_->fd);
			ipc_->fd=NULL;
			return -1;
		}

		ipc_->client->locate=-1;
		if(pop_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),(void *)&ipc_->client->locate) < 0){
			UnmapViewOfFile(ipc_->buffer);
			CloseHandle(ipc_->fd);
			ipc_->buffer=NULL;
			ipc_->fd=NULL;
			return -1;
		}
		if(ipc_->client->locate < 0){
			UnmapViewOfFile(ipc_->buffer);
			CloseHandle(ipc_->fd);
			ipc_->buffer=NULL;
			ipc_->fd=NULL;
			return -1;
		}
		printf("locate=%ld\n",ipc_->client->locate);
		if(ipc_->buffer){
			ipc_->buffer=ipc_->buffer+sizeof(uint)+get_ringarray_size()+IPC_PEER_SIZE*(ipc_->client->locate+1);
			*(uint *)ipc_->buffer=timeGetTime();
		}
		*(int *)(buffer+sizeof(ushort)+sizeof(uint))=ipc_->client->locate;
		init_ringbuffer((struct ringbuffer *)(ipc_->buffer+sizeof(uint)));
		sprintf_s(name,PATH_LEN,"event_%ld",ipc_->port);
		ipc_->client->evt=OpenSemaphore(SEMAPHORE_ALL_ACCESS,TRUE,name);
		if(!ipc_->client->evt){
			UnmapViewOfFile(ipc_->client->buffer);
			ipc_->buffer=NULL;
			CloseHandle(ipc_->fd);
			ipc_->fd=NULL;
			return -1;
		}
	}
#else
	*(ushort *)buffer=ipc_->client->mode;
	flag=global_rand_number();
	while(ipc_->evt<0){
		ipc_->evt=semget(flag,1, IPC_CREAT|IPC_EXCL|0600);
		if(ipc_->evt<0){
			flag=global_rand_number();
		}
	}
	ipc_->client->evt_flag=flag;
	*(uint *)(buffer+sizeof(ushort))=flag;
	// Try to open server shared memory
	ipc_->fd=shmget(ipc_->port,0,IPC_CREAT|0666);
	if(ipc_->fd >= 0){
		ipc_->client->buffer=ipc_->buffer=(char *)shmat(ipc_->fd,NULL,0);
		if(!ipc_->buffer){
			shmdt(ipc_->buffer);
			ipc_->buffer=NULL;
			close(ipc_->fd);
			ipc_->fd=-1;
			semctl(ipc_->evt,IPC_RMID,NULL);
			return -1;
		}
		ipc_->client->locate=-1;
		if(pop_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),(void *)&ipc_->client->locate) < 0){
			shmdt(ipc_->buffer);
			ipc_->buffer=NULL;
			close(ipc_->fd);
			semctl(ipc_->evt,IPC_RMID,NULL);
			ipc_->fd=-1;
			return -1;
		}
		if(ipc_->client->locate < 0){
			shmdt(ipc_->buffer);
			ipc_->buffer=NULL;
			close(ipc_->fd);
			semctl(ipc_->evt,IPC_RMID,NULL);
			ipc_->fd=-1;
			return -1;
		}
		printf("locate=%ld\n",ipc_->client->locate);
		if(ipc_->buffer){
			ipc_->buffer=ipc_->buffer+sizeof(uint)+get_ringarray_size()+IPC_PEER_SIZE*(ipc_->client->locate+1);
			*(uint *)ipc_->buffer=timeGetTime();
		}
		*(int *)(buffer+sizeof(ushort)+sizeof(uint))=ipc_->client->locate;
		init_ringbuffer((struct ringbuffer *)(ipc_->buffer+sizeof(uint)));
		ipc_->client->evt=semget(ipc_->port+0xFFFF,0,IPC_CREAT|0600);
		if(ipc_->client->evt<0){
			shmdt(ipc_->client->buffer);
			ipc_->buffer=NULL;
			close(ipc_->fd);
			semctl(ipc_->evt,IPC_RMID,NULL);
			ipc_->fd=-1;
			return -1;
		}
	}
#endif
	// Login packet sent to the server
#if defined (EMC_WINDOWS)
	if(ipc_->fd && ipc_->client->evt){
#else
	if(-1!=ipc_->fd && -1!=ipc_->client->evt){
#endif
		if(ipc_send_register(ipc_,buffer,2*sizeof(uint)+sizeof(ushort)) <0){
			return -1;
		}
	}else{
		return -1;
	}
	return 0;
}

struct ipc *create_ipc(uint ip,ushort port,int device,unsigned short mode,int type){
	struct ipc *ipc_=(struct ipc *)malloc(sizeof(struct ipc));
	if(!ipc_)return NULL;
	memset(ipc_,0,sizeof(struct ipc));
	ipc_->ip=ip;
	ipc_->port=port;
	ipc_->type=type;
	ipc_->device=device;
	if(EMC_LOCAL==type){
		ipc_->server=(struct ipc_server *)malloc(sizeof(struct ipc_server));
		if(!ipc_->server){
			free(ipc_);
			return NULL;
		}
		memset(ipc_->server,0,sizeof(struct ipc_server));
#if !defined (EMC_WINDOWS)
		ipc_->fd=-1;
		ipc_->evt=-1;
#endif
		if(init_ipc_server(ipc_) < 0){
			free(ipc_->server);
			free(ipc_);
			return NULL;
		}
	}else if(EMC_REMOTE==type){
		ipc_->client=(struct ipc_client *)malloc(sizeof(struct ipc_client));
		if(!ipc_->client){
			term_ipc(ipc_);
			free(ipc_);
			return NULL;
		}
		memset(ipc_->client,0,sizeof(struct ipc_client));
#if !defined (EMC_WINDOWS)
		ipc_->fd=-1;
		ipc_->evt=-1;
		ipc_->client->evt=-1;
#endif
		ipc_->client->id=-1;
		ipc_->client->inid=global_get_connect_id();
		ipc_->client->mode=mode;
		ipc_->reconnect=ipc_->client;
		if(init_ipc_client(ipc_) < 0){
			ipc_->reconnect=NULL;
		}
	}
	ipc_->sq=create_ringqueue(_RQ_M);
	ipc_->rmap=create_map(EMC_SOCKETS_DEFAULT);
	ipc_->send_heap=heap_new(sizeof(struct ipc_data),EMC_SOCKETS_DEFAULT);
	create_thread(ipc_work_cb,ipc_);
	create_thread(ipc_check_cb,ipc_);
	create_thread(ipc_send_cb,ipc_);
	return ipc_;
}

void delete_ipc(struct ipc *ipc_){
	if(ipc_){
		ipc_->exit=1;
		ipc_self_read_post(ipc_);
		nsleep(100);
		if(EMC_LOCAL==ipc_->type){
			map_foreach(ipc_->server->connection,map_foreach_logout_cb,ipc_);
			term_ipc(ipc_);
			delete_map(ipc_->server->connection);
			heap_delete(ipc_->server->client_heap);
			free(ipc_->server);
		}else if(EMC_REMOTE==ipc_->type){
			write_ipc_data(ipc_,ipc_->client,ipc_->client->id,EMC_CMD_LOGOUT,NULL,0);
			term_ipc(ipc_);
			free(ipc_->client);
		}
		delete_ringqueue(ipc_->sq);
		delete_map(ipc_->rmap);
		heap_delete(ipc_->send_heap);
		free(ipc_);
	}
}

int close_ipc(struct ipc * ipc_,int id){
	struct ipc_client *client=NULL;
	if(!ipc_ || EMC_LOCAL!=ipc_->type) return -1;
	if(map_get(ipc_->server->connection,id,(void **)&client) < 0) return -1;
	write_ipc_data(ipc_,client,id,EMC_CMD_LOGOUT,NULL,0);
	client->connected=0;
#if defined (EMC_WINDOWS)
	if(client->evt){
		CloseHandle(client->evt);client->evt=NULL;
	}
#else
	close(client->evt);client->evt=-1;
#endif
	if(0==map_erase(ipc_->server->connection,id)){
		if(get_device_monitor(ipc_->device)){
			// If you set the monitor to throw on disconnect events
			struct monitor_data *md=(struct monitor_data *)global_alloc_monitor();
			if(md){
				md->events=EMC_EVENT_CLOSED;
				md->id=client->id;
				strncpy(md->ip,"127.0.0.1",ADDR_LEN);
				md->port=(int)client->evt;
				push_device_event(ipc_->device,md);
			}
		}
		push_ringarray((struct ringarray *)(ipc_->buffer+sizeof(uint)),client->locate);
		global_idle_connect_id(id);
		heap_free(ipc_->server->client_heap,client);
	}
	return 0;
}

int send_ipc(struct ipc *ipc_,void *msg,int flag){
	if(!ipc_ || !msg){
		return -1;
	}
	switch(emc_msg_get_mode(msg)){
	case EMC_SUB:
	case EMC_REQ:
		if(EMC_REMOTE==ipc_->type){
			if(!ipc_->client->connected) return -1;
			emc_msg_setid(msg,ipc_->client->id);
		}
		break;
	}
	if(EMC_NOWAIT==flag){
		void *msg_r=NULL;
		struct ipc_data *data=NULL;
		
		data=(struct ipc_data *)heap_alloc(ipc_->send_heap);
		if(!data)return -1;
		msg_r=emc_msg_alloc(emc_msg_buffer(msg),emc_msg_length(msg));
		if(!msg_r){
			heap_free(ipc_->send_heap,data);
			return -1;
		}
		emc_msg_build(msg_r,msg);
		data->ipc_=ipc_;
		data->id=emc_msg_getid(msg_r);
		data->flag=flag;
		data->msg=msg_r;
		emc_msg_ref_add(msg_r);
		if(EMC_PUB!=emc_msg_get_mode(msg_r)){
			if(push_ringqueue(ipc_->sq,data) < 0){
				heap_free(ipc_->send_heap,data);
				emc_msg_free(msg_r);
				return -1;
			}
		}else{
			struct ipc_data unit={0,flag,ipc_,msg_r};
			map_foreach(ipc_->server->connection,ipc_send_pub_foreach_cb,&unit);
		}
	}else{
		if(EMC_PUB!=emc_msg_get_mode(msg)){
			emc_msg_ref_add(msg);
		}
		if(write_ipc(ipc_,msg,flag) < 0){
			if(EMC_PUB!=emc_msg_get_mode(msg)){
				emc_msg_ref_dec(msg);
			}
			return -1;
		}
		if(EMC_PUB==emc_msg_get_mode(msg)){
			while(0==emc_msg_zero_ref(msg)){
				micro_wait(1);
			}
		}else{
			emc_msg_ref_dec(msg);
		}
	}
	return 0;
}
