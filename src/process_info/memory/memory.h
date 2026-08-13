#ifndef MEMORY_H
#define MEMORY_H

#define mem_info_value_size 256



struct mem_info{
        char mem_total[mem_info_value_size];
        char mem_free[mem_info_value_size];
        char mem_available[mem_info_value_size];
        char buffers[mem_info_value_size];
        char cached[mem_info_value_size];
        char swap_cached[mem_info_value_size];
        char active[mem_info_value_size];
        char inactive[mem_info_value_size];
        char swap_total[mem_info_value_size];
        char swap_free[mem_info_value_size];
};

void memory_size_ctl(float *total,int flags);

#endif
