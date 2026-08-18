#include <stddef.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "process_info/process_info.h"

const char *device_info_dir_names[device_count] = {
        [cpu_data] = "cpuinfo",
        [gpu] = NULL,
        [memory] = "meminfo",
        [strage] = "mounts",
        [process] = "self",
};

// 指定デバイスに対応するprocfs情報を取得する。
// 引数: devはcpu_data、memory、process。戻り値: 取得結果。未対応時はゼロ初期化値。
// processで返すprocess_info配列は呼び出し側がfree()する。
struct device_info_result get_device_info(enum device dev){
        struct device_info_result dev_result = {0};
        const char *home_dir_path = "/proc/";
        switch(dev){
                case cpu_data:{
                        char cpu_path[512] = {0};

                        //パスの連結
                        snprintf(cpu_path,
                                512,
                                "%s%s",
                                home_dir_path,device_info_dir_names[cpu_data]);

                        get_cpu_info(&dev_result,cpu_path);
                        return dev_result;
                }
                case memory:{
                        char mem_path[512] = {0};
                        int joint_result = snprintf(mem_path,512,
                                "%s%s",
                                home_dir_path,device_info_dir_names[memory]);
                        mem_path[joint_result] = '\0';
                        get_memory_info(&dev_result,mem_path);
                        return dev_result;
                }
                case strage:{
                        get_strage_info(&dev_result,"/");
                        return dev_result;
                }
                case process:{
                        DIR *process_dir = opendir(home_dir_path);
                        if(process_dir == NULL)return dev_result;

                        struct process_list process_list = {0};
                        int allocated_num = 0;
                        struct dirent *dir_data = NULL;
                        while((dir_data = readdir(process_dir)) != NULL){
                                size_t dir_name_len = strlen(dir_data->d_name);
                                if(dir_name_len == 0 ||
                                   strspn(dir_data->d_name,"0123456789") != dir_name_len)continue;

                                char process_path[512] = {0};
                                int path_len = snprintf(process_path,sizeof(process_path),
                                        "%s%s/status",home_dir_path,dir_data->d_name);
                                if(path_len < 0 || path_len >= (int)sizeof(process_path))continue;

                                struct process_info process_info = {0};
                                get_process_info(&process_info,process_path);
                                if(process_info.pid <= 0)continue;

                                if(process_list.process_num >= allocated_num){
                                        int new_allocated_num = allocated_num == 0 ? 64 : allocated_num * 2;
                                        struct process_info *tmp_process_info =
                                                realloc(process_list.process_info,
                                                        sizeof(*tmp_process_info) * new_allocated_num);
                                        if(tmp_process_info == NULL)break;
                                        process_list.process_info = tmp_process_info;
                                        allocated_num = new_allocated_num;
                                }
                                process_list.process_info[process_list.process_num] =
                                        process_info;
                                process_list.process_num++;
                        }
                        closedir(process_dir);

                        if(process_list.process_num == 0){
                                free(process_list.process_info);
                                return dev_result;
                        }
                        dev_result.device_name = process;
                        dev_result.device_data.process_list = process_list;
                        return dev_result;
                }
                default:
                        return dev_result;
        }

}

// /procに情報ファイルが存在するデバイスを列挙する。
// 引数: device_tableはfound_dev_tableをNULLにして渡す。戻り値: なし。
// 成功時のfound_dev_tableは呼び出し側がfree()する。
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
