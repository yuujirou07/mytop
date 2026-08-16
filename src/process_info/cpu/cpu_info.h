#ifndef CPU_INFO_H
#define CPU_INFO_H

#include<stdbool.h>

#define cpu_model_name_size 256
#define cpu_total_use_log_max 1024

#define core_set 0
#define core_get 1

//使用率履歴へ1件積むまでに空ける最小間隔(ミリ秒)
//再描画周期(1秒)より短くしないと、揺らぎで1周期ぶん取りこぼす
#define cpu_stat_min_interval_ms 500

struct core_use_data{
        const int *core_palm_data;
        int core_palm_size;
};

struct cpu_info{
        int processor;
        int cpu_family;
        int model;
        char model_name[cpu_model_name_size];
        double cpu_MHz;
        unsigned int cache_size;//kB
        int physical_id;
        int siblings;
        int core_id;
        int cpu_cores;
        bool fpu;
        bool wp;
        int thread;
};



void set_core_usage_rate(int core_id,int usage);
void free_cpu_usage_data(void);
//返されるポインタは内部所有なので、呼び出し側では変更・解放しない
struct core_use_data get_core_usage_rate(int core_id);
int get_cpu_total_usage_rate(void);
unsigned long long get_cpu_sample_count(void);


#endif
