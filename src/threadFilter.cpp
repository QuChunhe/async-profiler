/*
 * Copyright 2020 Andrei Pangin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <string.h>
#include "threadFilter.h"

    
ThreadFilter::ThreadFilter() {
    memset(_bitmap, 0, sizeof(_bitmap));
    _enabled = false;
}

ThreadFilter::~ThreadFilter() {
    for (int i = 0; i < MAX_BITMAPS; i++) {
        free(_bitmap[i]);
    }
}

void ThreadFilter::init(const char* filter) {
    _enabled = filter != NULL;
}

void ThreadFilter::clear() {
    for (int i = 0; i < MAX_BITMAPS; i++) {
        if (_bitmap[i] != NULL) {
            memset(_bitmap[i], 0, BITMAP_SIZE);
        }
    }
}

bool ThreadFilter::accept(int thread_id) {
    u32* b = bitmap(thread_id);
    return b != NULL && (word(b, thread_id) & (1 << (thread_id & 0x1f)));
}

void ThreadFilter::add(int thread_id) {
    u32* b = bitmap(thread_id);
    if (b == NULL) {
        MutexLocker ml(_lock);
        b = bitmap(thread_id);
        if (b == NULL) {
            b = (u32*)calloc(1, BITMAP_SIZE);
            _bitmap[(u32)thread_id / BITMAP_CAPACITY] = b;
        }
    }
    __sync_or_and_fetch(&word(b, thread_id), 1 << (thread_id & 0x1f));
}

void ThreadFilter::remove(int thread_id) {
    u32* b = bitmap(thread_id);
    if (b != NULL) {
        __sync_and_and_fetch(&word(b, thread_id), ~(1 << (thread_id & 0x1f)));
    }
}
