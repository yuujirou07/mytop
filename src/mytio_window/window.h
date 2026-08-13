#ifndef WINDOW_H
#define WINDOW_H

#include<ncurses.h>
#include<wchar.h>
#include"core.h"
#include"process_info/process_info.h"
#define windowlist_add 0
#define windowlist_remove 1

#define winlist_max 64
struct line_result{
        struct line *line_data;
        int line_num;
};

//指定した辺の上へ文字列を乗せる(未実装)
int set_msg_on_line(struct window_data *win_data,enum line_side line_side,struct chr_data chr_data);

//window_dataを1つ確保して返す
//実体は親のサブウィンドウ(derwin)で、parent_winがNULLならstdscrの子になる
//作った直後は1x1なのでset_window_size()/set_window_pos()で伸ばす前提
//stdscrが必要なのでinit_render()より後に呼ぶこと
struct window_data *create_window(struct window_data **parent_win);


//枠線の文字色を辺ごとにセットする(背景色は触らない)
//持っておくだけで、実際に色が乗るのは描画時
int set_window_outline_color(struct window_data *win_data,int color,enum line_side line_side);

//WINDOWの大きさを変える
//親付きなら親の内側に収まる値へ丸められ、その結果として位置も動くことがある
int set_window_size(struct window_data *win_data,int x,int y);

//枠線を描くかどうかのフラグを立てるだけ
//実際に描くのはrender_window_outline()側
int show_window_outline(struct window_data *win_data,bool show_outline);

//WINDOWを動かす。指定するのは親から見た相対座標
//大きさは据え置きなので、親付きならその大きさで収まる位置まで押し戻される
int set_window_pos(struct window_data *win_data,int x,int y);

struct window_data *get_parent_win(struct window_data *win_data);

//WINDOWの大きさ(x=列数,y=行数)をvec2へ取り出す
void get_window_size(struct window_data *win_data,struct vec2 *vec2);

//WINDOWの位置を親から見た相対座標でvec2へ取り出す
void get_window_pos(struct window_data *win_data,struct vec2 *vec2);

//指定した辺の枠線の太さを返す
//枠線は各辺1セル固定なので、出す設定なら1、出さない設定なら0
int get_window_outline_size(struct window_data *win_data,enum line_side side);

//生成したwindow_dataをstaticな配列でまとめて覚えておく
//windowlist_addで末尾へ追加、windowlist_removeでWINDOWごと破棄する
int window_list_ctrl(struct window_data **win_data,int flags);

//子に与えたい位置とサイズ(child_box)を、親からはみ出さない値へ丸めて返す
//先に大きさを親のサイズ以内へ抑え、その大きさのまま収まる位置へ押し戻す
struct box check_child_win_data(struct window_data *parent_win,struct box child_box);

//子の角が親の枠線のどの辺に乗っているかをenum line_sideで返す
//corner_posは子から見たローカル座標(角の4点のどれか)
//どこにも乗っていない、または親の角と重なる場合は-1
int check_outline_joint(struct window_data *win_data,struct vec2 corner_pos);

//描画側(mytop_render)が実体のWINDOWと枠色を取り出すための関数

//window_dataは不透明な型なので、中のWINDOWを取り出す口だけ用意する
WINDOW *get_window_handle(struct window_data *win_data);

//枠線の色を4辺まとめてコピーして返す(値渡しなので書き換えは届かない)
void get_window_outline_color(struct window_data *win_data,struct line_color *line_color);

//ウィンドウへ表示する文字列を1つ積む
//msgは複製するので呼び出し側で保持しなくてよい
//積む数が足りなくなったら配列を倍へ伸ばす
void set_chr(struct window_data *win_data,
        const wchar_t *msg,struct vec2 msg_start_pos,
        struct color_pair color,attr_t style);

//積んである文字列データをまとめて返す
struct chr_data_arry get_window_msg_data(struct window_data *win_data);
void get_vec2_maxyx(WINDOW *win,struct vec2 *pos);
bool update_window_size(struct window_data *wind_data);
//引数のウィンドウ内で、直接の子ウィンドウと重ならない最大矩形を返す
//NULLならstdscr内のトップレベルウィンドウがない領域を調べる
//返す位置は対象ウィンドウから見た相対座標
//空きがない、または取得に失敗した場合はsizeが0のboxを返す
struct box get_window_empty_space(struct window_data *win_data);
void set_device_data(struct window_data *win_data,enum device dev);
void set_cpu_core_glaph(struct window_data *win_data,int core_id,
        struct vec2 glaph_start_pos);
void set_cpu_total_use_glaph(struct window_data *win_data,int num_percet);
struct color_pair get_window_msg_color_pair(struct window_data *win_data,int msg_num);
void set_memory_total(struct window_data *win_data,float memory_total);
void set_memory_total_used_state(struct window_data *win_data,float used_data);
void line_memory_allocate(struct window_data *win_data);
int set_line(struct window_data *win_data,struct color_pair color_pair,int line);
struct line_result get_line_data(struct window_data *win_data);
int free_window(struct window_data **win_data);
void set_memory_total_used_state_graph(struct window_data *win_data,float used_data);
void set_memory_cached_state(struct window_data *win_data,float cached_data);
void set_memory_cached_state_graph(struct window_data *win_data,float cached_data);
void set_memory_free_state(struct window_data *win_data,float free_data);
void set_memory_free_state_graph(struct window_data *win_data,float free_data);
#endif
