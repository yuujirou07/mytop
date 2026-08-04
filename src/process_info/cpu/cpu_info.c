#include <stdatomic.h>
#include <stddef.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include "process_info/process_info.h"
#include"cpu_info.h"

static int cpu_core_info(int *num,int flags);
static int **core_palm_data = NULL;//各コアの直近100件のデータ
static int *core_palm_count = NULL;//コアに何件入っているか
static int core_palm_num = 0;

void get_cpu_info(struct device_info_result *dev_result,const char *path){
        if(dev_result == NULL || path == NULL)return;

        FILE *file = fopen(path,"r");
        if(file == NULL)return;
        struct cpu_info cpu_info = {0};
        char file_line_buff[512] = {0};

        int cpu_info_member_count = 0;
        while(fgets(file_line_buff,sizeof(file_line_buff),file) != NULL){
                if(file_line_buff[0] == '\n' || file_line_buff[0] == '\r' || 
                        cpu_info_member_count > cpu_info_member_size)break;

                char *value = strchr(file_line_buff,':');
                if(value == NULL)continue;

                char *key_end = value;
                while(key_end > file_line_buff &&
                      (key_end[-1] == ' ' || key_end[-1] == '\t')){
                        key_end--;
                }
                *key_end = '\0';

                value++;
                while(*value == ' ' || *value == '\t')value++;
                value[strcspn(value,"\r\n")] = '\0';

                if(strcmp(file_line_buff,"processor") == 0){
                        cpu_info.processor = atoi(value);
                }
                else if(strcmp(file_line_buff,"cpu family") == 0){
                        snprintf(cpu_info.cpu_family,sizeof(cpu_info.cpu_family),"%s",value);
                }
                else if(strcmp(file_line_buff,"model") == 0){
                        snprintf(cpu_info.model,sizeof(cpu_info.model),"%s",value);
                }
                else if(strcmp(file_line_buff,"model name") == 0){
                        snprintf(cpu_info.model_name,sizeof(cpu_info.model_name),"%s",value);
                }
                else if(strcmp(file_line_buff,"cpu MHz") == 0){
                        snprintf(cpu_info.cpu_MHz,sizeof(cpu_info.cpu_MHz),"%s",value);
                }
                else if(strcmp(file_line_buff,"cache size") == 0){
                        snprintf(cpu_info.cache_size,sizeof(cpu_info.cache_size),"%s",value);
                }
                else if(strcmp(file_line_buff,"physical id") == 0){
                        snprintf(cpu_info.physical_id,sizeof(cpu_info.physical_id),"%s",value);
                }
                else if(strcmp(file_line_buff,"siblings") == 0){
                        snprintf(cpu_info.siblings,sizeof(cpu_info.siblings),"%s",value);
                }
                else if(strcmp(file_line_buff,"core id") == 0){
                        snprintf(cpu_info.core_id,sizeof(cpu_info.core_id),"%s",value);
                }
                else if(strcmp(file_line_buff,"cpu cores") == 0){
                        snprintf(cpu_info.cpu_cores,sizeof(cpu_info.cpu_cores),"%s",value);
                }
                else if(strcmp(file_line_buff,"fpu") == 0){
                        snprintf(cpu_info.fpu,sizeof(cpu_info.fpu),"%s",value);
                }
                else if(strcmp(file_line_buff,"wp") == 0){
                        snprintf(cpu_info.wp,sizeof(cpu_info.wp),"%s",value);
                }
                cpu_info_member_count++;
        }

        fclose(file);
        int cpu_core = atoi(cpu_info.siblings);
        if(atoi(cpu_info.cpu_cores) > 0){
                cpu_info.thread = cpu_core / atoi(cpu_info.cpu_cores);
        }

        cpu_core_info(&cpu_core,core_set);
        dev_result->device_name = cpu;
        dev_result->device_data.cpu_info = cpu_info;
}



void set_core_usage_rate(int core_id,int usage){
        if(core_palm_data == NULL || core_palm_count == NULL){
                cpu_core_info(&core_palm_num,core_get);
                //get_cpu_info()より先に呼ばれるとコア数が未設定なので端末から直接取る
                if(core_palm_num <= 0){
                        long online_core = sysconf(_SC_NPROCESSORS_ONLN);
                        if(online_core <= 0)return;
                        core_palm_num = (int)online_core;
                        cpu_core_info(&core_palm_num,core_set);
                }
                int **tmp_palm_data = calloc(core_palm_num,sizeof(int *));
                int *tmp_palm_count = calloc(core_palm_num,sizeof(int));
                if(tmp_palm_data == NULL || tmp_palm_count == NULL){
                        free(tmp_palm_data);
                        free(tmp_palm_count);
                        return;
                }
                core_palm_data = tmp_palm_data;
                core_palm_count = tmp_palm_count;
        }

        if(core_id < 0 || core_id >= core_palm_num)return;
        if(usage < 0)usage = 0;
        else if(usage > 100)usage = 100;

        if(core_palm_data[core_id] == NULL){
                core_palm_data[core_id] = calloc(100,sizeof(int));
                if(core_palm_data[core_id] == NULL)return;
        }
        if(core_palm_count[core_id] >= 100){
                memmove(&core_palm_data[core_id][0],&core_palm_data[core_id][1],sizeof(int) * 99);
                core_palm_count[core_id]--;
        }

        core_palm_data[core_id][core_palm_count[core_id]] = usage;
        core_palm_count[core_id]++;
}

struct core_use_data get_core_usage_rate(int core_id){
        struct core_use_data result = {0};

        if(core_palm_data == NULL || core_palm_count == NULL)return result;
        if(core_id < 0 || core_id >= core_palm_num)return result;
        if(core_palm_data[core_id] == NULL)return result;

        result.core_palm_data = core_palm_data[core_id];
        result.core_palm_size = core_palm_count[core_id];
        return result;
}

static int cpu_core_info(int *num,int flags){
        if(num == NULL)return -1;
        static int core_num = 0;
        switch(flags){
                case core_set:
                        core_num = *num;
                        return 0;
                        break;
                case core_get:
                        *num = core_num;
                        return core_num;
        }
        return 0;
}
