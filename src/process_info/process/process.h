#ifndef PROCESS_H
#define PROCESS_H

#define process_name_size 64

struct process_info{
        char name[process_name_size];
        char state;
        int pid;
        int threads;
};

struct process_list{
        struct process_info *process_info;
        int process_num;
};

void get_process_info(struct process_info *process_info,const char *path);
int check_max_pid_digit(struct process_list process_list);

#endif
