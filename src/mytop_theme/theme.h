#ifndef THEME_H
#define THEME_H
enum theme{
        dark,
        ligth,
        mono,
};

enum font_type{
        cpu_bar_graph,
};
int get_font(enum font_type);
#endif