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

#ifndef __EMC_RINGUQEUE_H_INCLUDED__
#define __EMC_RINGUQEUE_H_INCLUDED__

#ifdef __cplusplus
extern "C"{
#endif

	enum{
		// A thread is only one consumer
		_RQ_S,
		// Multithreading is only one consumer
		_RQ_M
	};

	struct ringqueue;

	/**************************************************************************
	* Name: create_ringqueue
	* Function: Create a ringqueue
	* Input: mode£º_RQ_S,_RQ_M
	* Output: N/A
	* Return: struct ringqueue pointer
	* Remark: N/A
	***************************************************************************/
	struct ringqueue * create_ringqueue(unsigned char mode);

	/**************************************************************************
	* Name: delete_ringqueue
	* Function: Destroy a ringqueue
	* Input: struct ringqueue pointer
	* Output: N/A
	* Return: N/A
	* Remark: N/A
	***************************************************************************/
	void delete_ringqueue(struct ringqueue *);

	/**************************************************************************
	* Name: get_single_consumer
	* Function: Obtain a consumer under _RQ_S mode
	* Input: struct ringqueue pointer
	* Output: N/A
	* Return: Number of consumers
	* Remark: N/A
	***************************************************************************/
	int get_single_consumer(struct ringqueue *);

	/**************************************************************************
	* Name: wait_ringqueue
	* Function: Wait ringqueue written inside the success event
	* Input: struct ringqueue pointer
	* Output: N/A
	* Return: 0 success£¬1 timeout£¬-1 failure
	* Remark: N/A
	***************************************************************************/
	int wait_ringqueue(struct ringqueue *);

	/**************************************************************************
	* Name: post_ringqueue
	* Function: Wake ringqueue write successful event inside
	* Input: struct ringqueue pointer
	* Output: N/A
	* Return: 0 success£¬1 timeout£¬-1 failure
	* Remark: N/A
	***************************************************************************/
	int post_ringqueue(struct ringqueue *);

	/**************************************************************************
	* Name: push_ringqueue
	* Function: Writing data to ringqueue
	* Input: struct ringqueue pointer,data pointer
	* Output: N/A
	* Return: 0 success£¬-1 failure
	* Remark: N/A
	***************************************************************************/
	int push_ringqueue(struct ringqueue *,void *);

	/**************************************************************************
	* Name: pop_ringqueue_single
	* Function: _RQ_S Mode, the pop-up data from ringqueue
	* Input: Struct ringqueue pointer,Number of consumers(get_single_consumer)
	* Output: Data pointer
	* Return: 0 success£¬-1 failure
	* Remark: N/A
	***************************************************************************/
	int pop_ringqueue_single(struct ringqueue *,int ,void **);

	/**************************************************************************
	* Name: pop_ringqueue_multiple
	* Function: _RQ_M Mode, the pop-up data from ringqueue
	* Input: struct ringqueue pointer
	* Output: Data poiner
	* Return: 0 success£¬-1 failure
	* Remark: N/A
	***************************************************************************/
	int pop_ringqueue_multiple(struct ringqueue *,void **);

	/**************************************************************************
	* Name: check_ringqueue_single
	* Function: Detection _RB_S mode ringqueue whether data has not eject
	* Input: Struct ringqueue pointer,Number of consumers
	* Output: N/A
	* Return: 0 empty£¬1 not empty
	* Remark: N/A
	***************************************************************************/
	uint check_ringqueue_single(struct ringqueue *,int);

	/**************************************************************************
	* Name: check_ringqueue_multiple
	* Function: Detection _RB_M mode ringqueue whether data has not eject
	* Input: struct ringqueue pointer
	* Output: N/A
	* Return: 0 empty£¬1 not empty
	* Remark: N/A
	***************************************************************************/
	uint check_ringqueue_multiple(struct ringqueue *);
#ifdef __cplusplus
}
#endif

#endif
