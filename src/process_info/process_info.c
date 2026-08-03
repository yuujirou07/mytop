#include <stddef.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "process_info/process_info.h"

const char *device_info_dir_names[device_count] = {
        [cpu] = "cpuinfo",
        [gpu] = NULL,
        [memory] = "meminfo",
};

struct device_info_result get_device_info(enum device dev){
        struct device_info_result dev_result = {0};
        const char *home_dir_path = "/proc/";
        switch(dev){
                case cpu:{
                        char cpu_path[512] = {0};

                        //パスの連結
                        snprintf(cpu_path,
                                512,
                                "%s%s",
                                home_dir_path,device_info_dir_names[cpu]);

                        get_cpu_info(&dev_result,cpu_path);
                        return dev_result;
                }
                default:
                        return dev_result;
        }

}

//device_table->found_dev_tableはNULLにしておく
void check_detectable_parts(struct found_device_parts_table *device_table){
        if(device_table == NULL || device_table->found_dev_table != NULL)return;
        device_table->found_dev_count = 0;

        DIR *dir = NULL;
        if((dir = opendir("/proc")) == NULL){
                return;
        }
        struct dirent *dir_data = NULL;
        enum device tmp_dev[device_count];
        int dev_count = 0;
        while((dir_data = readdir(dir)) != NULL){
                for(int dev = 0;dev < device_count;dev++){
                        if(device_info_dir_names[dev] != NULL &&
                           strcmp(dir_data->d_name,device_info_dir_names[dev]) == 0){
                                tmp_dev[dev_count] = dev;
                                dev_count++;
                                break;
                        }
                }
        }
        closedir(dir);
        if(dev_count == 0)return;
        device_table->found_dev_table = malloc(sizeof(enum device) * dev_count);
        if(device_table->found_dev_table == NULL)return;
        memcpy(device_table->found_dev_table,tmp_dev,sizeof(enum device) * dev_count);
        device_table->found_dev_count = dev_count;
}
