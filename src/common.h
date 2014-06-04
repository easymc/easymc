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

#ifndef __EMC_COMMON_H_INCLUDED__
#define __EMC_COMMON_H_INCLUDED__

#include "config.h"

#pragma pack(1)
// Packet protocol
struct ipc_data_unit{
	// Command
	uchar		cmd;
	// The number of clients receiving the data
	int			id;
	// Data unique serial number
	int			serial;
	// The total length
	int			total;
	// The packet sequence number
	int			no;
};

struct tcp_data_unit{
	// Command
	uchar		cmd;
	// Data unique serial number
	int			serial;
	// The total length
	int			total;
	// The packet sequence number
	int			no;
};

// Serial number
union data_serial{
	struct{
		// The number of clients receiving the data
		int		id; 
		// Data unique serial number
		int		serial;
	};
	int64		no;
};

// Reconnection data structure
struct reconnect{
	//callback function
	void		*cb;		
	// Reconnect the client's pointer
	void		*client;
	// Additional data
	void		*addition;
};
#pragma pack()

#endif
