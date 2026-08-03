#ifndef CORE_H
#define CORE_H

#include<stdbool.h>
#include<wchar.h>
#include<ncurses.h>
#include"theme.h"


struct window_data;
struct outline;
struct color_pair;
struct id_color_dic;
struct pos;
struct cell_data;
struct line_color;
enum line_side;




struct color_pair{
        int bg_color;
        int fg_color;
};

struct id_color_dic{
        int color_pair_id;
        struct color_pair color_pair;
};

struct vec2{
        int x;
        int y;
};

struct cell_data{
        struct vec2 cell_pos;
        struct color_pair cell_color;
};

struct line_color{
        struct color_pair top;
        struct color_pair bottom;
        struct color_pair right;
        struct color_pair left;
};


struct outline{
        struct vec2 pos;
        struct line_color line_color;
        int w;
        int h;
        bool show_outline;
};

struct chr_data{
        struct vec2 chr_st_pos;
        wchar_t *chr_data;
        struct color_pair chr_color;
        attr_t style;
};

struct chr_data_arry{
        struct chr_data **chr_data;
        int allocate_num;
        int chr_data_count;
};


enum line_side{
        top,
        bottom,
        right,
        left,
};

struct box{
        struct vec2 pos;
        struct vec2 size;
};
//テーマを差し替える
//保持するだけで再描画はしないので、変更後は描画側を呼び直す必要がある
void theme_set(enum theme theme_rq);
//現在のテーマを返す(初期値はdark)
enum theme theme_get();

#endif
