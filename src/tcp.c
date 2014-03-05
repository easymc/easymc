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
#include "util/hashmap.h"
#include "util/utility.h"
#include "util/sockhash.h"
#include "util/unpack.h"
#include "util/sendqueue.h"
#include "util/merger.h"
#include "util/event.h"
#include "util/ringqueue.h"
#include "util/uniquequeue.h"
#include "util/pqueue.h"
#include "util/lock.h"
#include "util//memory/jemalloc.h"
#include "global.h"
#include "device.h"
#include "msg.h"
#include "common.h"
#include "tcp.h"

#define TCP_TIMEOUT		100
//Data length
#define TCP_DATA_SIZE	8172

#if !defined (EMC_WINDOWS)
#define TCP_FD_SIZE		64
#endif

#if defined (EMC_WINDOWS)
#pragma pack(4)
#else
#pragma pack(1)
#endif

#if defined (EMC_WINDOWS)
struct tcp_overlapped{
	// Overlapping structures
	OVERLAPPED	ol;
	WSABUF		buf;
	int			mask;
};
#endif

struct tcp_data{
	//Whether the message deletion(0xdeaddead)
	volatile uint	flag;
	// Whether to wait
	int				wait;
	// Command
	ushort			cmd;
	// Serial Number
	uint			serial;
	// Original length
	uint			ori;
	// The actual length of the data
	uint			len;
	// Do not send out the remaining length
	volatile uint	lave;
	void			*msg;
};

struct tcp_unit{
	int			wait;
	struct tcp	*tcp_;
	void		*msg;
};

// Thread structure
struct tcp_area{
	// Number of socket has been connected
	volatile uint		count;
	// Id send queue
	struct uniquequeue	*wmq;
#if defined (EMC_WINDOWS)
	HANDLE				fd;
#else
	int					fd;
#endif
	struct tcp			*tcp_;
	char				rbuf[MAX_PROTOCOL_SIZE];
};

struct tcp_server{
	//socket handle
	int					fd;
	struct hashmap		*connection;
	struct tcp_area		*area;
};

struct tcp_client{
	// Connection ID
	int						id;
	int						fd;
	// Communication mode
	ushort					mode;
	char					ip[ADDR_LEN];
	ushort					port;
	uint					connected;
	// Connection process is completed
	volatile uint			completed;
#if defined (EMC_WINDOWS)
	struct tcp_overlapped	olr;
#endif
	struct tcp_area			*area;
	// Reading data unpack
	void					*rupk;
};

struct tcp{
	// Device ID
	int					device;
	// tcp type: local / remote
	int					type;
	//ip
	int					ip;
	//port
	ushort				port;
	//close lock
	struct emc_lock		*term_lck;
#if !defined (EMC_WINDOWS)	
	struct sockhash		*hash;
#endif
	// exit
	volatile	uint	exit;
	// Message received task list
	struct map			*rmap;
	struct tcp_server	*server;
	struct tcp_client	*client;
};
#pragma pack()

static int init_tcp_client(struct tcp * tcp_);

//Cas Operate
static uint tcp_number_cas(volatile uint * key, uint _old, uint _new){
#if defined (EMC_WINDOWS)
	return InterlockedCompareExchange((unsigned long*)key, _new, _old);
#else
	return __sync_val_compare_and_swap(key, _old, _new);
#endif
}

static void tcp_number_add(volatile uint * n){
	do{}while(0!=tcp_number_cas(n, 0, 1));
}

static void tcp_number_dec(volatile uint * n){
	do{}while(1 != tcp_number_cas(n, 1, 0));
}

// Set the socket keepalive properties
static int tcp_set_keepalive(int fd){
	int keepalive = 1;
#if defined (EMC_WINDOWS)
#define SIO_KEEPALIVE_VALS _WSAIOW(IOC_VENDOR, 4) 
	struct tcp_keepalive{
		u_long onoff;  
		u_long keepalivetime;  
		u_long keepaliveinterval;  
	};
	unsigned long bytes = 0;
	struct tcp_keepalive setting = {0};
	setting.onoff = 1 ;
	setting.keepalivetime = 10000 ; // Keep Alive
	setting.keepaliveinterval = 3000 ; // Resend if No-Reply
#else
	int keepidle = 10; // If the connection within 10 seconds without any data exchanges, then probed
	int keepinterval = 3; // When contracting probe interval is 3 seconds
	int keepcount = 5; // Attempts to detect if the 1st probe packets received a response, and then five times longer hair.
#endif
	if(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char*)&keepalive, sizeof(keepalive)) < 0) return -1;
#if defined (EMC_WINDOWS)
	if(WSAIoctl(fd, SIO_KEEPALIVE_VALS, &setting, sizeof(setting), NULL, 0, &bytes, NULL, NULL) < 0) return -1;
#else
	if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, (void*)&keepidle, sizeof(keepidle)) < 0) return -1;
	if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&keepinterval, sizeof(keepinterval)) < 0) return -1;
	if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, (void *)&keepcount, sizeof(keepcount)) < 0) return -1;
#endif
	return 0;
}

// Gets the minimum number of threads connected
static struct tcp_area* tcp_least_thread(struct tcp * tcp_){
	uint index=0, count=EMC_SOCKETS_DEFAULT, result=0;
	for(index=0; index<get_cpu_num(); index++){
		if(tcp_->server->area[index].count < count){
			count = tcp_->server->area[index].count;
			result = index;
			if(!count) break;
		}
	}
	return tcp_->server->area+result;
}

static int tcp_add_event(struct tcp_area * area, struct tcp_client * client, uint mask){
#if defined (EMC_WINDOWS)
	unsigned long flag=0, length=0;
	if(!CreateIoCompletionPort((HANDLE)client->fd, area->fd, client->id, 0)){
		return -1;
	}
	if(mask & EMC_READ){
		memset(&client->olr.ol, 0, sizeof(OVERLAPPED));
		client->olr.mask = EMC_READ;
		if(WSARecv(client->fd, &client->olr.buf, 1, &length, &flag, &client->olr.ol, NULL) < 0){
			if(WSA_IO_PENDING != GetLastError()){
				return -1;
			}
		}
	}
#else
	struct epoll_event e;
	e.events = 0;
	e.data.u64 = 0; /* avoid valgrind warning */
	e.data.fd = client->fd;
	if(epoll_ctl(area->fd, EPOLL_CTL_ADD, client->fd, &e) < 0){
		return -1;
	}
#endif
	return 0;
}

// Set socket literacy event
static int tcp_set_event(struct tcp_area * area, struct tcp_client * client, uint mask){
#if defined (EMC_WINDOWS)
	unsigned long flag=0, length=0;
	if(mask & EMC_READ){
		memset(&client->olr.ol, 0, sizeof(OVERLAPPED));
		client->olr.mask = EMC_READ;
		if(WSARecv(client->fd, &client->olr.buf, 1, &length, &flag, &client->olr.ol, NULL) < 0){
			if(WSA_IO_PENDING != GetLastError()){
				return -1;
			}
		}
	}
#else
	struct epoll_event e;
	if(mask & EMC_READ){
		e.events = EPOLLIN;
	}
	e.data.u64 = 0; /* avoid valgrind warning */
	e.data.fd = client->fd;
	if(epoll_ctl(area->fd, EPOLL_CTL_MOD, client->fd, &e) < 0){
		return -1;
	}
#endif
	return 0;
}

#if !defined (EMC_WINDOWS)
static void tcp_del_event(struct tcp * tcp_, struct tcp_area * area, int fd){
	epoll_ctl(area->fd, EPOLL_CTL_DEL, fd, NULL);
}
#endif

// Check the message reference count is 0, then consider the release of the message buffer
static void tcp_release_msg(struct tcp_data * data){
	if(EMC_LIVE == data->flag){
		//Release data
		data->flag = EMC_DEAD;
		if(	data->msg && EMC_NOWAIT==data->wait &&
			emc_msg_zero_ref(data->msg) > 0 && 
			emc_msg_serial(data->msg)==data->serial){
			emc_msg_free(data->msg);
		}
		data->msg = NULL;
		free_impl(data);
	}
}

// Throws monitoring messages
static void tcp_post_monitor(struct tcp * tcp_, struct tcp_client * client, int evt, void * msg){
	// If you set the monitor option throws up message
	if(get_device_monitor(tcp_->device)){
		struct monitor_data * md = (struct monitor_data *)global_alloc_monitor();
		if(md){
			md->events = evt;
			if(client){
				md->id = client->id;
				strncpy(md->ip, client->ip, ADDR_LEN);
				md->port = client->port;
			}
			if(msg){
				md->addition = emc_msg_get_addition(msg);
			}
			push_device_event(tcp_->device, md);
		}
	}
}

// tcp  data consolidation callback function
static void tcp_merger_cb(char * data, int len, int id, void * addition){
	struct tcp * tcp_ = (struct tcp *)addition;
	struct tcp_client * client = NULL;
	struct message * msg = (struct message *)emc_msg_alloc(data, len);

	if(EMC_LOCAL == tcp_->type){
		client = (struct tcp_client *)hashmap_search(tcp_->server->connection, id);
	}else if(EMC_REMOTE == tcp_->type){
		client = tcp_->client;
	}
	if(client && msg){
		emc_msg_setid(msg, id);
		emc_msg_set_mode(msg, client->mode);
		if(!client->completed){
			nsleep(1);
			if(!client->completed){
				emc_msg_free(msg);
				msg = NULL;
			}
		}
		if(msg && push_device_message(tcp_->device, msg) < 0){
			emc_msg_free(msg);
		}
	}
}

// Unpacking callback
static void tcp_unpack_cb(char * data, unsigned short len, int id, void * args){
	struct tcp_client * client = NULL;
	struct tcp_area * area = (struct tcp_area *)args;
	
	if(EMC_LOCAL == area->tcp_->type){
		client = (struct tcp_client *)hashmap_search(area->tcp_->server->connection, id);
	}else if(EMC_REMOTE == area->tcp_->type){
		client = area->tcp_->client;
	}
	if(client){
		if(((struct data_unit *)data)->len == ((struct data_unit *)data)->total){
			if(EMC_CMD_LOGIN == ((struct data_unit *)data)->cmd){
				client->mode = *(ushort *)(data+sizeof(struct data_unit));
			}else if(EMC_CMD_DATA == ((struct data_unit *)data)->cmd){
				struct message * msg = (struct message *)emc_msg_alloc(data+sizeof(struct data_unit), len-sizeof(struct data_unit));
				if(msg){
					emc_msg_setid(msg, id);
					emc_msg_set_mode(msg, client->mode);
					if(!client->completed){
						nsleep(1);
						if(!client->completed){
							emc_msg_free(msg);
							msg = NULL;
						}
					}
					if(msg && push_device_message(area->tcp_->device, msg) < 0){
						emc_msg_free(msg);
					}
				}
			}			
		}else{
			void * mg = NULL;
			union data_serial serial = {0};

			serial.id = id;
			serial.serial = ((struct data_unit *)data)->serial;
			if(map_get(area->tcp_->rmap,serial.no, (void**)&mg) < 0){
				mg = global_alloc_merger();
				if(mg){
					if(map_add(area->tcp_->rmap, serial.no, mg) < 0){
						global_free_merger(mg);
						mg=NULL;
					}
				}
			}
			if(mg){
				merger_init(mg, ((struct data_unit *)data)->total);
				merger_add(mg, ((struct data_unit *)data)->no * TCP_DATA_SIZE, data+sizeof(struct data_unit), len-sizeof(struct data_unit));
				if(0==merger_get(mg, tcp_merger_cb, id, area->tcp_)){
					global_free_merger(mg);
					map_erase(area->tcp_->rmap, serial.no);
				}
			}
		}
	}
}

static int tcp_recv_data(int fd, char * buf, int len){
	int nread = 0;
#if defined (EMC_WINDOWS)
	nread = recv(fd, buf, len, 0);
#else
	nread = recv(fd, buf, len, MSG_NOSIGNAL);
#endif
	if (nread < 0){
#if defined (EMC_WINDOWS)
		if(WSAEWOULDBLOCK == WSAGetLastError()){
			nread = 0;
		}else return -1;
#else
		if(errno==EINTR || errno==EWOULDBLOCK || errno==EAGAIN){
			nread=0;
		}else return -1;
#endif
	}
	else if(0 == nread) return -1;
	return nread;
}

//	Callback client reconnection
static int tcp_reconnect_cb(void * p, void * addition){
	return init_tcp_client((struct tcp *)addition);
}

static int process_close(struct tcp * tcp_, struct tcp_area * area, int id){
	struct tcp_client * client = NULL;
	void * msg = NULL;

	lock_enter(tcp_->term_lck);
	if(EMC_LOCAL == tcp_->type){
		if(!(client = (struct tcp_client *)hashmap_search(tcp_->server->connection, id))){
			lock_leave(tcp_->term_lck);
			return -1;
		}
	}else if(EMC_REMOTE == tcp_->type){
		client = tcp_->client;
	}
	_close_socket(client->fd);
	client->fd = -1;
	client->connected = 0;
#if !defined (EMC_WINDOWS)
	tcp_del_event(tcp_, area, client->fd);
#endif
	if(client->rupk){
		global_free_unpack(client->rupk);
		client->rupk = NULL;
	}
	while(0==global_pop_sendqueue(client->id, (void **)&msg)){
		if(emc_msg_zero_ref(msg) > 0){
			tcp_post_monitor(tcp_, client, EMC_EVENT_SNDFAIL, msg);
			emc_msg_free(msg);
		}
	}
	area->count --;
	if(EMC_LOCAL == tcp_->type){
		tcp_post_monitor(tcp_, client, EMC_EVENT_CLOSED, NULL);
		global_idle_connect_id(client->id);
		free_impl(client);
		hashmap_erase(tcp_->server->connection, id);
	}else if(EMC_REMOTE == tcp_->type){
		tcp_post_monitor(tcp_, client, EMC_EVENT_CLOSED, NULL);
		if(!tcp_->exit){
			global_add_reconnect(id, tcp_reconnect_cb, client, tcp_);
		}
	}
	lock_leave(tcp_->term_lck);
	return 0;
}

static void process_recv(struct tcp * tcp_, struct tcp_area * area, int id){
	int nread = -1;
	struct tcp_client * client = NULL;
	char buffer[MAX_DATA_SIZE] = {0};

	if(EMC_LOCAL == tcp_->type){
		client = (struct tcp_client *)hashmap_search(tcp_->server->connection, id);
	}else if(EMC_REMOTE == tcp_->type){
		if(tcp_->client->id == id){
			client = tcp_->client;
		}
	}
	if(client){
		while(1){
			nread = tcp_recv_data(client->fd, area->rbuf, MAX_DATA_SIZE);
			if(nread < 0){
				//close connection
				process_close(tcp_, area, id);
				break;
			}else if(0 == nread){
				// If the windows system, continue to probe whether the data readable
#if defined (EMC_WINDOWS)
				tcp_set_event(area, client, EMC_READ);
#endif
				break;
			}else{
				if(!client->rupk){
					client->rupk = global_alloc_unapck();
				}
				if(client->rupk){
					unpack_add(client->rupk, area->rbuf, nread);
					unpack_get(client->rupk, tcp_unpack_cb, id, area, buffer);
				}
			}
		}
	}
}

static int tcp_send_data(struct tcp * tcp_, struct tcp_client * client, ushort cmd, int flag, void * msg){
	struct tcp_data * data = (struct tcp_data *)malloc_impl(sizeof(struct tcp_data));
	if(!data) return -1;

	data->flag = EMC_LIVE;
	data->wait = flag;
	data->cmd = cmd;
	data->serial = emc_msg_serial(msg);
	data->ori = emc_msg_length(msg);
	// Data compression can be performed here
	data->lave = data->len = data->ori;
	data->msg = msg;
	if(emc_msg_ref_add(msg) < 0) return -1;
	if(global_push_sendqueue(client->id, data) < 0){
		post_uqueue(client->area->wmq);
		emc_msg_ref_dec(msg);
		return -1;
	}else{
		if(push_uqueue(client->area->wmq, client->id) < 0){
			emc_msg_ref_dec(msg);
			return -1;
		}
	}
	return 0;
}

// Push data to all subscribers end
static void tcp_pub_foreach_cb(struct hashmap * m, int key, void * p, void * addition){
	struct tcp_unit * unit = (struct tcp_unit *)addition;
	if(EMC_SUB == ((struct tcp_client *)p)->mode && ((struct tcp_client *)p)->connected){
		if(tcp_send_data(unit->tcp_,(struct tcp_client *)p, EMC_CMD_DATA, unit->wait, unit->msg) < 0){
			tcp_post_monitor(unit->tcp_, (struct tcp_client *)p, EMC_EVENT_SNDFAIL, unit->msg);
		}
	}
}

// Subcontracting send data
static int tcp_data_sep(struct tcp_data * data, char * buffer, int id){
	*(ushort *)buffer = EMC_HEAD;
	*(ushort *)(buffer+sizeof(ushort)) = data->lave>TCP_DATA_SIZE?MAX_DATA_SIZE:(data->lave+sizeof(struct data_unit));
	((struct data_unit *)(buffer+sizeof(uint)))->cmd = data->cmd;
	((struct data_unit *)(buffer+sizeof(uint)))->id = id;
	((struct data_unit *)(buffer+sizeof(uint)))->serial = data->serial;
	((struct data_unit *)(buffer+sizeof(uint)))->total = data->len;
	((struct data_unit *)(buffer+sizeof(uint)))->len = data->lave>TCP_DATA_SIZE?TCP_DATA_SIZE:data->lave;
	((struct data_unit *)(buffer+sizeof(uint)))->no = (data->len-data->lave)/TCP_DATA_SIZE;
	memcpy(buffer+sizeof(uint)+sizeof(struct data_unit), (char *)emc_msg_buffer(data->msg)+(data->len-data->lave),
		data->lave>TCP_DATA_SIZE?TCP_DATA_SIZE:data->lave);
	return data->lave>TCP_DATA_SIZE?MAX_PROTOCOL_SIZE:(data->lave+sizeof(struct data_unit)+sizeof(uint));
}

static int process_send(struct tcp * tcp_, struct tcp_area * area, int id){
	struct tcp_client * client = NULL;
	struct tcp_data * data = NULL;
	int nsend=-1, length=0, timeout=0;
	char buffer[MAX_PROTOCOL_SIZE] = {0};

	if(EMC_LOCAL == tcp_->type){
		client = (struct tcp_client *)hashmap_search(tcp_->server->connection, id);
	}else if(EMC_REMOTE == tcp_->type){
		if(tcp_->client->id == id){
			client = tcp_->client;
		}
	}
	if(!client){
		// Failed to send notification messages
		return -1;
	}
	while(1){
		if(global_pop_sendqueue(id, (void **)&data) < 0){
			break;
		}
		timeout = timeGetTime();
		while(data->lave > 0){
			length = tcp_data_sep(data, buffer, id);
#if defined (EMC_WINDOWS)
			nsend = send(client->fd, buffer, length, 0);
#else
			nsend = send(client->fd, buffer, length, MSG_NOSIGNAL);
#endif
			if(nsend < 0){
#if defined (EMC_WINDOWS)
				if(WSAEWOULDBLOCK == WSAGetLastError()){
#else
				if (errno==EINTR || errno==EWOULDBLOCK || errno==EAGAIN){
#endif
					if(timeGetTime()-timeout > TCP_TIMEOUT){
						// Transmission fails, the data added to the queue
						if(global_push_head_sendqueue(id, data) < 0){
							// If you set the monitor option throws up send failure message
							if(EMC_CMD_DATA == data->cmd){
								tcp_post_monitor(tcp_, client, EMC_EVENT_SNDFAIL, data->msg);
							}
							emc_msg_ref_dec(data->msg);
							// Join transmit queue fails, check whether the message reference count is 0, then consider the release of the message buffer
							tcp_release_msg(data);
							return -1;
						}
						push_uqueue(area->wmq, id);
						return -1;
					}
					continue;
				}
				emc_msg_ref_dec(data->msg);
				if(EMC_CMD_DATA == data->cmd){
					tcp_post_monitor(tcp_, client, EMC_EVENT_SNDFAIL, data->msg);
				}
				tcp_release_msg(data);
				process_close(tcp_,area, id);
				return -1;
			}else if(0 == nsend){
				emc_msg_ref_dec(data->msg);
				if(EMC_CMD_DATA == data->cmd){
					tcp_post_monitor(tcp_, client, EMC_EVENT_SNDFAIL, data->msg);
				}
				tcp_release_msg(data);
				process_close(tcp_, area, id);
				return -1;
			}else{
				data->lave -= (nsend-sizeof(uint)-sizeof(struct data_unit));
			}
		}
		// If you set the monitor option throws up send success message
		if(EMC_CMD_DATA == data->cmd){
			tcp_post_monitor(tcp_, client, EMC_EVENT_SNDSUCC, data->msg);
		}
		emc_msg_set_result(data->msg, 1);
		emc_msg_ref_dec(data->msg);
		tcp_release_msg(data);
	}
	return 0;
}

static int process_accept(struct tcp * tcp_, struct tcp_area * area, int fd, char * addr, ushort port){
	int flag=1, size=0x10000;
	struct tcp_client * client = NULL;

	if(0!=_nonblocking(fd, flag)){
		_close_socket(fd);
		return -1;
	}
	if(tcp_set_keepalive(fd) < 0){
		_close_socket(fd);
		return -1;
	}
	if(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&size, sizeof(int)) < 0){
		_close_socket(fd);
		return -1;
	}
	if(setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&size, sizeof(int)) < 0){
		_close_socket(fd);
		return -1;
	}
	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(int))!= 0){
		_close_socket(fd);
		return -1;
	}
	client = (struct tcp_client*)malloc_impl(sizeof(struct tcp_client));
	if(!client){
		_close_socket(fd);
		return -1;
	}
	strncpy(client->ip, addr, ADDR_LEN);
	client->port = port;
	client->fd = fd;
	client->area = area;
	client->id = global_get_connect_id();
	client->completed = 0;
#if !defined (EMC_WINDOWS)
	if(sockhash_insert(tcp_->hash, client->fd, client->id) < 0){
		global_idle_connect_id(client->id);
		heap_free(tcp_->server->client_heap, client);
		_close_socket(fd);
		return -1;
	}
#endif
	if(tcp_add_event(area, client, EMC_READ) < 0){
#if !defined (EMC_WINDOWS)
		sockhash_erase(tcp_->hash, client->fd);
#endif
		global_idle_connect_id(client->id);
		free_impl(client);
		_close_socket(fd);
		return -1;
	}
#if !defined (EMC_WINDOWS)
	if(tcp_set_event(area, client, EMC_READ) < 0){
		tcp_del_event(tcp_, area, fd);
		sockhash_erase(tcp_->hash, client->fd);
		global_idle_connect_id(client->id);
		heap_free(tcp_->server->client_heap, client);
		_close_socket(fd);
		return -1;
	}
#endif
	if(hashmap_insert(tcp_->server->connection, client->id, client) < 0){
#if !defined (EMC_WINDOWS)
		sockhash_erase(tcp_->hash, client->fd);
#endif
		global_idle_connect_id(client->id);
		free_impl(client);
		_close_socket(fd);
		return -1;
	}
	client->connected = 1;
	area->count ++;
	tcp_post_monitor(tcp_, client, EMC_EVENT_ACCEPT, NULL);
	tcp_number_add(&client->completed);
	return 0;
}

static emc_result_t EMC_CALL tcp_work_cb(void * args){
	int64 sid = -1;
#if defined (EMC_WINDOWS)
	uint length=0, key=0;
	struct tcp_overlapped *ol = NULL;
#else
	int retval=-1, id=-1;
	struct epoll_event	events[TCP_FD_SIZE] = {0};
#endif
	struct tcp_area	*area = (struct tcp_area *)args;
	struct tcp *tcp_ = area->tcp_;

	while(!tcp_->exit){
#if defined (EMC_WINDOWS)
		if(GetQueuedCompletionStatus(area->fd, (LPDWORD)&length, (PULONG_PTR)&key, (LPOVERLAPPED *)&ol, INFINITE)){
			if(key < EMC_SOCKETS_DEFAULT){
				if(ol){
					if(STATUS_REMOTE_DISCONNECT == ol->ol.Internal ||
						STATUS_LOCAL_DISCONNECT == ol->ol.Internal ||
						STATUS_CANCELLED == ol->ol.Internal){
						process_close(tcp_, area, key);
					}else{
						if(ol->mask & EMC_READ){
							process_recv(tcp_, area, key);
						}
					}
				}
			}
		}else{
			if(WAIT_TIMEOUT != WSAGetLastError()){
				if(ol && (STATUS_REMOTE_DISCONNECT == ol->ol.Internal ||
					STATUS_LOCAL_DISCONNECT == ol->ol.Internal ||
					STATUS_CANCELLED == ol->ol.Internal)){
					if(key < EMC_SOCKETS_DEFAULT){
						// Remote disconnect, processe close event
						process_close(tcp_, area, key);
					}
				}
			}
		}
#else
		retval = epoll_wait(area->fd, events, TCP_FD_SIZE, -1);
		if(retval > 0){
			int j = 0;
			for (j=0; j<retval; j++){
				id = sockhash_search(area->tcp_->hash,events[j].data.fd);
				if(id >=0 && id < EMC_SOCKETS_DEFAULT){
					if(events[j].events & EPOLLIN){
						process_recv(tcp_, area, id);
					}
				}
			}
		}
#endif
	}
#if defined (EMC_WINDOWS)
	return 0;
#else
	return NULL;
#endif
}

static emc_result_t EMC_CALL tcp_send_cb(void * args){
	struct tcp_area	* area = (struct tcp_area	*)args;
	struct tcp * tcp_ = area->tcp_;
	int id = -1;

	while(!tcp_->exit){
		wait_uqueue(area->wmq);
		while((id=pop_uqueue(area->wmq)) >= 0){
			process_send(tcp_, area, id);
		}
	}
#if defined (EMC_WINDOWS)
	return 0;
#else
	return NULL;
#endif
}

static emc_result_t EMC_CALL tcp_accept_cb(void * args){
	struct tcp * tcp_ = (struct tcp *)args;
	struct tcp_area * area = NULL;
	struct sockaddr_in sa = {0};
	int fd=-1, len=sizeof(struct sockaddr_in);
	while(!tcp_->exit){
		fd=accept(tcp_->server->fd, (struct sockaddr*)&sa, &len);
		if(fd > 0){
			area = tcp_least_thread(tcp_);
			process_accept(tcp_, area, fd, inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
		}else{
			nsleep(1);
		}
	}

#if defined (EMC_WINDOWS)
	return 0;
#else
	return NULL;
#endif
}

// Tcp server initialization
static int init_tcp_server(struct tcp * tcp_){
	int flag = 0;
	struct sockaddr_in	addr = {0};

	tcp_->server->fd = socket(AF_INET, SOCK_STREAM, 0);
	if(tcp_->server->fd < 0){
		return -1;
	}
	if(0!=setsockopt(tcp_->server->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag))){
		_close_socket(tcp_->server->fd);
		return -1;
	}
	addr.sin_family			= AF_INET;
	addr.sin_addr.s_addr	= tcp_->ip?tcp_->ip:INADDR_ANY;
	addr.sin_port			= htons(tcp_->port);
	if(bind(tcp_->server->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
		_close_socket(tcp_->server->fd);
		return -1;
	}
	if(listen(tcp_->server->fd, 100) < 0){
		_close_socket(tcp_->server->fd);
		return -1;
	}
	tcp_->server->connection  = hashmap_new(EMC_SOCKETS_DEFAULT);
	tcp_->server->area = (struct tcp_area *)malloc(sizeof(struct tcp_area) * get_cpu_num());
	memset(tcp_->server->area, 0, sizeof(struct tcp_area) * get_cpu_num());
	for(flag=0; flag<get_cpu_num(); flag++){
		tcp_->server->area[flag].wmq = create_uqueue();
#if defined (EMC_WINDOWS)
		tcp_->server->area[flag].fd  = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
#else
		tcp_->server->area[flag].fd  = epoll_create(EMC_SOCKETS_DEFAULT);
#endif
		tcp_->server->area[flag].tcp_ = tcp_;
		create_thread(tcp_work_cb, tcp_->server->area+flag);
		create_thread(tcp_send_cb, tcp_->server->area+flag);
	}
	create_thread(tcp_accept_cb, tcp_);
	return 0;
}

// The client sends the login packet
static void tcp_send_login(struct tcp * tcp_){
	void *msg=emc_msg_alloc(NULL, sizeof(ushort));
	emc_msg_setid(msg, tcp_->client->id);
	*(ushort *)emc_msg_buffer(msg) = tcp_->client->mode;
	if(tcp_send_data(tcp_,tcp_->client, EMC_CMD_LOGIN, EMC_NOWAIT, msg) < 0){
		emc_msg_free(msg);
	}
}

// Tcp client initialization
static int init_tcp_client(struct tcp * tcp_){
#if defined (EMC_WINDOWS)
	fd_set rdset={0}, wdset={0};
	struct timeval tv = {0};
#else
	struct pollfd fds;
#endif
	struct sockaddr_in	addr = {0};
	int size=0x10000, flag=1, erro=0, erro_len=sizeof(int), selecttime=5;

	if(!tcp_->client->area){
		tcp_->client->area = (struct tcp_area *)malloc(sizeof(struct tcp_area));
		memset(tcp_->client->area, 0, sizeof(struct tcp_area));
		tcp_->client->area->wmq = create_uqueue();
#if defined (EMC_WINDOWS)
		tcp_->client->area->fd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
#else
		tcp_->client->area->fd = epoll_create(EMC_SOCKETS_DEFAULT);
#endif
		tcp_->client->area->tcp_ = tcp_;
		tcp_->client->id = global_get_connect_id();
	}

	tcp_->client->completed = 0;
	tcp_->client->fd = socket(AF_INET, SOCK_STREAM, 0);
	if(tcp_->client->fd < 0){
		return -1;
	}
	if(0 != _nonblocking(tcp_->client->fd, flag)){
		_close_socket(tcp_->client->fd);
		return -1;
	}
	if(0 != tcp_set_keepalive(tcp_->client->fd)){
		_close_socket(tcp_->client->fd);
		return -1;
	}
	if(setsockopt(tcp_->client->fd, SOL_SOCKET, SO_SNDBUF, (char *)&size, sizeof(int)) < 0){
		_close_socket(tcp_->client->fd);
		return -1;
	}
	if(setsockopt(tcp_->client->fd, SOL_SOCKET, SO_RCVBUF, (char *)&size, sizeof(int)) < 0){
		_close_socket(tcp_->client->fd);
		return -1;
	}
	if(0!=setsockopt(tcp_->client->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&flag, sizeof(flag))){
		_close_socket(tcp_->client->fd);
		return -1;
	}
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = tcp_->ip;
	addr.sin_port = htons(tcp_->port);
	if(connect(tcp_->client->fd, (const struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0){
#if defined (EMC_WINDOWS)
		if(WSAEWOULDBLOCK != GetLastError()){
#else
		if(errno != EINPROGRESS){
#endif
			_close_socket(tcp_->client->fd);
			return -1;
		}
	}
	while(1){
#if defined (EMC_WINDOWS)
		FD_ZERO(&wdset);
		FD_ZERO(&rdset);
		FD_SET(tcp_->client->fd, &wdset);
		FD_SET(tcp_->client->fd, &rdset);
		tv.tv_sec = 0;
		tv.tv_usec = 300000;
		if(select(tcp_->client->fd+1, &rdset ,&wdset, NULL, &tv) < 0){
			_close_socket(tcp_->client->fd);
			return -1;
		}
		if(FD_ISSET(tcp_->client->fd, &rdset)){
			if(getsockopt(tcp_->client->fd, SOL_SOCKET, SO_ERROR, (char*)&erro, &erro_len) < 0){
				_close_socket(tcp_->client->fd);
				return -1;
			}
			if(0 != erro){
				_close_socket(tcp_->client->fd);
				return -1;
			}
		}
		if(FD_ISSET(tcp_->client->fd, &wdset)){
			if(getsockopt(tcp_->client->fd, SOL_SOCKET, SO_ERROR, (char*)&erro, &erro_len) < 0){
				_close_socket(tcp_->client->fd);
				return -1;
			}
			if(0 != erro){
				_close_socket(tcp_->client->fd);
				return -1;
			}
			tcp_->client->connected = 1;
			break;
		}
#else
		fds.fd = tcp_->client->fd;
		fds.events |= POLLOUT;
		if(poll(&fds, 1, 100) > 0){
			if(fds.revents & POLLOUT){
				if(getsockopt(tcp_->client->fd, SOL_SOCKET, SO_ERROR, (char*)&erro, &erro_len) < 0){
					_close_socket(tcp_->client->fd);
					return -1;
				}
				if(0 != erro){
					_close_socket(tcp_->client->fd);
					return -1;
				}
				tcp_->client->connected = 1;
				break;
			}
		}
#endif
		selecttime --;
		if(!selecttime){
			tcp_->client->connected = 0;
			_close_socket(tcp_->client->fd);
			return -1;
		}
	}
	if(tcp_add_event(tcp_->client->area, tcp_->client, EMC_READ) < 0){
		_close_socket(tcp_->client->fd);
		tcp_->client->connected = 0;
		return -1;
	}
#if !defined (EMC_WINDOWS)
	if(tcp_set_event(tcp_->client->area, tcp_->client, EMC_READ) < 0){
		_close_socket(tcp_->client->fd);
		tcp_->client->connected=0;
		return -1;
	}
#endif
	strncpy(tcp_->client->ip, inet_ntoa(addr.sin_addr), ADDR_LEN);
	tcp_->client->port = ntohs(addr.sin_port);
	tcp_post_monitor(tcp_, tcp_->client, EMC_EVENT_CONNECT, NULL);
	tcp_number_add(&tcp_->client->completed);
	return 0;
}

struct tcp * create_tcp(unsigned int ip, unsigned short port, int device, unsigned short mode, int type){
	struct tcp * tcp_ = (struct tcp *)malloc(sizeof(struct tcp));
	if(!tcp_) return NULL;
	memset(tcp_, 0, sizeof(struct tcp));
	tcp_->device = device;
	tcp_->ip = ip;
	tcp_->port = port;
	tcp_->type = type;
#if !defined (EMC_WINDOWS)
	tcp_->hash = sockhash_new(EMC_SOCKETS_DEFAULT);
#endif
	tcp_->term_lck = lock_new();
	tcp_->rmap = create_map(EMC_SOCKETS_DEFAULT);
	if(EMC_LOCAL == type){
		tcp_->server = (struct tcp_server *)malloc(sizeof(struct tcp_server));
		if(!tcp_->server){
			lock_delete(tcp_->term_lck);
			delete_map(tcp_->rmap);
#if !defined (EMC_WINDOWS)
			sockhash_delete(tcp_->hash);
#endif
			free(tcp_);
			return NULL;
		}
		memset(tcp_->server, 0, sizeof(struct tcp_server));
		if(init_tcp_server(tcp_) < 0){
			lock_delete(tcp_->term_lck);
			delete_map(tcp_->rmap);
#if !defined (EMC_WINDOWS)
			sockhash_delete(tcp_->hash);
#endif
			free(tcp_);
			return NULL;
		}
	}else if(EMC_REMOTE == type){
		tcp_->client = (struct tcp_client *)malloc(sizeof(struct tcp_client));
		if(!tcp_->client){
			lock_delete(tcp_->term_lck);
			delete_map(tcp_->rmap);
#if !defined (EMC_WINDOWS)
			sockhash_delete(tcp_->hash);
#endif
			free(tcp_);
			return NULL;
		}
		memset(tcp_->client, 0, sizeof(struct tcp_client));
		tcp_->client->mode = mode;
		if(init_tcp_client(tcp_) < 0){
			if(global_add_reconnect(tcp_->client->id, tcp_reconnect_cb, tcp_->client, tcp_) < 0){
				tcp_->exit = 1;
				_close_socket(tcp_->client->fd);
#if defined (EMC_WINDOWS)
				CloseHandle(tcp_->client->area->fd);
#else
				close(tcp_->client->area->fd);
#endif
				global_idle_connect_id(tcp_->client->id);
				free(tcp_->client->area);
				free(tcp_->client);
				lock_delete(tcp_->term_lck);
				delete_map(tcp_->rmap);
#if !defined (EMC_WINDOWS)
				sockhash_delete(tcp_->hash);
#endif
				free(tcp_);
				return NULL;
			}
		}
		create_thread(tcp_work_cb, tcp_->client->area);
		create_thread(tcp_send_cb, tcp_->client->area);
		// Log packet sent to the server
		tcp_send_login(tcp_);
	}
	return tcp_;
}

void delete_tcp(struct tcp * tcp_){
	if(tcp_){
		tcp_->exit = 1;
		nsleep(100);
		if(EMC_LOCAL == tcp_->type){
			uint index = 0;
			_close_socket(tcp_->server->fd);
			for(index=0; index<get_cpu_num(); index++){
				delete_uqueue(tcp_->server->area[index].wmq);
#if defined (EMC_WINDOWS)
				PostQueuedCompletionStatus(tcp_->server->area[index].fd, 0xFFFFFFFF, 0, NULL);
				CloseHandle(tcp_->server->area[index].fd);
#else
				close(tcp_->server->area[index].fd);
#endif
			}
			nsleep(100);
			hashmap_delete(tcp_->server->connection);
			free(tcp_->server->area);
			free(tcp_->server);
		}else if(EMC_REMOTE == tcp_->type){
			global_free_reconnect(tcp_->client->id);
			_close_socket(tcp_->client->fd);
#if defined (EMC_WINDOWS)
			CloseHandle(tcp_->client->area->fd);
#else
			close(tcp_->client->area->fd);
#endif
			delete_uqueue(tcp_->client->area->wmq);
			nsleep(100);
			free(tcp_->client->area);
			free(tcp_->client);
		}
		delete_map(tcp_->rmap);
#if !defined (EMC_WINDOWS)
		sockhash_delete(tcp_->hash);
#endif
		lock_delete(tcp_->term_lck);
		free(tcp_);
	}
}

int close_tcp(struct tcp * tcp_, int id){
	struct tcp_client * client = NULL;
	if(!tcp_ || EMC_LOCAL != tcp_->type) return -1;
	if(!(client = (struct tcp_client *)hashmap_search(tcp_->server->connection, id))){
		return -1;
	}
	return process_close(tcp_,client->area,id);
}

int send_tcp(struct tcp * tcp_, void * msg, int flag){
	struct tcp_client * client = NULL;
	if(!tcp_ || !msg){
		return -1;
	}
	if(EMC_LOCAL == tcp_->type){
		if(EMC_REQ == emc_msg_get_mode(msg)){
			emc_msg_set_mode(msg,EMC_REP);
		}
		if(EMC_SUB == emc_msg_get_mode(msg)){
			tcp_post_monitor(tcp_, tcp_->client, EMC_EVENT_SNDFAIL, msg);
			return -1;
		}
	}
	if(EMC_REMOTE == tcp_->type){
		if(emc_msg_get_mode(msg) != tcp_->client->mode){
			emc_msg_set_mode(msg, tcp_->client->mode);
		}
	}
	switch(emc_msg_get_mode(msg)){
	case EMC_REQ:
	case EMC_SUB:
		if(EMC_REMOTE == tcp_->type){
			emc_msg_setid(msg, tcp_->client->id);
			if(!tcp_->client->connected){
				tcp_post_monitor(tcp_, tcp_->client, EMC_EVENT_SNDFAIL, msg);
				return -1;
			}
		}
	case EMC_REP:
		if(EMC_LOCAL == tcp_->type){
			client = (struct tcp_client *)hashmap_search(tcp_->server->connection, emc_msg_getid(msg));
		}else{
			client = tcp_->client;
		}
		if(!client || !client->connected){
			return -1;
		}

		if(EMC_NOWAIT == flag){
			void * msg_r = emc_msg_alloc(emc_msg_buffer(msg), emc_msg_length(msg));
			if(!msg_r) {
				tcp_post_monitor(tcp_, client, EMC_EVENT_SNDFAIL, msg);
				return -1;
			}
			emc_msg_build(msg_r, msg);
			if(tcp_send_data(tcp_, client, EMC_CMD_DATA, flag, msg_r) < 0){
				emc_msg_free(msg_r);
				tcp_post_monitor(tcp_, client, EMC_EVENT_SNDFAIL, msg);
				return -1;
			}
		}else{
			uint result = 0;
			if(tcp_send_data(tcp_, client, EMC_CMD_DATA, flag, msg) < 0){
				tcp_post_monitor(tcp_, client, EMC_EVENT_SNDFAIL, msg);
				return -1;
			}
			while(client->connected && 0==emc_msg_zero_ref(msg)){}
			if(emc_msg_get_result(msg, &result) < 0){
				return -1;
			}
			if(result) return 0;
			if(!client->connected) return -1;
		}
		break;
	case EMC_PUB:
		if(EMC_LOCAL == tcp_->type){
			struct tcp_unit unit = {flag,tcp_,msg};
			if(EMC_NOWAIT == flag){
				void * msg_r = emc_msg_alloc(emc_msg_buffer(msg), emc_msg_length(msg));
				if(!msg_r) return -1;
				emc_msg_build(msg_r, msg);
				unit.msg = msg_r;
				hashmap_foreach(tcp_->server->connection, tcp_pub_foreach_cb, &unit);
			}else{
				hashmap_foreach(tcp_->server->connection, tcp_pub_foreach_cb, &unit);
				while(0 == emc_msg_zero_ref(msg)){}
			}
		}
		break;
	}
	return 0;
}
