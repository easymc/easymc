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

#ifndef __EMC_MERGER_H_INCLUDED__
#define __EMC_MERGER_H_INCLUDED__

/******************************************************************************
				Data merge function
 For receiving data merge under the agreement, to support large data transfers
*******************************************************************************/

#ifdef __cplusplus
extern "C"{
#endif

typedef void merger_get_cb(char * data, int len, int id, void * addition);

struct merger;

// Create an unpacker, max indicates how many different socket while unpacking the maximum allowed, 
// cb callback function which returns data address
struct merger * merger_new(unsigned int count);
// Delete depacketizer
void merger_delete(struct merger * un);
// Allocate an empty unit from depacketizer
void * merger_alloc(struct merger * un);
// Initialization, the memory space previously prepared
void merger_init(void * block, int len, int packets);
// Add data to unpacking vessel, start representing the starting position to copy data
int merger_add(void * block, int no, int start, char * data, int len);
// For a complete packet from the block, the data returned by the callback function
int merger_get(void * block, merger_get_cb * cb, int id, void * addition);
// Gets the last time the data received from the block
unsigned int merger_time(void * block);
// Release has finished using the Block
void merger_free(struct merger * un, void * block);

#ifdef __cplusplus
}
#endif

#endif
