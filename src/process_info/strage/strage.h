#ifndef STRAGE_H
#define STRAGE_H

#include<stdint.h>

struct device_info_result;

//各値の単位はmem_infoと合わせてkB。
struct strage_info{
        uint64_t strage_total;
        uint64_t strage_used;
        uint64_t strage_free;
};

//指定したマウントポイントの空き容量情報を取得する。
//引数: dev_resultは出力先、pathはstatvfs()へ渡すマウントポイント。戻り値: なし。
void get_strage_info(struct device_info_result *dev_result,const char *path);

//ストレージ総量を関数内に保存または取得する。
//引数: totalは入出力値、flagsはsetまたはget。戻り値: なし。
void strage_size_ctl(float *total,int flags);

#endif
