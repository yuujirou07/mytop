#include <ncurses.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#include"core.h"
#include "cpu_info.h"
#include"window.h"
#include"process_info/process_info.h"
#include"mytop_render.h"

static void make_cpu_window();
static void make_memory_window();
void end_process();
static void make_process_window();

// mytopを初期化して各ウィンドウを生成し、キー入力と1秒周期の再描画を処理する。
// 引数: argv[1]が"mono"ならモノクロテーマを使用する。戻り値: 正常終了時は0。
int main(int argc,char **argv){
        if(argc > 1 && strcmp(argv[1],"mono") == 0){
                theme_set(mono);
        }

        init_render();
        make_cpu_window();
        make_memory_window();
        make_process_window();
        //端末の背景をそのまま使う
        //COLOR_BLACKを指定すると、背景を透過させている端末で黒く塗り潰される
        set_background(COLOR_DEFAULT);
        timeout(100);
        window_push_screen();
        struct timespec last_update;
        clock_gettime(CLOCK_MONOTONIC,&last_update);
        while(1){
                int ch = getch();
                if(ch == 'q'){
                        break;
                }
                else if(ch == KEY_RESIZE){
                        
                }
                else if(ch == KEY_UP || ch == 'k' ||
                        ch == KEY_DOWN || ch == 'j'){
                        struct window_data **win_list = NULL;
                        int win_num = 0;
                        get_window_list(&win_list,&win_num);

                        for(int i = 0;i < win_num;i++){
                                enum device *dev = get_window_draw_dev(win_list[i]);
                                if(dev == NULL || *dev != process)continue;

                                int scroll_amount =
                                        ch == KEY_UP || ch == 'k' ? -1 : 1;
                                scroll_process(win_list[i],scroll_amount);
                                clear_window_chr_data(win_list[i]);
                                set_device_data(win_list[i],*dev);
                                render_window(win_list[i]);
                        }
                        window_push_screen();
                }

                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC,&now);
                double elapsed =
                now.tv_sec - last_update.tv_sec +
                (now.tv_nsec - last_update.tv_nsec) / 1000000000.0;

                if(elapsed >= 1.0){
                        last_update = now;
                        struct window_data **win_list = NULL;
                        int win_num = 0;
                        get_window_list(&win_list,&win_num);

                        for(int i = 0; i < win_num;i++){
                                enum device *dev = get_window_draw_dev(win_list[i]);
                                if(dev == NULL)continue;
                                clear_window_chr_data(win_list[i]);
                                set_device_data(win_list[i],*dev);
                                render_window(win_list[i]);
                        }
                        window_push_screen();
                }

        }

        end_process();
        return 0;
}

// CPU親ウィンドウとコア情報用の子ウィンドウを、画面の空き領域へ生成して初期描画する。
// 引数・戻り値: なし。生成できない場合はend_process()後に終了する。
static void make_cpu_window(){
        int x;
        int y;
        getmaxyx(stdscr,y,x);

        struct box empty_space = get_window_empty_space(NULL);
        if(empty_space.size.x < 1 || empty_space.size.y < 1){
                end_process();
                exit(1);
        }

        int cpu_win_width = x;
        int cpu_win_height = y/3;
        if(cpu_win_width > empty_space.size.x)cpu_win_width = empty_space.size.x;
        if(cpu_win_height < 1)cpu_win_height = 1;
        if(cpu_win_height > empty_space.size.y)cpu_win_height = empty_space.size.y;

        struct window_data *cpu_info_win = create_window(NULL);
        if(cpu_info_win == NULL){
                end_process();
                exit(1);
        }
        show_window_outline(cpu_info_win,true);
        

        for(int i = 0;i < 4;i++){
                set_window_outline_color(cpu_info_win,COLOR_WHITE,i);
        }

        set_window_size(cpu_info_win,cpu_win_width,cpu_win_height);
        set_window_pos(cpu_info_win,empty_space.pos.x,empty_space.pos.y);

        struct vec2 parent_size = {0,0};
        get_window_size(cpu_info_win,&parent_size);
        struct box cpu_data_empty_space = get_window_empty_space(cpu_info_win);
        if(cpu_data_empty_space.size.x < 1 || cpu_data_empty_space.size.y < 1){
                end_process();
                exit(1);
        }

        struct window_data *cpu_data_win = create_window(&cpu_info_win);
        if(cpu_data_win == NULL){
                end_process();
                exit(1);
        }
        show_window_outline(cpu_data_win,true);

        for(int i = 0;i < 4;i++){
                set_window_outline_color(cpu_data_win,COLOR_WHITE,i);
        }

        int size_w = parent_size.x/2.4;
        if(size_w < 1)size_w = 1;
        if(size_w > cpu_data_empty_space.size.x)size_w = cpu_data_empty_space.size.x;
        int size_h = parent_size.y;
        if(size_h > cpu_data_empty_space.size.y)size_h = cpu_data_empty_space.size.y;
        set_window_size(cpu_data_win,size_w,size_h);
        set_window_pos(cpu_data_win,
                cpu_data_empty_space.pos.x + cpu_data_empty_space.size.x - size_w,
                cpu_data_empty_space.pos.y);
        set_device_data(cpu_data_win,cpu_data);
        set_device_data(cpu_info_win,cpu_info);
        render_window(cpu_info_win);
        render_window(cpu_data_win);
}

// メモリ情報ウィンドウを画面の空き領域へ生成し、取得したメモリ情報を初期描画する。
// 引数・戻り値: なし。生成できない場合はend_process()後に終了する。
static void make_memory_window(){
        int x;
        int y;
        getmaxyx(stdscr,y,x);

        struct box empty_space = get_window_empty_space(NULL);
        if(empty_space.size.x < 1 || empty_space.size.y < 1){
                end_process();
                exit(1);
        }

        int memory_win_width = x;
        int memory_win_height = y/3;
        if(memory_win_width > empty_space.size.x)memory_win_width = empty_space.size.x;
        if(memory_win_height < 1)memory_win_height = 1;
        if(memory_win_height > empty_space.size.y)memory_win_height = empty_space.size.y;

        struct window_data *memory_window = create_window(NULL);
        if(memory_window == NULL){
                end_process();
                exit(1);
        }
        show_window_outline(memory_window,true);
        for(int i = 0;i < 4;i++){
                set_window_outline_color(memory_window,COLOR_WHITE,i);
        }

        set_window_size(memory_window,memory_win_width/2,memory_win_height);
        set_window_pos(memory_window,empty_space.pos.x,empty_space.pos.y);
        set_device_data(memory_window,memory);
        render_window(memory_window);
}
// 登録済みウィンドウとCPU使用率履歴を解放し、ncursesを終了する。
// 引数・戻り値: なし。呼び出し後は既存window_dataを使用できない。
void end_process(){
        struct window_data **winlist = NULL;
        int winlist_num = 0;
        get_window_list(&winlist,&winlist_num);

        for(int i = winlist_num - 1;i >= 0;i--){
                free_window(&winlist[i]);
        }
        free_cpu_usage_data();
        end_render();
}

static void make_process_window(){
        int x;
        int y;
        getmaxyx(stdscr,y,x);

        struct box empty_space = get_window_empty_space(NULL);
        if(empty_space.size.x < 1 || empty_space.size.y < 1){
                end_process();
                exit(1);
        }

        struct window_data *proc_win = create_window(NULL);

        show_window_outline(proc_win,true);

        for(int i = 0;i < 4;i++){
                set_window_outline_color(proc_win,COLOR_WHITE,i);
        }

        set_window_size(proc_win,empty_space.size.x/2,(y*2)/3);
        set_window_pos(proc_win,empty_space.size.x/2,y/3);
        set_device_data(proc_win,process);
        render_window(proc_win);




}
