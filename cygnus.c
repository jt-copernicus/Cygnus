/*
 * Cygnus Window Manager
 * A minimalistic window manager for X11 written in C.
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
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>

#define BORDER_WIDTH 2
#define COLOR_FOCUS "#3366ff"
#define COLOR_UNFOCUS "#000033"
#define PANEL_HEIGHT 24
#define COLOR_PANEL_BG "#1a1a1a"
#define COLOR_PANEL_FG "#ffffff"

Display *dpy;
Window root;
int screen;
int screen_width, screen_height;
unsigned long color_focus;
unsigned long color_unfocus;
unsigned long color_panel_bg;
unsigned long color_panel_fg;
Atom wm_protocols, wm_delete_window;
Atom net_system_tray_selection, net_system_tray_opcode, net_system_tray_orientation, manager_atom;
Cursor cursor;

int current_workspace = 0;
Window panel_win;
GC panel_gc;
XFontStruct *panel_font;

typedef struct KeyBinding {
    unsigned int mod;
    KeySym keysym;
    char *command;
    struct KeyBinding *next;
} KeyBinding;

KeyBinding *user_bindings = NULL;

typedef struct Client {
    Window window;
    int x, y, w, h;
    int is_maximized;
    int is_fullscreen;
    int workspace;
    struct Client *next;
} Client;

typedef struct TrayIcon {
    Window window;
    struct TrayIcon *next;
} TrayIcon;

TrayIcon *tray_head = NULL;
Client *head = NULL;

void setup();
void run();
void cleanup();
unsigned long get_color(const char *hex_color);
void grab_keys(Window w);
void grab_buttons(Client *c);
void on_map_request(XEvent *e);
void on_configure_request(XEvent *e);
void on_key_press(XEvent *e);
void on_button_press(XEvent *e);
void on_enter_notify(XEvent *e);
void on_unmap_notify(XEvent *e);
void on_destroy_notify(XEvent *e);
void on_expose(XEvent *e);
void on_client_message(XEvent *e);
void focus_client(Client *c);
void close_window(Window w);
void maximize_window(Window w);
void minimize_window(Window w);
void restore_window(Window w);
void toggle_fullscreen(Window w);
void cycle_windows();
void move_window(XEvent *e);
void spawn(const char *command);
void load_user_keys();
void add_user_binding(unsigned int mod, KeySym keysym, const char *command);
Client *get_client(Window w);
void add_client(Window w);
void remove_client(Window w);
int x_error_handler(Display *d, XErrorEvent *e);
void run_session_script();
void grab_key_wrapper(Display *d, KeyCode key, unsigned int mod, Window w);
void grab_button_wrapper(Display *d, unsigned int button, unsigned int mod, Window w);
void goto_workspace(int ws);
void draw_panel();
void add_tray_icon(Window w);
void remove_tray_icon(Window w);
void update_tray_layout();

typedef struct MenuItem {
    char label[64];
    char command[256];
    struct MenuItem *next;
} MenuItem;

MenuItem *menu_items = NULL;
Window menu_win = None;
int menu_width = 150;
int menu_item_height = 20;

void load_menu();
void show_menu(int x, int y);
void handle_menu_click(int x, int y);

Window runner_win = None;
char runner_buf[256];
int runner_ptr = 0;
void show_runner();
void handle_runner_key(XKeyEvent *ev);

int main() {
    setup();
    run_session_script();
    run();
    cleanup();
    return 0;
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
        fprintf(stderr, "cygnus: execl %s failed\n", command);
        exit(0);
    }
}

void setup() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cygnus: cannot open display\n");
        exit(1);
    }

    XSetErrorHandler(x_error_handler);

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    screen_width = DisplayWidth(dpy, screen);
    screen_height = DisplayHeight(dpy, screen);

    color_focus = get_color(COLOR_FOCUS);
    color_unfocus = get_color(COLOR_UNFOCUS);
    color_panel_bg = get_color(COLOR_PANEL_BG);
    color_panel_fg = get_color(COLOR_PANEL_FG);

    cursor = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cursor);

    wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    char tray_atom_name[32];
    snprintf(tray_atom_name, sizeof(tray_atom_name), "_NET_SYSTEM_TRAY_S%d", screen);
    net_system_tray_selection = XInternAtom(dpy, tray_atom_name, False);
    net_system_tray_opcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
    net_system_tray_orientation = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    manager_atom = XInternAtom(dpy, "MANAGER", False);

    XSetWindowAttributes p_attr;
    p_attr.override_redirect = True;
    p_attr.background_pixel = color_panel_bg;
    p_attr.event_mask = ExposureMask | ButtonPressMask;
    panel_win = XCreateWindow(dpy, root, 0, 0, screen_width, PANEL_HEIGHT, 0, DefaultDepth(dpy, screen), InputOutput, DefaultVisual(dpy, screen), CWOverrideRedirect | CWBackPixel | CWEventMask, &p_attr);

    panel_gc = XCreateGC(dpy, panel_win, 0, NULL);
    XSetForeground(dpy, panel_gc, color_panel_fg);
    XSetBackground(dpy, panel_gc, color_panel_bg);
    panel_font = XLoadQueryFont(dpy, "fixed");
    if (panel_font) XSetFont(dpy, panel_gc, panel_font->fid);

    Atom dock_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atom type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    XChangeProperty(dpy, panel_win, type_atom, XA_ATOM, 32, PropModeReplace, (unsigned char *)&dock_atom, 1);
    
    XMapWindow(dpy, panel_win);

    XSetSelectionOwner(dpy, net_system_tray_selection, panel_win, CurrentTime);
    if (XGetSelectionOwner(dpy, net_system_tray_selection) == panel_win) {
        XClientMessageEvent ev;
        ev.type = ClientMessage;
        ev.window = root;
        ev.message_type = manager_atom;
        ev.format = 32;
        ev.data.l[0] = CurrentTime;
        ev.data.l[1] = net_system_tray_selection;
        ev.data.l[2] = panel_win;
        XSendEvent(dpy, root, False, StructureNotifyMask, (XEvent *)&ev);

        long orientation = 0;
        XChangeProperty(dpy, panel_win, net_system_tray_orientation, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&orientation, 1);
    }

    signal(SIGCHLD, SIG_IGN);
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask | ButtonPressMask);

    grab_keys(root);

    load_user_keys();

    load_menu();
}

void goto_workspace(int ws) {
    if (ws == current_workspace) return;
    
    Client *c;
    for (c = head; c; c = c->next) {
        if (c->workspace == current_workspace) {
            XUnmapWindow(dpy, c->window);
        }
    }
    
    current_workspace = ws;

    for (c = head; c; c = c->next) {
        if (c->workspace == current_workspace) {
            XMapWindow(dpy, c->window);
        }
    }
    
    draw_panel();
    
    for (c = head; c; c = c->next) {
        if (c->workspace == current_workspace) {
            focus_client(c);
            break;
        }
    }
}

void draw_panel() {
    XClearWindow(dpy, panel_win);
    XSetForeground(dpy, panel_gc, color_panel_fg);

    for (int i = 0; i < 2; i++) {
        int x = 5 + (i * 30);
        int y = 2;
        int w = 20;
        int h = PANEL_HEIGHT - 4;
        
        if (i == current_workspace) {
            XSetForeground(dpy, panel_gc, color_focus);
            XFillRectangle(dpy, panel_win, panel_gc, x, y, w, h);
            XSetForeground(dpy, panel_gc, color_panel_fg);
        } else {
            XDrawRectangle(dpy, panel_win, panel_gc, x, y, w, h);
        }
        
        char label[2];
        snprintf(label, 2, "%d", i + 1);
        XDrawString(dpy, panel_win, panel_gc, x + 6, y + 14, label, 1);
    }

    int x_off = 70;
    Client *c;
    for (c = head; c; c = c->next) {
        XWMHints *hints = XGetWMHints(dpy, c->window);
        if (hints && (hints->initial_state == IconicState)) {
            char *name;
            if (XFetchName(dpy, c->window, &name)) {
                int len = strlen(name);
                if (len > 15) len = 15;
                XDrawString(dpy, panel_win, panel_gc, x_off, 16, name, len);
                XFree(name);
                x_off += 100;
            } else {
                XDrawString(dpy, panel_win, panel_gc, x_off, 16, "[Window]", 8);
                x_off += 100;
            }
        }
        if (hints) XFree(hints);
    }
    
    update_tray_layout();
}

void add_tray_icon(Window w) {
    TrayIcon *ti = malloc(sizeof(TrayIcon));
    ti->window = w;
    ti->next = tray_head;
    tray_head = ti;
    update_tray_layout();
}

void remove_tray_icon(Window w) {
    TrayIcon **curr = &tray_head;
    while (*curr) {
        if ((*curr)->window == w) {
            TrayIcon *temp = *curr;
            *curr = (*curr)->next;
            free(temp);
            update_tray_layout();
            return;
        }
        curr = &(*curr)->next;
    }
}

void update_tray_layout() {
    int x = screen_width - 5;
    TrayIcon *ti;
    for (ti = tray_head; ti; ti = ti->next) {
        XWindowAttributes attr;
        XGetWindowAttributes(dpy, ti->window, &attr);
        int w = attr.width > 0 ? attr.width : 24;
        x -= w;
        XMoveResizeWindow(dpy, ti->window, x, 0, w, PANEL_HEIGHT);
        XMapWindow(dpy, ti->window);
        x -= 5;
    }
}

void load_menu() {
    while (menu_items) {
        MenuItem *temp = menu_items;
        menu_items = menu_items->next;
        free(temp);
    }

    char path[1024];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof(path), "%s/.cygnus-wm/menu", home);
    
    FILE *f = fopen(path, "r");
    if (!f) return;
    
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        MenuItem *mi = malloc(sizeof(MenuItem));
        mi->next = NULL;
        
        if (strstr(line, "[exec]")) {
            char *label_start = strchr(line, '(');
            char *label_end = strchr(line, ')');
            char *cmd_start = strchr(line, '{');
            char *cmd_end = strchr(line, '}');
            
            if (label_start && label_end && cmd_start && cmd_end) {
                *label_end = '\0';
                *cmd_end = '\0';
                strncpy(mi->label, label_start + 1, 63);
                strncpy(mi->command, cmd_start + 1, 255);
            } else {
                free(mi);
                continue;
            }
        } else if (strstr(line, "[exit]")) {
            strcpy(mi->label, "Exit");
            strcpy(mi->command, "exit");
        } else if (strstr(line, "[restart]")) {
            strcpy(mi->label, "Restart");
            strcpy(mi->command, "restart");
        } else {
            free(mi);
            continue;
        }
        
        if (!menu_items) menu_items = mi;
        else {
            MenuItem *last = menu_items;
            while (last->next) last = last->next;
            last->next = mi;
        }
    }
    fclose(f);
}

void show_menu(int x, int y) {
    if (menu_win != None) XDestroyWindow(dpy, menu_win);
    
    int count = 0;
    MenuItem *mi;
    for (mi = menu_items; mi; mi = mi->next) count++;
    
    if (count == 0) return;
    
    int height = count * menu_item_height;
    
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixel = color_panel_bg;
    attr.border_pixel = color_focus;
    attr.event_mask = ExposureMask | ButtonPressMask | LeaveWindowMask;
    
    menu_win = XCreateWindow(dpy, root, x, y, menu_width, height, 1, DefaultDepth(dpy, screen), InputOutput, DefaultVisual(dpy, screen), CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &attr);
    XMapRaised(dpy, menu_win);
    XGrabPointer(dpy, root, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
}

void handle_menu_click(int x, int y) {
    int index = y / menu_item_height;
    MenuItem *mi = menu_items;
    while (mi && index > 0) {
        mi = mi->next;
        index--;
    }
    
    XUngrabPointer(dpy, CurrentTime);
    XDestroyWindow(dpy, menu_win);
    menu_win = None;
    
    if (mi) {
        if (strcmp(mi->command, "exit") == 0) exit(0);
        else if (strcmp(mi->command, "restart") == 0) {
            char *args[] = {"cygnus", NULL};
            execvp(args[0], args);
        } else if (strcmp(mi->command, "run") == 0) {
            show_runner();
        } else {
            spawn(mi->command);
        }
    }
}

void draw_menu() {
    if (menu_win == None) return;
    XClearWindow(dpy, menu_win);
    MenuItem *mi;
    int i = 0;
    for (mi = menu_items; mi; mi = mi->next) {
        XDrawString(dpy, menu_win, panel_gc, 5, (i + 1) * menu_item_height - 5, mi->label, strlen(mi->label));
        i++;
    }
}

void show_runner() {
    if (runner_win != None) return;
    
    runner_ptr = 0;
    runner_buf[0] = '\0';
    
    XSetWindowAttributes attr;
    attr.override_redirect = True;
    attr.background_pixel = color_panel_bg;
    attr.border_pixel = color_focus;
    attr.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | FocusChangeMask;
    
    runner_win = XCreateWindow(dpy, root, (screen_width - 300) / 2, 50, 300, 30, 1, DefaultDepth(dpy, screen), InputOutput, DefaultVisual(dpy, screen), CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &attr);
    XMapRaised(dpy, runner_win);
    XSetInputFocus(dpy, runner_win, RevertToPointerRoot, CurrentTime);
    XGrabPointer(dpy, root, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
}

void handle_runner_key(XKeyEvent *ev) {
    KeySym ks = XLookupKeysym(ev, 0);
    if (ks == XK_Return) {
        if (runner_ptr > 0) spawn(runner_buf);
        XUngrabPointer(dpy, CurrentTime);
        XDestroyWindow(dpy, runner_win);
        runner_win = None;
    } else if (ks == XK_Escape) {
        XUngrabPointer(dpy, CurrentTime);
        XDestroyWindow(dpy, runner_win);
        runner_win = None;
    } else if (ks == XK_BackSpace) {
        if (runner_ptr > 0) runner_buf[--runner_ptr] = '\0';
    } else {
        char buf[8];
        int len = XLookupString(ev, buf, sizeof(buf), NULL, NULL);
        if (len > 0 && runner_ptr + len < 255) {
            memcpy(runner_buf + runner_ptr, buf, len);
            runner_ptr += len;
            runner_buf[runner_ptr] = '\0';
        }
    }
    
    if (runner_win != None) {
        XClearWindow(dpy, runner_win);
        XDrawString(dpy, runner_win, panel_gc, 10, 20, "Run: ", 5);
        XDrawString(dpy, runner_win, panel_gc, 50, 20, runner_buf, runner_ptr);
    }
}

void grab_key_wrapper(Display *d, KeyCode key, unsigned int mod, Window w) {
    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, Mod2Mask|LockMask };
    int i;
    for (i = 0; i < 4; i++) {
        XGrabKey(d, key, mod | modifiers[i], w, True, GrabModeAsync, GrabModeAsync);
    }
}

void grab_button_wrapper(Display *d, unsigned int button, unsigned int mod, Window w) {
    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, Mod2Mask|LockMask };
    int i;
    for (i = 0; i < 4; i++) {
        XGrabButton(d, button, mod | modifiers[i], w, False, ButtonPressMask | ButtonMotionMask | ButtonReleaseMask,
                    GrabModeAsync, GrabModeAsync, None, None);
    }
}

void run_session_script() {
    char path[1024];
    const char *home = getenv("HOME");
    if (!home) return;

    snprintf(path, sizeof(path), "%s/.cygnus-wm/session", home);
    
    if (access(path, X_OK) == 0) {
        if (fork() == 0) {
            execl(path, path, NULL);
            exit(0);
        }
    }
}

void load_user_keys() {
    char path[1024];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof(path), "%s/.cygnus-wm/keys", home);
    
    FILE *f = fopen(path, "r");
    if (!f) return;
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char mod_str[64], key_str[64], cmd_str[128];
        if (sscanf(line, "%63s %63s %[^\n]", mod_str, key_str, cmd_str) == 3) {
            unsigned int mod = 0;
            if (strstr(mod_str, "Control") || strstr(mod_str, "Ctrl")) mod |= ControlMask;
            if (strstr(mod_str, "Alt") || strstr(mod_str, "Mod1")) mod |= Mod1Mask;
            if (strstr(mod_str, "Shift")) mod |= ShiftMask;
            if (strstr(mod_str, "Super") || strstr(mod_str, "Mod4")) mod |= Mod4Mask;
            
            KeySym ks = XStringToKeysym(key_str);
            if (ks != NoSymbol) {
                add_user_binding(mod, ks, cmd_str);
                grab_key_wrapper(dpy, XKeysymToKeycode(dpy, ks), mod, root);
            }
        }
    }
    fclose(f);
}

void add_user_binding(unsigned int mod, KeySym keysym, const char *command) {
    KeyBinding *kb = malloc(sizeof(KeyBinding));
    kb->mod = mod;
    kb->keysym = keysym;
    kb->command = strdup(command);
    kb->next = user_bindings;
    user_bindings = kb;
}

void run() {
    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case MapRequest: on_map_request(&ev); break;
            case ConfigureRequest: on_configure_request(&ev); break;
            case KeyPress: on_key_press(&ev); break;
            case ButtonPress: on_button_press(&ev); break;
            case EnterNotify: on_enter_notify(&ev); break;
            case UnmapNotify: on_unmap_notify(&ev); break;
            case DestroyNotify: on_destroy_notify(&ev); break;
            case Expose: on_expose(&ev); break;
            case ClientMessage: on_client_message(&ev); break;
            case FocusIn: {
                if (runner_win != None && ev.xfocus.window == runner_win) {
                } else {
                    XSetWindowBorder(dpy, ev.xfocus.window, color_focus);
                }
                break;
            }
            case FocusOut: {
                if (runner_win != None && ev.xfocus.window == runner_win) {
                    XUngrabPointer(dpy, CurrentTime);
                    XDestroyWindow(dpy, runner_win);
                    runner_win = None;
                } else {
                    XSetWindowBorder(dpy, ev.xfocus.window, color_unfocus);
                }
                break;
            }
        }
    }
}

void on_expose(XEvent *e) {
    if (e->xexpose.window == panel_win) {
        draw_panel();
    } else if (e->xexpose.window == menu_win) {
        draw_menu();
    } else if (e->xexpose.window == runner_win) {
        XClearWindow(dpy, runner_win);
        XDrawString(dpy, runner_win, panel_gc, 10, 20, "Run: ", 5);
        XDrawString(dpy, runner_win, panel_gc, 50, 20, runner_buf, runner_ptr);
    }
}

void on_client_message(XEvent *e) {
    XClientMessageEvent *ev = &e->xclient;
    if (ev->message_type == net_system_tray_opcode) {
        if (ev->data.l[1] == 0) {
            Window win = ev->data.l[2];
            XReparentWindow(dpy, win, panel_win, 0, 0);
            XSelectInput(dpy, win, StructureNotifyMask);
            add_tray_icon(win);
        }
    }
}

//key bindings
void grab_keys(Window w) {
    XUngrabKey(dpy, AnyKey, AnyModifier, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_q), Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_x), Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_n), Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_r), Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_q), ShiftMask | ControlMask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_F11), 0, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_Tab), Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_F4), Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_F5), 0, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_d), ControlMask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_Return), Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_f), Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_Left), ControlMask | Mod1Mask, w);
    grab_key_wrapper(dpy, XKeysymToKeycode(dpy, XK_Right), ControlMask | Mod1Mask, w);
}

//mouse window actions
void grab_buttons(Client *c) {
    grab_button_wrapper(dpy, Button1, Mod1Mask, c->window);
    grab_button_wrapper(dpy, Button1, ControlMask, c->window);
}


void on_map_request(XEvent *e) {
    XMapRequestEvent *ev = &e->xmaprequest;

    if (ev->window == panel_win) return;

    add_client(ev->window);
    Client *c = get_client(ev->window);

    XWindowAttributes attr;
    XGetWindowAttributes(dpy, ev->window, &attr);
    if (attr.y < PANEL_HEIGHT) {
        XMoveWindow(dpy, ev->window, attr.x, PANEL_HEIGHT);
    }

    //border width & color
    XSetWindowBorderWidth(dpy, ev->window, BORDER_WIDTH);
    XSetWindowBorder(dpy, ev->window, color_unfocus);

    XSelectInput(dpy, ev->window, FocusChangeMask | StructureNotifyMask | PropertyChangeMask | EnterWindowMask);

    grab_buttons(c);

    XMapWindow(dpy, ev->window);

    focus_client(c);
}

void on_configure_request(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges changes;
    changes.x = ev->x;
    changes.y = (ev->y < PANEL_HEIGHT) ? PANEL_HEIGHT : ev->y;
    changes.width = ev->width;
    changes.height = (ev->y < PANEL_HEIGHT) ? (ev->height - (PANEL_HEIGHT - ev->y)) : ev->height;
    if (changes.height < 20) changes.height = 20;

    changes.border_width = ev->border_width;
    changes.sibling = ev->above;
    changes.stack_mode = ev->detail;

    XConfigureWindow(dpy, ev->window, ev->value_mask, &changes);
}

void on_key_press(XEvent *e) {
    XKeyEvent *ev = &e->xkey;
    
    if (ev->window == runner_win) {
        handle_runner_key(ev);
        return;
    }

    KeySym keysym = XLookupKeysym(ev, 0);

    KeyBinding *kb;
    for (kb = user_bindings; kb; kb = kb->next) {
        unsigned int state = ev->state & (ControlMask | Mod1Mask | ShiftMask | Mod4Mask);
        if (kb->keysym == keysym && kb->mod == state) {
            if (strcmp(kb->command, "run") == 0) {
                show_runner();
            } else {
                spawn(kb->command);
            }
            return;
        }
    }

    //global actions
    if (keysym == XK_q && (ev->state & ShiftMask) && (ev->state & ControlMask)) {
        exit(0);
    } else if (keysym == XK_d && (ev->state & ControlMask)) {
        show_runner();
    } else if (keysym == XK_Return && (ev->state & Mod1Mask)) {
        spawn("x-terminal-emulator");
    } else if (keysym == XK_f && (ev->state & Mod1Mask)) {
        spawn("cygnus-fm");
    } else if (keysym == XK_Left && (ev->state & ControlMask) && (ev->state & Mod1Mask)) {
        goto_workspace(0);
    } else if (keysym == XK_Right && (ev->state & ControlMask) && (ev->state & Mod1Mask)) {
        goto_workspace(1);
    }

    Window focused;
    int revert_to;
    XGetInputFocus(dpy, &focused, &revert_to);
    

    if (focused == None || focused == root) return;

    if (keysym == XK_q && (ev->state & Mod1Mask)) {
        close_window(focused);
    } else if (keysym == XK_F4 && (ev->state & Mod1Mask)) {
        close_window(focused);
    } else if (keysym == XK_x && (ev->state & Mod1Mask)) {
        maximize_window(focused);
    } else if (keysym == XK_n && (ev->state & Mod1Mask)) {
        minimize_window(focused);
    } else if (keysym == XK_r && (ev->state & Mod1Mask)) {
        restore_window(focused);
    } else if (keysym == XK_F11) {
        toggle_fullscreen(focused);
    } else if (keysym == XK_Tab && (ev->state & Mod1Mask)) {
        cycle_windows();
    } else if (keysym == XK_F5) {
        Client *c;
        for (c = head; c; c = c->next) {
            XSetWindowBorder(dpy, c->window, (c->window == focused) ? color_focus : color_unfocus);
        }
    }
}

void on_button_press(XEvent *e) {
    XButtonEvent *ev = &e->xbutton;

    if (runner_win != None) {
        if (ev->window == runner_win) {
            return;
        } else {
            XWindowAttributes attr;
            XGetWindowAttributes(dpy, runner_win, &attr);
            if (ev->x_root >= attr.x && ev->x_root < attr.x + attr.width &&
                ev->y_root >= attr.y && ev->y_root < attr.y + attr.height) {
                return;
            } else {
                XUngrabPointer(dpy, CurrentTime);
                XDestroyWindow(dpy, runner_win);
                runner_win = None;
            }
        }
        return;
    }

    if (menu_win != None) {
        if (ev->window == menu_win) {
            handle_menu_click(ev->x, ev->y);
        } else {
            XWindowAttributes attr;
            XGetWindowAttributes(dpy, menu_win, &attr);
            if (ev->x_root >= attr.x && ev->x_root < attr.x + attr.width &&
                ev->y_root >= attr.y && ev->y_root < attr.y + attr.height) {
                handle_menu_click(ev->x_root - attr.x, ev->y_root - attr.y);
            } else {
                XUngrabPointer(dpy, CurrentTime);
                XDestroyWindow(dpy, menu_win);
                menu_win = None;
            }
        }
        return;
    }

    if (ev->window == root && ev->button == Button3) {
        show_menu(ev->x_root, ev->y_root);
        return;
    }

    if (ev->window == panel_win) {
        if (ev->x < 35) goto_workspace(0);
        else if (ev->x < 65) goto_workspace(1);
        else if (ev->x > 70 && ev->x < screen_width - 100) {
            int x_off = 70;
            Client *c;
            for (c = head; c; c = c->next) {
                XWMHints *hints = XGetWMHints(dpy, c->window);
                if (hints && (hints->initial_state == IconicState)) {
                    if (ev->x >= x_off && ev->x < x_off + 100) {
                        restore_window(c->window);
                        if (hints) XFree(hints);
                        return;
                    }
                    x_off += 100;
                }
                if (hints) XFree(hints);
            }
        }
        return;
    }

    Client *c = get_client(ev->window);
    if (!c && ev->subwindow) c = get_client(ev->subwindow);

    if (c) {
        focus_client(c);
        XRaiseWindow(dpy, c->window);

        if (ev->button == Button1 && (ev->state & Mod1Mask)) {
            XEvent notify;
            int start_x = ev->x_root;
            int start_y = ev->y_root;
            
            XWindowAttributes attr;
            XGetWindowAttributes(dpy, c->window, &attr);
            int start_win_x = attr.x;
            int start_win_y = attr.y;

            if (XGrabPointer(dpy, root, False, ButtonMotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime) != GrabSuccess)
                return;

            while (1) {
                XNextEvent(dpy, &notify);
                if (notify.type == MotionNotify) {
                    int dx = notify.xmotion.x_root - start_x;
                    int dy = notify.xmotion.y_root - start_y;
                    int ny = start_win_y + dy;
                    if (ny < PANEL_HEIGHT) ny = PANEL_HEIGHT;
                    XMoveWindow(dpy, c->window, start_win_x + dx, ny);
                } else if (notify.type == ButtonRelease) {
                    XUngrabPointer(dpy, CurrentTime);
                    break;
                }
            }
        } else if (ev->button == Button1 && (ev->state & ControlMask)) {
            XEvent notify;
            int start_x_root = ev->x_root;
            int start_y_root = ev->y_root;
            
            XWindowAttributes attr;
            XGetWindowAttributes(dpy, c->window, &attr);
            int ocx = attr.x;
            int ocy = attr.y;
            int ocw = attr.width;
            int och = attr.height;
            int horiz = (ev->x < attr.width / 2) ? 0 : 1;
            int vert = (ev->y < attr.height / 2) ? 0 : 1;

            if (XGrabPointer(dpy, root, False, ButtonMotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, cursor, CurrentTime) != GrabSuccess)
                return;

            while (1) {
                XNextEvent(dpy, &notify);
                if (notify.type == MotionNotify) {
                    int dx = notify.xmotion.x_root - start_x_root;
                    int dy = notify.xmotion.y_root - start_y_root;
                    
                    int nx = ocx, ny = ocy, nw = ocw, nh = och;
                    
                    if (horiz == 0) {
                        nx = ocx + dx;
                        nw = ocw - dx;
                    } else {
                        nw = ocw + dx;
                    }
                    
                    if (vert == 0) {
                        ny = ocy + dy;
                        nh = och - dy;
                        if (ny < PANEL_HEIGHT) {
                            nh = och - (PANEL_HEIGHT - ocy);
                            ny = PANEL_HEIGHT;
                        }
                    } else {
                        nh = och + dy;
                    }

                    if (nw < 20) {
                        if (horiz == 0) nx = ocx + ocw - 20;
                        nw = 20;
                    }
                    if (nh < 20) {
                        if (vert == 0) ny = ocy + och - 20;
                        nh = 20;
                    }

                    XMoveResizeWindow(dpy, c->window, nx, ny, nw, nh);
                } else if (notify.type == ButtonRelease) {
                    XUngrabPointer(dpy, CurrentTime);
                    break;
                }
            }
        }
    }
}

void on_enter_notify(XEvent *e) {
    XEnterWindowEvent *ev = &e->xcrossing;
    Client *c = get_client(ev->window);

    if (c) {
        focus_client(c);
    }
}

void on_unmap_notify(XEvent *e) {

    draw_panel();
}

void on_destroy_notify(XEvent *e) {
    remove_client(e->xdestroywindow.window);
    remove_tray_icon(e->xdestroywindow.window);
    draw_panel();
}


void close_window(Window w) {
    XEvent ev;
    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete_window;
    ev.xclient.data.l[1] = CurrentTime;

    Status s = XSendEvent(dpy, w, False, NoEventMask, &ev);
    if (s == 0) {
        XKillClient(dpy, w);
    }
}

void maximize_window(Window w) {
    Client *c = get_client(w);
    if (!c) return;
    
    if (c->is_maximized) {
        restore_window(w);
        return;
    }

    XWindowAttributes attr;
    XGetWindowAttributes(dpy, w, &attr);
    c->x = attr.x;
    c->y = attr.y;
    c->w = attr.width;
    c->h = attr.height;

    //maximize but keep panel
    XMoveResizeWindow(dpy, w, 0, PANEL_HEIGHT, screen_width - (2 * BORDER_WIDTH), screen_height - PANEL_HEIGHT - (2 * BORDER_WIDTH));
    c->is_maximized = 1;
}

void minimize_window(Window w) {
    Client *c = get_client(w);
    if (c && !c->is_maximized) {
        XWindowAttributes attr;
        XGetWindowAttributes(dpy, w, &attr);
        c->x = attr.x;
        c->y = attr.y;
        c->w = attr.width;
        c->h = attr.height;
    }
    //minimize
    XUnmapWindow(dpy, w);
    XWMHints *hints = XGetWMHints(dpy, w);
    if (!hints) hints = XAllocWMHints();
    hints->initial_state = IconicState;
    hints->flags |= StateHint;
    XSetWMHints(dpy, w, hints);
    XFree(hints);
    draw_panel();
}

void restore_window(Window w) {
    Client *c = get_client(w);
    if (!c) return;
    
    XMapWindow(dpy, w);
    XWMHints *hints = XGetWMHints(dpy, w);
    if (!hints) hints = XAllocWMHints();
    hints->initial_state = NormalState;
    hints->flags |= StateHint;
    XSetWMHints(dpy, w, hints);
    XFree(hints);

    XMoveResizeWindow(dpy, w, c->x, c->y, c->w, c->h);
    c->is_maximized = 0;
    
    focus_client(c);
    draw_panel();
}

void toggle_fullscreen(Window w) {
    Client *c = get_client(w);
    if (!c) return;
    
    if (c->is_fullscreen) {
        XSetWindowBorderWidth(dpy, w, BORDER_WIDTH);
        //restore (from maximize)
        restore_window(w);
        c->is_fullscreen = 0;
    } else {
        if (!c->is_maximized) {
            XWindowAttributes attr;
            XGetWindowAttributes(dpy, w, &attr);
            c->x = attr.x;
            c->y = attr.y;
            c->w = attr.width;
            c->h = attr.height;
        }

        int screen_w = DisplayWidth(dpy, screen);
        int screen_h = DisplayHeight(dpy, screen);
        
        XMoveResizeWindow(dpy, w, 0, 0, screen_w, screen_h);
        XSetWindowBorderWidth(dpy, w, 0);
        c->is_fullscreen = 1;
    }
}

void focus_client(Client *c) {
    if (!c) return;
    
    Client *curr;
    for (curr = head; curr; curr = curr->next) {
        if (curr != c) {
            XSetWindowBorder(dpy, curr->window, color_unfocus);
        }
    }
    
    XSetInputFocus(dpy, c->window, RevertToPointerRoot, CurrentTime);
    XSetWindowBorder(dpy, c->window, color_focus);
    
    XRaiseWindow(dpy, panel_win);
}

void cycle_windows() {
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    
    Client *c = get_client(focused);
    if (!c && head) {
        focus_client(head);
        XRaiseWindow(dpy, head->window);
        return;
    }
    
    if (c && c->next) {
        focus_client(c->next);
        XRaiseWindow(dpy, c->next->window);
    } else if (head) {
        focus_client(head);
        XRaiseWindow(dpy, head->window);
    }
}

void add_client(Window w) {
    if (get_client(w)) return;
    
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, w, &attr);

    Client *c = malloc(sizeof(Client));
    c->window = w;
    c->x = attr.x;
    c->y = attr.y;
    c->w = attr.width;
    c->h = attr.height;
    c->is_maximized = 0;
    c->is_fullscreen = 0;
    c->workspace = current_workspace;
    c->next = head;
    head = c;
    
    draw_panel();
}

void remove_client(Window w) {
    Client **curr = &head;
    while (*curr) {
        if ((*curr)->window == w) {
            Client *temp = *curr;
            *curr = (*curr)->next;
            free(temp);
            draw_panel();
            return;
        }
        curr = &(*curr)->next;
    }
}

Client *get_client(Window w) {
    Client *c;
    for (c = head; c; c = c->next) {
        if (c->window == w) return c;
    }
    return NULL;
}

int x_error_handler(Display *d, XErrorEvent *e) {
    return 0;
}

void cleanup() {
    XCloseDisplay(dpy);
}
