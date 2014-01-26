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

#ifndef __EMC_RINGBUFFER_H_INCLUDE__
#define __EMC_RINGBUFFER_H_INCLUDE__

#ifdef __cplusplus
extern "C"{
#endif

	struct ringbuffer;

	void init_ringbuffer(struct ringbuffer *);

	/**************************************************************************
	* Name: push_ringbuffer
	* Function: Writes a value to ringbuffer inside
	* Input: struct ringbuffer pointer,data pointer,data length
	* Output: N/A
	* Return: 0 Success£¬-1 Failure
	* Remark: N/A
	***************************************************************************/
	int push_ringbuffer(struct ringbuffer *,void *,uint);

	/**************************************************************************
	* Name: pop_ringbuffer
	* Function: Pop-up data from the ringbuffer
	* Input: struct ringbuffer pointer,Incoming buffer address from the outside
	* Output: N/A
	* Return: 0 Success£¬-1 Failure
	* Remark: N/A
	***************************************************************************/
	int pop_ringbuffer(struct ringbuffer *,void *);

#ifdef __cplusplus
}
#endif

#endif
