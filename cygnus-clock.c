/*
 * cygnus-clock
 * A simple digital clock applet for the Cygnus WM panel.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define COL_BG "#1a1a1a"
#define COL_FG "#ffffff"
#define SYSTEM_TRAY_REQUEST_DOCK 0

unsigned long get_color(Display *dpy, int screen, const char *hex) {
    XColor c;
    Colormap cm = DefaultColormap(dpy, screen);
    XParseColor(dpy, cm, hex, &c);
    XAllocColor(dpy, cm, &c);
    return c.pixel;
}

void dock(Display *dpy, Window win, int screen) {
    char atom_name[32];
    snprintf(atom_name, sizeof(atom_name), "_NET_SYSTEM_TRAY_S%d", screen);
    Atom tray_atom = XInternAtom(dpy, atom_name, False);
    Window tray_win = XGetSelectionOwner(dpy, tray_atom);

    if (tray_win != None) {
        XClientMessageEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = ClientMessage;
        ev.window = tray_win;
        ev.message_type = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
        ev.format = 32;
        ev.data.l[0] = CurrentTime;
        ev.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
        ev.data.l[2] = win;
        XSendEvent(dpy, tray_win, False, NoEventMask, (XEvent *)&ev);
        XSync(dpy, False);
    }
}

int main() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;

    int screen = DefaultScreen(dpy);
    unsigned long bg = get_color(dpy, screen, COL_BG);
    unsigned long fg = get_color(dpy, screen, COL_FG);

    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, 80, 24, 0, fg, bg);
    
    XClassHint *ch = XAllocClassHint();
    if (ch) {
        ch->res_name = "cygnus-clock";
        ch->res_class = "CygnusClock";
        XSetClassHint(dpy, win, ch);
        XFree(ch);
    }

    XSelectInput(dpy, win, ExposureMask);
    XMapWindow(dpy, win);

    dock(dpy, win, screen);

    GC gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, fg);
    XFontStruct *font = XLoadQueryFont(dpy, "fixed");
    if (font) XSetFont(dpy, gc, font->fid);

    while (1) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose && ev.xexpose.count == 0) {
            }
        }

        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        char buf[32];
        strftime(buf, sizeof(buf), "%I:%M %p", tm);

        XClearWindow(dpy, win);
        int tw = XTextWidth(font ? font : XLoadQueryFont(dpy, "fixed"), buf, strlen(buf));
        XDrawString(dpy, win, gc, (80 - tw) / 2, 16, buf, strlen(buf));
        XFlush(dpy);

        sleep(1);
    }

    XCloseDisplay(dpy);
    return 0;
}
