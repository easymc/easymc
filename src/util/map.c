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

#include "../emc.h"
#include "../config.h"
#include "map.h"

#pragma pack(1)
struct map_node{
	int64	key;
	void	*p;
};

struct map{
	struct map_node		*node;
	volatile uint		size;
	volatile uint		used;
#if defined (EMC_WINDOWS)
	CRITICAL_SECTION	lock;                    
#else
	pthread_mutex_t		lock;
#endif
};
#pragma pack()

static __inline void lock_map(struct map * m){
#if defined (EMC_WINDOWS)
	EnterCriticalSection(&m->lock);
#else
	pthread_mutex_lock(&m->lock);
#endif
}

static __inline void unlock_map(struct map * m){
#if defined (EMC_WINDOWS)
	LeaveCriticalSection(&m->lock);
#else
	pthread_mutex_unlock(&m->lock);
#endif
}

static int map_sort_swap(struct map * m, int64 key, void * val){
	int low=0, mid=0, high=m->used-1;

	while(high>=0 && low<m->used  && low <= high){
		mid = (low+high)/2;
		if(key == m->node[mid].key) return -1;
		else if(key < m->node[mid].key){
			high = mid-1;
		}else{
			low = mid+1;
		}
	}

	if(m->used){
		if(low >= m->used){
			m->node[m->used].key = key;
			m->node[m->used].p = val;
		}else{
			memmove(m->node+low+1, m->node+low, (m->used-low)*sizeof(struct map_node));
			m->node[low].key = key;
			m->node[low].p = val;
		}
	}else{
		m->node[low].key = key;
		m->node[low].p = val;
	}
	m->used ++;
	return 0;
}

static int map_search_cb(struct map * m, int64 key){
	int low=0, mid=0, high=m->used-1;
	while(high>=0 && low<m->used && low <= high){
		mid = (low+high)/2;
		if(key == m->node[mid].key){
			return mid;
		}
		else if(key < m->node[mid].key){
			high = mid-1;
		}else{
			low = mid+1;
		}
	}
	return -1;
}

struct map * create_map(int size){
	struct map * m = (struct map *)malloc(sizeof(struct map));
	if(!m) return NULL;
	memset(m, 0, sizeof(struct map));
	m->size = size;
	m->node = (struct map_node*)malloc(sizeof(struct map_node) * size);
	memset(m->node, 0, sizeof(struct map_node) * size);
#if defined (EMC_WINDOWS)
	InitializeCriticalSection(&m->lock);
#else
	pthread_mutex_init(&m->lock,NULL);		
#endif
	return m;
}

void delete_map(struct map * m){
	free(m->node);
#if defined (EMC_WINDOWS)
	DeleteCriticalSection(&m->lock);
#else
	pthread_mutex_destroy(&m->lock);		
#endif
	free(m);
}

int map_add(struct map * m, int64 key, void * val){
	lock_map(m);
	if(map_search_cb(m, key) >= 0){
		unlock_map(m);
		return -1;
	}
	if(m->used >= m->size){
		m->node = (struct map_node*)realloc(m->node, 2*m->size*sizeof(struct map_node));
		m->size *= 2;
	}
	map_sort_swap(m, key, val);
	unlock_map(m);
	return 0;
}

int map_get(struct map * m, int64 key, void ** val){
	int n = -1;
	if(val) *val = NULL;

	lock_map(m);
	if(!m->used){
		unlock_map(m);
		return -1;
	}
	n = map_search_cb(m, key);
	if(n < 0){
		unlock_map(m);
		return -1;
	}
	if(val)	*val = m->node[n].p;
	unlock_map(m);
	return 0;
}

int map_set(struct map * m, int64 key, void * val){
	int n = -1;

	lock_map(m);
	if(!m->used){
		unlock_map(m);
		return -1;
	}
	n = map_search_cb(m, key);
	if(n < 0){
		unlock_map(m);
		return -1;
	}
	m->node[n].p = val;
	unlock_map(m);
	return 0;
}

int	map_erase(struct map * m, int64 key){
	int n = -1;
	struct map_node * _node = NULL;

	lock_map(m);
	if(!m->used){
		unlock_map(m);
		return -1;
	}
	n = map_search_cb(m, key);
	if(n < 0){
		unlock_map(m);
		return -1;
	}
	m->node[n].key = -1;
	m->node[n].p = NULL;
	_node = &m->node[n];
	if(_node != &m->node[m->used-1]){
		uint index = ((char*)_node-(char*)&m->node[0])/sizeof(struct map_node);
		memmove(m->node+index, m->node+index+1, (m->used-1-index)*(sizeof(struct map_node)));
	}
	m->used --;
	unlock_map(m);
	return 0;
}

int	map_erase_nonlock(struct map * m, int64 key){
	int n = -1;
	struct map_node * _node = NULL;
	if(!m->used){
		return -1;
	}
	n = map_search_cb(m, key);
	if(n < 0){
		return -1;
	}
	m->node[n].key = -1;
	m->node[n].p = NULL;
	_node = &m->node[n];
	if(_node != &m->node[m->used-1]){
		uint index = ((char*)_node-(char*)&m->node[0])/sizeof(struct map_node);
		memmove(m->node+index, m->node+index+1, (m->used-1-index)*(sizeof(struct map_node)));
	}
	m->used --;
	return 0;
}

void map_foreach(struct map * m, map_foreach_cb * cb, void * addition){
	int index = 0;

	lock_map(m);
	for(index=0; index<m->used; index++){
		if(cb){
			if(cb(m, m->node[index].key, m->node[index].p, addition)){
				index --;
			}
		}
	}
	unlock_map(m);
}

uint map_size(struct map * m){
	uint size = 0;
	lock_map(m);
	size = m->used;
	unlock_map(m);
	return size;
}

void map_clear(struct map * m){
	lock_map(m);
	m->used = 0;
	unlock_map(m);
}
