#ifndef MYTOP_RENDER_H
#define MYTOP_RENDER_H

#include<ncurses.h>
#include"core.h"
#define win_set 1
#define win_get 2

#define col_pair_dic_set 0
#define col_pair_dic_get 1




//背景色8種×文字色8種の64通りを、あらかじめ全部カラーペアとして登録する
//idはbg*8+fgで決め打ちなので、後から(bg,fg)で引ける
void set_color();

//描画まわりの入口
//ncursesを立ち上げ、色が使える端末ならカラーペアも先に全部登録しておく
//create_window()はstdscrを必要とするので、必ずこれより後に呼ぶ
int init_render();

//ncursesを初期化して、stdscrをwin_ctrl()へ預ける
//noecho()で打った文字が勝手に出るのを止め、cbreak()でEnterを待たず
//1キーずつgetch()へ流れるようにする
void init_ncurses();

//画面全体を描き直す(未実装)
int render_screen();

//画面全体のWINDOW(stdscr)を保持して取り出せるようにする
//win_setでセット、win_getで取得する
WINDOW *win_ctrl(WINDOW *win,int flags);

//ncursesを終了して端末を元の状態へ戻す
//stdscrはendwin()側の管理なのでこちらでは解放しない
int end_render();

//画面全体(stdscr)の背景色をbkgd()で塗り替える
//以降そこへ書かれる文字はこのカラーペアを引き継ぐ
void set_background(int bg_color);

//各WINDOWに描いた内容を実画面へ反映する
//WINDOWはstdscrのサブウィンドウで文字バッファを共有しているので
//stdscrごと転送すればまとめて出る
int window_push_screen();

//ウィンドウの中身(枠以外)を描く予定の場所(未実装)
void render_window_obj(struct window_data *win_data);

//ウィンドウ1枚分をWINDOWのバッファへ描き直す
//実画面へ出るのは、この後にwindow_push_screen()を呼んだとき
int render_window(struct window_data *win_data);

//WINDOW自身の外周1セルへ枠線を描く
//box()でまとめて引いてから、親の枠と重なる角をT字へつなぎ、辺ごとに色を塗る
void render_window_outline(struct window_data *win_data);

//積んである文字列を実際にWINDOWへ書き込む(未実装)
void render_msg_data(struct window_data *win_data);

//積んである文字列の表示位置を、枠線の内側へ収まるよう丸める
//入りきらない長さの文字列は内側の幅で切り詰める
void check_msg_data(struct window_data *win_data);

#endif
