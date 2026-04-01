/*
 * cygnus-shot
 * Screenshot utility for Cygnus WM.
 *
 * by Jonathan Torres
 * 
 * This program is free software: you can redistribute it and/or modify it under the terms of the 
 * GNU General Public License as published by the Free Software Foundation, either version 3 of 
 * the License, or (at your option) any later version.
 * 
 */

#define _DEFAULT_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void save_ppm(const char *filename, int w, int h, XImage *img) {
    FILE *f = fopen(filename, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned long pixel = XGetPixel(img, x, y);
            unsigned char r = (pixel & img->red_mask) >> 16;
            unsigned char g = (pixel & img->green_mask) >> 8;
            unsigned char b = (pixel & img->blue_mask);
            fwrite(&r, 1, 1, f);
            fwrite(&g, 1, 1, f);
            fwrite(&b, 1, 1, f);
        }
    }
    fclose(f);
}

int main(int argc, char *argv[]) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    int rx = 0, ry = 0, rw = width, rh = height;

    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        //cover whole screen for selection
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        attrs.cursor = XCreateFontCursor(dpy, XC_crosshair);
        Window win = XCreateWindow(dpy, root, 0, 0, width, height, 0, 
                                 CopyFromParent, InputOutput, CopyFromParent, 
                                 CWOverrideRedirect | CWCursor, &attrs);
        XMapRaised(dpy, win);
        XGrabPointer(dpy, win, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

        //snapshot
        GC gc = XCreateGC(dpy, win, 0, NULL);
        XSetSubwindowMode(dpy, gc, IncludeInferiors);
        XCopyArea(dpy, root, win, gc, 0, 0, width, height, 0, 0);

        XEvent ev;
        int start_x, start_y, end_x, end_y;
        int active = 0;
        
        //draw selection box
        GC xor_gc = XCreateGC(dpy, win, 0, NULL);
        XSetFunction(dpy, xor_gc, GXxor);
        XSetForeground(dpy, xor_gc, WhitePixel(dpy, screen));

        while (1) {
            XNextEvent(dpy, &ev);
            if (ev.type == ButtonPress) {
                start_x = ev.xbutton.x;
                start_y = ev.xbutton.y;
                rx = start_x;
                ry = start_y;
                rw = 0;
                rh = 0;
                active = 1;
            } else if (ev.type == MotionNotify && active) {
                if (rw > 0 && rh > 0)
                    XDrawRectangle(dpy, win, xor_gc, rx, ry, rw, rh);
                
                end_x = ev.xmotion.x;
                end_y = ev.xmotion.y;
                rx = (start_x < end_x) ? start_x : end_x;
                ry = (start_y < end_y) ? start_y : end_y;
                rw = abs(start_x - end_x);
                rh = abs(start_y - end_y);

                if (rw > 0 && rh > 0)
                    XDrawRectangle(dpy, win, xor_gc, rx, ry, rw, rh);
            } else if (ev.type == ButtonRelease && active) {
                break;
            } else if (ev.type == KeyPress) {
                //any key to cancel
                XDestroyWindow(dpy, win);
                XCloseDisplay(dpy);
                return 0;
            }
        }
        XFreeGC(dpy, gc);
        XFreeGC(dpy, xor_gc);
        XDestroyWindow(dpy, win);
    }

    if (rw <= 0 || rh <= 0) {
        rw = width; rh = height; rx = 0; ry = 0;
    }

    usleep(100000);

    XImage *img = XGetImage(dpy, root, rx, ry, rw, rh, AllPlanes, ZPixmap);
    if (!img) {
        fprintf(stderr, "Failed to get image\n");
        return 1;
    }

    char filename[512];
    char *home = getenv("HOME");
    char path[256];
    snprintf(path, sizeof(path), "%s/Pictures", home ? home : ".");
    mkdir(path, 0755);

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    snprintf(filename, sizeof(filename), "%s/shot_%04d-%02d-%02d_%02d-%02d-%02d.ppm",
             path, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);

    save_ppm(filename, rw, rh, img);
    printf("Screenshot saved: %s\n", filename);

    XDestroyImage(img);
    XCloseDisplay(dpy);
    return 0;
}
