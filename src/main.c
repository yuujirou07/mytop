#include <ncurses.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"core.h"
#include "cpu_info.h"
#include"window.h"
#include"process_info/process_info.h"
#include"mytop_render.h"



int main(int argc,char **argv){
        if(argc > 1 && strcmp(argv[1],"mono") == 0){
                theme_set(mono);
        }

        init_render();
        struct window_data *cpu_info_win = create_window(NULL);
        if(cpu_info_win == NULL){
                end_render();
                return 1;
        }
        show_window_outline(cpu_info_win,true);
        

        for(int i = 0;i < 4;i++){
                set_window_outline_color(cpu_info_win,COLOR_WHITE,i);
        }

        int x;
        int y;
        getmaxyx(stdscr,y,x);
        set_window_size(cpu_info_win,x,y/3);
        set_window_pos(cpu_info_win,0,0);

        struct window_data *cpu_data_win = create_window(&cpu_info_win);
        if(cpu_data_win == NULL){
                end_render();
                return 1;
        }
        show_window_outline(cpu_data_win,true);

        for(int i = 0;i < 4;i++){
                set_window_outline_color(cpu_data_win,COLOR_WHITE,i);
        }

        //親の枠線の内側に収める
        //はみ出す値を渡しても親の中へ丸められる
        struct vec2 parent_size = {0,0};
        struct vec2 parent_pos = {0,0};
        get_window_size(cpu_info_win,&parent_size);
        int size_w = parent_size.x/2.4;
        set_window_size(cpu_data_win,size_w,parent_size.y);
        get_window_pos(cpu_data_win,&parent_pos);
        set_window_pos(cpu_data_win,parent_size.x - size_w,0);  
        set_device_data(cpu_data_win,cpu);
        

        set_background(COLOR_BLACK);
        render_window(cpu_info_win);
        render_window(cpu_data_win);
        window_push_screen();
        while(1){
                int ch = getch();
                if(ch == 'q'){
                        break;
                }
                else if(ch == KEY_RESIZE){
                        
                }
                addch(ch);
        }

        end_render();
        return 0;
}

