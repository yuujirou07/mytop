

#include "process.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

void get_process_info(struct process_info *process_info,const char *path){
        if(process_info == NULL || path == NULL)return;
        *process_info = (struct process_info){0};

        FILE *file = fopen(path,"r");
        if(file == NULL)return;
        char file_line_buff[512] = {0};

        while(fgets(file_line_buff,sizeof(file_line_buff),file) != NULL){
                char *value = strchr(file_line_buff,':');
                if(value == NULL)continue;

                *value = '\0';
                value++;
                while(*value == ' ' || *value == '\t')value++;
                value[strcspn(value,"\r\n")] = '\0';

                if(strcmp(file_line_buff,"Name") == 0){
                        snprintf(process_info->name,sizeof(process_info->name),"%s",value);
                }
                else if(strcmp(file_line_buff,"State") == 0){
                        process_info->state = value[0];
                }
                else if(strcmp(file_line_buff,"Pid") == 0){
                        process_info->pid = atoi(value);
                }
                else if(strcmp(file_line_buff,"Threads") == 0){
                        process_info->threads = atoi(value);
                }
        }

        fclose(file);
}

int check_max_pid_digit(struct process_list process_list){
        if(process_list.process_num <= 0 || process_list.process_info == NULL)return 0;

        int max_pid_digit = 0;
        for(int i = 0; i < process_list.process_num;i++){
                int pid = process_list.process_info[i].pid;
                int pid_digit = 1;
                while(pid >= 10){
                        pid /= 10;
                        pid_digit++;
                }
                if(max_pid_digit < pid_digit){
                        max_pid_digit = pid_digit;
                }
                if(max_pid_digit >= 7)return 7;
        }
        return max_pid_digit;
}
