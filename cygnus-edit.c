/*
 * cygnus-edit
 * A minimalist text editor for the Cygnus WM.
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
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_BUFFER (1024 * 1024 * 5)
#define GAP_SIZE 1024
#define COL_BG "#1a1a1a"
#define COL_FG "#ffffff"
#define COL_SEL "#3366ff"
#define COL_BAR "#222222"

typedef enum { MODE_EDIT, MODE_SAVE_NAME, MODE_SAVE_CONFIRM, MODE_SEARCH } Mode;

char text_buf[MAX_BUFFER];
int gap_start = 0;
int gap_end = MAX_BUFFER;
int cursor = 0;
int sel_start = -1;
int sel_end = -1;
int sel_anchor = -1;
int scroll_y = 0;
int dragging = 0;

char filename[1024] = "";
char status_msg[256] = "";
char input_buf[1024] = "";
Mode mode = MODE_EDIT;
int dirty = 0;

typedef struct {
    int start;
    int len;
} VisualLine;

VisualLine *lines = NULL;
int line_count = 0;
int line_capacity = 0;
int max_cols = 80;
int max_rows = 24;

Display *dpy;
Window win;
int screen;
GC gc;
XFontStruct *font;
Atom clipboard_atom, targets_atom, string_atom, utf8_atom, wm_delete_window;
char *clipboard_content = NULL;

unsigned long get_color(const char *hex) {
    XColor c;
    Colormap cm = DefaultColormap(dpy, screen);
    XParseColor(dpy, cm, hex, &c);
    XAllocColor(dpy, cm, &c);
    return c.pixel;
}

void move_gap(int pos) {
    if (pos == gap_start) return;
    if (pos < gap_start) {
        int len = gap_start - pos;
        memmove(text_buf + gap_end - len, text_buf + pos, len);
        gap_start -= len;
        gap_end -= len;
    } else {
        int len = pos - gap_start;
        memmove(text_buf + gap_start, text_buf + gap_end, len);
        gap_start += len;
        gap_end += len;
    }
}

void insert_char(char c) {
    if (gap_start == gap_end) return;
    move_gap(cursor);
    text_buf[gap_start++] = c;
    cursor++;
    dirty = 1;
}

void insert_str(const char *s) {
    while (*s) insert_char(*s++);
}

void delete_char() {
    if (cursor == 0) return;
    move_gap(cursor);
    gap_start--;
    cursor--;
    dirty = 1;
}

char get_char(int i) {
    return (i < gap_start) ? text_buf[i] : text_buf[i + (gap_end - gap_start)];
}

int buf_len() {
    return MAX_BUFFER - (gap_end - gap_start);
}

void clear_selection() {
    sel_start = sel_end = sel_anchor = -1;
}

void set_selection(int start, int end) {
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    int len = buf_len();
    if (start > len) start = len;
    if (end > len) end = len;
    sel_start = (start < end) ? start : end;
    sel_end = (start < end) ? end : start;
}

int get_pos_at(int x, int y) {
    int line_h = font->ascent + font->descent;
    int vrow = y / line_h + scroll_y;
    if (vrow < 0) vrow = 0;
    if (vrow >= line_count) vrow = line_count - 1;
    
    VisualLine vl = lines[vrow];
    char lbuf[1024];
    int draw_len = 0;
    for (int k = 0; k < vl.len && k < 1023; k++) {
        char c = get_char(vl.start + k);
        if (c == '\n') break;
        lbuf[draw_len++] = c;
    }
    
    int cx = 0;
    for (int k = 0; k < draw_len; k++) {
        int cw = XTextWidth(font, lbuf + k, 1);
        if (x < cx + cw / 2) return vl.start + k;
        cx += cw;
    }
    return vl.start + draw_len;
}

char *get_selection_text() {
    if (sel_start == -1) return NULL;
    int len = sel_end - sel_start;
    char *s = malloc(len + 1);
    for (int i = 0; i < len; i++) s[i] = get_char(sel_start + i);
    s[len] = '\0';
    return s;
}

void delete_selection() {
    if (sel_start == -1) return;
    move_gap(sel_end);
    int len = sel_end - sel_start;
    gap_start -= len;
    cursor = sel_start;
    clear_selection();
    dirty = 1;
}

void calculate_layout(int width) {
    if (width <= 0) return;
    int char_w = XTextWidth(font, "W", 1);
    if (char_w == 0) char_w = 8;
    max_cols = width / char_w;
    if (max_cols < 1) max_cols = 1;

    line_count = 0;
    int pos = 0;
    int len = buf_len();
    
    while (pos < len) {
        if (line_count >= line_capacity) {
            line_capacity = (line_capacity == 0) ? 128 : line_capacity * 2;
            lines = realloc(lines, sizeof(VisualLine) * line_capacity);
        }
        
        int line_len = 0;
        
        for (int i = 0; i < max_cols; i++) {
            if (pos + i >= len) {
                line_len = i;
                break;
            }
            char c = get_char(pos + i);
            if (c == '\n') {
                line_len = i + 1;
                break;
            }
            line_len = i + 1;
        }
        
        lines[line_count].start = pos;
        lines[line_count].len = line_len;
        line_count++;
        pos += line_len;
    }

    if (len == 0 || get_char(len-1) == '\n') {
        if (line_count >= line_capacity) {
            lines = realloc(lines, sizeof(VisualLine) * (line_capacity + 128));
            line_capacity += 128;
        }
        lines[line_count].start = len;
        lines[line_count].len = 0;
        line_count++;
    }
}

void get_visual_pos(int idx, int *vrow, int *vcol) {
    for (int i = 0; i < line_count; i++) {
        if (idx >= lines[i].start && idx < lines[i].start + lines[i].len) {
            *vrow = i;
            *vcol = idx - lines[i].start;
            return;
        }
    }

    *vrow = line_count - 1;
    *vcol = idx - lines[line_count-1].start;
}

int get_buffer_pos(int vrow, int vcol) {
    if (vrow < 0) vrow = 0;
    if (vrow >= line_count) vrow = line_count - 1;
    int start = lines[vrow].start;
    int len = lines[vrow].len;
    if (len > 0 && get_char(start + len - 1) == '\n') len--;
    if (vcol > len) vcol = len;
    return start + vcol;
}


void load_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    
    struct stat st;
    stat(path, &st);
    if (st.st_size >= MAX_BUFFER) {
        fclose(f);
        snprintf(status_msg, sizeof(status_msg), "File too large!");
        return;
    }
    
    fread(text_buf, 1, st.st_size, f);
    fclose(f);
    
    gap_start = st.st_size;
    gap_end = MAX_BUFFER;
    cursor = 0;
    strncpy(filename, path, sizeof(filename)-1);
    dirty = 0;
}

void save_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(status_msg, sizeof(status_msg), "Error saving file!");
        return;
    }
    fwrite(text_buf, 1, gap_start, f);
    fwrite(text_buf + gap_end, 1, MAX_BUFFER - gap_end, f);
    fclose(f);
    strncpy(filename, path, sizeof(filename)-1);
    dirty = 0;
    snprintf(status_msg, sizeof(status_msg), "Saved: %s", path);
}


void send_clipboard() {
    char *txt = get_selection_text();
    if (!txt) return;
    if (clipboard_content) free(clipboard_content);
    clipboard_content = txt;
    XSetSelectionOwner(dpy, clipboard_atom, win, CurrentTime);
}

void request_clipboard() {
    XConvertSelection(dpy, clipboard_atom, string_atom, string_atom, win, CurrentTime);
}


void draw() {
    XWindowAttributes wa;
    XGetWindowAttributes(dpy, win, &wa);
    calculate_layout(wa.width);
    max_rows = (wa.height - 20) / (font->ascent + font->descent);

    XClearWindow(dpy, win);
    
    int vr, vc;
    get_visual_pos(cursor, &vr, &vc);
    if (vr < scroll_y) scroll_y = vr;
    if (vr >= scroll_y + max_rows) scroll_y = vr - max_rows + 1;

    int y_off = 0;
    int line_h = font->ascent + font->descent;

    for (int i = 0; i < max_rows; i++) {
        int li = scroll_y + i;
        if (li >= line_count) break;
        
        VisualLine vl = lines[li];
        char lbuf[1024];
        int draw_len = 0;
        
        for (int k = 0; k < vl.len && k < 1023; k++) {
            char c = get_char(vl.start + k);
            if (c == '\n') break;
            lbuf[draw_len++] = c;
        }
        
        if (sel_start != -1) {
            for (int k = 0; k < draw_len; k++) {
                int abs_pos = vl.start + k;
                if (abs_pos >= sel_start && abs_pos < sel_end) {
                    int cw = XTextWidth(font, lbuf+k, 1);
                    int cx = XTextWidth(font, lbuf, k);
                    XSetForeground(dpy, gc, get_color("#ffffff"));
                    XFillRectangle(dpy, win, gc, cx, y_off, cw, line_h);
                    XSetForeground(dpy, gc, get_color("#000000"));
                    XDrawString(dpy, win, gc, cx, y_off + font->ascent, lbuf + k, 1);
                }
            }
        }
        

        XSetForeground(dpy, gc, get_color(COL_FG));
        for (int k = 0; k < draw_len; k++) {
            int abs_pos = vl.start + k;
            if (sel_start == -1 || abs_pos < sel_start || abs_pos >= sel_end) {
                int cx = XTextWidth(font, lbuf, k);
                XDrawString(dpy, win, gc, cx, y_off + font->ascent, lbuf + k, 1);
            }
        }

        if (li == vr) {
            int cx = XTextWidth(font, lbuf, vc);
            XDrawRectangle(dpy, win, gc, cx, y_off, 1, line_h);
        }
        
        y_off += line_h;
    }
    

    XSetForeground(dpy, gc, get_color(COL_BAR));
    XFillRectangle(dpy, win, gc, 0, wa.height - 20, wa.width, 20);
    XSetForeground(dpy, gc, get_color(COL_FG));
    
    char bar[2048];
    if (mode == MODE_EDIT) {
        snprintf(bar, sizeof(bar), "%s%s | %d:%d | %s", filename[0]?filename:"[No Name]", dirty?"*":"", vr+1, vc+1, status_msg);
    } else if (mode == MODE_SAVE_NAME) {
        snprintf(bar, sizeof(bar), "Save As: %s_", input_buf);
    } else if (mode == MODE_SAVE_CONFIRM) {
        snprintf(bar, sizeof(bar), "Overwrite %s? (y/n)", input_buf);
    } else if (mode == MODE_SEARCH) {
        snprintf(bar, sizeof(bar), "Search: %s_", input_buf);
    }
    XDrawString(dpy, win, gc, 5, wa.height - 5, bar, strlen(bar));
}

int main(int argc, char *argv[]) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    screen = DefaultScreen(dpy);
    
    unsigned long bg = get_color(COL_BG);
    unsigned long fg = get_color(COL_FG);

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 800, 600, 1, fg, bg);
    XStoreName(dpy, win, "Cygnus Editor");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask | ButtonPressMask | ButtonMotionMask);
    XMapWindow(dpy, win);
    
    gc = XCreateGC(dpy, win, 0, NULL);
    font = XLoadQueryFont(dpy, "fixed");
    if (!font) font = XLoadQueryFont(dpy, "9x15");
    XSetFont(dpy, gc, font->fid);

    clipboard_atom = XInternAtom(dpy, "CLIPBOARD", False);
    targets_atom = XInternAtom(dpy, "TARGETS", False);
    string_atom = XInternAtom(dpy, "STRING", False);
    utf8_atom = XInternAtom(dpy, "UTF8_STRING", False);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);
    
    if (argc > 1) load_file(argv[1]);

    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) {
            draw();
        } else if (ev.type == ClientMessage) {
            if (ev.xclient.data.l[0] == wm_delete_window) break;
        } else if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            char buf[32];
            XLookupString(&ev.xkey, buf, sizeof(buf), &ks, NULL);
            int ctrl = ev.xkey.state & ControlMask;
            int shift = ev.xkey.state & ShiftMask;

            if (mode == MODE_EDIT) {
                if (ctrl && ks == XK_q) break;
                else if (ctrl && ks == XK_o) {
                    if (filename[0]) {
                        strncpy(input_buf, filename, sizeof(input_buf));
                        mode = MODE_SAVE_CONFIRM;
                    } else {
                        mode = MODE_SAVE_NAME;
                        input_buf[0] = '\0';
                    }
                }
                else if (ctrl && ks == XK_w) { mode = MODE_SEARCH; input_buf[0] = '\0'; }
                else if (ctrl && ks == XK_c) { send_clipboard(); }
                else if (ctrl && ks == XK_x) { send_clipboard(); delete_selection(); }
                else if (ctrl && ks == XK_v) { request_clipboard(); }
                else if (ks == XK_Left || ks == XK_Right || ks == XK_Up || ks == XK_Down) {
                    if (shift) {
                        if (sel_anchor == -1) sel_anchor = cursor;
                        if (ks == XK_Left && cursor > 0) cursor--;
                        else if (ks == XK_Right && cursor < buf_len()) cursor++;
                        else if (ks == XK_Up) {
                            int r, c; get_visual_pos(cursor, &r, &c);
                            cursor = get_buffer_pos(r - 1, c);
                        }
                        else if (ks == XK_Down) {
                            int r, c; get_visual_pos(cursor, &r, &c);
                            cursor = get_buffer_pos(r + 1, c);
                        }
                        set_selection(sel_anchor, cursor);
                    } else {
                        if (ks == XK_Left && cursor > 0) cursor--;
                        else if (ks == XK_Right && cursor < buf_len()) cursor++;
                        else if (ks == XK_Up) {
                            int r, c; get_visual_pos(cursor, &r, &c);
                            cursor = get_buffer_pos(r - 1, c);
                        }
                        else if (ks == XK_Down) {
                            int r, c; get_visual_pos(cursor, &r, &c);
                            cursor = get_buffer_pos(r + 1, c);
                        }
                        clear_selection();
                    }
                }
                else if (ks == XK_BackSpace) {
                    if (sel_start != -1) delete_selection(); else delete_char();
                }
                else if (ks == XK_Return) {
                    if (sel_start != -1) delete_selection();
                    insert_char('\n');
                }
                else if (!ctrl && buf[0] >= 32 && buf[0] < 127) {
                    if (sel_start != -1) delete_selection();
                    insert_char(buf[0]);
                }
            } else if (mode == MODE_SAVE_NAME || mode == MODE_SEARCH) {
                if (ks == XK_Return) {
                    if (mode == MODE_SAVE_NAME) {
                        if (access(input_buf, F_OK) == 0) {
                            mode = MODE_SAVE_CONFIRM;
                        } else {
                            save_file(input_buf);
                            mode = MODE_EDIT;
                        }
                    } else {
                        int len = buf_len();
                        int slen = strlen(input_buf);
                        int found = -1;
                        if (slen > 0) {
                            for (int i = cursor + 1; i <= len - slen; i++) {
                                int match = 1;
                                for (int j = 0; j < slen; j++) {
                                    if (get_char(i+j) != input_buf[j]) { match = 0; break; }
                                }
                                if (match) { found = i; break; }
                            }
                            if (found == -1) {
                                for (int i = 0; i <= cursor; i++) {
                                    int match = 1;
                                    for (int j = 0; j < slen; j++) {
                                        if (get_char(i+j) != input_buf[j]) { match = 0; break; }
                                    }
                                    if (match) { found = i; break; }
                                }
                            }
                        }
                        if (found != -1) {
                            cursor = found;
                            set_selection(found, found + slen);
                            int vr, vc; get_visual_pos(cursor, &vr, &vc);
                            if (vr < scroll_y || vr >= scroll_y + max_rows) {
                                scroll_y = vr - max_rows / 2;
                                if (scroll_y < 0) scroll_y = 0;
                            }
                        }
                        mode = MODE_EDIT;
                    }
                } else if (ks == XK_BackSpace) {
                    int l = strlen(input_buf);
                    if (l > 0) input_buf[l-1] = '\0';
                } else if (buf[0] >= 32 && buf[0] < 127) {
                    int l = strlen(input_buf);
                    if (l < 1000) { input_buf[l] = buf[0]; input_buf[l+1] = '\0'; }
                }
 else if (ks == XK_Escape) {
                    mode = MODE_EDIT;
                }
            } else if (mode == MODE_SAVE_CONFIRM) {
                if (ks == XK_y) {
                    save_file(input_buf);
                    mode = MODE_EDIT;
                } else if (ks == XK_n || ks == XK_Escape) {
                    mode = MODE_EDIT;
                }
            }
            draw();
        } else if (ev.type == ButtonPress) {
            if (ev.xbutton.button == 1) {
                cursor = get_pos_at(ev.xbutton.x, ev.xbutton.y);
                sel_anchor = cursor;
                clear_selection();
                dragging = 1;
            }
            draw();
        } else if (ev.type == MotionNotify) {
            if (dragging) {
                cursor = get_pos_at(ev.xmotion.x, ev.xmotion.y);
                set_selection(sel_anchor, cursor);
                draw();
            }
        } else if (ev.type == ButtonRelease) {
            if (ev.xbutton.button == 1) {
                dragging = 0;
            }
            draw();
        } else if (ev.type == SelectionRequest) {
            XSelectionRequestEvent *req = &ev.xselectionrequest;
            if (clipboard_content) {
                XChangeProperty(dpy, req->requestor, req->property, req->target, 8, PropModeReplace, (unsigned char*)clipboard_content, strlen(clipboard_content));
                XSelectionEvent sev = { .type = SelectionNotify, .requestor = req->requestor, .selection = req->selection, .target = req->target, .property = req->property, .time = req->time };
                XSendEvent(dpy, req->requestor, True, 0, (XEvent*)&sev);
            }
        } else if (ev.type == SelectionNotify) {
            XSelectionEvent *sev = &ev.xselection;
            if (sev->property == None) continue;
            Atom type; int fmt; unsigned long nitems, bytes_after;
            unsigned char *data = NULL;
            XGetWindowProperty(dpy, win, sev->property, 0, MAX_BUFFER, False, AnyPropertyType, &type, &fmt, &nitems, &bytes_after, &data);
            if (data) {
                if (sel_start != -1) delete_selection();
                insert_str((char*)data);
                XFree(data);
                draw();
            }
        }
    }
    XCloseDisplay(dpy);
    return 0;
}
