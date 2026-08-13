#ifndef PROCESS_INFO_H
#define PROCESS_INFO_H
#include"cpu/cpu_info.h"
#include"memory.h"



enum device{
        cpu,
        gpu,
        memory,
        device_count,
};


struct found_device_parts_table{
        enum device *found_dev_table;
        int found_dev_count;
};

struct device_info_result{
        enum device device_name;
        union {
                struct cpu_info cpu_info;
                struct mem_info mem_info;
        }device_data;
};

extern const char *device_info_dir_names[device_count];





void check_detectable_parts(struct found_device_parts_table *device_table);
void get_cpu_info(struct device_info_result *dev_result,const char *path);
struct device_info_result get_device_info(enum device dev);
void get_memory_info(struct device_info_result *dev_result,const char *path);
#endif
