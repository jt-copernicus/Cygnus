/*
 * cygnus-open
 * File picker for Cygnus WM.
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
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <strings.h>

#define COL_BG "#1a1a1a"
#define COL_FG "#ffffff"
#define COL_BTN "#222222"
#define COL_HL "#3366ff"
#define COL_BORDER "#444444"

#define WIN_WIDTH 400
#define WIN_HEIGHT 500
#define ROW_HEIGHT 25
#define BTN_WIDTH 100
#define BTN_HEIGHT 35
#define PADDING 10

typedef struct FileEntry {
    char name[256];
    int is_dir;
    struct FileEntry *next;
} FileEntry;

typedef enum { MODE_VIEW, MODE_MEDIA, MODE_ALL } OpenMode;

Display *dpy;
Window win;
int screen;
GC gc;
XFontStruct *font;
Atom wm_delete_window;

char current_path[4096];
FileEntry *file_list = NULL;
int file_count = 0;
int selected_index = -1;
int scroll_offset = 0;
OpenMode open_mode = MODE_ALL;

int show_warning = 0;
char warning_msg[256] = "";

void load_dir(const char *path);
void draw_ui();
void cleanup_list();
unsigned long get_color(const char *hex);
int is_image_file(const char *name);
int is_media_file(const char *name);
int is_compatible(const char *name);

unsigned long get_color(const char *hex) {
    XColor c;
    Colormap cm = DefaultColormap(dpy, screen);
    XParseColor(dpy, cm, hex, &c);
    XAllocColor(dpy, cm, &c);
    return c.pixel;
}

int is_image_file(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".jpg") == 0 ||
            strcasecmp(ext, ".jpeg") == 0 || strcasecmp(ext, ".bmp") == 0 ||
            strcasecmp(ext, ".gif") == 0 || strcasecmp(ext, ".ppm") == 0);
}

int is_media_file(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".mkv") == 0 ||
            strcasecmp(ext, ".avi") == 0 || strcasecmp(ext, ".mov") == 0 ||
            strcasecmp(ext, ".mp3") == 0 || strcasecmp(ext, ".wav") == 0 ||
            strcasecmp(ext, ".ogg") == 0 || strcasecmp(ext, ".flac") == 0 ||
            strcasecmp(ext, ".webm") == 0 || strcasecmp(ext, ".m4a") == 0);
}

int is_compatible(const char *name) {
    if (open_mode == MODE_VIEW) return is_image_file(name);
    if (open_mode == MODE_MEDIA) return is_media_file(name);
    return 1;
}

void cleanup_list() {
    FileEntry *curr = file_list;
    while (curr) {
        FileEntry *next = curr->next;
        free(curr);
        curr = next;
    }
    file_list = NULL;
    file_count = 0;
}

int compare_entries(const void *a, const void *b) {
    FileEntry *ea = *(FileEntry **)a;
    FileEntry *eb = *(FileEntry **)b;
    if (ea->is_dir && !eb->is_dir) return -1;
    if (!ea->is_dir && eb->is_dir) return 1;
    return strcasecmp(ea->name, eb->name);
}

void load_dir(const char *path) {
    cleanup_list();
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *de;
    FileEntry **entries = NULL;
    int count = 0;
    int capacity = 100;
    entries = malloc(sizeof(FileEntry *) * capacity);

    while ((de = readdir(dir))) {
        if (strcmp(de->d_name, ".") == 0) continue;
        
        struct stat st;
        char full[4400];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        if (stat(full, &st) != 0) continue;

        int is_dir = S_ISDIR(st.st_mode);
        
        FileEntry *fe = malloc(sizeof(FileEntry));
        strncpy(fe->name, de->d_name, 255);
        fe->name[255] = '\0';
        fe->is_dir = is_dir;
        fe->next = NULL;

        if (count >= capacity) {
            capacity *= 2;
            entries = realloc(entries, sizeof(FileEntry *) * capacity);
        }
        entries[count++] = fe;
    }
    closedir(dir);

    if (count > 0) {
        qsort(entries, count, sizeof(FileEntry *), compare_entries);
        file_list = entries[0];
        for (int i = 0; i < count - 1; i++) {
            entries[i]->next = entries[i+1];
        }
        entries[count-1]->next = NULL;
        file_count = count;
    }
    free(entries);
    selected_index = (file_count > 0) ? 0 : -1;
    scroll_offset = 0;
}

void draw_ui() {
    XClearWindow(dpy, win);
    XSetForeground(dpy, gc, get_color(COL_FG));
    XDrawString(dpy, win, gc, PADDING, 20, current_path, strlen(current_path));
    XDrawLine(dpy, win, gc, 0, 30, WIN_WIDTH, 30);
    int list_y = 35;
    int visible_rows = (WIN_HEIGHT - 35 - BTN_HEIGHT - 30) / ROW_HEIGHT;
    
    FileEntry *curr = file_list;
    for (int i = 0; i < scroll_offset && curr; i++) curr = curr->next;

    for (int i = 0; i < visible_rows && curr; i++) {
        int idx = scroll_offset + i;
        int y = list_y + i * ROW_HEIGHT;

        if (idx == selected_index) {
            XSetForeground(dpy, gc, get_color(COL_HL));
            XFillRectangle(dpy, win, gc, 0, y, WIN_WIDTH, ROW_HEIGHT);
            XSetForeground(dpy, gc, get_color(COL_FG));
        } else {
            XSetForeground(dpy, gc, get_color(COL_FG));
        }

        char label[300];
        if (curr->is_dir) snprintf(label, sizeof(label), "[ %s ]", curr->name);
        else snprintf(label, sizeof(label), "  %s", curr->name);

        XDrawString(dpy, win, gc, PADDING, y + 18, label, strlen(label));
        curr = curr->next;
    }

    //scrollbar
    if (file_count > visible_rows) {
        int sb_h = (WIN_HEIGHT - 35 - BTN_HEIGHT - 30);
        int bar_h = (visible_rows * sb_h) / file_count;
        if (bar_h < 10) bar_h = 10;
        int bar_y = 35 + (scroll_offset * (sb_h - bar_h)) / (file_count - visible_rows);
        XSetForeground(dpy, gc, get_color(COL_HL));
        XFillRectangle(dpy, win, gc, WIN_WIDTH - 5, bar_y, 5, bar_h);
    }

    int btn_y = WIN_HEIGHT - BTN_HEIGHT - PADDING;
    
    XSetForeground(dpy, gc, get_color(COL_BTN));
    XFillRectangle(dpy, win, gc, PADDING, btn_y, BTN_WIDTH, BTN_HEIGHT);
    XSetForeground(dpy, gc, get_color(COL_FG));
    XDrawRectangle(dpy, win, gc, PADDING, btn_y, BTN_WIDTH, BTN_HEIGHT);
    int tw = XTextWidth(font, "Cancel (c)", 10);
    XDrawString(dpy, win, gc, PADDING + (BTN_WIDTH - tw)/2, btn_y + 22, "Cancel (c)", 10);
    XSetForeground(dpy, gc, get_color(COL_BTN));
    XFillRectangle(dpy, win, gc, WIN_WIDTH - BTN_WIDTH - PADDING, btn_y, BTN_WIDTH, BTN_HEIGHT);
    XSetForeground(dpy, gc, get_color(COL_FG));
    XDrawRectangle(dpy, win, gc, WIN_WIDTH - BTN_WIDTH - PADDING, btn_y, BTN_WIDTH, BTN_HEIGHT);
    tw = XTextWidth(font, "Open (o)", 8);
    XDrawString(dpy, win, gc, WIN_WIDTH - BTN_WIDTH - PADDING + (BTN_WIDTH - tw)/2, btn_y + 22, "Open (o)", 8);


    if (show_warning) {
        int ww = 300, wh = 100;
        int wx = (WIN_WIDTH - ww) / 2;
        int wy = (WIN_HEIGHT - wh) / 2;
        
        XSetForeground(dpy, gc, get_color(COL_BG));
        XFillRectangle(dpy, win, gc, wx, wy, ww, wh);
        XSetForeground(dpy, gc, get_color(COL_FG));
        XDrawRectangle(dpy, win, gc, wx, wy, ww, wh);
        
        tw = XTextWidth(font, warning_msg, strlen(warning_msg));
        XDrawString(dpy, win, gc, wx + (ww - tw)/2, wy + 40, warning_msg, strlen(warning_msg));
        

        int ok_w = 60, ok_h = 25;
        int ok_x = wx + (ww - ok_w)/2;
        int ok_y = wy + 60;
        XSetForeground(dpy, gc, get_color(COL_BTN));
        XFillRectangle(dpy, win, gc, ok_x, ok_y, ok_w, ok_h);
        XSetForeground(dpy, gc, get_color(COL_FG));
        XDrawRectangle(dpy, win, gc, ok_x, ok_y, ok_w, ok_h);
        tw = XTextWidth(font, "OK", 2);
        XDrawString(dpy, win, gc, ok_x + (ok_w - tw)/2, ok_y + 17, "OK", 2);
    }
}

void navigate(const char *name) {
    if (strcmp(name, "..") == 0) {
        char *ls = strrchr(current_path, '/');
        if (ls && ls != current_path) {
            *ls = '\0';
        } else if (ls == current_path) {
            strcpy(current_path, "/");
        }
    } else {
        if (strcmp(current_path, "/") != 0) strcat(current_path, "/");
        strcat(current_path, name);
    }
    load_dir(current_path);
}

void handle_select() {
    if (selected_index < 0) return;
    FileEntry *curr = file_list;
    for (int i = 0; i < selected_index && curr; i++) curr = curr->next;
    if (!curr) return;

    if (curr->is_dir) {
        navigate(curr->name);
    } else {
        if (is_compatible(curr->name)) {
            printf("%s/%s\n", current_path, curr->name);
            exit(0);
        } else {
            show_warning = 1;
            snprintf(warning_msg, sizeof(warning_msg), "File not compatible");
        }
    }
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "-view") == 0) open_mode = MODE_VIEW;
        else if (strcmp(argv[1], "-media") == 0) open_mode = MODE_MEDIA;
    }

    if (!getcwd(current_path, sizeof(current_path))) {
        strcpy(current_path, ".");
    }

    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    screen = DefaultScreen(dpy);
    
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, WIN_WIDTH, WIN_HEIGHT, 1, 
                             get_color(COL_BORDER), get_color(COL_BG));
    XStoreName(dpy, win, "Open File");
    
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask);
    XMapWindow(dpy, win);
    
    gc = XCreateGC(dpy, win, 0, NULL);
    font = XLoadQueryFont(dpy, "fixed");
    XSetFont(dpy, gc, font->fid);
    
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);

    load_dir(current_path);

    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) draw_ui();
        else if (ev.type == ClientMessage) {
            if (ev.xclient.data.l[0] == wm_delete_window) break;
        } else if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (show_warning) {
                if (ks == XK_Return || ks == XK_Escape || ks == XK_space) show_warning = 0;
            } else {
                if (ks == XK_Escape || ks == XK_c) exit(1);
                else if (ks == XK_o || ks == XK_Return) handle_select();
                else if (ks == XK_Up) {
                    if (selected_index > 0) {
                        selected_index--;
                        if (selected_index < scroll_offset) scroll_offset = selected_index;
                    }
                } else if (ks == XK_Down) {
                    if (selected_index < file_count - 1) {
                        selected_index++;
                        int visible_rows = (WIN_HEIGHT - 35 - BTN_HEIGHT - 30) / ROW_HEIGHT;
                        if (selected_index >= scroll_offset + visible_rows) scroll_offset = selected_index - visible_rows + 1;
                    }
                } else if (ks == XK_BackSpace) {
                    navigate("..");
                }
            }
            draw_ui();
        } else if (ev.type == ButtonPress) {
            int mx = ev.xbutton.x;
            int my = ev.xbutton.y;
            
            if (show_warning) {
                int ww = 300, wh = 100;
                int wx = (WIN_WIDTH - ww) / 2;
                int wy = (WIN_HEIGHT - wh) / 2;
                int ok_w = 60, ok_h = 25;
                int ok_x = wx + (ww - ok_w)/2;
                int ok_y = wy + 60;
                if (mx >= ok_x && mx <= ok_x + ok_w && my >= ok_y && my <= ok_y + ok_h) {
                    show_warning = 0;
                }
            } else {
                int btn_y = WIN_HEIGHT - BTN_HEIGHT - PADDING;
                if (mx >= PADDING && mx <= PADDING + BTN_WIDTH && my >= btn_y && my <= btn_y + BTN_HEIGHT) exit(1);
                if (mx >= WIN_WIDTH - BTN_WIDTH - PADDING && mx <= WIN_WIDTH - PADDING && my >= btn_y && my <= btn_y + BTN_HEIGHT) handle_select();
                
                if (my >= 35 && my < WIN_HEIGHT - BTN_HEIGHT - 30) {
                    int idx = scroll_offset + (my - 35) / ROW_HEIGHT;
                    if (idx < file_count) {
                        if (idx == selected_index) handle_select();
                        else selected_index = idx;
                    }
                }
            }
            draw_ui();
        }
    }

    cleanup_list();
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
