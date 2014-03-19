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

// Version control
#define EMC_VERSION 0.4

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
typedef uint				emc_cb_t;
typedef void*				emc_result_t;
#define EMC_CALL			__stdcall
#define EMC_BIND			__cdecl
#else
typedef void*				emc_cb_t;
typedef unsigned long int	emc_result_t;
#define EMC_CALL			__attribute__((__stdcall__))
#define EMC_BIND			__attribute__((__cdecl__))
#endif

/* Errors */

// Custom errno range starting value
#define EMC_CUSTOM_ERRNO	-214748360

/* some standard POSIX errnos are not defined */
#ifndef EUSERS
#define EUSERS	(EMC_CUSTOM_ERRNO-1)
#define EMC_EUSERS_DEFINED
#endif

#ifndef ENOPLUG
#define ENOPLUG	(EMC_CUSTOM_ERRNO-2)
#define EMC_ENOPLUG_DEFINED
#endif

#ifndef EREBIND
#define EREBIND	(EMC_CUSTOM_ERRNO-3)
#define EMC_EREBIND_DEFINED
#endif

#ifndef ENOSOCK
#define ENOSOCK (EMC_CUSTOM_ERRNO-4)
#define EMC_ENOSOCK_DEFINED
#endif

#ifndef ENOLIVE
#define ENOLIVE (EMC_CUSTOM_ERRNO-5)
#define EMC_ENOLIVE_DEFINED
#endif

#ifndef EQUEUE
#define EQUEUE	(EMC_CUSTOM_ERRNO-6)
#define EMC_EQUEUE_DEFINED
#endif

#ifndef EMODE
#define EMODE	(EMC_CUSTOM_ERRNO-7)
#define EMC_EMODE_DEFINED
#endif

#ifndef ENOEXIST
#define ENOEXIST (EMC_CUSTOM_ERRNO-8)
#define EMC_ENOEXIST_DEFINED
#endif

#ifndef ENODEVICE
#define ENODEVICE	(EMC_CUSTOM_ERRNO-9)
#define EMC_ENODEVICE_DEFINED
#endif

#ifndef EINVAL
#define EINVAL	(EMC_CUSTOM_ERRNO-10)
#define EMC_EINVAL_DEFINED
#endif

#ifndef ENOMEM
#define ENOMEM	(EMC_CUSTOM_ERRNO-11)
#define EMC_ENOMEM_DEFINED
#endif

#ifndef ETIME
#define ETIME	(EMC_CUSTOM_ERRNO-12)
#define EMC_ETIME_DEFINED
#endif

// Monitoring response data type
struct monitor_data{
	// Monitoring messages from which plug
	int		plug;
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
#define EMC_OPT_CONTROL			2	// Settings are available to control plug
#define EMC_OPT_THREAD			4	// Set the device thread number,valid only for tcp

// easymc events type
#define EMC_EVENT_ACCEPT		1	// Service to accept a new connection
#define EMC_EVENT_CONNECT		2	// The client connects to the server
#define EMC_EVENT_CLOSED		4	// Client disconnects or service disconnected
#define EMC_EVENT_SNDFAIL		8	// Send data failed
#define EMC_EVENT_SNDSUCC		16	// Send data successful

// easymc control type
#define EMC_CTL_CLOSE			1	// Initiative to close the connection,by bind the plug to be effective


// emc send message flag
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

// Get the errorno from the library
EMC_EXP int EMC_BIND emc_errno(void);

// Returns the errorno description
EMC_EXP const char * EMC_BIND emc_errno_str(int errn);

// Thread callback function type
typedef emc_cb_t EMC_CALL emc_thread_cb(void *);
// Thread function
EMC_EXP emc_result_t EMC_BIND emc_thread(emc_thread_cb * cb, void * args);
// Wait thread end
EMC_EXP int EMC_BIND emc_thread_join(emc_result_t ert);

// Message function definition.
EMC_EXP void * EMC_BIND emc_msg_alloc(void * data, uint size);
// Initialize the message structure
EMC_EXP void EMC_BIND emc_msg_build(void * msg, const void * old);
// Set the message mode
EMC_EXP void EMC_BIND emc_msg_set_mode(void * msg, ushort mode);
// Get the message mode
EMC_EXP ushort EMC_BIND emc_msg_get_mode(void * msg);
// Get the client id from message
EMC_EXP int EMC_BIND emc_msg_getid(void * msg);
// Set to send or receive this message client id
EMC_EXP void EMC_BIND emc_msg_setid(void * msg, int id);
// Set the message additional data.[Returns additional data by monitoring SNDSUCC/SNDFAIL events]
EMC_EXP void EMC_BIND emc_msg_set_addition(void * msg, void * addition);
// Get the message length
EMC_EXP int EMC_BIND emc_msg_length(void * msg);
// Free the messsage
EMC_EXP int EMC_BIND emc_msg_free(void * msg);
// Get the message buffer
EMC_EXP void * EMC_BIND emc_msg_buffer(void * msg);


// Equipment operation function definition
EMC_EXP int EMC_BIND emc_device(void);
EMC_EXP void EMC_BIND emc_destory(int device);
// Set the device's option,optval value greater than 0 add option,otherwise reduce option
// opt can be a combination of multiple option
EMC_EXP int EMC_BIND emc_set(int device, int opt, void * optval, int optlen);

EMC_EXP int EMC_BIND emc_plug(int device);
EMC_EXP int EMC_BIND emc_bind(int plug, const char * ip, const ushort port);
EMC_EXP int EMC_BIND emc_connect(int plug, ushort mode, const char * ip, const ushort port);
// Control plug,id is connected via monitor returns number.
EMC_EXP int EMC_BIND emc_control(int plug, int id, int ctl);
EMC_EXP int EMC_BIND emc_close(int plug);
// After processing is complete message needs to call emc_msg_free() to release.
EMC_EXP int EMC_BIND emc_recv(int plug, void ** msg, int flag);
EMC_EXP int EMC_BIND emc_send(int plug, void * msg, int flag);

// Monitoring the device event
EMC_EXP int EMC_BIND emc_monitor(int device, struct monitor_data * data, int flag);

#ifdef __cplusplus
}
#endif

#endif
