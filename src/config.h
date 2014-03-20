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

#ifndef __EMC_CONFIG_H_INCLUDED__
#define __EMC_CONFIG_H_INCLUDED__

#if defined (EMC_WINDOWS)
#include <winsock2.h>
#include <windows.h>
#include <time.h>
#include <memory.h>
#include <mmsystem.h>
#else
#include <pthread.h>
#include <sys/time.h>
#include <sys/sem.h>
#include <semaphore.h>
#include <unistd.h>
#include <iconv.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#endif
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>


#define _shutdown_socket    shutdown
#if defined (EMC_WINDOWS)
#define _close_socket(s) do{ \
	_shutdown_socket(s,SD_BOTH); \
	closesocket(s); \
}while(0)
#else
#define _close_socket(s) do{ \
	_shutdown_socket(s,2); \
	close(s); \
}while(0)
#endif

#if defined (EMC_WINDOWS)
#define _nonblocking(s,flag) ioctlsocket(s,FIONBIO,(u_long*)&flag)
#else
#define _nonblocking(s,flag)  fcntl(s,F_SETFL,fcntl(s,F_GETFL)|O_NONBLOCK)
#endif

#ifdef EMC_WINDOWS
#define emc_mb	MemoryBarrier
#else
#define emc_mb	__sync_synchronize
#endif

#if defined (EMC_WINDOWS)
#define emc_inline	__inline
#else
#define emc_inline	inline
#endif

// The maximum number of supported socket
#define EMC_SOCKETS_DEFAULT 0x4000

// Server mode
#define EMC_NONE	 0

// Readable, writable event
#define EMC_READABLE 1
#define EMC_WRITABLE 2

// tcp, ipc modes: local, remote
#define EMC_LOCAL	1
#define EMC_REMOTE	2
// Communication modes: inter-process and cross-machine
#define EMC_IPC		1
#define EMC_TCP		2
// Survival data identification
#define EMC_LIVE	0xACACCAFE
#define EMC_DEAD	0xDEADDEAD
// IOCP error code
#if defined (EMC_WINDOWS)
#ifndef STATUS_REMOTE_DISCONNECT
#define STATUS_REMOTE_DISCONNECT		(0xC000013CL)
#endif
#ifndef STATUS_LOCAL_DISCONNECT
#define STATUS_LOCAL_DISCONNECT			(0xC000013BL)
#endif
#ifndef STATUS_CANCELLED
#define STATUS_CANCELLED				(0xC0000120L)
#endif
#endif
// Path length
#if defined (EMC_WINDOWS)
#define PATH_LEN	MAX_PATH
#else
#define PATH_LEN	PATH_MAX
#endif

#define ADDR_LEN	16

// Packet size
#define MAX_DATA_SIZE	8192
// Protocol packet size
#define MAX_PROTOCOL_SIZE	8196
//tcp data length
#define TCP_DATA_SIZE	8179
//ipc data length
#define IPC_DATA_SIZE	8175
// Header logo
#define EMC_HEAD		0x5876

// Login command
#define EMC_CMD_LOGIN	0x61
// Logout command
#define EMC_CMD_LOGOUT	0x62
// Data command
#define EMC_CMD_DATA	0x63

//Loopback address 127.0.0.1
#define LOOPBACK		0x100007F

// Handle is readable, writable
#define EMC_READ		1
#define EMC_WRITE		2

// Wait function
#if defined (EMC_WINDOWS)
#define nsleep(x)	Sleep(x)
#else
#define nsleep(x)	usleep(x*1000)
#define sprintf_s	snprintf
#endif

#endif
