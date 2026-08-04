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
#include "process_info/process_info.h"
#include "theme.h"


static void set_cpu_cores_info(struct window_data *win_data,int cores_num);

//長方形(位置・サイズ)の管理はncursesのWINDOWに任せる
//そのためwindow_data側では位置とサイズを持たない
//WINDOWはすべて親のサブウィンドウ(derwin)として作るので
//親を指定しない場合はstdscrの子になる
struct window_data{
        WINDOW *win;
        struct chr_data_arry chr_data;
        struct outline outline;
        struct window_data *parent_window_data;
};

//丸めた結果のboxをWINDOWへ反映する
//必ず移動→リサイズの順に呼ぶ
//先に広げると移動前の位置のまま親をはみ出してwresize()が失敗するため
static int apply_window_box(struct window_data *win_data,struct box win_box){
        if(mvderwin(win_data->win,win_box.pos.y,win_box.pos.x) == ERR)return -1;
        if(wresize(win_data->win,win_box.size.y,win_box.size.x) == ERR)return -1;
        return 0;
}

//今のWINDOWの位置と大きさをboxとして取り出す
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
        tmp_win_data->chr_data.chr_data = calloc(8,sizeof(struct chr_data *));
        if(tmp_win_data->chr_data.chr_data == NULL){
                free(tmp_win_data);
                return NULL;
        }
        tmp_win_data->chr_data.chr_data_count = 0;
        tmp_win_data->chr_data.allocate_num = 8;

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

        return tmp_win_data;
}

//WINDOWの大きさをwresize()で変える
//サイズの実体はWINDOWが持つので、window_data側には控えを残さない
//位置は今のものを据え置きで渡し、親付きなら親の内側に収まる値へ丸めてもらう
//要求サイズが大きすぎると丸めの結果として位置も動くので、boxごと反映する
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
int show_window_outline(struct window_data *win_data,bool show_outline){
        if(win_data == NULL)return -1;
        win_data->outline.show_outline = show_outline;
        return 0;
}


//定義したウィンドウの枠線の色をセットする
//辺ごとに1回ずつ呼ぶ。持っておくだけで、色を実際に乗せるのは描画時
//引数colorは文字色(COLOR_WHITEなど)で、背景色は触らない
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
int window_list_ctrl(struct window_data **win_data,int flags){
        if(win_data == NULL || *win_data == NULL)return -1;
        static struct window_data *winlist[winlist_max] = {0};
        static int windowlist_count = 0;

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
WINDOW *get_window_handle(struct window_data *win_data){
        if(win_data == NULL)return NULL;
        return win_data->win;
}

//枠線の色を4辺まとめてコピーして返す
//描画側から中身を直接書き換えられないよう、値渡しで取り出す
void get_window_outline_color(struct window_data *win_data,struct line_color *line_color){
        if(win_data == NULL || line_color == NULL)return;
        *line_color = win_data->outline.line_color;
}


//子ウィンドウの角が、親の枠線のどの辺の上に乗っているかを判定する
//corner_posは子ウィンドウから見たローカル座標(角の4点のどれか)
//乗っている辺をenum line_sideで返し、どこにも乗っていなければ-1を返す
//親の角(縦横2本が交わる点)と重なった場合はT字にすると親の角が壊れるので-1
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

void set_chr(struct window_data *win_data,
        const wchar_t *msg,struct vec2 msg_start_pos,
        struct color_pair color,attr_t style){

        if(win_data == NULL || msg == NULL)return;
        if(win_data->chr_data.chr_data == NULL)return;

        if(win_data->chr_data.chr_data_count >= win_data->chr_data.allocate_num){
                int new_allocate_num = win_data->chr_data.allocate_num * 2;
                struct chr_data **tmp_chr_data =
                        realloc(win_data->chr_data.chr_data,
                                sizeof(*tmp_chr_data) * new_allocate_num);
                if(tmp_chr_data == NULL)return;
                win_data->chr_data.chr_data = tmp_chr_data;
                win_data->chr_data.allocate_num = new_allocate_num;
        }

        struct chr_data *new_chr_data = calloc(1,sizeof(*new_chr_data));
        if(new_chr_data == NULL)return;
        size_t msg_size = wcslen(msg) + 1;
        new_chr_data->chr_data = malloc(sizeof(wchar_t) * msg_size);
        if(new_chr_data->chr_data == NULL){
                free(new_chr_data);
                return;
        }
        wmemcpy(new_chr_data->chr_data,msg,msg_size);
        new_chr_data->chr_color = color;
        new_chr_data->chr_st_pos = msg_start_pos;
        new_chr_data->style = style;
        win_data->chr_data.chr_data[win_data->chr_data.chr_data_count] = new_chr_data;
        win_data->chr_data.chr_data_count++;
}

struct chr_data_arry get_window_msg_data(struct window_data *win_data){
        return win_data->chr_data;
}

void get_vec2_maxyx(WINDOW *win,struct vec2 *pos){
        if(win == NULL || pos == NULL) return;
        struct vec2 tmp_pos = {0,0};
        getmaxyx(win,tmp_pos.y,tmp_pos.x);
        *pos = tmp_pos; 
}


struct window_data *get_parent_win(struct window_data *win_data){
        if(win_data == NULL)return NULL;
        return win_data->parent_window_data;
}



void set_device_data(struct window_data *win_data,enum device dev){
        if(win_data == NULL)return;
        struct found_device_parts_table parts_table;
        //端末にある取得可能なパーツを探す
        check_detectable_parts(&parts_table);
        struct device_info_result device_result;

        bool is_found = false;
        for(int i = 0; i < parts_table.found_dev_count;i++){
                //もし探したパーツのテーブル内に第3引数と同じものがあればそのデータを取得する
                if(parts_table.found_dev_table[i] != dev)continue;
                device_result = get_device_info(dev);
                is_found = true;
        }
        if(is_found == false)return;

        switch(device_result.device_name){
                case cpu:{
                        struct cpu_info cpu_info = device_result.device_data.cpu_info;
                        set_cpu_cores_info(win_data,atoi(cpu_info.cpu_cores));
                        break;
                }
                case memory:{
                        struct mem_info mem_info = device_result.device_data.mem_info;
                        FILE *p = fopen("d.txt","r");
                        fputs(mem_info.mem_total,p);
                        fclose(p);
                }
                
                default:
                        break;
        }



}


void set_cpu_cores_info(struct window_data *win_data,int cores_num){
        if(win_data == NULL)return;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);
        set_cpu_total_use_glaph(win_data,20);
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
        int palm_num_len = 2;
        int tmp_palms = 0;
        if(core_use_data.core_palm_data != NULL){
                if(core_use_data.core_palm_data[core_use_data.core_palm_size] > 99)palm_num_len = 4;
                else if(core_use_data.core_palm_data[core_use_data.core_palm_size] > 10)palm_num_len = 3;
                tmp_palms = core_use_data.core_palm_data[core_use_data.core_palm_size];
        }
        wchar_t palams_num[palm_num_len+1];//'\0'用に＋1する
        int joint_result = 
                swprintf(palams_num,palm_num_len+1,
                        L"%d%%",tmp_palms);
        palams_num[joint_result] = '\0';
        //0%でも一番下の2点は必ず出すので、まず白いベースラインを敷いてから
        //値のある文字だけ後から色付きで上書きする
        //まだ標本が1件も無いコアでもベースラインだけは引く
        const wchar_t glaph_base_line = 0x2800 | 0x40 | 0x80;
        glaph_len -= palm_num_len;
        wchar_t glaph_str[glaph_len + 1];
        for(int i = 0;i < glaph_len;i++)glaph_str[i] = glaph_base_line;
        glaph_str[glaph_len] = L'\0';
        set_chr(win_data,glaph_str,glaph_start_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_NORMAL);

        int palms_num_start_pos_x = 
                (glaph_start_pos.x > win_size.x/2)?
                win_size.x - palm_num_len : win_size.x/2 - palm_num_len;

        struct vec2 palms_num_start_pos = {palms_num_start_pos_x,glaph_start_pos.y};
        set_chr(win_data,palams_num,
                palms_num_start_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_NORMAL);

        if(core_use_data.core_palm_data == NULL || core_use_data.core_palm_size <= 0)return;

        //一番後ろ(最新)から2件ずつ読み、左端の文字から右へ古い順に並べる
        //1文字の中も左列が新しい方、右列がその1つ前になる
        int glaph_id = 0;
        for(int i = core_use_data.core_palm_size - 1;
                i >= 0 && glaph_id < glaph_len;i -= 2){
                int left_percent = core_use_data.core_palm_data[i];
                int right_percent = i - 1 >= 0 ?
                        core_use_data.core_palm_data[i - 1] : 0;
                //一番下の段はベースラインなので値に関係なく常に点灯させる
                wchar_t glaph_chr = glaph_base_line;

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
                glaph_id++;
        }
}



void set_cpu_total_use_glaph(struct window_data *win_data,int percent){
        if(win_data == NULL)return;

        if(percent > 100)percent = 100;
        else if(percent < 0) percent = 0;

        struct vec2 win_size = {0,0};
        get_window_size(win_data,&win_size);

        const wchar_t *top_msg = L"CPU";
        struct vec2 top_msg_st_pos = {0,1};
        size_t top_msg_len = wcslen(top_msg);
        set_chr(win_data,top_msg,
                top_msg_st_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},
                A_BOLD);

        struct vec2 total_cpu_use_glaph_area_start_pos;
        total_cpu_use_glaph_area_start_pos.x = top_msg_st_pos.x + top_msg_len+3;
        total_cpu_use_glaph_area_start_pos.y = 1;

        int glaph_str_max_size = win_size.x - total_cpu_use_glaph_area_start_pos.x ;
        int now_uage = glaph_str_max_size * percent/100;
        wchar_t glaph_str[now_uage];
        for(int i = 0;i < now_uage;i++)glaph_str[i] = get_font(cpu_bar_graph);
        glaph_str[now_uage] = L'\0';

        set_chr(win_data,glaph_str,total_cpu_use_glaph_area_start_pos,
                (struct color_pair){COLOR_DEFAULT,COLOR_WHITE},A_BOLD);
}

struct color_pair get_window_msg_color_pair(struct window_data *win_data,int msg_num){
        //取り出せなかったときは端末の既定色を返す
        //ここで0(COLOR_BLACK)を返すと透過端末で背景が黒く塗られる
        struct color_pair default_col_pair = {COLOR_DEFAULT,COLOR_DEFAULT};
        if(win_data == NULL)return default_col_pair;
        struct color_pair tmp_col_pair = default_col_pair;
        if(win_data->chr_data.chr_data[msg_num] == NULL)return tmp_col_pair;
        tmp_col_pair = win_data->chr_data.chr_data[msg_num]->chr_color;
        return tmp_col_pair;
}


int check_cpu_usage_glaph_area(struct window_data *win_data){
        return 0;
}
