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

#ifndef __EMC_EVENT_H_INCLUDED__
#define __EMC_EVENT_H_INCLUDED__

/*************************************************************/
/*				linux and window events						 */
/*************************************************************/

#ifdef __cplusplus
extern "C"{
#endif

struct event;
/**************************************************************************
* Name: create_event
* Function: Create an event
* Input: N/A
* Output: N/A
* Return: event pointer
* Remark: N/A
***************************************************************************/
struct event* create_event(void);

/**************************************************************************
* Name: delete_event
* Function: Delete an event
* Input: event pointer
* Output: N/A
* Return: N/A
* Remark: N/A
***************************************************************************/
void delete_event(struct event * evt);

/**************************************************************************
* Name: wait_event
* Function: Wait incident
* Input: event pointer,kn_int Timeout number of milliseconds(-1Wait forever)
* Output: N/A
* Return: 0 Mean success£¬1 Mean timeout£¬-1 Mean failure
* Remark: N/A
***************************************************************************/
int wait_event(struct event * evt,int timeout);

/**************************************************************************
* Name: post_event
* Function: Activation event
* Input: event pointer
* Output: N/A
* Return: 0 Mean success£¬-1 Mean failure
* Remark: N/A
***************************************************************************/
int post_event(struct event * evt);

#ifdef __cplusplus
}
#endif

#endif
