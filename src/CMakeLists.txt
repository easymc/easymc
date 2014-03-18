 # Copyright (c) 2014, mashka <easymc2014@gmail.com>
 # All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions are met:
 #
 #   * Redistributions of source code must retain the above copyright notice,
 #     this list of conditions and the following disclaimer.
 #   * Redistributions in binary form must reproduce the above copyright
 #     notice, this list of conditions and the following disclaimer in the
 #     documentation and/or other materials provided with the distribution.
 #   * Neither the name of easymc nor the names of its contributors may be used
 #     to endorse or promote products derived from this software without
 #     specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 # AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 # ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 # LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 # CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 # SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 # INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 # CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 # ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 # POSSIBILITY OF SUCH DAMAGE.


set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_COMPILER_IS_GNUCC)
	set (CMAKE_C_FLAGS "-g -o2 -march=i486")
endif ()

set (	EMC_SOURCES
	emc.h
	config.h
	ipc.h
	tcp.h
	global.h
	device.h
	msg.h
	plug.h
	common.h
	util/memory/jemalloc.h
	util/memory/jemalloc_config.h
	util/memory/jemalloc_types.h
	util/memory/linkedlist.h
	util/memory/osx_zone_types.h
	util/memory/ql.h
	util/memory/qr.h
	util/memory/rb.h
	util/hashmap.h
	util/queue.h
	util/ringqueue.h
	util/event.h
	util/idqueue.h
	util/thread.h
	util/utility.h
	util/unpack.h
	util/map.h
	util/merger.h
	util/sendqueue.h
	util/ringbuffer.h
	util/ringarray.h
	util/lock.h
	util/uniquequeue.h

	ipc.c
	msg.c
	device.c
	tcp.c
	global.c
	plug.c
	util/hashmap.c
	util/ringqueue.c
	util/event.c
	util/idqueue.c
	util/thread.c
	util/utility.c
	util/unpack.c
	util/map.c
	util/merger.c
	util/sendqueue.c
	util/ringbuffer.c
	util/ringarray.c
	util/uniquequeue.c
	util/memory/jemalloc.c
)

add_library (easymc SHARED ${EMC_SOURCES})
if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_link_libraries (easymc ws2_32 winmm)
else ()
	target_link_libraries (easymc pthread rt)
endif ()
