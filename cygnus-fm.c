/*
 * cygnus-fm
 * A minimalist file manager for Cygnus WM.
 *
 * by Jonathan Torres
 * 
 * This program is free software: you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation, either version 3 of 
 * the License, or (at your option) any later version.
 * 
 */

#define _GNU_SOURCE
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdlib.h>

#define WIN_WIDTH 800
#define WIN_HEIGHT 600
#define SIDEBAR_WIDTH 180
#define TOP_BAR_HEIGHT 40
#define STATUS_BAR_HEIGHT 24
#define ROW_HEIGHT 24
#define PADDING 5
#define DOUBLE_CLICK_TIME 300 /* ms */

#define COL_BG "#1a1a1a"
#define COL_FG "#ffffff"
#define COL_SEL "#3366ff"
#define COL_SIDEBAR "#111111"
#define COL_TOPBAR "#222222"
#define COL_BORDER "#444444"
#define COL_ICON_DIR "#e6b800"
#define COL_ICON_FILE "#aaaaaa"
#define COL_ICON_EXE "#00cc00"
#define COL_ICON_IMG "#cc00cc"

Display *dpy;
Window win;
int screen;
GC gc;
XFontStruct *font;
Atom wm_delete_window;
int win_width = WIN_WIDTH;
int win_height = WIN_HEIGHT;

char current_path[4096];
char status_msg[256];
char search_query[256] = "";
int search_active = 0;
int show_hidden = 0;


//icon theme & cache
char icon_theme[256] = "none";
char icon_theme_path[1024] = "";

Pixmap icon_pixmap_dir = None;
Pixmap icon_pixmap_file = None;
Pixmap icon_pixmap_exe = None;
Pixmap icon_pixmap_img = None;
int icon_w = 16, icon_h = 16;

typedef struct FileEntry {
    char name[256];
    char full_path[4400];
    int is_dir;

    int is_exe;
    int is_img;
    off_t size;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    time_t mtime;
    struct FileEntry *next;
} FileEntry;

FileEntry *file_list = NULL;
int file_count = 0;
int scroll_offset = 0;
int selected_index = -1;
Time last_click_time = 0;
int last_click_index = -1;

//sidebar
typedef struct SidebarItem {
    char label[64];
    char path[1024];
    int is_mount;
    struct SidebarItem *next;
} SidebarItem;

SidebarItem *sidebar_items = NULL;

//context menu
int ctx_menu_active = 0;
int ctx_menu_x = 0;
int ctx_menu_y = 0;
int ctx_selected_file_index = -1;
int ctx_sidebar_item_index = -1;

//clipboard
char clipboard_path[4400] = "";
int is_cutting = 0;

typedef enum { DIALOG_NONE, DIALOG_INPUT, DIALOG_PROPS } DialogType;
DialogType active_dialog = DIALOG_NONE;
char dialog_title[64];
char dialog_buffer[256];
int dialog_cursor = 0;
void (*dialog_callback)(const char*);
FileEntry *props_entry = NULL;

int address_bar_focused = 0;
int search_bar_focused = 0;
char address_buffer[4200];
int address_cursor = 0;

void setup();
void run();
void cleanup();
unsigned long get_color(const char *hex_color);
void load_sidebar();
void navigate(const char *path);
void reload_dir();
void draw_ui();
void open_file(const char *path);
void spawn(const char *command);
void update_status(const char *msg);
int is_image_file(const char *name);
int is_text_file(const char *name);
int is_media_file(const char *name);
void load_icon_theme();
void load_theme_icons();


Pixmap create_pixmap_from_image(unsigned char *data, int w, int h, int channels) {
    if (!data) return None;
    

    XImage *xi = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen), 
                              ZPixmap, 0, NULL, w, h, 32, 0);
    xi->data = malloc(xi->bytes_per_line * xi->height);
    
    XColor bg_col;
    Colormap colormap = DefaultColormap(dpy, screen);
    XParseColor(dpy, colormap, COL_BG, &bg_col);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * channels;
            unsigned char r = data[idx];
            unsigned char g = data[idx+1];
            unsigned char b = data[idx+2];
            unsigned char a = (channels == 4) ? data[idx+3] : 255;

            r = (r * a + (bg_col.red >> 8) * (255 - a)) / 255;
            g = (g * a + (bg_col.green >> 8) * (255 - a)) / 255;
            b = (b * a + (bg_col.blue >> 8) * (255 - a)) / 255;

            unsigned long pixel = (r << 16) | (g << 8) | b;
            XPutPixel(xi, x, y, pixel);
        }
    }

    Pixmap p = XCreatePixmap(dpy, win, w, h, DefaultDepth(dpy, screen));
    XPutImage(dpy, p, gc, xi, 0, 0, 0, 0, w, h);
    XDestroyImage(xi);
    return p;
}

void load_theme_icons() {
    if (strcmp(icon_theme, "none") == 0) return;
    
    const char *names_dir[] = {"folder", "inode-directory", "folder-blue", "directory", NULL};
    const char *names_file[] = {"text-x-generic", "empty", "gnome-fs-regular", "text-plain", NULL};
    const char *names_exe[] = {"application-x-executable", "application-x-executable-script", "exec", NULL};
    const char *names_img[] = {"image-x-generic", "image-jpeg", "image", NULL};

    char full_path[2048];
    int w, h, c;
    unsigned char *data;

    for (int i=0; names_dir[i]; i++) {
        snprintf(full_path, sizeof(full_path), "%s/16x16/places/%s.png", icon_theme_path, names_dir[i]);
        if (access(full_path, R_OK) != 0) snprintf(full_path, sizeof(full_path), "%s/16x16/mimetypes/%s.png", icon_theme_path, names_dir[i]);
        if (access(full_path, R_OK) == 0) {
            data = stbi_load(full_path, &w, &h, &c, 4);
            if (data) {
                if (icon_pixmap_dir != None) XFreePixmap(dpy, icon_pixmap_dir);
                icon_pixmap_dir = create_pixmap_from_image(data, w, h, 4);
                icon_w = w; icon_h = h;
                stbi_image_free(data);
                break;
            }
        }
    }
    

    for (int i=0; names_file[i]; i++) {
        snprintf(full_path, sizeof(full_path), "%s/16x16/mimetypes/%s.png", icon_theme_path, names_file[i]);
        if (access(full_path, R_OK) == 0) {
            data = stbi_load(full_path, &w, &h, &c, 4);
            if (data) {
                if (icon_pixmap_file != None) XFreePixmap(dpy, icon_pixmap_file);
                icon_pixmap_file = create_pixmap_from_image(data, w, h, 4);
                if (icon_pixmap_dir == None) { icon_w = w; icon_h = h; }
                stbi_image_free(data);
                break;
            }
        }
    }


    for (int i=0; names_exe[i]; i++) {
        snprintf(full_path, sizeof(full_path), "%s/16x16/mimetypes/%s.png", icon_theme_path, names_exe[i]);
        if (access(full_path, R_OK) == 0) {
            data = stbi_load(full_path, &w, &h, &c, 4);
            if (data) {
                if (icon_pixmap_exe != None) XFreePixmap(dpy, icon_pixmap_exe);
                icon_pixmap_exe = create_pixmap_from_image(data, w, h, 4);
                if (icon_pixmap_dir == None && icon_pixmap_file == None) { icon_w = w; icon_h = h; }
                stbi_image_free(data);
                break;
            }
        }
    }


    for (int i=0; names_img[i]; i++) {
        snprintf(full_path, sizeof(full_path), "%s/16x16/mimetypes/%s.png", icon_theme_path, names_img[i]);
        if (access(full_path, R_OK) == 0) {
            data = stbi_load(full_path, &w, &h, &c, 4);
            if (data) {
                if (icon_pixmap_img != None) XFreePixmap(dpy, icon_pixmap_img);
                icon_pixmap_img = create_pixmap_from_image(data, w, h, 4);
                if (icon_pixmap_dir == None && icon_pixmap_file == None && icon_pixmap_exe == None) { icon_w = w; icon_h = h; }
                stbi_image_free(data);
                break;
            }
        }
    }
}

void load_icon_theme() {
    char path[1024];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof(path), "%s/.cygnus-wm/icons", home);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "icontheme=", 10) == 0) {
            char *theme = line + (sizeof("icontheme=") - 1);
            theme[strcspn(theme, "\r\n")] = 0;
            
            char theme_path[1024];
            snprintf(theme_path, sizeof(theme_path), "/usr/share/icons/%s", theme);
            struct stat st;
            if (stat(theme_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                strncpy(icon_theme, theme, sizeof(icon_theme) - 1);
                strncpy(icon_theme_path, theme_path, sizeof(icon_theme_path) - 1);
                load_theme_icons();
            }
            break;
        }
    }
    fclose(f);
}

unsigned long get_color(const char *hex_color) {
    XColor color;
    Colormap colormap = DefaultColormap(dpy, screen);
    XParseColor(dpy, colormap, hex_color, &color);
    XAllocColor(dpy, colormap, &color);
    return color.pixel;
}

void spawn(const char *command) {
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        exit(0);
    }
}

void add_sidebar_item(const char *label, const char *path, int is_mount) {
    SidebarItem *item = malloc(sizeof(SidebarItem));
    strncpy(item->label, label, sizeof(item->label) - 1);
    item->label[sizeof(item->label)-1] = '\0';
    strncpy(item->path, path, sizeof(item->path) - 1);
    item->path[sizeof(item->path)-1] = '\0';
    item->is_mount = is_mount;
    item->next = NULL;
    if (!sidebar_items) {
        sidebar_items = item;
    } else {
        SidebarItem *curr = sidebar_items;
        while (curr->next) curr = curr->next;
        curr->next = item;
    }
}

void load_sidebar() {
    while (sidebar_items) {
        SidebarItem *next = sidebar_items->next;
        free(sidebar_items);
        sidebar_items = next;
    }

    const char *home = getenv("HOME");
    add_sidebar_item("Root", "/", 0);
    if (home) {
        char path[1024];
        add_sidebar_item("Home", home, 0);
        
        snprintf(path, sizeof(path), "%s/Documents", home);
        if (access(path, F_OK) == 0) add_sidebar_item("Documents", path, 0);
        
        snprintf(path, sizeof(path), "%s/Downloads", home);
        if (access(path, F_OK) == 0) add_sidebar_item("Downloads", path, 0);
        
        snprintf(path, sizeof(path), "%s/Pictures", home);
        if (access(path, F_OK) == 0) add_sidebar_item("Pictures", path, 0);
    }


    DIR *d = opendir("/media");
    if (d) {
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_name[0] == '.') continue;
            char subpath[1024];
            snprintf(subpath, sizeof(subpath), "/media/%s", dir->d_name);
            struct stat st;
            if (stat(subpath, &st) == 0 && S_ISDIR(st.st_mode)) {
                add_sidebar_item(dir->d_name, subpath, 1);
            }
        }
        closedir(d);
    }
}

void reload_dir() {
    FileEntry *curr = file_list;
    while (curr) {
        FileEntry *next = curr->next;
        free(curr);
        curr = next;
    }
    file_list = NULL; file_count = 0; selected_index = -1; scroll_offset = 0;

    DIR *d = opendir(current_path);
    if (!d) { update_status("Failed to open directory"); return; }

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (strcmp(dir->d_name, ".") == 0) continue;
        if (strcmp(dir->d_name, "..") == 0 && strcmp(current_path, "/") == 0) continue;
        if (!show_hidden && dir->d_name[0] == '.') continue;

        if (search_active && strlen(search_query) > 0) {
            if (!strcasestr(dir->d_name, search_query)) continue;
        }

        FileEntry *entry = malloc(sizeof(FileEntry));
        snprintf(entry->name, sizeof(entry->name), "%s", dir->d_name);
        snprintf(entry->full_path, sizeof(entry->full_path), "%s/%s", strcmp(current_path, "/") == 0 ? "" : current_path, dir->d_name);
        
        struct stat st;
        if (stat(entry->full_path, &st) == 0) {
            entry->size = st.st_size; entry->mode = st.st_mode;
            entry->uid = st.st_uid; entry->gid = st.st_gid;
            entry->mtime = st.st_mtime; entry->is_dir = S_ISDIR(st.st_mode);
            entry->is_exe = (st.st_mode & S_IXUSR); entry->is_img = is_image_file(entry->name);
        } else { entry->is_dir = 0; entry->size = 0; }

        if (!file_list) { entry->next = NULL; file_list = entry; }
        else {
            FileEntry *prev = NULL, *curr_node = file_list;
            int inserted = 0;
            while (curr_node) {
                int should_insert = 0;
                if (entry->is_dir && !curr_node->is_dir) should_insert = 1;
                else if (entry->is_dir == curr_node->is_dir && strcasecmp(entry->name, curr_node->name) < 0) should_insert = 1;
                if (should_insert) {
                    if (prev) prev->next = entry; else file_list = entry;
                    entry->next = curr_node; inserted = 1; break;
                }
                prev = curr_node; curr_node = curr_node->next;
            }
            if (!inserted) { prev->next = entry; entry->next = NULL; }
        }
        file_count++;
    }
    closedir(d);
    snprintf(address_buffer, sizeof(address_buffer), "%s", current_path);
    address_cursor = strlen(address_buffer);
}

void navigate(const char *path) {
    char real_path[4096];
    if (realpath(path, real_path)) snprintf(current_path, sizeof(current_path), "%s", real_path);
    else if (path != current_path) snprintf(current_path, sizeof(current_path), "%s", path);
    reload_dir(); draw_ui();
}

int is_image_file(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".jpg") == 0 ||
            strcasecmp(ext, ".jpeg") == 0 || strcasecmp(ext, ".bmp") == 0 ||
            strcasecmp(ext, ".gif") == 0 || strcasecmp(ext, ".ppm") == 0);
}

void draw_icon(int x, int y, int type) {
    Pixmap p = None;
    if (type == 0) p = icon_pixmap_dir;
    else if (type == 2) p = icon_pixmap_img;
    else if (type == 3) p = icon_pixmap_exe;
    else p = icon_pixmap_file;

    if (p != None) {
        XCopyArea(dpy, p, win, gc, 0, 0, icon_w, icon_h, x, y);
        return;
    }

    //fallback
    if (type == 0) {
        XSetForeground(dpy, gc, get_color(COL_ICON_DIR));
        XPoint points[] = { {x, y+2}, {x+6, y+2}, {x+8, y}, {x+14, y}, {x+14, y+12}, {x, y+12}, {x, y+2} };
        XFillPolygon(dpy, win, gc, points, 7, Convex, CoordModeOrigin);
    } else {
        XSetForeground(dpy, gc, get_color(type == 2 ? COL_ICON_IMG : (type == 3 ? COL_ICON_EXE : COL_ICON_FILE)));
        XFillRectangle(dpy, win, gc, x+2, y, 10, 14);
    }
}

void draw_ui() {
    XClearWindow(dpy, win);
    XSetForeground(dpy, gc, get_color(COL_SIDEBAR));
    XFillRectangle(dpy, win, gc, 0, TOP_BAR_HEIGHT, SIDEBAR_WIDTH, win_height - TOP_BAR_HEIGHT - STATUS_BAR_HEIGHT);
    int sy = TOP_BAR_HEIGHT + 10;
    for (SidebarItem *item = sidebar_items; item; item = item->next, sy += ROW_HEIGHT + 2) {
        if (strcmp(item->path, current_path) == 0) {
            XSetForeground(dpy, gc, get_color(COL_SEL));
            XFillRectangle(dpy, win, gc, 5, sy, SIDEBAR_WIDTH - 10, ROW_HEIGHT);
        }
        XSetForeground(dpy, gc, get_color(COL_FG));
        XDrawString(dpy, win, gc, 15, sy + 16, item->label, strlen(item->label));
    }

    XSetForeground(dpy, gc, get_color(COL_TOPBAR));
    XFillRectangle(dpy, win, gc, 0, 0, win_width, TOP_BAR_HEIGHT);
    XSetForeground(dpy, gc, get_color(COL_BG));
    XFillRectangle(dpy, win, gc, SIDEBAR_WIDTH + 10, 5, win_width - SIDEBAR_WIDTH - 250, 30);
    XSetForeground(dpy, gc, get_color(COL_FG));
    XDrawString(dpy, win, gc, SIDEBAR_WIDTH + 15, 25, address_buffer, strlen(address_buffer));
    if (address_bar_focused) {
        int cx = SIDEBAR_WIDTH + 15 + XTextWidth(font, address_buffer, address_cursor);
        XDrawLine(dpy, win, gc, cx, 8, cx, 32);
    }
    int sx = win_width - 230;
    XSetForeground(dpy, gc, get_color(COL_BG));
    XFillRectangle(dpy, win, gc, sx, 5, 220, 30);
    XSetForeground(dpy, gc, get_color(COL_FG));
    char sbuf[1024]; snprintf(sbuf, sizeof(sbuf), "Search: %s", search_query);
    XDrawString(dpy, win, gc, sx + 5, 25, sbuf, strlen(sbuf));

    int start_x = SIDEBAR_WIDTH + PADDING, start_y = TOP_BAR_HEIGHT + PADDING, y = start_y;
    XSetClipRectangles(dpy, gc, 0, 0, &(XRectangle){start_x, start_y, win_width - start_x, win_height - start_y - STATUS_BAR_HEIGHT}, 1, Unsorted);
    int idx = 0;
    for (FileEntry *e = file_list; e; e = e->next, idx++) {
        if (idx < scroll_offset) continue;
        if (y > win_height - STATUS_BAR_HEIGHT - ROW_HEIGHT) break;
        if (idx == selected_index) {
            XSetForeground(dpy, gc, get_color(COL_SEL));
            XFillRectangle(dpy, win, gc, start_x, y, win_width - start_x - PADDING, ROW_HEIGHT);
        }
        draw_icon(start_x + 5, y + 5, e->is_dir ? 0 : (e->is_img ? 2 : (e->is_exe ? 3 : 1)));
        XSetForeground(dpy, gc, get_color(COL_FG));
        XDrawString(dpy, win, gc, start_x + 30, y + 16, e->name, strlen(e->name));
        y += ROW_HEIGHT;
    }
    XSetClipMask(dpy, gc, None);
    XSetForeground(dpy, gc, get_color(COL_TOPBAR));
    XFillRectangle(dpy, win, gc, 0, win_height - STATUS_BAR_HEIGHT, win_width, STATUS_BAR_HEIGHT);
    XSetForeground(dpy, gc, get_color(COL_FG));
    char st[512]; snprintf(st, sizeof(st), " %d items | Theme: %s | %s", file_count, icon_theme, status_msg);
    XDrawString(dpy, win, gc, 5, win_height - 6, st, strlen(st));
    if (ctx_menu_active) {
        int w = 150, h = (ctx_sidebar_item_index != -1 ? 2 : 9) * 20;
        XSetForeground(dpy, gc, get_color(COL_TOPBAR));
        XFillRectangle(dpy, win, gc, ctx_menu_x, ctx_menu_y, w, h);
        XSetForeground(dpy, gc, get_color(COL_FG));
        if (ctx_sidebar_item_index != -1) {
            XDrawString(dpy, win, gc, ctx_menu_x + 10, ctx_menu_y + 15, "Mount", 5);
            XDrawString(dpy, win, gc, ctx_menu_x + 10, ctx_menu_y + 35, "Unmount", 7);
        } else {
            char *m[] = {"Open", "Copy", "Cut", "Paste", "Rename", "Delete", "Properties", "New Folder", "New File"};
            for (int i=0; i<9; i++) XDrawString(dpy, win, gc, ctx_menu_x + 10, ctx_menu_y + (i+1)*20 - 5, m[i], strlen(m[i]));
        }
    }
    if (active_dialog != DIALOG_NONE) {
        int w = 400, h = 200, dx = (win_width-w)/2, dy = (win_height-h)/2;
        XSetForeground(dpy, gc, get_color(COL_TOPBAR));
        XFillRectangle(dpy, win, gc, dx, dy, w, h);
        XSetForeground(dpy, gc, get_color(COL_FG));
        XDrawString(dpy, win, gc, dx + 10, dy + 20, dialog_title, strlen(dialog_title));
        if (active_dialog == DIALOG_INPUT) {
            XSetForeground(dpy, gc, get_color(COL_BG));
            XFillRectangle(dpy, win, gc, dx+20, dy+50, w-40, 30);
            XSetForeground(dpy, gc, get_color(COL_FG));
            XDrawString(dpy, win, gc, dx+30, dy+70, dialog_buffer, strlen(dialog_buffer));
        } else if (active_dialog == DIALOG_PROPS && props_entry) {
            char b[1024]; snprintf(b, sizeof(b), "Name: %s", props_entry->name);
            XDrawString(dpy, win, gc, dx+20, dy+50, b, strlen(b));
            snprintf(b, sizeof(b), "Size: %ld bytes", (long)props_entry->size);
            XDrawString(dpy, win, gc, dx+20, dy+70, b, strlen(b));
        }
    }
}

void callback_rename(const char *n) {
    FileEntry *e = file_list; for (int i=0; i<ctx_selected_file_index && e; i++) e = e->next;
    if (e) { char p[4500]; snprintf(p, sizeof(p), "%s/%s", current_path, n); rename(e->full_path, p); reload_dir(); }
}
void callback_mkdir(const char *n) { char p[4500]; snprintf(p, sizeof(p), "%s/%s", current_path, n); mkdir(p, 0755); reload_dir(); }
void callback_mkfile(const char *n) { char p[4500]; snprintf(p, sizeof(p), "%s/%s", current_path, n); close(open(p, O_CREAT|O_WRONLY, 0644)); reload_dir(); }

void handle_click(int b, int x, int y, Time t) {
    if (active_dialog != DIALOG_NONE) { active_dialog = DIALOG_NONE; draw_ui(); return; }
    if (ctx_menu_active) {
        int item = (y - ctx_menu_y) / 20;
        if (ctx_sidebar_item_index != -1) {
            SidebarItem *si = sidebar_items; for (int i=0; i<ctx_sidebar_item_index && si; i++) si = si->next;
            if (si) { char c[2048]; snprintf(c, sizeof(c), "udisksctl %s -b \"%s\"", item==0?"mount":"unmount", si->path); spawn(c); }
        } else {
            FileEntry *e = file_list; for (int i=0; i<ctx_selected_file_index && e; i++) e = e->next;
            if (item == 0 && e) { if (e->is_dir) navigate(e->full_path); else open_file(e->full_path); }
            else if (item == 1 && e) { snprintf(clipboard_path, sizeof(clipboard_path), "%s", e->full_path); is_cutting = 0; update_status("Copied"); }
            else if (item == 2 && e) { snprintf(clipboard_path, sizeof(clipboard_path), "%s", e->full_path); is_cutting = 1; update_status("Cut"); }
            else if (item == 3) {
                if (clipboard_path[0]) {
                    char cmd[9000];
                    if (is_cutting) snprintf(cmd, sizeof(cmd), "mv \"%s\" \"%s/\"", clipboard_path, current_path);
                    else snprintf(cmd, sizeof(cmd), "cp -r \"%s\" \"%s/\"", clipboard_path, current_path);
                    spawn(cmd);
                    if (is_cutting) clipboard_path[0] = '\0';
                    reload_dir();
                }
            }
            else if (item == 4 && e) { active_dialog = DIALOG_INPUT; strcpy(dialog_title, "Rename:"); snprintf(dialog_buffer, sizeof(dialog_buffer), "%s", e->name); dialog_cursor = strlen(dialog_buffer); dialog_callback = callback_rename; }
            else if (item == 5 && e) { char c[8192]; snprintf(c, sizeof(c), "rm -rf \"%s\"", e->full_path); spawn(c); reload_dir(); }
            else if (item == 6 && e) { active_dialog = DIALOG_PROPS; props_entry = e; strcpy(dialog_title, "Properties"); }
            else if (item == 7) { active_dialog = DIALOG_INPUT; strcpy(dialog_title, "New Folder:"); dialog_buffer[0]='\0'; dialog_cursor=0; dialog_callback = callback_mkdir; }
            else if (item == 8) { active_dialog = DIALOG_INPUT; strcpy(dialog_title, "New File:"); dialog_buffer[0]='\0'; dialog_cursor=0; dialog_callback = callback_mkfile; }
        }
        ctx_menu_active = 0; draw_ui(); return;
    }
    if (y < TOP_BAR_HEIGHT) { address_bar_focused = (x > SIDEBAR_WIDTH+10 && x < win_width-240); search_bar_focused = (x > win_width-230); draw_ui(); return; }
    if (x < SIDEBAR_WIDTH) {
        int iy = TOP_BAR_HEIGHT + 10, idx = 0;
        for (SidebarItem *i = sidebar_items; i; i = i->next, idx++) {
            if (y >= iy && y < iy + ROW_HEIGHT) {
                if (b == Button1) navigate(i->path);
                else if (b == Button3) { ctx_menu_active = 1; ctx_menu_x = x; ctx_menu_y = y; ctx_sidebar_item_index = idx; }
                return;
            }
            iy += ROW_HEIGHT + 2;
        }
        return;
    }
    int idx = (y - TOP_BAR_HEIGHT - PADDING) / ROW_HEIGHT + scroll_offset;
    if (idx >= 0 && idx < file_count) {
        if (b == Button1) {
            if (idx == last_click_index && (t - last_click_time) < DOUBLE_CLICK_TIME) {
                FileEntry *e = file_list; for (int i=0; i<idx && e; i++) e = e->next;
                if (e) { if (e->is_dir) navigate(e->full_path); else open_file(e->full_path); }
            }
            selected_index = idx; last_click_index = idx; last_click_time = t;
        } else if (b == Button3) { ctx_menu_active = 1; ctx_menu_x = x; ctx_menu_y = y; ctx_selected_file_index = idx; ctx_sidebar_item_index = -1; }
    } else if (b == Button3) {
        ctx_menu_active = 1; ctx_menu_x = x; ctx_menu_y = y; ctx_selected_file_index = -1; ctx_sidebar_item_index = -1;
    }
    draw_ui();
}

void handle_key(XKeyEvent *ev) {
    KeySym ks = XLookupKeysym(ev, 0); char buf[32]; int len = XLookupString(ev, buf, sizeof(buf), NULL, NULL);
    if (active_dialog == DIALOG_INPUT) {
        if (ks == XK_Return) { if (dialog_callback) dialog_callback(dialog_buffer); active_dialog = DIALOG_NONE; }
        else if (ks == XK_Escape) active_dialog = DIALOG_NONE;
        else if (ks == XK_BackSpace && dialog_cursor > 0) dialog_buffer[--dialog_cursor] = '\0';
        else if (len > 0 && dialog_cursor < 255) { dialog_buffer[dialog_cursor++] = buf[0]; dialog_buffer[dialog_cursor] = '\0'; }
    } else if (address_bar_focused) {
        if (ks == XK_Return) { navigate(address_buffer); address_bar_focused = 0; }
        else if (ks == XK_BackSpace && address_cursor > 0) address_buffer[--address_cursor] = '\0';
        else if (len > 0 && address_cursor < 4095) { address_buffer[address_cursor++] = buf[0]; address_buffer[address_cursor] = '\0'; }
    } else if (search_bar_focused) {
        if (ks == XK_Return) { search_active = 1; reload_dir(); }
        else if (ks == XK_BackSpace) { int sl = strlen(search_query); if (sl > 0) search_query[sl-1] = '\0'; search_active = (strlen(search_query)>0); reload_dir(); }
        else if (len > 0) { 
            int sl = strlen(search_query);
            if (sl < sizeof(search_query) - 1) {
                search_query[sl] = buf[0];
                search_query[sl+1] = '\0';
            }
            search_active = 1; reload_dir(); 
        }
    } else {
        if (ks == XK_h && (ev->state & ControlMask)) { show_hidden = !show_hidden; reload_dir(); }
        else if (ks == XK_c && (ev->state & ControlMask)) {
            FileEntry *e = file_list; for (int i=0; i<selected_index && e; i++) e = e->next;
            if (e) { snprintf(clipboard_path, sizeof(clipboard_path), "%s", e->full_path); is_cutting = 0; update_status("Copied"); }
        }
        else if (ks == XK_x && (ev->state & ControlMask)) {
            FileEntry *e = file_list; for (int i=0; i<selected_index && e; i++) e = e->next;
            if (e) { snprintf(clipboard_path, sizeof(clipboard_path), "%s", e->full_path); is_cutting = 1; update_status("Cut"); }
        }
        else if (ks == XK_v && (ev->state & ControlMask)) {
            if (clipboard_path[0]) {
                char cmd[9000];
                if (is_cutting) snprintf(cmd, sizeof(cmd), "mv \"%s\" \"%s/\"", clipboard_path, current_path);
                else snprintf(cmd, sizeof(cmd), "cp -r \"%s\" \"%s/\"", clipboard_path, current_path);
                spawn(cmd);
                if (is_cutting) clipboard_path[0] = '\0';
                reload_dir();
                update_status("Pasted");
            }
        }
        else if (ks == XK_Up && selected_index > 0) selected_index--;
        else if (ks == XK_Down && selected_index < file_count-1) selected_index++;
        else if (ks == XK_Return && selected_index >= 0) {
            FileEntry *e = file_list; for (int i=0; i<selected_index && e; i++) e = e->next;
            if (e) { if (e->is_dir) navigate(e->full_path); else open_file(e->full_path); }
        } else if (ks == XK_BackSpace) {
            char *ls = strrchr(current_path, '/');
            if (ls && ls != current_path) { *ls = '\0'; navigate(current_path); } else if (strcmp(current_path, "/") != 0) navigate("/");
        }
    }
    draw_ui();
}

int is_text_file(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".c") == 0 ||
            strcasecmp(ext, ".h") == 0 || strcasecmp(ext, ".cpp") == 0 ||
            strcasecmp(ext, ".py") == 0 || strcasecmp(ext, ".sh") == 0 ||
            strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".json") == 0 ||
            strcasecmp(ext, ".conf") == 0 || strcasecmp(ext, ".ini") == 0);
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

void open_file(const char *p) { 
    if (fork()==0) { 
        if (dpy) close(ConnectionNumber(dpy)); 
        if (is_image_file(p)) {
            execlp("cygnus-view", "cygnus-view", p, NULL);
        } else if (is_text_file(p)) {
            execlp("cygnus-edit", "cygnus-edit", p, NULL);
        } else if (is_media_file(p)) {
            execlp("cygnus-media", "cygnus-media", p, NULL);
        } else {
            execlp("xdg-open", "xdg-open", p, NULL);
        }
        exit(0); 
    } 
}
void update_status(const char *m) { strncpy(status_msg, m, 255); draw_ui(); }

void setup() {
    dpy = XOpenDisplay(NULL); screen = DefaultScreen(dpy);
    XSetWindowAttributes a; a.background_pixel = get_color(COL_BG);
    a.event_mask = ExposureMask|KeyPressMask|ButtonPressMask|StructureNotifyMask;
    win = XCreateWindow(dpy, RootWindow(dpy, screen), 100, 100, WIN_WIDTH, WIN_HEIGHT, 0, DefaultDepth(dpy, screen), InputOutput, DefaultVisual(dpy, screen), CWBackPixel|CWEventMask, &a);
    XStoreName(dpy, win, "Cygnus File Manager");
    gc = XCreateGC(dpy, win, 0, NULL);
    font = XLoadQueryFont(dpy, "fixed"); if (font) XSetFont(dpy, gc, font->fid);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False); XSetWMProtocols(dpy, win, &wm_delete_window, 1);
    const char *h = getenv("HOME"); strncpy(current_path, h?h:"/", 4095);
    load_sidebar(); load_icon_theme(); reload_dir(); XMapWindow(dpy, win);
}

void run() {
    XEvent e;
    while (1) {
        XNextEvent(dpy, &e);
        if (e.type == Expose && e.xexpose.count == 0) draw_ui();
        else if (e.type == ButtonPress) handle_click(e.xbutton.button, e.xbutton.x, e.xbutton.y, e.xbutton.time);
        else if (e.type == KeyPress) handle_key(&e.xkey);
        else if (e.type == ConfigureNotify) { win_width = e.xconfigure.width; win_height = e.xconfigure.height; draw_ui(); }
        else if (e.type == ClientMessage && e.xclient.data.l[0] == wm_delete_window) break;
    }
}

int main() { setup(); run(); cleanup(); return 0; }
void cleanup() { XCloseDisplay(dpy); }

