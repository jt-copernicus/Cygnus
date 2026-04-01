/*
 * cygnus-view
 * Image viewer for Cygnus WM.
 *
 * by Jonathan Torres
 *
 * This app uses stb_image.h for direct JPEG/PNG output.
 * stb_image.h was written by Sean Barret, and is used under the MIT license.
 * License text included within the header file.
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation, either version 3 of 
 * the License, or (at your option) any later version.
 * 
 */

#define _DEFAULT_SOURCE
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define COL_BG "#1a1a1a"
#define COL_FG "#ffffff"

typedef struct {
    unsigned char *data;
    int w, h, channels;
    float zoom;
    int offset_x, offset_y;
} Image;

Display *dpy;
Window win;
int screen;
GC gc;
Image current_img = {0};
Atom wm_delete_window;
char current_path[4096];
char **files = NULL;
int file_count = 0;
int current_file_index = -1;

unsigned long get_color(const char *hex_color) {
    XColor color;
    Colormap colormap = DefaultColormap(dpy, screen);
    XParseColor(dpy, colormap, hex_color, &color);
    XAllocColor(dpy, colormap, &color);
    return color.pixel;
}

void load_image_file(const char *path) {
    if (current_img.data) stbi_image_free(current_img.data);
    current_img.data = stbi_load(path, &current_img.w, &current_img.h, &current_img.channels, 4);
    if (!current_img.data) {
        fprintf(stderr, "Could not load image: %s\n", path);
        return;
    }
    strncpy(current_path, path, sizeof(current_path)-1);
    current_img.zoom = 1.0f;
    current_img.offset_x = 0;
    current_img.offset_y = 0;

    XWindowAttributes wa;
    XGetWindowAttributes(dpy, win, &wa);
    float scale_w = (float)wa.width / current_img.w;
    float scale_h = (float)wa.height / current_img.h;
    current_img.zoom = (scale_w < scale_h) ? (scale_w < 1.0 ? scale_w : 1.0) : (scale_h < 1.0 ? scale_h : 1.0);
    
    current_img.offset_x = (wa.width - current_img.w * current_img.zoom) / 2;
    current_img.offset_y = (wa.height - current_img.h * current_img.zoom) / 2;
    
    XStoreName(dpy, win, path);
}

void draw_image() {
    if (!current_img.data) {
        XSetForeground(dpy, gc, get_color(COL_FG));
        XDrawString(dpy, win, gc, 20, 30, "No image loaded. Use Right/Left to navigate directory.", 53);
        return;
    }

    XWindowAttributes wa;
    XGetWindowAttributes(dpy, win, &wa);
    XSetForeground(dpy, gc, get_color(COL_BG));
    XFillRectangle(dpy, win, gc, 0, 0, wa.width, wa.height);

    int view_w = current_img.w * current_img.zoom;
    int view_h = current_img.h * current_img.zoom;

    if (view_w <= 0 || view_h <= 0) return;

    
    XImage *xi = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen), 
                              ZPixmap, 0, NULL, wa.width, wa.height, 32, 0);
    xi->data = malloc(xi->bytes_per_line * xi->height);
    memset(xi->data, 0, xi->bytes_per_line * xi->height);

    for (int y = 0; y < wa.height; y++) {
        int img_y = (y - current_img.offset_y) / current_img.zoom;
        if (img_y < 0 || img_y >= current_img.h) continue;
        
        for (int x = 0; x < wa.width; x++) {
            int img_x = (x - current_img.offset_x) / current_img.zoom;
            if (img_x < 0 || img_x >= current_img.w) continue;

            unsigned char *p = &current_img.data[(img_y * current_img.w + img_x) * 4];
            unsigned long pixel = (p[0] << 16) | (p[1] << 8) | p[2];
            XPutPixel(xi, x, y, pixel);
        }
    }

    XPutImage(dpy, win, gc, xi, 0, 0, 0, 0, wa.width, wa.height);
    XDestroyImage(xi);
}

int is_image(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    return (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 ||
            strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".bmp") == 0 ||
            strcasecmp(ext, ".ppm") == 0);
}

void scan_directory(const char *path) {
    char dir_path[4096];
    strncpy(dir_path, path, sizeof(dir_path)-1);
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) *last_slash = '\0';
    else strcpy(dir_path, ".");

    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        if (is_image(dir->d_name)) {
            files = realloc(files, sizeof(char *) * (file_count + 1));
            char full[4096];
            snprintf(full, sizeof(full), "%s/%s", dir_path, dir->d_name);
            files[file_count++] = strdup(full);
        }
    }
    closedir(d);

    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i], path) == 0) {
            current_file_index = i;
            break;
        }
    }
}

void apply_zoom(float factor, int cx, int cy) {
    float old_zoom = current_img.zoom;
    current_img.zoom *= factor;
    if (current_img.zoom < 0.01) current_img.zoom = 0.01;
    if (current_img.zoom > 100.0) current_img.zoom = 100.0;

    current_img.offset_x = cx - (cx - current_img.offset_x) * (current_img.zoom / old_zoom);
    current_img.offset_y = cy - (cy - current_img.offset_y) * (current_img.zoom / old_zoom);
    draw_image();
}

int main(int argc, char *argv[]) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    screen = DefaultScreen(dpy);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 800, 600, 1,
                             get_color(COL_FG), get_color(COL_BG));
    
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask | PointerMotionMask);
    XMapWindow(dpy, win);
    gc = XCreateGC(dpy, win, 0, NULL);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);

    if (argc > 1) {
        scan_directory(argv[1]);
        load_image_file(argv[1]);
    } else {
        FILE *fp = popen("cygnus-open -view", "r");
        if (fp) {
            char path[4096];
            if (fgets(path, sizeof(path), fp)) {
                path[strcspn(path, "\n")] = '\0';
                scan_directory(path);
                load_image_file(path);
            }
            pclose(fp);
        }
    }

    if (!current_img.data) {
        fprintf(stderr, "No image loaded. Exiting.\n");
        XCloseDisplay(dpy);
        return 1;
    }

    XEvent ev;
    int mouse_x = 0, mouse_y = 0;

    while (1) {
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) {
            draw_image();
        } else if (ev.type == ConfigureNotify) {
            draw_image();
        } else if (ev.type == MotionNotify) {
            mouse_x = ev.xmotion.x;
            mouse_y = ev.xmotion.y;
        } else if (ev.type == ClientMessage) {
            if (ev.xclient.data.l[0] == wm_delete_window) break;
        } else if (ev.type == KeyPress) {
            KeySym sym = XLookupKeysym(&ev.xkey, 0);
            if (sym == XK_Escape || sym == XK_q) break;
            else if (sym == XK_Left) {
                if (file_count > 0) {
                    current_file_index = (current_file_index - 1 + file_count) % file_count;
                    load_image_file(files[current_file_index]);
                    draw_image();
                }
            } else if (sym == XK_Right) {
                if (file_count > 0) {
                    current_file_index = (current_file_index + 1) % file_count;
                    load_image_file(files[current_file_index]);
                    draw_image();
                }
            } else if (sym == XK_Up) {
                apply_zoom(1.1f, mouse_x, mouse_y);
            } else if (sym == XK_Down) {
                apply_zoom(0.9f, mouse_x, mouse_y);
            }
        }
    }

    if (current_img.data) stbi_image_free(current_img.data);
    XCloseDisplay(dpy);
    return 0;
}
