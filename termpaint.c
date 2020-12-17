/*
 * termpaint is MS Paint in the terminal: draw with your mouse.
 * Copyright (C) 2020 George Zakhour
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of  MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>

#define CSI "\e["

#define STATE_READ          0
#define STATE_CHOOSE_BG     1
#define STATE_CHOOSE_FG     2
#define STATE_SAVE          3
#define STATE_CLEAR         4
#define STATE_QUIT          5
#define STATE_INSERT        6
#define STATE_ERASE         7

#define ON_ESC(state) if (c == 27) return state;
#define SHIFT_GET(value, shift, get) ((value >> (shift)) & ((1 << (get)) - 1))


/****************************** GLOBAL VARIABLES ******************************/
int state = STATE_READ;

int mouse_col = 1;
int mouse_row = 1;

int color_bg = 4;
int color_fg = 1;
int underline = 0;

int bright_bg = 0;
int bright_fg = 0;

int canvas[224][224];

char filename[1024];
int filename_length = 0;


/******************************* UTIL FUNCTIONS *******************************/
void serialize(FILE* fp) {
    if (fp == NULL) return;
    fprintf(fp, CSI "2J");
    for (int r=1; r<224; r++) for (int c=1; c<224; c++) {
        int value = canvas[r][c];
        if (value == 0) continue;
        int ul = SHIFT_GET(value, 14, 1),
            fg = SHIFT_GET(value, 11, 3), fg_bright = SHIFT_GET(value, 16, 1),
            bg = SHIFT_GET(value,  8, 3), bg_bright = SHIFT_GET(value, 15, 1);
        fprintf(fp, CSI "%d;%dH", r, c);
        fprintf(fp, CSI "%dm" CSI "%d%sm",
                40+bg+60*bg_bright, 30+fg, fg_bright?";1":"");
        if (ul) fprintf(fp, CSI "4m");
        fprintf(fp, "%c" CSI "0m", value & 0b11111111);
    }
}

int stdin_has_data() {
    struct timeval tv;
    tv.tv_sec = 0; tv.tv_usec = 100000; // tv=0.1s
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

int encode(char c, int bg, int x, int fg, int y, int underline) {
    // yxufffbbbcccccccc
    return c + (bg << 8) + (fg << 11) + (underline << 14) + (x << 15) + (y << 16);
}


/****************************** TERMINAL CONTROL ******************************/
void cursor_goto(int row, int col) { printf(CSI "%d;%dH", row, col); }

void cursor_set_visible(int visible) { printf(CSI "?25%c", visible == 0 ? 'l' : 'h'); }

void cursor_save() { printf(CSI "s"); }

void cursor_reset() { printf(CSI "u"); }

void style_reset() { printf(CSI "0m"); }

void style_invert() { printf(CSI "7m"); }

void style_underline() { printf(CSI "4m"); }

void style_bg(int color, int bright) {
    printf(CSI "%dm", 40 + bright*60 + color);
}

void style_fg(int color, int bright) {
    if (bright) printf(CSI "%d;1m", 30 + color);
    else printf(CSI "%dm", 30 + color);
}

void line_clear() { printf(CSI "2K"); }

void line_clear_till_end() { printf(CSI "0K"); }

void screen_clear() { printf(CSI "2J"); }

void screen_clear_till_end() { printf(CSI "0J"); }


/************************ COMPONENT DRAWING FUNCTIONS ************************/
void draw_palette(int bright) {
    for (int i=0; i<8; i++) {
        style_bg(i, bright);
        style_fg(7-i, bright);
        printf(" %d ", i);
    }
    style_reset();
}

void draw_inverted(char* label) {
    style_invert();
    printf("%s", label);
    style_reset();
}

void draw_btn(char* label, int active) {
    printf(CSI "%dm " CSI "4m%s" CSI "0m" CSI "%dm " CSI "0m", active*7, label, active*7);
}

void draw_str(char* str, int bg, int bg_bright, int fg, int fg_bright, int underline) {
    style_bg(bg, bg_bright);
    style_fg(fg, fg_bright);
    if (underline) style_underline();
    printf("%s", str);
    style_reset();
}

void draw_menu() {
    cursor_goto(1, 1);
    line_clear();

    if (state == STATE_CHOOSE_BG || state == STATE_CHOOSE_FG) {
        int is_back = state == STATE_CHOOSE_BG;
        printf("Choose color for %sground: ", is_back ? "back" : "for");
        draw_palette(is_back ? bright_bg : bright_fg);
        goto end_draw_menu;
    }

    if (state == STATE_INSERT)
        printf("-insert-   ");
    else if (state == STATE_ERASE)
        printf("-eraser-   ");

    draw_str(" sample ", color_bg, bright_bg, color_fg, bright_fg, underline);
    printf("   ");

    draw_str(" b ", color_bg, bright_bg, 7-color_bg, bright_bg, 0);
    draw_str(" f ", color_fg, bright_fg, 7-color_fg, bright_fg, 0);
    printf("   ");

    draw_btn("u", underline);
    draw_btn("b", bright_bg);
    draw_btn("f", bright_fg);

    if (state == STATE_QUIT || state == STATE_CLEAR) {
        printf("   ");
        draw_inverted(" Are you sure? [y/N] ");
    } else if (state == STATE_SAVE) {
        printf("   ");
        draw_inverted("file: ");
        draw_inverted(filename);
    }

    end_draw_menu:
    cursor_save();
    printf("   %d,%d", mouse_row, mouse_col);
}


/************************** HANDLERS FOR EVERY STATE **************************/
int handle_quit(int c) {
    if (c == 'y' || c == 'Y') return -1;
    return STATE_READ;
}

int handle_clear(int c) {
    if (c == 'y' || c == 'Y') {
        cursor_goto(2, 1);
        screen_clear_till_end();
    }
    return STATE_READ;
}

int handle_save(char c) {
    int state = STATE_SAVE;
    if (c == 127) {
        if (filename_length > 0) filename_length--;
        filename[filename_length] = 0;
    } else if (c == 10) {
        FILE* file = fopen(filename, "w");
        serialize(file);
        if (file != NULL) fclose(file);
        return STATE_READ;
    } else if (c >= ' ') {
        if (filename_length < 1024)
            filename[filename_length++] = c;
    }
    ON_ESC(STATE_READ);
    draw_menu();
    return STATE_SAVE;
}

int handle_choose_bg(int c) {
    if (c >= '0' && c <= '7') {
        color_bg = c - '0';
        return STATE_READ;
    }
    if (c == 'b') {
        bright_bg = 1 - bright_bg;
        draw_menu();
    }
    ON_ESC(STATE_READ);
    return STATE_CHOOSE_BG;
}

int handle_choose_fg(int c) {
    if (c >= '0' && c <= '7') {
        color_fg = c - '0';
        return STATE_READ;
    }
    if (c == 'b') {
        bright_fg = 1 - bright_fg;
        draw_menu();
    }
    ON_ESC(STATE_READ);
    return STATE_CHOOSE_FG;
}

int handle_underline(int c) {
    underline = 1 - underline;
    draw_menu();
    return STATE_READ;
}

int handle_insert(int c) {
    ON_ESC(STATE_READ);
    if (c < ' ') return STATE_INSERT;
    if (c == 127) {
        if (mouse_col > 1) mouse_col--;
        canvas[mouse_row][mouse_col] = 0;
        cursor_goto(mouse_row, mouse_col);
        style_reset();
        printf(" ");
    } else if (c < 127) {
        cursor_goto(mouse_row, mouse_col);
        char str[2] = { (char)c, '\0' };
        draw_str(str, color_bg, bright_bg, color_fg, bright_fg, underline);
        canvas[mouse_row][mouse_col] = encode((char)c, color_bg,
                bright_bg, color_fg, bright_fg, underline);
        mouse_col++;
    }
    return STATE_INSERT;
}

int handle_erase(char c) {
    ON_ESC(STATE_READ);
    return STATE_ERASE;
}

int handle_read(char c) {
    switch (c) {
    case 'c': return STATE_CLEAR;
    case 'q': return STATE_QUIT;
    case 's':
        for (int i=0; i<filename_length; i++) filename[i] = 0;
        filename_length = 0;
        return STATE_SAVE;
    case 'b': return STATE_CHOOSE_BG;
    case 'f': return STATE_CHOOSE_FG;
    case 'u': return handle_underline(c);
    case 'i': return STATE_INSERT;
    case 'e': return STATE_ERASE;
    default: return STATE_READ;
    }
}


/******************************* MOUSE HANDLER *******************************/
void handle_mouse(int prev_state) {
    if (getchar() != 77)
        return;
    int mouse_action = getchar();
    mouse_col = getchar() - 32;
    mouse_row = getchar() - 32;
    int drawable = mouse_action == 32 || mouse_action == 64;
    fflush(stdin);
    cursor_reset();
    line_clear_till_end();
    printf("   %d,%d", mouse_row, mouse_col);
    if (drawable && prev_state == state) {
        if (state == STATE_READ) {
            handle_insert(' ');
        } else if (state == STATE_ERASE) {
            canvas[mouse_row][mouse_col] = 0;
            cursor_goto(mouse_row, mouse_col);
            style_reset();
            printf(" ");
        }
    }
}


/************************* STATE TRANSITION FUNCTION *************************/
int next(int c) {
    static int prev_state;
    static int got_esc = 0;
    if (c == 27) {
        prev_state = state;
        got_esc = 1;
        if (stdin_has_data()) c = getchar();
    }
    if (c == 91 && got_esc) {
        got_esc = 0;
        handle_mouse(prev_state);
        return prev_state;
    }
    got_esc = c == 27;

    switch (state) {
    case STATE_READ: return handle_read(c);
    case STATE_QUIT: return handle_quit(c);
    case STATE_CLEAR: return handle_clear(c);
    case STATE_SAVE: return handle_save(c);
    case STATE_CHOOSE_BG: return handle_choose_bg(c);
    case STATE_CHOOSE_FG: return handle_choose_fg(c);
    case STATE_INSERT: return handle_insert(c);
    case STATE_ERASE: return handle_erase(c);
    }
}


/**************************** REST OF THE PROGRAM ****************************/
void signal_iterrupt(int sig) {
    printf("\e[?1003l");
    fflush(stdin);
    state = STATE_READ;
    printf("\e[?1003h");
    fflush(stdout);
}

void main() {
    int old_echo, old_icanon;
    struct termios term;

    tcgetattr(0, &term);
    old_echo = term.c_lflag & ECHO;
    old_icanon = term.c_lflag & ICANON;
    term.c_lflag &= ~ECHO & ~ICANON;
    tcsetattr(0, TCSANOW, &term);

    screen_clear();
    cursor_set_visible(0);
    cursor_goto(1, 1);

    printf("\e[?1003h");
    fflush(stdout);

    /* Here's what's happening:
     * on a really large display the col/row can be < 33 (when overflown) which
     *    a) are interpretted as signals, or
     *    b) getchar/read tries to read an overflown char
     * anyways, all kinds of signals are sent to the program.
     *
     * Using signal() will not quit on ^C, and it's not really possible to know
     * whether the ^C signal came from the keyboard or from the bowels of read()
     *
     * Using sigaction will terminate the program if the user typed ^C, but will
     * not when the signal is received from the internals of read()
     *
     * Those signals are ignored cause they cause the program to be killed:
     *  SIGWINCH(=28)
     *
     * I do not really trust this, but it's the only thing that worked
    */
    for (int sig=1; sig<=30; sig++) {
        if (sig == 28) continue; // ignore SIGWINCH
        struct sigaction new_action;
        new_action.sa_handler = signal_iterrupt;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(sig, &new_action, NULL);
    }

    draw_menu();

    int prev_state = -1;
    while ((state = next(getchar())) != -1) {
        if (prev_state != state) draw_menu();
        fflush(stdout);
        prev_state = state;
    }

    printf("\e[?1003l");
    cursor_set_visible(1);
    cursor_goto(1, 1);
    screen_clear();
    fflush(stdout);
    fflush(stdin);

    tcgetattr(0, &term);
    term.c_lflag |= old_echo | old_icanon;
    tcsetattr(0, TCSANOW, &term);
}
