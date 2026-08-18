#include "strage.h"
#include<sys/statvfs.h>
#include"core.h"
#include"process_info/process_info.h"

// 指定したマウントポイントの空き容量情報を取得する。
// 引数: dev_resultは出力先、pathはstatvfs()へ渡すマウントポイント。戻り値: なし。
void get_strage_info(struct device_info_result *dev_result,const char *path){
        if(dev_result == NULL || path == NULL)return;

        struct statvfs strage_stat = {0};
        if(statvfs(path,&strage_stat) != 0)return;

        uint64_t block_size = strage_stat.f_frsize;
        struct strage_info strage_info = {0};
        strage_info.strage_total = strage_stat.f_blocks * block_size / 1024;
        strage_info.strage_free = strage_stat.f_bavail * block_size / 1024;
        strage_info.strage_used = strage_info.strage_total - strage_info.strage_free;

        dev_result->device_name = strage;
        dev_result->device_data.strage_info = strage_info;
}

// ストレージ総量を関数内に保存または取得する。
// 引数: totalは入出力値、flagsはsetまたはget。戻り値: なし。
void strage_size_ctl(float *total,int flags){
        if(total == NULL)return;
        static float static_total = 0;
        if(flags == set){
                static_total = *total;
                return;
        }
        else if(flags == get){
                *total = static_total;
                return;
        }
}
