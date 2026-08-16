#include <stddef.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdbool.h>
#include<ncurses.h>
#include <wchar.h>
#include"core.h"
#include"window.h"
#include "cpu_info.h"
#include "memory.h"
#include"mytop_render.h"
#include "process.h"
#include "process_info/process_info.h"
#include "theme.h"


static void set_cpu_cores_info(struct window_data *win_data,struct cpu_info cpu_info,int cores_num);
static void set_cpu_total_use_big_graph(struct window_data *win_data);
static int cpu_use_dot_height(int percent,int dot_row_count);
static int cpu_graph_newest_column(int max_sample_count);

//点字1文字は横2列・縦4段の点でできている
//U+2800に点のビットを足すと、その並びの点字になる
#define braille_base_chr 0x2800
#define braille_dot_rows 4

//長方形(位置・サイズ)の管理はncursesのWINDOWに任せる
//そのためwindow_data側では位置とサイズを持たない
//WINDOWはすべて親のサブウィンドウ(derwin)として作るので
//親を指定しない場合はstdscrの子になる
struct window_data{
        WINDOW *win;
        struct chr_data_arry chr_data;
        struct outline outline;
        struct line *line;
        int line_count;
        int line_capacity;
        struct window_data *parent_window_data;
        enum device dev;
        int process_scroll;
        int process_max_scroll;
};

static struct window_data *winlist[winlist_max] = {0};
static int windowlist_count = 0;

//丸めた結果のboxをWINDOWへ反映する
//必ず移動→リサイズの順に呼ぶ
//先に広げると移動前の位置のまま親をはみ出してwresize()が失敗するため
// 仕様: WINDOWを指定boxへ移動後リサイズする。引数: 対象と相対座標box。戻り値: 成功0、失敗-1。
static int apply_window_box(struct window_data *win_data,struct box win_box){
        if(mvderwin(win_data->win,win_box.pos.y,win_box.pos.x) == ERR)return -1;
        if(wresize(win_data->win,win_box.size.y,win_box.size.x) == ERR)return -1;
        return 0;
}

//今のWINDOWの位置と大きさをboxとして取り出す
// 仕様: WINDOWの現在位置とサイズを取得する。引数: 対象。戻り値: 現在のbox。
static struct box get_window_box(struct window_data *win_data){
        struct box win_box = {{0,0},{0,0}};
        get_window_pos(win_data,&win_box.pos);
        get_window_size(win_data,&win_box.size);
        return win_box;
}

//window_dataを1つ確保して、実体となるWINDOWをぶら下げて返す
//WINDOWはderwin()で親のサブウィンドウとして作る
//親を渡さなかった場合はstdscrの子になるので、init_render()より後に呼ぶこと
//この時点ではサイズと位置が未定なので1x1で作り、後から
//set_window_size()/set_window_pos()で伸ばす前提
// 仕様: 1x1の子WINDOWと管理領域を生成して一覧へ登録する。引数: 親のアドレス、NULLならstdscr。戻り値: 所有ポインタ、失敗時NULL。
struct window_data *create_window(struct window_data **parent_win){
        WINDOW *parent_win_handle = NULL;

        if(parent_win != NULL && *parent_win != NULL){
                parent_win_handle = (*parent_win)->win;
        }
        //親の指定がなければ画面全体(stdscr)を親にする
        if(parent_win_handle == NULL)parent_win_handle = win_ctrl(NULL,win_get);
        //init_render()より前に呼ばれた場合はstdscrがまだ無い
        if(parent_win_handle == NULL)return NULL;

        struct window_data *tmp_win_data =
                calloc(1,sizeof(struct window_data));
        if(tmp_win_data == NULL)return NULL;
        tmp_win_data->chr_data.chr_data = calloc(8,sizeof(struct chr_data));
        if(tmp_win_data->chr_data.chr_data == NULL){
                free(tmp_win_data);
                return NULL;
        }
        tmp_win_data->chr_data.chr_data_count = 0;
        tmp_win_data->chr_data.allocate_num = 8;
        tmp_win_data->line = NULL;
        tmp_win_data->line_count = 0;
        tmp_win_data->line_capacity = 0;
        tmp_win_data->dev = device_count;

        //実際の位置とサイズはset_window_pos()/set_window_size()で決めるので
        //ここでは最小サイズのWINDOWを作っておく
        tmp_win_data->win = derwin(parent_win_handle,1,1,0,0);
        if(tmp_win_data->win == NULL){
                free(tmp_win_data->chr_data.chr_data);
                free(tmp_win_data);
                return NULL;
        }
        
        //calloc()のままだと枠線の背景色が0(COLOR_BLACK)になり、
        //背景を透過させている端末では枠のところだけ黒く浮いてしまう
        //背景は端末の既定色、文字色は白を初期値にしておく
        struct color_pair default_line_color = {COLOR_DEFAULT,COLOR_WHITE};
        tmp_win_data->outline.line_color.top = default_line_color;
        tmp_win_data->outline.line_color.bottom = default_line_color;
        tmp_win_data->outline.line_color.left = default_line_color;
        tmp_win_data->outline.line_color.right = default_line_color;

        if(parent_win != NULL)tmp_win_data->parent_window_data = *parent_win;
        else tmp_win_data->parent_window_data = NULL;
        if(window_list_ctrl(&tmp_win_data,windowlist_add) != 0){
                delwin(tmp_win_data->win);
                free(tmp_win_data->chr_data.chr_data);
                free(tmp_win_data);
                return NULL;
        }
        return tmp_win_data;
}

//WINDOWの大きさをwresize()で変える
//サイズの実体はWINDOWが持つので、window_data側には控えを残さない
//位置は今のものを据え置きで渡し、親付きなら親の内側に収まる値へ丸めてもらう
//要求サイズが大きすぎると丸めの結果として位置も動くので、boxごと反映する
// 仕様: WINDOWを親領域内に収めてリサイズする。引数: 対象、幅x、高さy。戻り値: 成功0、失敗-1。
int set_window_size(struct window_data *win_data,int x,int y){
        if(win_data == NULL || win_data->win == NULL)return -1;
        if(x < 1 || y < 1)return -1;

        struct box win_box = get_window_box(win_data);
        win_box.size.x = x;
        win_box.size.y = y;

        if(win_data->parent_window_data != NULL){
                //親ウィンドウからはみ出していないかチェックする
                win_box = check_child_win_data(win_data->parent_window_data,
                                win_box);
        }

        return apply_window_box(win_data,win_box);
}

//WINDOWの位置を動かす
//サブウィンドウなのでmvderwin()、つまり指定するのは親から見た相対座標
//大きさは今のものを据え置きで渡すため、親付きなら
//その大きさを保ったまま収まる位置まで押し戻される
// 仕様: WINDOWを親基準の座標へ移動する。引数: 対象、列x、行y。戻り値: 成功0、失敗-1。
int set_window_pos(struct window_data *win_data,int x,int y){
        if(win_data == NULL || win_data->win == NULL)return -1;
        if(x < 0 || y < 0)return -1;

        struct box win_box = get_window_box(win_data);
        win_box.pos.x = x;
        win_box.pos.y = y;

        if(win_data->parent_window_data != NULL){
                //ウィンドウの座標が親ウィンドウから出ていないかチェックする
                win_box = check_child_win_data(win_data->parent_window_data,
                                win_box);
        }

        return apply_window_box(win_data,win_box);
}

//枠線を出すかどうかのフラグを立てるだけ
//実際に描くのはrender_window_outline()側
// 仕様: 枠を表示するか設定する。引数: 対象と表示フラグ。戻り値: 成功0、対象がNULLなら-1。
int show_window_outline(struct window_data *win_data,bool show_outline){
        if(win_data == NULL)return -1;
        win_data->outline.show_outline = show_outline;
        return 0;
}


//定義したウィンドウの枠線の色をセットする
//辺ごとに1回ずつ呼ぶ。持っておくだけで、色を実際に乗せるのは描画時
//引数colorは文字色(COLOR_WHITEなど)で、背景色は触らない
// 仕様: 指定した枠辺の文字色を設定する。引数: 対象、色、辺。戻り値: 成功0、対象がNULLなら-1。
int set_window_outline_color(struct window_data *win_data,int color,enum line_side line_side){
        if(win_data == NULL)return -1;
        switch(line_side){
                case bottom:
                        win_data->outline.line_color.bottom.fg_color = color;
                        break;
                case top:
                        win_data->outline.line_color.top.fg_color = color;
                        break;
                case left:
                        win_data->outline.line_color.left.fg_color = color;
                        break;
                case right:
                        win_data->outline.line_color.right.fg_color = color;
                        break;
        }
    return 0;
}



//生成したwindow_dataをstaticな配列でまとめて覚えておくための関数
//windowlist_addで末尾に追加、windowlist_removeでWINDOWごと破棄する
//removeでは削除位置より後ろをmemmove()で前へ詰めて穴を埋める
// 仕様: window_dataを共有一覧へ追加または一覧から破棄する。引数: 対象のアドレスと操作種別。戻り値: 成功0、失敗-1。
int window_list_ctrl(struct window_data **win_data,int flags){
        if(win_data == NULL || *win_data == NULL)return -1;

        switch(flags){
                case windowlist_add:
                        if(windowlist_count >= winlist_max)return -1;
                        winlist[windowlist_count++] = *win_data;
                        break;
                case windowlist_remove:
                        for(int i = 0; i < windowlist_count;i++){
                                if(winlist[i] != *win_data)continue;
                                if(winlist[i]->win != NULL)delwin(winlist[i]->win);
                                free(winlist[i]);
                                memmove(&winlist[i],&winlist[i+1],
                                        sizeof(struct window_data*) * (windowlist_count - i - 1));
                                windowlist_count--;
                                break;
                        }
                        break;
        }
        return 0;
}

//WINDOWが持っているサイズをgetmaxyx()で取り出してvec2へ移す
//getmaxyxは(行数,列数)の順なので、x=列数 y=行数へ入れ替えている
// 仕様: WINDOWの幅と高さを出力する。引数: 対象と出力先。戻り値: なし。無効引数では変更しない。
void get_window_size(struct window_data *win_data,struct vec2 *vec2){
        if(win_data == NULL || win_data->win == NULL || vec2 == NULL)return;

        int h = 0;
        int w = 0;
        getmaxyx(win_data->win,h,w);
        vec2->x = w;
        vec2->y = h;
}

//WINDOWの位置を取り出す
//set_window_pos()と揃えるため、画面の絶対座標ではなく
//getparyx()で親から見た相対座標を返す
// 仕様: 親基準のWINDOW座標を出力する。引数: 対象と出力先。戻り値: なし。無効引数では変更しない。
void get_window_pos(struct window_data *win_data,struct vec2 *vec2){
        if(win_data == NULL || win_data->win == NULL || vec2 == NULL)return;

        int y = 0;
        int x = 0;
        getparyx(win_data->win,y,x);
        vec2->x = x;
        vec2->y = y;
}

//WINDOWの枠線は各辺1セル固定なので
//枠を出す設定なら1、出さない設定なら0を返す
// 仕様: 指定辺の枠が占めるセル数を返す。引数: 対象と辺。戻り値: 表示時1、非表示時0、無効時-1。
int get_window_outline_size(struct window_data *win_data,enum line_side outline_side){
        if(win_data == NULL)return -1;
        if(!win_data->outline.show_outline)return 0;

        switch(outline_side){
                case bottom:
                case top:
                case left:
                case right:
                        return 1;
        }
    return 0;
}

//window_dataは不透明な型なので、中のWINDOWを取り出す口だけ用意する
//描画側(mytop_render)がwaddch等を呼ぶために使う
// 仕様: window_dataが所有するncurses WINDOWを参照する。引数: 対象。戻り値: 内部所有ポインタ、無効時NULL。
WINDOW *get_window_handle(struct window_data *win_data){
        if(win_data == NULL)return stdscr;
        return win_data->win;
}

//枠線の色を4辺まとめてコピーして返す
//描画側から中身を直接書き換えられないよう、値渡しで取り出す
// 仕様: 4辺の枠色をコピーする。引数: 対象と出力先。戻り値: なし。
void get_window_outline_color(struct window_data *win_data,struct line_color *line_color){
        if(win_data == NULL || line_color == NULL)return;
        *line_color = win_data->outline.line_color;
}


//子ウィンドウの角が、親の枠線のどの辺の上に乗っているかを判定する
//corner_posは子ウィンドウから見たローカル座標(角の4点のどれか)
//乗っている辺をenum line_sideで返し、どこにも乗っていなければ-1を返す
//親の角(縦横2本が交わる点)と重なった場合はT字にすると親の角が壊れるので-1
// 仕様: 子の角が重なる親枠の辺を調べる。引数: 子と子ローカル角座標。戻り値: enum line_side、接続なしは-1。
int check_outline_joint(struct window_data *win_data,struct vec2 corner_pos){
        if(win_data == NULL)return -1;

        struct window_data *parent_win = win_data->parent_window_data;
        if(parent_win == NULL)return -1;
        //親が枠を描いていなければつなぐ相手がいない
        if(get_window_outline_size(parent_win,top) <= 0)return -1;

        struct vec2 child_pos = {0,0};
        struct vec2 parent_size = {0,0};
        get_window_pos(win_data,&child_pos);
        get_window_size(parent_win,&parent_size);

        //子のローカル座標を親から見た座標へ直す
        int x = child_pos.x + corner_pos.x;
        int y = child_pos.y + corner_pos.y;

        //親の枠線は外周1セルなので、上下は端の行、左右は端の列
        bool on_top = (y == 0);
        bool on_bottom = (y == parent_size.y - 1);
        bool on_left = (x == 0);
        bool on_right = (x == parent_size.x - 1);

        if((on_top || on_bottom) && (on_left || on_right))return -1;

        if(on_top)return top;
        if(on_bottom)return bottom;
        if(on_left)return left;
        if(on_right)return right;
        return -1;
}


//子ウィンドウに与えたい位置とサイズ(child_box)を受け取り、
//親からはみ出さない値へ丸めた結果を返す
//先に大きさを親のサイズ以内へ抑え、その大きさのまま収まる位置へ押し戻す
//この順にすると、位置が動くのは大きさを優先して確保できないときだけになる
//子の座標は親から見た相対座標(mvderwin/getparyx)なので、
//親自身が画面のどこにあるかは計算に関係しない
// 仕様: 子boxを親WINDOW内へ丸める。引数: 親と要求box。戻り値: 補正後box。
struct box check_child_win_data(struct window_data *parent_win,struct box child_box){
        struct box tmp_box = child_box;

        struct vec2 p_size = {0,0};
        get_window_size(parent_win,&p_size);
        //親が取れなければ丸めようがないのでそのまま返す
        if(p_size.x < 1 || p_size.y < 1)return tmp_box;

        //親より大きくはできない
        if(tmp_box.size.x > p_size.x)tmp_box.size.x = p_size.x;
        if(tmp_box.size.y > p_size.y)tmp_box.size.y = p_size.y;
        if(tmp_box.size.x < 1)tmp_box.size.x = 1;
        if(tmp_box.size.y < 1)tmp_box.size.y = 1;

        //この大きさで親に収まる位置の上限
        int max_pos_x = p_size.x - tmp_box.size.x;
        int max_pos_y = p_size.y - tmp_box.size.y;

        if(tmp_box.pos.x < 0)tmp_box.pos.x = 0;
        else if(tmp_box.pos.x > max_pos_x)tmp_box.pos.x = max_pos_x;

        if(tmp_box.pos.y < 0)tmp_box.pos.y = 0;
        else if(tmp_box.pos.y > max_pos_y)tmp_box.pos.y = max_pos_y;

        return tmp_box;
}

// 仕様: 描画文字列・座標・色・属性をWINDOWの描画待ち配列へコピーする。
// 引数: 対象、NUL終端文字列、開始座標、色、属性。戻り値: なし。文字列の所有権は呼び出し側に残る。
void set_chr(struct window_data *win_data,
        const wchar_t *msg,struct vec2 msg_start_pos,
        struct color_pair color,attr_t style){

        if(win_data == NULL || msg == NULL)return;
        if(win_data->chr_data.chr_data == NULL)return;

        if(win_data->chr_data.chr_data_count >= win_data->chr_data.allocate_num){
                int new_allocate_num = win_data->chr_data.allocate_num * 2;
                struct chr_data *tmp_chr_data =
                        realloc(win_data->chr_data.chr_data,
                                sizeof(*tmp_chr_data) * new_allocate_num);
                if(tmp_chr_data == NULL)return;
                win_data->chr_data.chr_data = tmp_chr_data;
                win_data->chr_data.allocate_num = new_allocate_num;
        }

        size_t msg_size = wcslen(msg) + 1;
        wchar_t *msg_copy = malloc(sizeof(wchar_t) * msg_size);
        if(msg_copy == NULL)return;
        wmemcpy(msg_copy,msg,msg_size);

        struct chr_data *new_chr_data =
                &win_data->chr_data.chr_data[win_data->chr_data.chr_data_count];
        *new_chr_data = (struct chr_data){
                .chr_st_pos = msg_start_pos,
                .chr_data = msg_copy,
                .chr_color = color,
                .style = style
        };
        win_data->chr_data.chr_data_count++;
}

// 仕様: WINDOWに登録された全描画文字列を解放して件数を0にする。引数: 対象。戻り値: なし。
void clear_window_chr_data(struct window_data *win_data){
        if(win_data == NULL)return;
        for(int i = 0;i < win_data->chr_data.chr_data_count;i++){
                free(win_data->chr_data.chr_data[i].chr_data);
                win_data->chr_data.chr_data[i].chr_data = NULL;
        }
        win_data->chr_data.chr_data_count = 0;
}

// 仕様: 登録文字列配列を参照用に返す。引数: 対象。戻り値: 内部配列と件数。要素を解放してはならない。
struct chr_data_arry get_window_msg_data(struct window_data *win_data){
        return win_data->chr_data;
}

// 仕様: ncurses WINDOWの幅と高さをvec2へ出力する。引数: WINDOWと出力先。戻り値: なし。
void get_vec2_maxyx(WINDOW *win,struct vec2 *pos){
        if(win == NULL || pos == NULL) return;
        struct vec2 tmp_pos = {0,0};
        getmaxyx(win,tmp_pos.y,tmp_pos.x);
        *pos = tmp_pos; 
}


// 仕様: 親window_dataを取得する。引数: 子。戻り値: 内部所有の親ポインタ、親なしまたは無効時NULL。
struct window_data *get_parent_win(struct window_data *win_data){
        if(win_data == NULL)return NULL;
        return win_data->parent_window_data;
}



// 仕様: 指定デバイスの情報を取得し、対象WINDOWへ対応する描画データを登録する。
// 引数: 対象とcpu_info/cpu_data/memory。戻り値: なし。未検出時は描画を追加しない。
void set_device_data(struct window_data *win_data,enum device dev){
        if(win_data == NULL)return;
        enum device data_dev = dev;
        if(dev == cpu_info)data_dev = cpu_data;
        struct found_device_parts_table parts_table = {0};
        //端末にある取得可能なパーツを探す
        check_detectable_parts(&parts_table);
        struct device_info_result device_result;

        bool is_found = false;
        for(int i = 0; i < parts_table.found_dev_count;i++){
                //もし探したパーツのテーブル内に第3引数と同じものがあればそのデータを取得する
                if(parts_table.found_dev_table[i] != data_dev)continue;
                device_result = get_device_info(data_dev);
                is_found = true;
        }
        if(is_found == false){
                free(parts_table.found_dev_table);
                return;
        }
        win_data->dev = dev;
        switch(dev){
                case cpu_info:{
                        set_cpu_total_use_big_graph(win_data);
                        break;
                }
                case cpu_data:{
                        struct cpu_info cpu_info = device_result.device_data.cpu_info;
                        set_cpu_cores_info(win_data,cpu_info,cpu_info.cpu_cores);
                        break;
                }
                case memory:{
                        struct mem_info mem_info = device_result.device_data.mem_info;
                
                        float mem_total = mem_info.mem_total / 1000000.0f;
                        float mem_available = mem_info.mem_available / 1000000.0f;
                        float mem_cached = mem_info.cached / 1000000.0f;
                        float mem_free = mem_info.mem_free / 1000000.0f;
                        float mem_used = mem_total - mem_available;
                        set_memory_total(win_data,mem_total);
                        set_memory_total_used_state(win_data,mem_used);
                        set_memory_total_used_state_graph(win_data,mem_used);
                        set_memory_cached_state(win_data,mem_cached);
                        set_memory_cached_state_graph(win_data,mem_cached);
                        set_memory_free_state(win_data,mem_free);
                        set_memory_free_state_graph(win_data,mem_free);
                        break;
                }
                case process:{
                        struct process_list process_list =
                                device_result.device_data.process_list;
                        struct vec2 win_size = {0};
                        get_window_size(win_data,&win_size);

                        int pid_column_width = check_max_pid_digit(process_list);
                        if(pid_column_width < 3)pid_column_width = 3;
                        set_chr(win_data,
                                L"PID",
                                (struct vec2){(pid_column_width - 3) / 2 + 1,1},
                                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                                A_NORMAL);
                        int name_column_x = pid_column_width + 2;
                        set_chr(win_data,
                                L"NAME",
                                (struct vec2){name_column_x,1},
                                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                                A_NORMAL);

                        int process_num = process_list.process_num;
                        int visible_process_num = win_size.y - 3;
                        if(visible_process_num < 0)visible_process_num = 0;
                        win_data->process_max_scroll = process_num - visible_process_num;
                        if(win_data->process_max_scroll < 0)win_data->process_max_scroll = 0;
                        if(win_data->process_scroll > win_data->process_max_scroll){
                                win_data->process_scroll = win_data->process_max_scroll;
                        }

                        int draw_process_num = process_num - win_data->process_scroll;
                        if(draw_process_num > visible_process_num){
                                draw_process_num = visible_process_num;
                        }
                        for(int i = 0;i < draw_process_num;i++){
                                int process_id = win_data->process_scroll + i;
                                set_process_pid(win_data,
                                        &process_list.process_info[process_id],
                                        pid_column_width,i + 2);
                                set_process_name(win_data,
                                        &process_list.process_info[process_id],
                                        name_column_x,i + 2);
                        }
                        free(process_list.process_info);
                        break;
                }
                
                default:
                        break;
        }
        free(parts_table.found_dev_table);
}


// 仕様: コア名・時系列グラフ・CPU全体バーを描画待ちデータへ登録する。
// 引数: 対象、CPU情報、表示するコア数。戻り値: なし。
void set_cpu_cores_info(struct window_data *win_data,struct cpu_info cpu_info,int cores_num){
        if(win_data == NULL)return;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);

        set_cpu_total_use_glaph(win_data);
        int core_pos_y = 2;
        int win_split_size = win_size.x/2;
        for(int i = 0; i < cores_num;i++){
                if(core_pos_y >= win_size.y)continue;

                wchar_t buff[32] = {0};
                int str_size = swprintf(buff,32,L"C%d",i);
                struct vec2 str_pos = {0,0};
                if(i % 2 != 0){
                        str_pos = (struct vec2){win_split_size,core_pos_y};
                        set_chr(win_data,
                                buff,
                                str_pos,
                                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                                A_NORMAL);
                                core_pos_y++;
                }
                else{
                        str_pos = (struct vec2){0,core_pos_y};
                        set_chr(win_data,
                                buff,
                                str_pos,
                                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                                A_NORMAL);
                }
                str_pos.x += str_size  + 1;
                set_cpu_core_glaph(win_data,i,str_pos);
        }

}


// 仕様: 指定コアの履歴を、右端が最新となるBraille時系列グラフとして登録する。
// 引数: 対象、0起点コア番号、グラフ開始座標。戻り値: なし。
void set_cpu_core_glaph(struct window_data *win_data,int core_id,struct vec2 glaph_start_pos){
        if(win_data == NULL)return;
        
        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);
        int split_x = win_size.x/2;
        int right_edge = win_size.x;
        if(get_window_outline_size(win_data,right) > 0)right_edge--;
        int glaph_end_x = glaph_start_pos.x < split_x ? split_x : right_edge;
        int glaph_len = glaph_end_x - glaph_start_pos.x;
        if(glaph_len <= 0)return;
        struct core_use_data core_use_data = get_core_usage_rate(core_id);
        int tmp_palms = 0;
        if(core_use_data.core_palm_data != NULL && core_use_data.core_palm_size > 0){
                tmp_palms = core_use_data.core_palm_data[core_use_data.core_palm_size -1];
        }
        int palm_num_len = 4;
        wchar_t palams_num[5] = {0};
        int joint_result = 
                swprintf(palams_num,5,L"%3d%%",tmp_palms);
        if(joint_result < 0)return;
        palams_num[joint_result] = L'\0';
        glaph_len -= palm_num_len;
        if(glaph_len <= 0)return;

        int palms_num_start_pos_x = 
                (glaph_start_pos.x > win_size.x/2)?
                win_size.x - palm_num_len : win_size.x/2 - palm_num_len;

        struct vec2 palms_num_start_pos = {palms_num_start_pos_x,glaph_start_pos.y};
        set_chr(win_data,palams_num,
                palms_num_start_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_NORMAL);

        int max_sample_count = glaph_len * 2;
        int newest_sample = -1;
        if(core_use_data.core_palm_data != NULL && core_use_data.core_palm_size > 0){
                newest_sample = core_use_data.core_palm_size - 1;
        }
        //最新の値を置く点の列。ここより左は古い値、右は空きになる
        int newest_column = cpu_graph_newest_column(max_sample_count);

        for(int glaph_id = 0;glaph_id < glaph_len;glaph_id++){
                int left_column = glaph_id * 2;
                int right_column = left_column + 1;
                int left_percent = 0;
                int right_percent = 0;
                //最新の列からの距離が、そのまま何件ぶん古いかになる
                int left_sample = newest_sample - (newest_column - left_column);
                int right_sample = newest_sample - (newest_column - right_column);
                if(left_column <= newest_column && left_sample >= 0){
                        left_percent = core_use_data.core_palm_data[left_sample];
                }
                if(right_column <= newest_column && right_sample >= 0){
                        right_percent = core_use_data.core_palm_data[right_sample];
                }

                wchar_t glaph_chr = 0x2800 | 0x40 | 0x80;

                if(left_percent > 25)glaph_chr |= 0x04;
                if(left_percent > 50)glaph_chr |= 0x02;
                if(left_percent > 75)glaph_chr |= 0x01;

                if(right_percent > 25)glaph_chr |= 0x20;
                if(right_percent > 50)glaph_chr |= 0x10;
                if(right_percent > 75)glaph_chr |= 0x08;

                //2列のうち高い方の値で1文字ぶんの色を決める
                //両方0%ならベースラインだけなので白のまま
                int color_percent = left_percent > right_percent ?
                        left_percent : right_percent;
                int glaph_color = COLOR_WHITE;
                if(color_percent > 75)glaph_color = COLOR_RED;
                else if(color_percent > 50)glaph_color = COLOR_YELLOW;
                else if(color_percent > 0)glaph_color = COLOR_GREEN;

                wchar_t glaph_cell[2] = {glaph_chr,L'\0'};
                struct vec2 cell_pos = {glaph_start_pos.x + glaph_id,
                        glaph_start_pos.y};
                set_chr(win_data,glaph_cell,cell_pos,
                        (struct color_pair){COLOR_DEFAULT,glaph_color},A_NORMAL);
        }
}



// 仕様: CPU全体の実測使用率をメモリグラフと同じ形式の横棒として登録する。引数: 対象。戻り値: なし。
void set_cpu_total_use_glaph(struct window_data *win_data){
        if(win_data == NULL)return;

        int percent = get_cpu_total_usage_rate();
        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);

        const wchar_t *top_msg = L"CPU";
        struct vec2 top_msg_st_pos = {1,1};
        size_t top_msg_len = wcslen(top_msg);
        set_chr(win_data,top_msg,
                top_msg_st_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_BOLD);

        wchar_t buff[16] = {0};
        int str_size = swprintf(buff,16,L"%d%%",percent);
        if(str_size < 0)return;

        int graph_st_pos = top_msg_st_pos.x + top_msg_len + 1;
        int graph_line_size = win_size.x - graph_st_pos - str_size - 4;
        if(graph_line_size <= 0)return;
        int graph_end_pos = graph_st_pos + graph_line_size + 1;
        int usage_str_pos = graph_end_pos + 2;
        int graph_used_size = graph_line_size * percent/100;

        int color = COLOR_GREEN;
        if(percent > 50)color = COLOR_YELLOW;
        if(percent > 90)color = COLOR_RED;

        wchar_t graph_used_space_buff[graph_used_size + 1];
        for(int i = 0;i < graph_used_size;i++){
                graph_used_space_buff[i] = L'|';
        }
        graph_used_space_buff[graph_used_size] = L'\0';

        wchar_t graph_unused_space_buff[graph_line_size - graph_used_size + 1];
        for(int i = 0;i < graph_line_size - graph_used_size;i++){
                graph_unused_space_buff[i] = L'|';
        }
        graph_unused_space_buff[graph_line_size - graph_used_size] = L'\0';

        wchar_t graph_st_chr[2] = {L'[',L'\0'};
        wchar_t graph_end_chr[2] = {L']',L'\0'};

        set_chr(win_data,graph_used_space_buff,
                (struct vec2){graph_st_pos + 1,1},
                (struct color_pair){COLOR_DEFAULT,color},A_BOLD);
        set_chr(win_data,graph_unused_space_buff,
                (struct vec2){graph_st_pos + 1 + graph_used_size,1},
                (struct color_pair){COLOR_DEFAULT,COLOR_BLACK},A_NORMAL);
        set_chr(win_data,buff,(struct vec2){usage_str_pos,1},
                (struct color_pair){COLOR_DEFAULT,color},A_BOLD);
        set_chr(win_data,graph_st_chr,(struct vec2){graph_st_pos,1},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);
        set_chr(win_data,graph_end_chr,(struct vec2){graph_end_pos,1},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);
}

// 仕様: 登録文字列の色を取得する。引数: 対象と0起点の登録番号。戻り値: 色、無効対象時は既定色。
struct color_pair get_window_msg_color_pair(struct window_data *win_data,int msg_num){
        //取り出せなかったときは端末の既定色を返す
        //ここで0(COLOR_BLACK)を返すと透過端末で背景が黒く塗られる
        struct color_pair default_col_pair = {COLOR_DEFAULT,COLOR_DEFAULT};
        if(win_data == NULL)return default_col_pair;
        struct color_pair tmp_col_pair = default_col_pair;
        tmp_col_pair = win_data->chr_data.chr_data[msg_num].chr_color;
        return tmp_col_pair;
}


// 仕様: CPUグラフ領域検査用の予約関数。引数: 対象。戻り値: 現在は常に0。
int check_cpu_usage_glaph_area(struct window_data *win_data){
        return 0;
}


// 仕様: 2つのWINDOW矩形が重なるか判定する。引数: 比較対象2つ。戻り値: 重なる場合true。
bool check_box_overlay(struct window_data *win1,struct window_data *win2){
        if(win1 == NULL || win2 == NULL){
                return 0;
        }

        if( get_parent_win(win1) != get_parent_win(win2))return 0;
        if(win1 == win2)return true;        

        struct box win1_box;
        struct box win2_box;
        win1_box = get_window_box(win1);
        win2_box = get_window_box(win2);

        int win1_right = win1_box.pos.x + win1_box.size.x;
        int win1_bottom = win1_box.pos.y + win1_box.size.y;

        int win2_right = win2_box.pos.x + win2_box.size.x;
        int win2_bottom = win2_box.pos.y + win2_box.size.y;

        bool x_overlap = win1_box.pos.x < win2_right &&
                win2_box.pos.x < win1_right;
        bool y_overlap = win1_box.pos.y < win2_bottom &&
                win2_box.pos.y < win1_bottom;

        return x_overlap && y_overlap;
}

// 仕様: 対象領域内で直接の子と重ならない最大矩形を求める。
// 引数: 親window_data、NULLならstdscr。戻り値: 最大空きbox、失敗時はゼロサイズ。
struct box get_window_empty_space(struct window_data *win_data){
        struct box empty_space = {{0,0},{0,0}};
        WINDOW *win = get_window_handle(win_data);
        if(win == NULL)return empty_space;

        int height = 0;
        int width = 0;
        getmaxyx(win,height,width);
        if(width < 1 || height < 1)return empty_space;

        unsigned char *occupied = calloc((size_t)height,(size_t)width);
        int *heights = calloc((size_t)width,sizeof(int));
        int *stack = malloc(sizeof(int) * (size_t)width);
        if(occupied == NULL || heights == NULL || stack == NULL){
                free(occupied);
                free(heights);
                free(stack);
                return empty_space;
        }

        for(int i = 0;i < windowlist_count;i++){
                struct window_data *child = winlist[i];
                if(child == NULL || child->parent_window_data != win_data)continue;

                struct box child_box = get_window_box(child);
                int left = child_box.pos.x;
                int top = child_box.pos.y;
                int right = left + child_box.size.x;
                int bottom = top + child_box.size.y;

                if(left < 0)left = 0;
                if(top < 0)top = 0;
                if(right > width)right = width;
                if(bottom > height)bottom = height;
                if(left >= right || top >= bottom)continue;

                for(int y = top;y < bottom;y++){
                        memset(&occupied[(size_t)y * (size_t)width + (size_t)left],
                                1,(size_t)(right - left));
                }
        }

        //各行を底辺とする空き高さから、最大面積の矩形を取り出す
        size_t max_area = 0;
        for(int y = 0;y < height;y++){
                for(int x = 0;x < width;x++){
                        if(occupied[(size_t)y * (size_t)width + (size_t)x]){
                                heights[x] = 0;
                        }
                        else{
                                heights[x]++;
                        }
                }

                int stack_top = -1;
                for(int x = 0;x <= width;x++){
                        int current_height = x < width ? heights[x] : 0;
                        while(stack_top >= 0 && heights[stack[stack_top]] > current_height){
                                int rect_height = heights[stack[stack_top--]];
                                int left = stack_top >= 0 ? stack[stack_top] + 1 : 0;
                                int rect_width = x - left;
                                size_t area = (size_t)rect_width * (size_t)rect_height;
                                if(area > max_area){
                                        max_area = area;
                                        empty_space.pos.x = left;
                                        empty_space.pos.y = y - rect_height + 1;
                                        empty_space.size.x = rect_width;
                                        empty_space.size.y = rect_height;
                                }
                        }
                        if(x < width)stack[++stack_top] = x;
                }
        }

        free(occupied);
        free(heights);
        free(stack);
        return empty_space;
}

// 仕様: メモリ総量を保存し、GiB表示を登録する。引数: 対象と総量。戻り値: なし。
void set_memory_total(struct window_data *win_data,float memory_total){
        if(win_data == NULL)return;

        memory_size_ctl(&memory_total,set);

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);
        wchar_t total[16];

        int joint_result = 
                swprintf(total,16,L"TOTAL: %.1f GiB",memory_total);
        
        if(joint_result > win_size.x)return;
        int mem_total_str_st_pos = (win_size.x - joint_result)/2;
        struct vec2 mem_total_st_pos ={mem_total_str_st_pos,1};

        set_chr(win_data,
                total,
                mem_total_st_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_BOLD);
}

// 仕様: 使用中メモリ量の文字表示を登録する。引数: 対象と使用量。戻り値: なし。
void set_memory_total_used_state(struct window_data *win_data,float used_data){
        if(win_data == NULL)return;

        struct vec2 win_size ={0,0};
        get_window_size(win_data,&win_size);
        wchar_t str[16];
        
        int joint_result = 
                swprintf(str,sizeof(str),L"Used: %0.1f GiB",used_data);

        int mem_used_str_st_pos_x = (win_size.x - joint_result)/2;
        struct vec2 mem_used_str_st_pos  = {mem_used_str_st_pos_x,2};

        set_chr(win_data,
                str,
                mem_used_str_st_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_NORMAL);

        set_line(win_data,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                mem_used_str_st_pos.y);
}

// 仕様: 水平線を登録し、同じ行なら色を更新する。引数: 対象、色、行番号。戻り値: 成功0、失敗-1。
int set_line(struct window_data *win_data,struct color_pair color_pair,int line){
        if(win_data == NULL)return 0;
        for(int i = 0;i < win_data->line_count;i++){
                if(win_data->line[i].line != line)continue;
                win_data->line[i].color = color_pair;
                return win_data->line_count;
        }
        if(win_data->line_count >= win_data->line_capacity){
                line_memory_allocate(win_data);
                if(win_data->line_count >= win_data->line_capacity)return win_data->line_count;
        }
        win_data->line[win_data->line_count].line = line;
        win_data->line[win_data->line_count].color = color_pair;
        win_data->line_count++;
        return win_data->line_count;
}

// 仕様: 水平線配列の初期容量を確保する。引数: 対象。戻り値: なし。既に確保済みなら何もしない。
void line_memory_allocate(struct window_data *win_data){
        if(win_data == NULL)return;
        int new_capacity = win_data->line_capacity == 0 ? 4 : win_data->line_capacity * 2;
        struct line *tmp_line = realloc(win_data->line,
                sizeof(*tmp_line) * new_capacity);
        if(tmp_line == NULL)return;
        win_data->line = tmp_line;
        win_data->line_capacity = new_capacity;
}

// 仕様: 水平線データのコピーを返す。引数: 対象。戻り値: 呼び出し側がfree()する配列と件数。
struct line_result get_line_data(struct window_data *win_data){
        if(win_data == NULL)return (struct line_result){NULL,0};
        struct line_result tmp_line;
        int line_num = win_data->line_count;
        if(line_num <= 0)return (struct line_result){NULL,0};
        tmp_line.line_data = calloc(line_num,sizeof(struct line));
        if(tmp_line.line_data == NULL)return (struct line_result){NULL,0};
        memcpy(tmp_line.line_data,win_data->line,sizeof(struct line) * line_num);
        struct line_result tmp_line_result = {tmp_line.line_data,line_num};
        return tmp_line_result;
}
// 仕様: 登録WINDOW一覧を参照用に返す。引数: 一覧と件数の出力先。戻り値: なし。配列をfree()してはならない。
void get_window_list(struct window_data ***win_list,int *win_num){
        if(win_list == NULL || win_num == NULL)return;
        *win_list = winlist;
        *win_num = windowlist_count;
}


// 仕様: 指定WINDOWを一覧から外して関連メモリを解放する。引数: 所有ポインタのアドレス。戻り値: 成功0、失敗-1。
int free_window(struct window_data **win_data){
        if(win_data == NULL || *win_data == NULL)return -1;

        struct window_data *target = *win_data;
        clear_window_chr_data(target);
        free(target->chr_data.chr_data);
        free(target->line);

        return window_list_ctrl(win_data,windowlist_remove);
}


// 仕様: 使用中メモリの割合グラフを登録する。引数: 対象と使用量。戻り値: なし。
void set_memory_total_used_state_graph(struct window_data *win_data,float used_data){
        if(win_data == NULL)return;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);

        //ウィンドウサイズがグラフを描く５行未満だと関数を終わる
        if(win_size.y < 5)return;

        wchar_t buff[32] ={0};
        float mem_total = 0;
        memory_size_ctl(&mem_total,get);
        if(mem_total <= 0)return;
        float mem_ratio = used_data/mem_total * 100;

        int color = COLOR_GREEN;
        if(mem_ratio > 50)color = COLOR_YELLOW;
        if(mem_ratio > 90)color = COLOR_RED;

        int str_size =
                swprintf(buff,32,L"%.2fG/%.2fG %.2f%%",
                        used_data,mem_total,mem_ratio);
        int graph_line_size = win_size.x -5 - str_size;
        if(graph_line_size <= 0)return;
        int graph_end_pos = 2 + graph_line_size;
        int usage_str_pos = graph_end_pos + 2;

        //メモリ使用率のグラフのメモリ
        int mem_graph_memory = graph_line_size * used_data/mem_total;
        if(mem_graph_memory < 0)mem_graph_memory = 0;

        if(mem_graph_memory > graph_line_size)mem_graph_memory = graph_line_size;

        wchar_t graph_used_space_buff[mem_graph_memory + 1];
        for(int i = 0;i < mem_graph_memory;i++){
                graph_used_space_buff[i] = L'|';
        }
        graph_used_space_buff[mem_graph_memory] = L'\0';

        wchar_t graph_unused_space_buff[graph_line_size + 1];
        for(int i = 0;i < graph_line_size - mem_graph_memory;i++){
                graph_unused_space_buff[i] = L'|';
        }
        graph_unused_space_buff[graph_line_size - mem_graph_memory] = L'\0';
        
        wchar_t graph_st_chr[2] = {L'[',L'\0'};
        wchar_t graph_end_chr[2] = {L']',L'\0'};


        //グラフの使用グラフ部分
        set_chr(win_data,graph_used_space_buff,(struct vec2){2,3},
                (struct color_pair){COLOR_DEFAULT,color},A_BOLD);
        
        //使われていないスペースの部分
        set_chr(win_data,graph_unused_space_buff,(struct vec2){2 + mem_graph_memory,3},
                (struct color_pair){COLOR_DEFAULT,COLOR_BLACK},A_NORMAL);

        //使用率の数値
        set_chr(win_data,buff,(struct vec2){usage_str_pos,3},
                (struct color_pair){COLOR_DEFAULT,color},A_BOLD);

        //グラフの初めの括弧
        set_chr(win_data,graph_st_chr,(struct vec2){1,3},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);

        //グラフ終わりの括弧
        set_chr(win_data,graph_end_chr,(struct vec2){graph_end_pos,3},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);

}



// 仕様: キャッシュ済みメモリ量の文字表示を登録する。引数: 対象と量。戻り値: なし。
void set_memory_cached_state(struct window_data *win_data,float cached_data){
        if(win_data == NULL)return;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);
        if(win_size.y < 6)return;
        wchar_t str[20];

        int joint_result =
                swprintf(str,20,L"Cached: %.1f GiB",cached_data);

        int cached_str_st_pos_x = (win_size.x - joint_result)/2;
        struct vec2 cached_str_st_pos = {cached_str_st_pos_x,4};

        set_chr(win_data,
                str,
                cached_str_st_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_NORMAL);

        set_line(win_data,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                cached_str_st_pos.y);
}

// 仕様: キャッシュ済みメモリの割合グラフを登録する。引数: 対象と量。戻り値: なし。
void set_memory_cached_state_graph(struct window_data *win_data,float cached_data){
        if(win_data == NULL)return;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);
        if(win_size.y < 7)return;

        wchar_t buff[32] = {0};
        float mem_total = 0;
        memory_size_ctl(&mem_total,get);
        if(mem_total <= 0)return;
        float mem_ratio = cached_data/mem_total * 100;

        int color = COLOR_GREEN;
        if(mem_ratio > 50)color = COLOR_YELLOW;
        if(mem_ratio > 90)color = COLOR_RED;

        int str_size =
                swprintf(buff,32,L"%.2fG/%.2fG %.2f%%",
                        cached_data,mem_total,mem_ratio);
        int graph_line_size = win_size.x -5 - str_size;
        if(graph_line_size <= 0)return;
        int graph_end_pos = 2 + graph_line_size;
        int usage_str_pos = graph_end_pos + 2;

        int mem_graph_memory = graph_line_size * cached_data/mem_total;
        if(mem_graph_memory < 0)mem_graph_memory = 0;
        if(mem_graph_memory > graph_line_size)mem_graph_memory = graph_line_size;

        wchar_t graph_cached_space_buff[mem_graph_memory + 1];
        for(int i = 0;i < mem_graph_memory;i++){
                graph_cached_space_buff[i] = L'|';
        }
        graph_cached_space_buff[mem_graph_memory] = L'\0';

        wchar_t graph_unused_space_buff[graph_line_size + 1];
        for(int i = 0;i < graph_line_size - mem_graph_memory;i++){
                graph_unused_space_buff[i] = L'|';
        }
        graph_unused_space_buff[graph_line_size - mem_graph_memory] = L'\0';

        wchar_t graph_st_chr[2] = {L'[',L'\0'};
        wchar_t graph_end_chr[2] = {L']',L'\0'};

        set_chr(win_data,graph_cached_space_buff,(struct vec2){2,5},
                (struct color_pair){COLOR_DEFAULT,color},A_BOLD);
        set_chr(win_data,graph_unused_space_buff,(struct vec2){2 + mem_graph_memory,5},
                (struct color_pair){COLOR_DEFAULT,COLOR_BLACK},A_NORMAL);
        set_chr(win_data,buff,(struct vec2){usage_str_pos,5},
                (struct color_pair){COLOR_DEFAULT,color},A_BOLD);
        set_chr(win_data,graph_st_chr,(struct vec2){1,5},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);
        set_chr(win_data,graph_end_chr,(struct vec2){graph_end_pos,5},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);
}

// 仕様: 空きメモリ量の文字表示を登録する。引数: 対象と量。戻り値: なし。
void set_memory_free_state(struct window_data *win_data,float free_data){
        if(win_data == NULL)return;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);
        if(win_size.y < 8)return;
        wchar_t str[20];

        int joint_result =
                swprintf(str,20,L"Free: %.1f GiB",free_data);

        int free_str_st_pos_x = (win_size.x - joint_result)/2;
        struct vec2 free_str_st_pos = {free_str_st_pos_x,6};

        set_chr(win_data,
                str,
                free_str_st_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_NORMAL);

        set_line(win_data,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                free_str_st_pos.y);
}

// 仕様: 空きメモリの割合グラフを登録する。引数: 対象と量。戻り値: なし。
void set_memory_free_state_graph(struct window_data *win_data,float free_data){
        if(win_data == NULL)return;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);
        if(win_size.y < 9)return;

        wchar_t buff[32] = {0};
        float mem_total = 0;
        memory_size_ctl(&mem_total,get);
        if(mem_total <= 0)return;
        float mem_ratio = free_data/mem_total * 100;

        int color = COLOR_GREEN;
        if(mem_ratio < 60)color = COLOR_YELLOW;
        if(mem_ratio < 30)color = COLOR_RED;

        int str_size =
                swprintf(buff,32,L"%.2fG/%.2fG %.2f%%",
                        free_data,mem_total,mem_ratio);
        int graph_line_size = win_size.x -5 - str_size;
        if(graph_line_size <= 0)return;
        int graph_end_pos = 2 + graph_line_size;
        int usage_str_pos = graph_end_pos + 2;

        int mem_graph_memory = graph_line_size * free_data/mem_total;
        if(mem_graph_memory < 0)mem_graph_memory = 0;
        if(mem_graph_memory > graph_line_size)mem_graph_memory = graph_line_size;

        wchar_t graph_free_space_buff[mem_graph_memory + 1];
        for(int i = 0;i < mem_graph_memory;i++){
                graph_free_space_buff[i] = L'|';
        }
        graph_free_space_buff[mem_graph_memory] = L'\0';

        wchar_t graph_used_space_buff[graph_line_size + 1];
        for(int i = 0;i < graph_line_size - mem_graph_memory;i++){
                graph_used_space_buff[i] = L'|';
        }
        graph_used_space_buff[graph_line_size - mem_graph_memory] = L'\0';

        wchar_t graph_st_chr[2] = {L'[',L'\0'};
        wchar_t graph_end_chr[2] = {L']',L'\0'};

        set_chr(win_data,graph_free_space_buff,(struct vec2){2,7},
                (struct color_pair){COLOR_DEFAULT,color},A_BOLD);
        set_chr(win_data,graph_used_space_buff,(struct vec2){2 + mem_graph_memory,7},
                (struct color_pair){COLOR_DEFAULT,COLOR_BLACK},A_NORMAL);
        set_chr(win_data,buff,(struct vec2){usage_str_pos,7},
                (struct color_pair){COLOR_DEFAULT,color},A_BOLD);
        set_chr(win_data,graph_st_chr,(struct vec2){1,7},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);
        set_chr(win_data,graph_end_chr,(struct vec2){graph_end_pos,7},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);
}

// 仕様: WINDOWに設定された再描画対象デバイスを参照する。引数: 対象。戻り値: 内部enumへのポインタ、未設定時NULL。
enum device *get_window_draw_dev(struct window_data *win_data){
        if(win_data == NULL || win_data->dev == device_count )return NULL;
        return &win_data->dev;
}

void scroll_process(struct window_data *win_data,int amount){
        if(win_data == NULL || win_data->dev != process)return;

        int next_scroll = win_data->process_scroll + amount;
        if(next_scroll < 0)next_scroll = 0;
        else if(next_scroll > win_data->process_max_scroll){
                next_scroll = win_data->process_max_scroll;
        }
        win_data->process_scroll = next_scroll;
}


// 仕様: CPU全体の使用率履歴を、コアグラフと同じ点字の時系列グラフとして登録する。
// 引数: 対象。戻り値: なし。右端が最新で、1文字が横2件・縦4段ぶんを表す。
static void set_cpu_total_use_big_graph(struct window_data *win_data){
        if(win_data == NULL)return;
        int cpu_total_use = get_cpu_total_usage_rate();
        cpu_total_use_log(&cpu_total_use,set);

        int cpu_total_use_log_arry[cpu_total_use_log_max] = {0};
        int cpu_total_use_log_num =
                cpu_total_use_log(cpu_total_use_log_arry,get);
        if(cpu_total_use_log_num <= 0)return;

        struct box graph_space = get_window_empty_space(win_data);
        int graph_left = graph_space.pos.x + get_window_outline_size(win_data,left);
        int graph_top = graph_space.pos.y + get_window_outline_size(win_data,top);
        int graph_width = graph_space.size.x -
                get_window_outline_size(win_data,left) -
                get_window_outline_size(win_data,right);
        int graph_height = graph_space.size.y -
                get_window_outline_size(win_data,top) -
                get_window_outline_size(win_data,bottom);
        if(graph_width <= 0 || graph_height <= 0)return;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);
        int graph_zero_y = win_size.y/2;
        if(graph_zero_y < graph_top)graph_zero_y = graph_top;
        if(graph_zero_y >= graph_top + graph_height){
                graph_zero_y = graph_top + graph_height - 1;
        }
        int graph_max_height = graph_zero_y - graph_top;

        int graph_row_count = graph_max_height + 1;
        int dot_row_count = graph_row_count * braille_dot_rows;

        //1文字で2件ぶんを表すので、同じ幅でも棒グラフの倍の期間が入る
        int max_sample_count = graph_width * 2;
        int newest_sample = cpu_total_use_log_num - 1;
        int newest_column = max_sample_count - 1;

        //1文字ぶんの点を下から上へ並べたもの
        //左列は点7・3・2・1、右列は点8・6・5・4に当たる
        static const wchar_t left_dot_bit[braille_dot_rows] = {0x40,0x04,0x02,0x01};
        static const wchar_t right_dot_bit[braille_dot_rows] = {0x80,0x20,0x10,0x08};

        for(int glaph_id = 0;glaph_id < graph_width;glaph_id++){
                int left_column = glaph_id * 2;
                int right_column = left_column + 1;
                int left_percent = 0;
                int right_percent = 0;
                int left_dot_height = 0;
                int right_dot_height = 0;
                //最新の列からの距離が、そのまま何件ぶん古いかになる
                int left_sample = newest_sample - (newest_column - left_column);
                int right_sample = newest_sample - (newest_column - right_column);
                if(left_column <= newest_column && left_sample >= 0){
                        left_percent = cpu_total_use_log_arry[left_sample];
                        left_dot_height = cpu_use_dot_height(left_percent,dot_row_count);
                }
                if(right_column <= newest_column && right_sample >= 0){
                        right_percent = cpu_total_use_log_arry[right_sample];
                        right_dot_height = cpu_use_dot_height(right_percent,dot_row_count);
                }
                if(left_dot_height == 0 && right_dot_height == 0)continue;

                //2列のうち高い方の値で1文字ぶんの色を決める
                int color_percent = left_percent > right_percent ?
                        left_percent : right_percent;
                int color = COLOR_GREEN;
                if(color_percent > 50)color = COLOR_YELLOW;
                if(color_percent > 90)color = COLOR_RED;

                for(int row = 0;row < graph_row_count;row++){
                        wchar_t glaph_chr = braille_base_chr;
                        //この行が受け持つのは、下から数えてdot_row_base段目からの4段
                        int dot_row_base = row * braille_dot_rows;
                        for(int dot = 0;dot < braille_dot_rows;dot++){
                                if(dot_row_base + dot < left_dot_height){
                                        glaph_chr |= left_dot_bit[dot];
                                }
                                if(dot_row_base + dot < right_dot_height){
                                        glaph_chr |= right_dot_bit[dot];
                                }
                        }
                        //点が1つも無ければ、これより上の行も空になる
                        if(glaph_chr == braille_base_chr)break;

                        wchar_t glaph_cell[2] = {glaph_chr,L'\0'};
                        set_chr(win_data,glaph_cell,
                                (struct vec2){graph_left + glaph_id,
                                        graph_zero_y - row},
                                (struct color_pair){COLOR_DEFAULT,color},A_NORMAL);
                }
        }
}

// 仕様: 最新の値を入れる点の列(0起点、右端がmax_sample_count-1)を返す。
// 引数: グラフ全体に入る件数。戻り値: 0以上max_sample_count-1以下の列番号。
// 点字1文字は2件ぶんなので、左右どちらの点列に入るかを採取回数で固定する。
// 1件進むたびに入れ替えると同じ値でも組み合わせが変わり、
// 1文字ぶんで決めている色が、グラフが動くたびに変わってしまう。
// そのぶんグラフは2件ごとに1文字ずつ左へ動く。
static int cpu_graph_newest_column(int max_sample_count){
        int newest_column = max_sample_count - 1;
        if((newest_column % 2) != (int)(get_cpu_sample_count() % 2))newest_column--;
        if(newest_column < 0)newest_column = 0;
        return newest_column;
}

// 仕様: 使用率を、下から数えた点字の点の段数へ直す。
// 引数: 0〜100の使用率と、グラフ全体の段数。戻り値: 1以上dot_row_count以下の段数。
// 0%でも1段だけ点けて、コアグラフと同じく底辺を残す。
static int cpu_use_dot_height(int percent,int dot_row_count){
        if(percent < 0)percent = 0;
        else if(percent > 100)percent = 100;

        int dot_height = percent * dot_row_count / 100;
        if(dot_height < 1)dot_height = 1;
        return dot_height;
}

void set_process_pid(struct window_data *win_data,
        const struct process_info *process_info,int digit,int line){
        if(win_data == NULL || process_info == NULL || digit <= 0)return;

        wchar_t pid[digit + 1];
        if(swprintf(pid,digit + 1,L"%*d",digit,process_info->pid) < 0)return;

        set_chr(win_data,
                pid,
                (struct vec2){1,line},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_NORMAL);
}

void set_process_name(struct window_data *win_data,
        const struct process_info *process_info,int column,int line){
        if(win_data == NULL || process_info == NULL)return;

        struct vec2 win_size = {0};
        get_window_size(win_data,&win_size);
        int name_max_len = win_size.x - column -
                get_window_outline_size(win_data,right);
        if(name_max_len <= 0)return;
        if(name_max_len >= process_name_size){
                name_max_len = process_name_size - 1;
        }

        wchar_t name[process_name_size] = {0};
        size_t name_len = mbstowcs(name,process_info->name,process_name_size - 1);
        if(name_len == (size_t)-1)return;
        if(name_len > (size_t)name_max_len)name_len = name_max_len;
        name[name_len] = L'\0';

        set_chr(win_data,
                name,
                (struct vec2){column,line},
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_NORMAL);
}


void get_visible_process_area(){

}
