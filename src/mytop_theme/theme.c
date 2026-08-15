#include<stdio.h>
#include"theme.h"



//現在のテーマをこのファイル内のstatic変数だけで持つ
//初期値はdark
static enum theme theme = dark;

//テーマを差し替える
//保持するだけで再描画はしないので、変更後は描画側を呼び直す必要がある
// 現在のテーマ設定を更新する。
// 引数: theme_rqは設定するテーマ。戻り値: なし。再描画は呼び出し側が行う。
void theme_set(enum theme theme_rq){
        theme = theme_rq;
}

//現在のテーマを返す
// 現在設定されているテーマを取得する。
// 引数: なし。戻り値: 現在のenum theme値。
enum theme theme_get(){
        return theme;
}


// 描画用途に対応するUnicode文字を取得する。
// 引数: fontは文字の用途。戻り値: Unicodeコードポイント。未対応時は0。
int get_font(enum font_type font){
        switch(font){
                case cpu_bar_graph:
                        return 0x2588;
        }
        return 0;
}
