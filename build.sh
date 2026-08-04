#!/bin/sh
# ビルドを実行した端末向けに実行ファイル mytop を作る
#
# 依存: ncursesw (Arch: pacman -S ncurses / Debian: apt install libncursesw5-dev)
#
# 環境変数:
#   CC       コンパイラ            (既定: gcc)
#   CFLAGS   追加のコンパイルオプション (既定: -O2 -Wall -Wextra)
set -e

CC="${CC:-gcc}"
CFLAGS="${CFLAGS:--O2 -Wall -Wextra}"

case "$(uname -s)" in
    Linux)
        # ncursesw のリンク指定は pkg-config があればそちらを優先する
        # (Arch などでは実体が libncursesw になっているため)
        if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists ncursesw; then
            NCURSES_CFLAGS="$(pkg-config --cflags ncursesw)"
            NCURSES_LIBS="$(pkg-config --libs ncursesw)"
        else
            NCURSES_CFLAGS=""
            NCURSES_LIBS="-lncursesw"
        fi

        # shellcheck disable=SC2086
        "$CC" $CFLAGS \
                -g \
                -fsanitize=address -fno-omit-frame-pointer \
                src/main.c \
                src/mytop_theme/theme.c \
                src/mytop_render/mytop_render.c \
                src/mytio_window/window.c \
                src/process_info/process_info.c \
                src/process_info/cpu/cpu_info.c \
                src/process_info/memory/memory.c \
                -o mytop \
                -Isrc/ \
                -Isrc/mytop_theme/ \
                -Isrc/mytop_render/ \
                -Isrc/process_info/cpu \
                -Isrc/mytio_window/ \
                -Isrc/process_info/ \
                -Isrc/process_info/memory \
                $NCURSES_CFLAGS $NCURSES_LIBS
        echo "ビルド完了: mytop"
        ;;
    *)
        echo "未対応のOSです: $(uname -s)" >&2
        echo "mytop は procfs と ncurses に依存するため Linux 専用です" >&2
        exit 1
        ;;
esac
