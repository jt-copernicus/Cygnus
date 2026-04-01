/*
 * cygnus-calc
 * A simple calculator program for the Cygnus WM.
 *
 * by Jonathan Torres
 * 
 * This program is free software: you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation, either version 3 of 
 * the License, or (at your option) any later version.
 * 
 */

#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define COL_BG "#1a1a1a"
#define COL_FG "#ffffff"
#define COL_BTN "#222222"
#define COL_HL "#3366ff"

typedef struct {
    char label[8];
    int x, y, w, h;
    void (*action)();
} Button;

double current_val = 0;
double prev_val = 0;
double memory = 0;
char display_buf[64] = "0";
char op = 0;
int new_num = 1;
int has_dot = 0;

Display *dpy;
Window win;
int screen;
GC gc;
XFontStruct *font;
Atom wm_delete_window;

unsigned long get_color(const char *hex) {
    XColor c;
    Colormap cm = DefaultColormap(dpy, screen);
    XParseColor(dpy, cm, hex, &c);
    XAllocColor(dpy, cm, &c);
    return c.pixel;
}

void update_display() {
    if (fabs(current_val) >= 1e10 || (fabs(current_val) < 1e-7 && current_val != 0)) {
        snprintf(display_buf, sizeof(display_buf), "%.4e", current_val);
    } else {
        snprintf(display_buf, sizeof(display_buf), "%.10g", current_val);
    }
}

void do_calc() {
    switch (op) {
        case '+': current_val = prev_val + current_val; break;
        case '-': current_val = prev_val - current_val; break;
        case '*': current_val = prev_val * current_val; break;
        case '/': if (current_val != 0) current_val = prev_val / current_val; break;
    }
    op = 0;
    new_num = 1;
    update_display();
}

void btn_num(int n) {
    if (new_num) {
        current_val = n;
        new_num = 0;
        has_dot = 0;
    } else {
        current_val = current_val * 10 + n;
    }
    update_display();
}

void btn_dot() {
    if (new_num) { current_val = 0; new_num = 0; }
    has_dot = 1;
}

void btn_op(char o) {
    if (op) do_calc();
    prev_val = current_val;
    op = o;
    new_num = 1;
}

void btn_sqrt() { current_val = sqrt(current_val); new_num = 1; update_display(); }
void btn_perc() { current_val = current_val / 100.0; new_num = 1; update_display(); }
void btn_neg()  { current_val = -current_val; update_display(); }
void btn_ce()   { current_val = 0; prev_val = 0; op = 0; new_num = 1; update_display(); }
void btn_mplus(){ memory += current_val; new_num = 1; }
void btn_mr()   { current_val = memory; new_num = 1; update_display(); }
void btn_mminus(){ memory = 0; }

Button buttons[25];
int btn_count = 0;

void add_btn(const char *lab, int x, int y, int w, int h, void (*act)()) {
    strncpy(buttons[btn_count].label, lab, 7);
    buttons[btn_count].x = x;
    buttons[btn_count].y = y;
    buttons[btn_count].w = w;
    buttons[btn_count].h = h;
    buttons[btn_count].action = act;
    btn_count++;
}

void draw() {
    XClearWindow(dpy, win);
    XSetForeground(dpy, gc, get_color(COL_FG));

    XDrawRectangle(dpy, win, gc, 10, 10, 280, 40);
    int tw = XTextWidth(font, display_buf, strlen(display_buf));
    XDrawString(dpy, win, gc, 280 - tw, 35, display_buf, strlen(display_buf));

    for (int i = 0; i < btn_count; i++) {
        XSetForeground(dpy, gc, get_color(COL_BTN));
        XFillRectangle(dpy, win, gc, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h);
        XSetForeground(dpy, gc, get_color(COL_FG));
        XDrawRectangle(dpy, win, gc, buttons[i].x, buttons[i].y, buttons[i].w, buttons[i].h);
        
        int lw = XTextWidth(font, buttons[i].label, strlen(buttons[i].label));
        XDrawString(dpy, win, gc, buttons[i].x + (buttons[i].w - lw)/2, buttons[i].y + 25, 
                     buttons[i].label, strlen(buttons[i].label));
    }
}

int main() {
    dpy = XOpenDisplay(NULL);
    screen = DefaultScreen(dpy);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 300, 400, 1, 
                             get_color(COL_FG), get_color(COL_BG));
    XStoreName(dpy, win, "Cygnus Calculator");
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | KeyPressMask);
    XMapWindow(dpy, win);
    gc = XCreateGC(dpy, win, 0, NULL);
    font = XLoadQueryFont(dpy, "fixed");
    XSetFont(dpy, gc, font->fid);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);

	//layout
    int bw = 50, bh = 40, gap = 5;
    int x0 = 10, y0 = 70;
    
    add_btn("CE", x0, y0, bw, bh, btn_ce);
    add_btn("M+", x0+(bw+gap), y0, bw, bh, btn_mplus);
    add_btn("MR", x0+(bw+gap)*2, y0, bw, bh, btn_mr);
    add_btn("M-", x0+(bw+gap)*3, y0, bw, bh, btn_mminus);
    add_btn("sqrt", x0+(bw+gap)*4, y0, bw, bh, btn_sqrt);

    y0 += bh + gap;
    add_btn("7", x0, y0, bw, bh, NULL);
    add_btn("8", x0+(bw+gap), y0, bw, bh, NULL);
    add_btn("9", x0+(bw+gap)*2, y0, bw, bh, NULL);
    add_btn("/", x0+(bw+gap)*3, y0, bw, bh, NULL);
    add_btn("%", x0+(bw+gap)*4, y0, bw, bh, btn_perc);

    y0 += bh + gap;
    add_btn("4", x0, y0, bw, bh, NULL);
    add_btn("5", x0+(bw+gap), y0, bw, bh, NULL);
    add_btn("6", x0+(bw+gap)*2, y0, bw, bh, NULL);
    add_btn("*", x0+(bw+gap)*3, y0, bw, bh, NULL);
    add_btn("+/-", x0+(bw+gap)*4, y0, bw, bh, btn_neg);

    y0 += bh + gap;
    add_btn("1", x0, y0, bw, bh, NULL);
    add_btn("2", x0+(bw+gap), y0, bw, bh, NULL);
    add_btn("3", x0+(bw+gap)*2, y0, bw, bh, NULL);
    add_btn("-", x0+(bw+gap)*3, y0, bw, bh, NULL);
    add_btn("=", x0+(bw+gap)*4, y0, bw, bh*2+gap, do_calc);

    y0 += bh + gap;
    add_btn("0", x0, y0, bw*2+gap, bh, NULL);
    add_btn(".", x0+(bw+gap)*2, y0, bw, bh, btn_dot);
    add_btn("+", x0+(bw+gap)*3, y0, bw, bh, NULL);

    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) draw();
        else if (ev.type == ClientMessage) {
            if (ev.xclient.data.l[0] == wm_delete_window) break;
        } else if (ev.type == ButtonPress) {
            for (int i=0; i<btn_count; i++) {
                if (ev.xbutton.x >= buttons[i].x && ev.xbutton.x <= buttons[i].x + buttons[i].w &&
                    ev.xbutton.y >= buttons[i].y && ev.xbutton.y <= buttons[i].y + buttons[i].h) {
                    if (buttons[i].action) buttons[i].action();
                    else {
                        char c = buttons[i].label[0];
                        if (c >= '0' && c <= '9') btn_num(c - '0');
                        else if (c == '+' || c == '-' || c == '*' || c == '/') btn_op(c);
                    }
                    draw();
                    break;
                }
            }
        } else if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks >= XK_0 && ks <= XK_9) btn_num(ks - XK_0);
            else if (ks == XK_plus || ks == XK_KP_Add) btn_op('+');
            else if (ks == XK_minus || ks == XK_KP_Subtract) btn_op('-');
            else if (ks == XK_asterisk || ks == XK_KP_Multiply) btn_op('*');
            else if (ks == XK_slash || ks == XK_KP_Divide) btn_op('/');
            else if (ks == XK_percent) btn_perc();
            else if (ks == XK_s) btn_sqrt();
            else if (ks == XK_m) btn_mplus();
            else if (ks == XK_r) btn_mr();
            else if (ks == XK_n) {
                if (ev.xkey.state & ShiftMask) btn_neg();
                else btn_mminus();
            }
            else if (ks == XK_period || ks == XK_KP_Decimal) btn_dot();
            else if (ks == XK_equal || ks == XK_Return || ks == XK_KP_Enter) do_calc();
            else if (ks == XK_Escape) btn_ce();
            draw();
        }
    }
    return 0;
}
