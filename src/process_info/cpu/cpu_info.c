#include <stdatomic.h>
#include <stddef.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<time.h>
#include<unistd.h>
#include"core.h"
#include "process_info/process_info.h"
#include"cpu_info.h"

static int cpu_core_info(int *num,int flags);
static bool is_cpu_stat_sampling_time(void);
static int **core_palm_data = NULL;//各コアの直近100件のデータ
static int *core_palm_count = NULL;//コアに何件入っているか
static int core_palm_num = 0;
static unsigned long long *previous_cpu_total = NULL;
static unsigned long long *previous_cpu_idle = NULL;
static int previous_cpu_count = 0;
static unsigned long long previous_total_cpu_time = 0;
static unsigned long long previous_total_cpu_idle = 0;
static int cpu_total_usage_rate = 0;
static struct timespec previous_stat_sample_time = {0,0};//前回/proc/statを読んだ時刻
static bool has_previous_stat_sample = false;
static unsigned long long cpu_sample_count = 0;//履歴へ積んだ回数の通し番号

// cpuinfoを解析し、/proc/statとの差分から各論理コアの使用率履歴も更新する。
// 引数: dev_resultは出力先、pathはcpuinfo形式のファイルパス。戻り値: なし。
// 初回だけ100ms間隔で2回採取し、初期描画前に使用率を確定する。
// 使用率の採取は前回からcpu_stat_min_interval_ms以上経った時だけ行う。
// この関数はCPU情報を使うWINDOWごとに呼ばれるので、
// 呼ばれるたびに履歴へ積むと1周期に複数件入り、時間軸が不揃いになる。
void get_cpu_info(struct device_info_result *dev_result,const char *path){
        if(dev_result == NULL || path == NULL)return;

        FILE *file = fopen(path,"r");
        if(file == NULL)return;
        struct cpu_info cpu_info = {0};
        char file_line_buff[512] = {0};

        while(fgets(file_line_buff,sizeof(file_line_buff),file) != NULL){
                if(file_line_buff[0] == '\n' || file_line_buff[0] == '\r')break;

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
                        cpu_info.cpu_family = atoi(value);
                }
                else if(strcmp(file_line_buff,"model") == 0){
                        cpu_info.model = atoi(value);
                }
                else if(strcmp(file_line_buff,"model name") == 0){
                        snprintf(cpu_info.model_name,sizeof(cpu_info.model_name),"%s",value);
                }
                else if(strcmp(file_line_buff,"cpu MHz") == 0){
                        cpu_info.cpu_MHz = strtod(value,NULL);
                }
                else if(strcmp(file_line_buff,"cache size") == 0){
                        cpu_info.cache_size = strtoul(value,NULL,10);
                }
                else if(strcmp(file_line_buff,"physical id") == 0){
                        cpu_info.physical_id = atoi(value);
                }
                else if(strcmp(file_line_buff,"siblings") == 0){
                        cpu_info.siblings = atoi(value);
                }
                else if(strcmp(file_line_buff,"core id") == 0){
                        cpu_info.core_id = atoi(value);
                }
                else if(strcmp(file_line_buff,"cpu cores") == 0){
                        cpu_info.cpu_cores = atoi(value);
                }
                else if(strcmp(file_line_buff,"fpu") == 0){
                        cpu_info.fpu = strcmp(value,"yes") == 0;
                }
                else if(strcmp(file_line_buff,"wp") == 0){
                        cpu_info.wp = strcmp(value,"yes") == 0;
                }
        }

        fclose(file);
        int cpu_core = cpu_info.siblings;
        if(cpu_info.cpu_cores > 0){
                cpu_info.thread = cpu_core / cpu_info.cpu_cores;
        }

        cpu_core_info(&cpu_core,core_set);
        if(cpu_core > 0 && previous_cpu_count != cpu_core){
                unsigned long long *tmp_total = calloc(cpu_core,sizeof(unsigned long long));
                unsigned long long *tmp_idle = calloc(cpu_core,sizeof(unsigned long long));
                if(tmp_total != NULL && tmp_idle != NULL){
                        free(previous_cpu_total);
                        free(previous_cpu_idle);
                        previous_cpu_total = tmp_total;
                        previous_cpu_idle = tmp_idle;
                        previous_cpu_count = cpu_core;
                }
                else{
                        free(tmp_total);
                        free(tmp_idle);
                }
        }

        if(is_cpu_stat_sampling_time() == false){
                dev_result->device_name = cpu_data;
                dev_result->device_data.cpu_info = cpu_info;
                return;
        }

        int stat_sample_count = previous_total_cpu_time == 0 ? 2 : 1;
        for(int stat_sample = 0;stat_sample < stat_sample_count;stat_sample++){
                file = fopen("/proc/stat","r");
                if(file != NULL && previous_cpu_count == cpu_core){
                        while(fgets(file_line_buff,sizeof(file_line_buff),file) != NULL){
                                int core_id = 0;
                                unsigned long long user = 0;
                                unsigned long long nice = 0;
                                unsigned long long system = 0;
                                unsigned long long idle = 0;
                                unsigned long long iowait = 0;
                                unsigned long long irq = 0;
                                unsigned long long softirq = 0;
                                unsigned long long steal = 0;
                                if(strncmp(file_line_buff,"cpu ",4) == 0){
                                        int total_read_count = sscanf(file_line_buff,
                                                "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                                                &user,&nice,&system,&idle,&iowait,
                                                &irq,&softirq,&steal);
                                        if(total_read_count >= 5){
                                                unsigned long long now_total_idle = idle + iowait;
                                                unsigned long long now_total_cpu = user + nice + system +
                                                        now_total_idle + irq + softirq + steal;
                                                if(previous_total_cpu_time > 0 &&
                                                   now_total_cpu > previous_total_cpu_time &&
                                                   now_total_idle >= previous_total_cpu_idle){
                                                        unsigned long long total_diff =
                                                                now_total_cpu - previous_total_cpu_time;
                                                        unsigned long long idle_diff =
                                                                now_total_idle - previous_total_cpu_idle;
                                                        if(idle_diff <= total_diff){
                                                                cpu_total_usage_rate =
                                                                        (int)((total_diff - idle_diff) * 100 /
                                                                        total_diff);
                                                        }
                                                }
                                                previous_total_cpu_time = now_total_cpu;
                                                previous_total_cpu_idle = now_total_idle;
                                        }
                                        continue;
                                }
                                int read_count = sscanf(file_line_buff,
                                        "cpu%d %llu %llu %llu %llu %llu %llu %llu %llu",
                                        &core_id,&user,&nice,&system,&idle,&iowait,
                                        &irq,&softirq,&steal);
                                if(read_count < 5)continue;
                                if(core_id < 0 || core_id >= previous_cpu_count)continue;

                                unsigned long long now_idle = idle + iowait;
                                unsigned long long now_total = user + nice + system +
                                        now_idle + irq + softirq + steal;
                                if(previous_cpu_total[core_id] > 0 &&
                                   now_total > previous_cpu_total[core_id]){
                                        unsigned long long total_diff =
                                                now_total - previous_cpu_total[core_id];
                                        unsigned long long idle_diff =
                                                now_idle - previous_cpu_idle[core_id];
                                        int usage = (int)((total_diff - idle_diff) * 100 /
                                                total_diff);
                                        set_core_usage_rate(core_id,usage);
                                }
                                previous_cpu_total[core_id] = now_total;
                                previous_cpu_idle[core_id] = now_idle;
                        }
                        fclose(file);
                }
                else if(file != NULL){
                        fclose(file);
                }
                if(stat_sample + 1 < stat_sample_count)usleep(100000);
        }
        //1回の採取で各コアへ1件ずつ積まれるので、ここが履歴の通し番号になる
        cpu_sample_count++;
        dev_result->device_name = cpu_data;
        dev_result->device_data.cpu_info = cpu_info;
}



// /proc/statを採取してよいタイミングかを判定し、採取するなら時刻を更新する。
// 引数: なし。戻り値: 前回採取からcpu_stat_min_interval_ms以上経っていればtrue。
// 履歴を一定間隔で積むための番人なので、trueを返した側は必ず採取すること。
static bool is_cpu_stat_sampling_time(void){
        struct timespec now = {0,0};
        if(clock_gettime(CLOCK_MONOTONIC,&now) != 0)return true;

        if(has_previous_stat_sample == true){
                long elapsed_ms =
                        (long)(now.tv_sec - previous_stat_sample_time.tv_sec) * 1000 +
                        (now.tv_nsec - previous_stat_sample_time.tv_nsec) / 1000000;
                if(elapsed_ms < cpu_stat_min_interval_ms)return false;
        }

        previous_stat_sample_time = now;
        has_previous_stat_sample = true;
        return true;
}

// 指定コアの使用率を最大100件の履歴末尾へ追加する。
// 引数: core_idは0起点の論理コア番号、usageは0〜100に丸められる使用率。戻り値: なし。
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

// 全コアの使用率履歴と/proc/statの前回採取値を解放し、初期状態へ戻す。
// 引数・戻り値: なし。複数回呼び出してもよい。
void free_cpu_usage_data(void){
        if(core_palm_data != NULL){
                for(int i = 0;i < core_palm_num;i++){
                        free(core_palm_data[i]);
                }
        }
        free(core_palm_data);
        free(core_palm_count);
        free(previous_cpu_total);
        free(previous_cpu_idle);
        core_palm_data = NULL;
        core_palm_count = NULL;
        core_palm_num = 0;
        previous_cpu_total = NULL;
        previous_cpu_idle = NULL;
        previous_cpu_count = 0;
        previous_total_cpu_time = 0;
        previous_total_cpu_idle = 0;
        cpu_total_usage_rate = 0;
        previous_stat_sample_time = (struct timespec){0,0};
        has_previous_stat_sample = false;
        cpu_sample_count = 0;
}

// 指定コアの使用率履歴を参照用として返す。
// 引数: core_idは0起点の論理コア番号。戻り値: 履歴ポインタと件数。無効時はゼロ値。
// 返されるポインタは内部所有のため、変更やfree()をしてはならない。
struct core_use_data get_core_usage_rate(int core_id){
        struct core_use_data result = {0};

        if(core_palm_data == NULL || core_palm_count == NULL)return result;
        if(core_id < 0 || core_id >= core_palm_num)return result;
        if(core_palm_data[core_id] == NULL)return result;

        result.core_palm_data = core_palm_data[core_id];
        result.core_palm_size = core_palm_count[core_id];
        return result;
}

// CPU全体の直近使用率を返す。
// 引数: なし。戻り値: /proc/statの前回差分から求めた0〜100の使用率。初回は0。
int get_cpu_total_usage_rate(void){
        return cpu_total_usage_rate;
}

// 使用率を履歴へ積んだ回数を返す。
// 引数: なし。戻り値: 起動からの通し番号。履歴が上限で押し出されても増え続ける。
// 履歴の配列番号は押し出しでずれるので、時間の進みを見るにはこちらを使う。
unsigned long long get_cpu_sample_count(void){
        return cpu_sample_count;
}

// 論理コア数を関数内に保存または取得する。
// 引数: numは入出力先、flagsはcore_setまたはcore_get。戻り値: 成功時は0または保存値、失敗時は-1。
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

int cpu_total_use_log(int *cpu_total,enum flags flags){
        if(cpu_total == NULL)return -1;
        static int static_cpu_total_use_log[cpu_total_use_log_max] = {0};
        static int static_cpu_total_use_count = 0;
        int static_cpu_total_use_max =
                sizeof(static_cpu_total_use_log)/sizeof(static_cpu_total_use_log[0]);
        if(flags == set){
                if(static_cpu_total_use_count >= static_cpu_total_use_max){
                        memmove(&static_cpu_total_use_log[0],
                                &static_cpu_total_use_log[1],
                                (static_cpu_total_use_count - 1) * sizeof(int));
                        static_cpu_total_use_count--;
                }

                static_cpu_total_use_log[static_cpu_total_use_count++] = *cpu_total;
                return 0;
        }
        else if(flags == get){
                memcpy(cpu_total,static_cpu_total_use_log,
                        static_cpu_total_use_count * sizeof(int));
                return static_cpu_total_use_count;
        }
        return -1;
}
