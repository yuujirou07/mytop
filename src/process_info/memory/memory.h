#ifndef MEMORY_H
#define MEMORY_H

#include<stdint.h>
//各値の単位は/proc/meminfoと同じkB。
struct mem_info{
        uint64_t mem_total;
        uint64_t mem_free;
        uint64_t mem_available;
        uint64_t buffers;
        uint64_t cached;
        uint64_t swap_cached;
        uint64_t active;
        uint64_t inactive;
        uint64_t swap_total;
        uint64_t swap_free;
};

void memory_size_ctl(float *total,int flags);

#endif
