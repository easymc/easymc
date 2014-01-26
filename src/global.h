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

#ifndef __EMC_GLOBAL_H_INCLUDED__
#define __EMC_GLOBAL_H_INCLUDED__

#ifdef __cplusplus
extern "C"{
#endif

// Reconnection callback function
typedef int on_reconnect_cb(void *client,void *addition);

void global_term(void);

int global_add_device(void *device_);
void *global_get_device(int id);
void global_erase_device(int id);

void *global_alloc_merger();
void global_free_merger(void *);

void *global_alloc_unapck(void);
void global_free_unpack(void *);

unsigned int global_get_data_serial();

int global_get_connect_id();
void global_idle_connect_id(int id);

int global_push_sendqueue(int id,void *p);
int global_push_head_sendqueue(int id,void *p);
int global_pop_sendqueue(int id,void **p);
unsigned int global_sendqueue_size(int id);

void *global_alloc_monitor();
void global_free_monitor(void *data);

int global_add_reconnect(int id,on_reconnect_cb *cb,void *client,void *addition);

int global_rand_number();

#ifdef __cplusplus
}
#endif

#endif
