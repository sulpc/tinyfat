#pragma once

#include "util_heap.h"

static inline void* tf_malloc(util_size_t size)
{
    void* p = util_malloc(size);
    return p;
}

static inline void tf_free(void* ptr)
{
    util_free(ptr);
}
