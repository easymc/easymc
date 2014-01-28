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

#ifndef __EMC_H_INCLUDED__
#define __EMC_H_INCLUDED__

// Version control(current version 0.1)
#define EMC_VERSION 0.1

#ifdef __cplusplus
extern "C" {
#endif

// Data type definition
#ifndef uchar
typedef unsigned char uchar;
#endif
#ifndef ushort
typedef unsigned short ushort;
#endif
#ifndef uint
typedef unsigned int uint;
#endif
#ifndef int64
#	if defined _WIN32
typedef __int64 int64;
#	else
typedef long long int64;
#	endif
#endif
#ifndef uint64
#	if defined _WIN32
typedef unsigned __int64 uint64;
#	else
typedef unsigned long long uint64;
#	endif
#endif
#if defined _WIN32
typedef uint		emc_result_t;
#define EMC_CALL	__stdcall
#else
typedef void*		emc_result_t;
#define EMC_CALL
#endif

// Monitoring response data type
struct monitor_data{
	// The following are several types of events
	int		events;
	// A unique number for each connection
	int		id;
	char	ip[16];
	int		port;
	// Monitoring event returns additional items
	void	*addition; 
};

// Equipment type definition
#define EMC_REQ		1	//request
#define EMC_REP		2	//response

#define EMC_PUB		4	//publish
#define EMC_SUB		8	//subscribe

// easymc device operating parameters
#define EMC_OPT_MONITOR			1	// Set the device to monitor events
#define EMC_OPT_CONTROL			2	// Settings are available to control device

// easymc events type
#define EMC_EVENT_ACCEPT		1	// Service to accept a new connection
#define EMC_EVENT_CONNECT		2	// The client connects to the server
#define EMC_EVENT_CLOSED		4	// Client disconnects or service disconnected
#define EMC_EVENT_SNDFAIL		8	// Send data failed
#define EMC_EVENT_SNDSUCC		16	// Send data successful

// easymc control type
#define EMC_CTL_CLOSE			1	// Initiative to close the connection,by bind the device to be effective


//emc send message flag
#define EMC_NOWAIT	1

//  Compilation symbols control
#if defined _WIN32
#   define EMC_EXP __declspec(dllexport)
#else
#   if defined __SUNPRO_C  || defined __SUNPRO_CC
#       define EMC_EXP __global
#   elif (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#       define EMC_EXP __attribute__ ((visibility("default")))
#   else
#       define EMC_EXP
#   endif
#endif

// Thread callback function type
typedef emc_result_t EMC_CALL emc_thread_cb(void *);
// Thread function
EMC_EXP int emc_thread(emc_thread_cb *cb,void *args);

// Message function definition.
EMC_EXP void *emc_msg_alloc(void *data,uint size);
// Initialize the message structure
EMC_EXP void emc_msg_build(void *msg,const void *old);
// Set the message mode
EMC_EXP void emc_msg_set_mode(void *msg,ushort mode);
// Set the message additional data.[Returns additional data by monitoring SNDSUCC/SNDFAIL events]
EMC_EXP void emc_msg_set_addition(void *msg,void *addition);
// Get the message additional data
EMC_EXP void *emc_msg_get_addition(void *msg);
// Get the message length
EMC_EXP int emc_msg_length(void *msg);
// Free the messsage
EMC_EXP int emc_msg_free(void *msg);
// Get the message buffer
EMC_EXP void *emc_msg_buffer(void *msg);


// Equipment operation function definition
EMC_EXP int emc_device(void);
EMC_EXP void emc_destory(int device);
// Set the device's option,optval value greater than 0 add option,otherwise reduce option
// opt can be a combination of multiple option
EMC_EXP int emc_set(int device,int opt,void *optval,int optlen);
EMC_EXP int emc_bind(int device,const char *ip,const ushort port);
EMC_EXP int emc_connect(int device,ushort mode,const char *ip,const ushort port);
// Control device,id is connected via monitor returns number.
EMC_EXP int emc_control(int device,int id,int ctl);
EMC_EXP int emc_close(int device);
// After processing is complete message needs to call emc_msg_free() to release.
EMC_EXP int emc_recv(int device,void **msg);
EMC_EXP int emc_send(int device,void *msg,int flag);
// Monitoring event device
EMC_EXP int emc_monitor(int device,struct monitor_data *data);

#ifdef __cplusplus
}
#endif

#endif
