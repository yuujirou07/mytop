#include "memory.h"
#include<stdio.h>
#include<string.h>
#include"process_info/process_info.h"


void get_memory_info(struct device_info_result *dev_result,const char *path){
        if(dev_result == NULL || path == NULL)return;

        FILE *file = fopen(path,"r");
        if(file == NULL)return;
        struct mem_info mem_info = {0};
        char str_line_buff[512] = {0};

        while(fgets(str_line_buff,sizeof(str_line_buff),file) != NULL){
                char *value = strchr(str_line_buff,':');
                if(value == NULL)continue;

                char *key_end = value;
                while(key_end > str_line_buff &&
                      (key_end[-1] == ' ' || key_end[-1] == '\t')){
                        key_end--;
                }
                *key_end = '\0';

                value++;
                while(*value == ' ' || *value == '\t')value++;
                value[strcspn(value,"\r\n")] = '\0';

                if(strcmp(str_line_buff,"MemTotal") == 0){
                        snprintf(mem_info.mem_total,sizeof(mem_info.mem_total),"%s",value);
                }
                else if(strcmp(str_line_buff,"MemFree") == 0){
                        snprintf(mem_info.mem_free,sizeof(mem_info.mem_free),"%s",value);
                }
                else if(strcmp(str_line_buff,"MemAvailable") == 0){
                        snprintf(mem_info.mem_available,sizeof(mem_info.mem_available),"%s",value);
                }
                else if(strcmp(str_line_buff,"Buffers") == 0){
                        snprintf(mem_info.buffers,sizeof(mem_info.buffers),"%s",value);
                }
                else if(strcmp(str_line_buff,"Cached") == 0){
                        snprintf(mem_info.cached,sizeof(mem_info.cached),"%s",value);
                }
                else if(strcmp(str_line_buff,"SwapCached") == 0){
                        snprintf(mem_info.swap_cached,sizeof(mem_info.swap_cached),"%s",value);
                }
                else if(strcmp(str_line_buff,"Active") == 0){
                        snprintf(mem_info.active,sizeof(mem_info.active),"%s",value);
                }
                else if(strcmp(str_line_buff,"Inactive") == 0){
                        snprintf(mem_info.inactive,sizeof(mem_info.inactive),"%s",value);
                }
                else if(strcmp(str_line_buff,"SwapTotal") == 0){
                        snprintf(mem_info.swap_total,sizeof(mem_info.swap_total),"%s",value);
                }
                else if(strcmp(str_line_buff,"SwapFree") == 0){
                        snprintf(mem_info.swap_free,sizeof(mem_info.swap_free),"%s",value);
                }
        }

        fclose(file);
        dev_result->device_name = memory;
        dev_result->device_data.mem_info = mem_info;
}
