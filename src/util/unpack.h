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

#ifndef __UNPACK_H__
#define __UNPACK_H__

/**********************************************************************
  According to the agreement when unpacking socket for receiving data
**********************************************************************/

#ifdef __cplusplus
extern "C"{
#endif

struct unpack;

typedef void unpack_get_data(char* data,unsigned short len,int id,void* args);

// Create an unpacker, max indicates how many different socket while unpacking the maximum allowed, 
// cb callback function which returns data address
struct unpack * unpack_new(unsigned int count);
// Delete depacketizer
void unpack_delete(struct unpack *un);
// Allocate an empty unit from depacketizer
void* unpack_alloc(struct unpack *un);
// Writing data to the unpacker, NULL if the block is to open a new block, if the block is not empty block in the original operating
int unpack_add(void* block,char *data,int len);
// For a complete packet, the data returned by the callback function
void unpack_get(void* block,unpack_get_data *cb,int id,void* args,char *buffer);
// Release has finished using the unpacking unit
void unpack_free(struct unpack *un,void* block);

#ifdef __cplusplus
}
#endif

#endif
