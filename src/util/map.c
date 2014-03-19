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
#include "lock.h"
#include "map.h"

#pragma pack(1)
struct map_node{
	int64	key;
	void	*p;
};

struct map{
	struct map_node	*	node;
	volatile uint		size;
	volatile uint		used;
	volatile uint		lock;
	volatile uint		foreach;
};
#pragma pack()

static int map_sort_swap(struct map * m, int64 key, void * val){
	int low = 0, mid = 0, high = m->used - 1;

	while(high >= 0 && low < m->used  && low <= high){
		mid = (low+high) / 2;
		if(key == m->node[mid].key) return -1;
		else if(key < m->node[mid].key){
			high = mid - 1;
		}else{
			low = mid + 1;
		}
	}

	if(m->used){
		if(low >= m->used){
			m->node[m->used].key = key;
			m->node[m->used].p = val;
		}else{
			memmove(m->node+low + 1, m->node + low, (m->used - low) * sizeof(struct map_node));
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
	int low = 0, mid = 0, high = m->used - 1;
	while(high >= 0 && low < m->used && low <= high){
		mid = (low + high) / 2;
		if(key == m->node[mid].key){
			return mid;
		}
		else if(key < m->node[mid].key){
			high = mid - 1;
		}else{
			low = mid + 1;
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
	return m;
}

void delete_map(struct map * m){
	free(m->node);
	free(m);
}

int map_add(struct map * m, int64 key, void * val){
	emc_lock(&m->lock);
	if(map_search_cb(m, key) >= 0){
		emc_unlock(&m->lock);
		return -1;
	}
	if(m->used >= m->size){
		m->node = (struct map_node*)realloc(m->node, 2 * m->size * sizeof(struct map_node));
		m->size *= 2;
	}
	map_sort_swap(m, key, val);
	emc_unlock(&m->lock);
	return 0;
}

int map_get(struct map * m, int64 key, void ** val){
	int n = -1, locked = 0;
	if(val) *val = NULL;
	if(!m->foreach){
		emc_lock(&m->lock);
		locked = 1;
	}
	if(!m->used){
		if(locked){
			emc_unlock(&m->lock);
		}
		return -1;
	}
	n = map_search_cb(m, key);
	if(n < 0){
		if(locked){
			emc_unlock(&m->lock);
		}
		return -1;
	}
	if(val)	*val = m->node[n].p;
	if(locked){
		emc_unlock(&m->lock);
	}
	return 0;
}

int map_set(struct map * m, int64 key, void * val){
	int n = -1, locked = 0;

	if(!m->foreach){
		emc_lock(&m->lock);
		locked = 1;
	}
	if(!m->used){
		if(locked){
			emc_unlock(&m->lock);
		}
		return -1;
	}
	n = map_search_cb(m, key);
	if(n < 0){
		if(locked){
			emc_unlock(&m->lock);
		}
		return -1;
	}
	m->node[n].p = val;
	if(locked){
		emc_unlock(&m->lock);
	}
	return 0;
}

int	map_erase(struct map * m, int64 key){
	int n = -1, locked = 0;
	struct map_node * _node = NULL;

	if(!m->foreach){
		emc_lock(&m->lock);
		locked = 1;
	}
	if(!m->used){
		if(locked){
			emc_unlock(&m->lock);
		}
		return -1;
	}
	n = map_search_cb(m, key);
	if(n < 0){
		if(locked){
			emc_unlock(&m->lock);
		}
		return -1;
	}
	m->node[n].key = -1;
	m->node[n].p = NULL;
	_node = &m->node[n];
	if(_node != &m->node[m->used - 1]){
		uint index = ((char*)_node - (char*)&m->node[0]) / sizeof(struct map_node);
		memmove(m->node + index, m->node + index + 1, (m->used - 1 - index) * (sizeof(struct map_node)));
	}
	m->used --;
	if(locked){
		emc_unlock(&m->lock);
	}
	return 0;
}

void map_foreach(struct map * m, map_foreach_cb * cb, void * addition){
	int index = 0;

	emc_lock(&m->lock);
	emc_lock(&m->foreach);
	for(index = 0; index < m->used; index ++){
		if(cb){
			if(cb(m, m->node[index].key, m->node[index].p, addition)){
				index --;
			}
		}
	}
	emc_unlock(&m->foreach);
	emc_unlock(&m->lock);
}

uint map_size(struct map * m){
	uint size = 0, locked = 0;
	if(!m->foreach){
		emc_lock(&m->lock);
		locked = 1;
	}
	size = m->used;
	if(locked){
		emc_unlock(&m->lock);
	}
	return size;
}

void map_clear(struct map * m){
	emc_lock(&m->lock);
	m->used = 0;
	emc_unlock(&m->lock);
}
