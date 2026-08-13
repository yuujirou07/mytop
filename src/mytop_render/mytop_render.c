
#include"mytop_render.h"
#include "core.h"
#include"theme.h"
#include "window.h"
#include <ncurses.h>
#include <locale.h>
#include <string.h>
#include <wchar.h>
#include<stdlib.h>

#define color_pair_max 128


static int color_pair_dic(int *pair_id,int color[2],int flags);
static int outline_color_pair(struct color_pair color);
static int color_pair_id(int bg_color,int fg_color);
//描画まわりの入口
//ncursesを立ち上げ、色が使える端末ならカラーペアも先に全部登録しておく
//create_window()はstdscrを必要とするので、必ずこれより後に呼ぶ
int init_render(){
        init_ncurses();
        if(has_colors()){
                set_color();
                //stdscrの背景を端末の既定色にしておく
                //ここより後にderwin()で作る子ウィンドウはこの背景を引き継ぐので、
                //背景が透過している端末でも黒で塗り潰されない
                set_background(COLOR_DEFAULT);
        }


      return 0;
}

int render_push(){
        return 0;
}

//ncursesを終了して端末を元の状態へ戻す
//stdscrはendwin()側の管理なのでこちらでは解放しない
int end_render(){
        //stdscrはendwin()側の管理なのでdelwin()しない
        endwin();
        return 0;
}



//ncursesの初期化関数
//WINDOWの取得関数も初期化する
//noecho()で打った文字が勝手に出るのを止め、cbreak()で
//Enterを待たず1キーずつgetch()へ流れるようにする
void init_ncurses(){
        setlocale(LC_ALL,"");
        WINDOW *win = initscr();
        win_ctrl(win,win_set);
        noecho();
        curs_set(0);
        cbreak();
        refresh();
        return;
}

//画面全体のWINDOW(stdscr)を保持して取り出せるようにする
//win_setでセット、win_getで取得する
WINDOW *win_ctrl(WINDOW *win,int flags){

        static WINDOW *static_win = NULL;

        switch(flags){
                case win_set:
                        if(win == NULL)return NULL;
                        static_win = win;
                        return static_win;
                case win_get:
                        return static_win;
        }
        return NULL;
}

//色の指定(bg,fg)からカラーペアのidを決める
//COLOR_DEFAULT(-1)も色の1つとして扱うため+1して0起点へ寄せる
//ペア0はncursesが「既定色の組み合わせ」として予約していて
//init_pair()で書き換えられないので、さらに+1して1から使う
int color_pair_id(int bg_color,int fg_color){
        return (bg_color + 1) * 9 + (fg_color + 1) + 1;
}

//背景色9種×文字色9種の組み合わせ81通りを、あらかじめ全部
//カラーペアとして登録しておく
//9種なのは通常の8色にCOLOR_DEFAULT(端末の既定色)を足したぶん
//idはcolor_pair_id()で決め打ちなので、後から(bg,fg)で引ける
void set_color(){
        start_color(); // 色の準備
        //これを呼ぶと-1を「端末が元から使っている色」として指定できるようになる
        use_default_colors();
        for(int bg = COLOR_DEFAULT;bg < 8;bg++){
                for(int  fg= COLOR_DEFAULT;fg < 8;fg++){
                        int color[2] ={bg,fg};
                        int color_pair_idx = color_pair_id(bg,fg);
                        color_pair_dic(&color_pair_idx,color,
                                col_pair_dic_set);
                }
        }
}


//カラーペアをディクショナリとして保存しておき、検索できるようにする
//第二引数はcolor[0]が背景色color[1]が文字色
//もし辞書内に同じpoair_idがあれば色データを上書きする仕様
//col_pair_dic_getのときは*pair_idに見つかったidを書き戻す
int color_pair_dic(int *pair_id,int color[2],int flags){
        if(pair_id == NULL)return -1;

        //引数の値制限
        //下限はCOLOR_DEFAULT(-1)、つまり端末の既定色まで許す
        if(color[0] < COLOR_DEFAULT)color[0] = COLOR_DEFAULT;
        else if(color[0] > 7)color[0] = 7;

        if(color[1] < COLOR_DEFAULT)color[1] = COLOR_DEFAULT;
        else if(color[1] > 7)color[1] = 7;

        static struct id_color_dic color_pair_dic[color_pair_max] = {0};
        static int color_pair_count = 0;

        switch(flags){
                case col_pair_dic_set:
                        //前にpair_idが登録されていないかチェック
                        //もし登録されていたら上書き判定
                        for(int i = 0; i < color_pair_count;i++){
                                if(color_pair_dic[i].color_pair_id == *pair_id){
                                        color_pair_dic[i].color_pair.bg_color = color[0];
                                        color_pair_dic[i].color_pair.fg_color = color[1];
                                        init_pair(*pair_id,color[1],color[0]);
                                        return 0;
                                }
                        }
                        if(color_pair_count >= color_pair_max)return -1;
                        color_pair_dic[color_pair_count].color_pair_id = *pair_id;
                        color_pair_dic[color_pair_count].color_pair.bg_color = color[0];
                        color_pair_dic[color_pair_count].color_pair.fg_color = color[1];
                        init_pair(*pair_id,color[1],color[0]);
                        color_pair_count++;
                        return 0;

                case col_pair_dic_get:
                        for(int i = 0; i < color_pair_count;i++){
                                if(color_pair_dic[i].color_pair.bg_color == color[0] &&
                                        color_pair_dic[i].color_pair.fg_color == color[1]){
                                               *pair_id = color_pair_dic[i].color_pair_id;
                                               return 0;
                                }
                        }
                        *pair_id = -1;
                        break;
        }
        return -1;

}


//枠線の色からカラーペアのidを引く
//色が使えない端末や未登録の色のときは既定のペア(0)を返す
int outline_color_pair(struct color_pair color){
        if(!has_colors())return 0;

        int pair_id = 0;
        int color_set[2] = {color.bg_color,color.fg_color};
        if(color_pair_dic(&pair_id,color_set,col_pair_dic_get) != 0)return 0;
        return pair_id;
}


//画面全体(stdscr)の背景色をbkgd()で塗り替える
//以降そこへ書かれる文字はこのカラーペアを引き継ぐ
//bg_colorにCOLOR_DEFAULTを渡すと端末の背景をそのまま使う(透過が保たれる)
void set_background(int bg_color){
        if(!has_colors())return;
        //文字色は指定しないので端末の既定色に任せる
        int color[2] = {bg_color,COLOR_DEFAULT};
        int id = 0;
        if(color_pair_dic(&id,color,col_pair_dic_get) != 0)return;
        bkgd(COLOR_PAIR(id));
}



//各WINDOWに描いた内容を実画面へ反映する
//WINDOWはstdscrのサブウィンドウで文字バッファを共有しているので
//stdscrごと転送すればまとめて出る
int window_push_screen(){
        WINDOW *root_win = win_ctrl(NULL,win_get);
        if(root_win == NULL)return -1;

        touchwin(root_win);
        wrefresh(root_win);
        return 0;
}

//ウィンドウの中身(枠以外)を描く予定の場所
//まだ何も持たせていないので空
void render_window_obj(struct window_data *win_data){
        (void)win_data;
        return;
}


//ウィンドウ1枚分をWINDOWのバッファへ描き直す
//werase()で一度消してから枠を引くだけなので、実画面へ出るのは
//この後にwindow_push_screen()を呼んだとき
int render_window(struct window_data *win_data){
        WINDOW *win = get_window_handle(win_data);
        if(win == NULL)return -1;

        werase(win);
        check_msg_data(win_data);
        render_window_outline(win_data);
        render_line(win_data);
        render_msg_data(win_data);

        return 0;
}

//角の文字と、そこに重なっている親の辺から、つなぎ目のT字を決める
//親の辺が横線なら子の縦線がどちらへ伸びるかで上下のT字、
//親の辺が縦線なら子の横線の向きで左右のT字になる
//つなぐ相手がいない(joint_sideが-1)ときは角のまま返す
static chtype outline_joint_chr(chtype corner,int joint_side){
        if(joint_side < 0)return corner;

        if(joint_side == top || joint_side == bottom){
                //角から下へ伸びる=上側の角なので┬
                if(corner == ACS_ULCORNER || corner == ACS_URCORNER)return ACS_TTEE;
                //角から上へ伸びる=下側の角なので┴
                if(corner == ACS_LLCORNER || corner == ACS_LRCORNER)return ACS_BTEE;
                return corner;
        }

        //角から右へ伸びる=左側の角なので├
        if(corner == ACS_ULCORNER || corner == ACS_LLCORNER)return ACS_LTEE;
        //角から左へ伸びる=右側の角なので┤
        if(corner == ACS_URCORNER || corner == ACS_LRCORNER)return ACS_RTEE;
        return corner;
}

//box()が置いた角1つを見て、親の枠と重なっていればT字へ書き換える
//色はこの後のchgat()でまとめて乗るので、ここでは文字だけ差し替える
static void join_outline_corner(struct window_data *win_data,struct vec2 corner_pos,chtype corner){
        WINDOW *win = get_window_handle(win_data);
        if(win == NULL)return;

        chtype joint = outline_joint_chr(corner,
                check_outline_joint(win_data,corner_pos));
        if(joint == corner)return;

        mvwaddch(win,corner_pos.y,corner_pos.x,joint);
}

//枠線はWINDOW自身の外周1セルに描く
//box()でまとめて引いてから、辺ごとに色だけ塗り替える
//chgat()は属性を「置き換える」ので、box()が付けたA_ALTCHARSET
//(罫線文字であるという印)を必ず渡し直す
//これを落とすとlやqといった生の文字がそのまま表示される
void render_window_outline(struct window_data *win_data){
        WINDOW *win = get_window_handle(win_data);
        if(win == NULL)return;
        if(get_window_outline_size(win_data,top) <= 0)return;

        int h = 0;
        int w = 0;
        getmaxyx(win,h,w);
        //外周を描くには最低でも2x2必要
        if(h < 2 || w < 2)return;

        box(win,0,0);

        //親の枠と重なっている角をT字へつなぎ直す
        //色を乗せるchgat()より前に済ませておく
        struct vec2 ul = {0,0};
        struct vec2 ur = {w - 1,0};
        struct vec2 ll = {0,h - 1};
        struct vec2 lr = {w - 1,h - 1};
        join_outline_corner(win_data,ul,ACS_ULCORNER);
        join_outline_corner(win_data,ur,ACS_URCORNER);
        join_outline_corner(win_data,ll,ACS_LLCORNER);
        join_outline_corner(win_data,lr,ACS_LRCORNER);

        struct line_color line_color = {0};
        get_window_outline_color(win_data,&line_color);

        //角は上下の辺の色に合わせるので、先に左右を塗る
        for(int y = 1; y < h - 1;y++){
                mvwchgat(win,y,0,1,A_ALTCHARSET,
                        outline_color_pair(line_color.left),NULL);
                mvwchgat(win,y,w - 1,1,A_ALTCHARSET,
                        outline_color_pair(line_color.right),NULL);
        }
        mvwchgat(win,0,0,w,A_ALTCHARSET,
                outline_color_pair(line_color.top),NULL);
        mvwchgat(win,h - 1,0,w,A_ALTCHARSET,
                outline_color_pair(line_color.bottom),NULL);
}


void render_msg_data(struct window_data *win_data){
        if(win_data == NULL)return;
        struct chr_data_arry chr_arry = get_window_msg_data(win_data);
        WINDOW *win = get_window_handle(win_data);
        for(int i = 0 ; i< chr_arry.chr_data_count;i++){
                struct color_pair col_pair = 
                        get_window_msg_color_pair(win_data,i);
                int col_id = 0;
                int int_col_pair[2] = {col_pair.bg_color,col_pair.fg_color};
                struct chr_data *chr_data = chr_arry.chr_data[i];
                if(color_pair_dic(&col_id,int_col_pair,col_pair_dic_get) != 0)continue;
                wattrset(win,COLOR_PAIR(col_id) | chr_data->style);
                mvwaddwstr(win,chr_data->chr_st_pos.y,chr_data->chr_st_pos.x,chr_data->chr_data);
        }
        wattrset(win,A_NORMAL);
}


void check_msg_data(struct window_data *win_data){
        WINDOW *win = get_window_handle(win_data);
        if(win == NULL)return;

        int h = 0;
        int w = 0;
        getmaxyx(win,h,w);

        int min_x = get_window_outline_size(win_data,left) > 0 ? 1 : 0;
        int min_y = get_window_outline_size(win_data,top) > 0 ? 1 : 0;
        int max_x = w - (get_window_outline_size(win_data,right) > 0 ? 1 : 0) - 1;
        int max_y = h - (get_window_outline_size(win_data,bottom) > 0 ? 1 : 0) - 1;
        if(max_x < min_x || max_y < min_y)return;

        struct chr_data_arry msg_data = get_window_msg_data(win_data);
        for(int i = 0;i < msg_data.chr_data_count;i++){
                struct chr_data *msg = msg_data.chr_data[i];
                if(msg == NULL || msg->chr_data == NULL)continue;

                int content_w = max_x - min_x + 1;
                int msg_width = 0;
                for(size_t j = 0;msg->chr_data[j] != L'\0';j++){
                        int chr_width = wcwidth(msg->chr_data[j]);
                        if(chr_width < 0)chr_width = 1;
                        if(msg_width + chr_width > content_w){
                                msg->chr_data[j] = L'\0';
                                break;
                        }
                        msg_width += chr_width;
                }
                int max_start_x = max_x;
                if(msg_width > 0){
                        max_start_x = max_x - msg_width + 1;
                }

                if(msg->chr_st_pos.x < min_x)msg->chr_st_pos.x = min_x;
                else if(msg->chr_st_pos.x > max_start_x)msg->chr_st_pos.x = max_start_x;

                if(msg->chr_st_pos.y < min_y)msg->chr_st_pos.y = min_y;
                else if(msg->chr_st_pos.y > max_y)msg->chr_st_pos.y = max_y;
        }

}


void render_line(struct window_data *win_data){
        if(win_data == NULL)return;
        struct line_result line_data = get_line_data(win_data);
        if(line_data.line_data == NULL)return;
        struct vec2 win_size;
        get_window_size(win_data,&win_size);
        wchar_t line[win_size.x+1];
        line[0] = L'\u251C';
        wmemset(&line[1],L'\u2500',(win_size.x-2));
        line[win_size.x-1] = L'\u2524';
        line[win_size.x] = L'\0';
        WINDOW *win = get_window_handle(win_data);
        for(int i = 0;i < line_data.line_num;i++){
                if(line_data.line_data[i].line >= win_size.y || 
                        line_data.line_data[i].line < 0)continue;
                        mvwaddwstr(win,line_data.line_data[i].line,0,line);
        }
        free(line_data.line_data);
}
