#include "gui_handler.h"
#include "shell_tab.h"
#include "line_editor.h"
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

#define WIN_W 1000
#define WIN_H 700

static Display *dpy;
static Window win;
static GC green_gc, white_gc, blue_gc;
static XFontStruct *font;
static unsigned long bgpixel;
static int width = WIN_W, height = WIN_H;
static int tabbar_h = 32;

int gui_init(void) {
    setlocale(LC_ALL, "");
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open X display\n");
        return -1;
    }
    int s = DefaultScreen(dpy);
    Window root = RootWindow(dpy, s);
    Colormap cm = DefaultColormap(dpy, s);
    XColor bg = {0}, green = {0}, blue = {0};
    bg.red = 12330;
    bg.green = 2570;
    bg.blue = 9256;
    green.red = 0;
    green.green = 65535;
    green.blue = 0;
    blue.red = 20000;
    blue.green = 40000;
    blue.blue = 65535;

    if (!XAllocColor(dpy, cm, &bg) || !XAllocColor(dpy, cm, &green) || !XAllocColor(dpy, cm, &blue)) {
        return -1;
    }

    bgpixel = bg.pixel;
    win = XCreateSimpleWindow(dpy, root, 100, 100, width, height, 0, bgpixel, bgpixel);
    XStoreName(dpy, win, "MyTerm - Custom X11 Terminal");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask | ButtonPressMask);

    green_gc = XCreateGC(dpy, win, 0, NULL);
    white_gc = XCreateGC(dpy, win, 0, NULL);
    blue_gc = XCreateGC(dpy, win, 0, NULL);

    XSetForeground(dpy, green_gc, green.pixel);
    XSetForeground(dpy, white_gc, WhitePixel(dpy, s));
    XSetForeground(dpy, blue_gc, blue.pixel);

    font = XLoadQueryFont(dpy, "10x20");
    if (!font) font = XLoadQueryFont(dpy, "9x18");
    if (!font) font = XLoadQueryFont(dpy, "fixed");
    if (!font) return -1;

    XSetFont(dpy, green_gc, font->fid);
    XSetFont(dpy, white_gc, font->fid);
    XSetFont(dpy, blue_gc, font->fid);

    XMapWindow(dpy, win);
    return 0;
}

void gui_shutdown(void) {
    if (!dpy) return;
    if (font) XFreeFont(dpy, font);
    XFreeGC(dpy, green_gc);
    XFreeGC(dpy, white_gc);
    XFreeGC(dpy, blue_gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    dpy = NULL;
}

Display *gui_display(void) {
    return dpy;
}

Window gui_window(void) {
    return win;
}

int gui_tab_at(int x, int y) {
    if (y >= tabbar_h) return -1;
    int n = shell_tabs_count();
    int tw = 100;
    int idx = x / tw;
    if (x >= n * tw && x < (n * tw + 45)) return -2;
    return idx < n ? idx : -1;
}

static void draw_text(GC gc, int x, int y, const char *s) {
    XDrawString(dpy, win, gc, x, y, s, (int)strlen(s));
}

void gui_redraw(void) {
    if (!dpy) return;
    XClearWindow(dpy, win);
    int n = shell_tabs_count(), active = shell_tabs_active();
    
    for (int i = 0; i < n; i++) {
        ShellTab *t = shell_tab_get(i);
        if (!t) continue;
        int x = i * 100;
        XDrawRectangle(dpy, win, green_gc, x, 0, 96, tabbar_h - 2);
        draw_text(i == active ? green_gc : white_gc, x + 8, 21, t->title);
    }
    
    draw_text(green_gc, n * 100 + 12, 21, "+  Ctrl+W: new tab");
    
    ShellTab *t = shell_tab_get(active);
    if (!t) {
        XFlush(dpy);
        return;
    }
    
    int lh = font->ascent + font->descent + 3;
    int y = tabbar_h + font->ascent + 8;
    int available = (height - tabbar_h - 50) / lh;
    
    char snapshot[OUTPUT_CAP];
    pthread_mutex_lock(&t->mutex);
    snprintf(snapshot, sizeof(snapshot), "%s", t->output);
    pthread_mutex_unlock(&t->mutex);
    
    char *lines[4000];
    int lc = 0;
    char *save = NULL;
    char *line = strtok_r(snapshot, "\n", &save);
    while (line && lc < 4000) {
        lines[lc++] = line;
        line = strtok_r(NULL, "\n", &save);
    }
    
    int start = lc > available ? lc - available : 0;
    for (int i = start; i < lc; i++) {
        draw_text(white_gc, 10, y, lines[i]);
        y += lh;
    }
    
    if (y > height - lh) y = height - lh;
    
    char cwd[4096];
    snprintf(cwd, sizeof(cwd), "%s", t->cwd);
    char *base = strrchr(cwd, '/');
    base = base ? base + 1 : cwd;
    if (!*base) base = "/";
    
    char prompt[256];
    snprintf(prompt, sizeof(prompt), "ayushman@terminator:");
    draw_text(green_gc, 10, y, prompt);
    int pw = XTextWidth(font, prompt, strlen(prompt));
    
    draw_text(blue_gc, 10 + pw, y, base);
    int bw = XTextWidth(font, base, strlen(base));
    
    draw_text(green_gc, 10 + pw + bw, y, "$ ");
    int sw = XTextWidth(font, "$ ", 2);
    
    draw_text(white_gc, 10 + pw + bw + sw, y, t->input);
    
    int before = t->cursor;
    char tmp[INPUT_CAP];
    snprintf(tmp, sizeof(tmp), "%s", t->input);
    tmp[before] = '\0';
    int cw = XTextWidth(font, tmp, before);
    int cx = 10 + pw + bw + sw + cw;
    
    XDrawLine(dpy, win, white_gc, cx, y - font->ascent, cx, y + font->descent);
    XFlush(dpy);
}

int gui_handle_event(XEvent *e) {
    if (e->type == Expose) {
        if (e->xexpose.count == 0) gui_redraw();
        return 1;
    }
    if (e->type == ConfigureNotify) {
        width = e->xconfigure.width;
        height = e->xconfigure.height;
        gui_redraw();
        return 1;
    }
    if (e->type == ButtonPress) {
        int idx = gui_tab_at(e->xbutton.x, e->xbutton.y);
        if (idx >= 0) shell_tabs_switch(idx);
        else if (idx == -2) shell_tabs_create();
        gui_redraw();
        return 1;
    }
    if (e->type == KeyPress) {
        KeySym k;
        XLookupString(&e->xkey, NULL, 0, &k, NULL);
        if ((e->xkey.state & ControlMask) && (k == XK_w || k == XK_W)) {
            shell_tabs_create();
            gui_redraw();
            return 1;
        }
        if (k == XK_F1) {
            shell_tabs_switch(0);
            gui_redraw();
            return 1;
        }
        if (k == XK_F2) {
            shell_tabs_switch(1);
            gui_redraw();
            return 1;
        }
        if (k == XK_F3) {
            shell_tabs_switch(2);
            gui_redraw();
            return 1;
        }
        ShellTab *t = shell_tab_get(shell_tabs_active());
        if (t) {
            int r = line_editor_handle_key(t, &e->xkey);
            if (r == 2) {
                int a = shell_tabs_active();
                shell_tabs_close(a);
                if (shell_tabs_count() == 0) return 0;
                shell_tabs_switch(0);
            }
            gui_redraw();
        }
        return 1;
    }
    if (e->type == DestroyNotify) return 0;
    return 1;
}