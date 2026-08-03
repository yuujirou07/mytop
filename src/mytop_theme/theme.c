#include<stdio.h>
#include"theme.h"



//現在のテーマをこのファイル内のstatic変数だけで持つ
//初期値はdark
static enum theme theme = dark;

//テーマを差し替える
//保持するだけで再描画はしないので、変更後は描画側を呼び直す必要がある
void theme_set(enum theme theme_rq){
        theme = theme_rq;
}

//現在のテーマを返す
enum theme theme_get(){
        return theme;
}


int get_font(enum font_type font){
        switch(font){
                case cpu_bar_graph:
                        return 0x2588;
        }
        return 0;
}

