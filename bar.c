/*
 * bar.c - X11 taskbar/toolbar
 * Shows open windows as tabs and time on the right
 * Compile: gcc -o bar bar.c -lX11
 */

#define _DEFAULT_SOURCE 1
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "chicago12.h"

/* Bar configuration */
#define BAR_HEIGHT 56
#define TAB_PADDING 4
#define TAB_MAX_WIDTH 240
#define TAB_MIN_WIDTH 80
#define TIME_WIDTH 100
#define TEXT_SCALE 2

/* Colors */
#define COLOR_BG       0xffffff
#define COLOR_TAB      0xeeeeee
#define COLOR_TAB_SEL  0xcccccc
#define COLOR_TAB_ICON 0xdddddd
#define COLOR_TEXT     0x000000
#define COLOR_TIME_BG  0xeeeeee
#define COLOR_BORDER   0x000000

/* Max windows to track */
#define MAX_WINDOWS 64

typedef struct {
    Window win;
    char title[256];
    int active;
    int iconified;
} WinInfo;

/* Global state */
static Display *dpy;
static Window bar_win;
static GC gc;
static XImage *img;
static uint32_t *buf;
static int screen_width;
static int screen_height;
static int bar_y;
static WinInfo windows[MAX_WINDOWS];
static int num_windows = 0;
static Window active_window = None;

/* Atoms */
static Atom wm_name;
static Atom net_wm_name;
static Atom utf8_string;
static Atom wm_state;

/* Drawing primitives from fenster */
static void bar_rect(int x, int y, int w, int h, uint32_t c) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < screen_width && py >= 0 && py < BAR_HEIGHT) {
                buf[py * screen_width + px] = c;
            }
        }
    }
}

static void draw_icn(char *sprite, int x, int y, int scale, uint32_t color) {
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            if (*(sprite + dy) << dx & 0x80) {
                bar_rect(x + dx * scale, y + dy * scale, scale, scale, color);
            }
        }
    }
}

static void bar_text(unsigned char *font, int x, int y, char *s, int scale, uint32_t c) {
    while (*s) {
        char chr = *s++;
        int size = font[(unsigned char)chr];
        if (chr > 32) {
            char *sprite = (char *)&font[(unsigned char)chr * 8 * 4 + 256];
            draw_icn(sprite, x, y, scale, c);
            draw_icn(sprite + 8, x, y + 8 * scale, scale, c);
            if (size > 8) {
                draw_icn(sprite + 16, x + 8 * scale, y, scale, c);
                draw_icn(sprite + 24, x + 8 * scale, y + 8 * scale, scale, c);
            }
        }
        x = x + size * scale;
    }
}

/* Calculate text width */
static int text_width(unsigned char *font, char *s, int scale) {
    int w = 0;
    while (*s) {
        w += font[(unsigned char)*s++] * scale;
    }
    return w;
}

/* Initialize X11 atoms */
static void init_atoms(void) {
    wm_name = XInternAtom(dpy, "WM_NAME", False);
    net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
    utf8_string = XInternAtom(dpy, "UTF8_STRING", False);
    wm_state = XInternAtom(dpy, "WM_STATE", False);
}

/* Get WM_STATE of a window: 0=Withdrawn, 1=Normal, 3=Iconic, -1=no state */
static int get_wm_state(Window win) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    int state = -1;

    if (XGetWindowProperty(dpy, win, wm_state, 0, 2, False, wm_state,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop && nitems >= 1) {
        state = *(long *)prop;
        XFree(prop);
    }
    return state;
}

/* Get window title */
static void get_window_title(Window win, char *title, size_t maxlen) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    title[0] = '\0';

    /* Try _NET_WM_NAME first (UTF-8, used by most modern apps) */
    if (XGetWindowProperty(dpy, win, net_wm_name, 0, 256, False, utf8_string,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop) {
        strncpy(title, (char *)prop, maxlen - 1);
        title[maxlen - 1] = '\0';
        XFree(prop);
        return;
    }

    /* Fall back to WM_NAME */
    if (XGetWindowProperty(dpy, win, wm_name, 0, 256, False, XA_STRING,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop) {
        strncpy(title, (char *)prop, maxlen - 1);
        title[maxlen - 1] = '\0';
        XFree(prop);
    }
}

/* Get active window using XGetInputFocus */
static Window get_active_window(void) {
    Window focus;
    int revert_to;
    XGetInputFocus(dpy, &focus, &revert_to);
    if (focus != None && focus != PointerRoot && focus != DefaultRootWindow(dpy)) {
        return focus;
    }
    return None;
}

/* Update window list using XQueryTree */
static void update_windows(void) {
    Window root_return, parent_return;
    Window *children = NULL;
    unsigned int nchildren = 0;

    num_windows = 0;
    active_window = get_active_window();

    if (!XQueryTree(dpy, DefaultRootWindow(dpy), &root_return, &parent_return, &children, &nchildren)) {
        return;
    }

    for (unsigned int i = 0; i < nchildren && num_windows < MAX_WINDOWS; i++) {
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(dpy, children[i], &attrs)) continue;
        if (attrs.override_redirect) continue;

        int state = get_wm_state(children[i]);
        int dominated_by_wm_state = (state == 1 || state == 3);
        int is_viewable = (attrs.map_state == IsViewable);
        int is_iconic = (state == 3);

        if (dominated_by_wm_state) {
            if (state != 1 && state != 3) continue;
        } else {
            if (!is_viewable) continue;
        }

        windows[num_windows].win = children[i];
        get_window_title(children[i], windows[num_windows].title, sizeof(windows[num_windows].title));
        windows[num_windows].active = (children[i] == active_window);
        windows[num_windows].iconified = is_iconic;

        if (windows[num_windows].title[0] != '\0') {
            num_windows++;
        }
    }

    if (children) XFree(children);

    /* Sort by window ID for deterministic order */
    for (int i = 0; i < num_windows - 1; i++) {
        for (int j = i + 1; j < num_windows; j++) {
            if (windows[i].win > windows[j].win) {
                WinInfo tmp = windows[i];
                windows[i] = windows[j];
                windows[j] = tmp;
            }
        }
    }
}

/* Truncate title to fit width */
static void truncate_title(char *dest, const char *src, int max_width, unsigned char *font, int scale) {
    strcpy(dest, src);
    int w = text_width(font, dest, scale);

    if (w <= max_width) return;

    int len = strlen(dest);
    while (len > 3 && text_width(font, dest, scale) > max_width - text_width(font, "...", scale)) {
        dest[--len] = '\0';
    }
    strcat(dest, "...");
}

/* Draw the bar */
static void draw_bar(void) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_str[16];

    snprintf(time_str, sizeof(time_str), "%02d:%02d", tm->tm_hour, tm->tm_min);

    /* Clear background */
    bar_rect(0, 0, screen_width, BAR_HEIGHT, COLOR_BG);

    /* Draw top border */
    bar_rect(0, 0, screen_width, 1, COLOR_BORDER);

    /* Calculate tab width */
    int tabs_area = screen_width - TIME_WIDTH - TAB_PADDING;
    int tab_width = TAB_MAX_WIDTH;

    if (num_windows > 0) {
        tab_width = (tabs_area - TAB_PADDING) / num_windows - TAB_PADDING;
        if (tab_width > TAB_MAX_WIDTH) tab_width = TAB_MAX_WIDTH;
        if (tab_width < TAB_MIN_WIDTH) tab_width = TAB_MIN_WIDTH;
    }

    /* Draw tabs */
    int tab_x = TAB_PADDING;
    unsigned char *font = chicago;

    for (int i = 0; i < num_windows; i++) {
        if (tab_x + tab_width > tabs_area) break;

        /* Tab background */
        uint32_t tab_color = windows[i].active ? COLOR_TAB_SEL :
                             windows[i].iconified ? COLOR_TAB_ICON : COLOR_TAB;
        bar_rect(tab_x, TAB_PADDING, tab_width, BAR_HEIGHT - TAB_PADDING * 2, tab_color);

        /* Tab border */
        bar_rect(tab_x, TAB_PADDING, tab_width, 1, COLOR_BORDER);
        bar_rect(tab_x, BAR_HEIGHT - TAB_PADDING - 1, tab_width, 1, COLOR_BORDER);
        bar_rect(tab_x, TAB_PADDING, 1, BAR_HEIGHT - TAB_PADDING * 2, COLOR_BORDER);
        bar_rect(tab_x + tab_width - 1, TAB_PADDING, 1, BAR_HEIGHT - TAB_PADDING * 2, COLOR_BORDER);

        /* Tab title */
        char truncated[256];
        truncate_title(truncated, windows[i].title, tab_width - TAB_PADDING * 2, font, TEXT_SCALE);
        int text_y = (BAR_HEIGHT - 16 * TEXT_SCALE) / 2;
        bar_text(font, tab_x + TAB_PADDING, text_y, truncated, TEXT_SCALE, COLOR_TEXT);

        tab_x += tab_width + TAB_PADDING;
    }

    /* Draw time area */
    int time_x = screen_width - TIME_WIDTH;
    bar_rect(time_x, TAB_PADDING, TIME_WIDTH - TAB_PADDING, BAR_HEIGHT - TAB_PADDING * 2, COLOR_TIME_BG);

    /* Time border */
    bar_rect(time_x, TAB_PADDING, TIME_WIDTH - TAB_PADDING, 1, COLOR_BORDER);
    bar_rect(time_x, BAR_HEIGHT - TAB_PADDING - 1, TIME_WIDTH - TAB_PADDING, 1, COLOR_BORDER);
    bar_rect(time_x, TAB_PADDING, 1, BAR_HEIGHT - TAB_PADDING * 2, COLOR_BORDER);
    bar_rect(screen_width - TAB_PADDING - 1, TAB_PADDING, 1, BAR_HEIGHT - TAB_PADDING * 2, COLOR_BORDER);

    /* Draw time */
    int time_text_w = text_width(font, time_str, TEXT_SCALE);
    int time_text_x = time_x + (TIME_WIDTH - time_text_w) / 2;
    int time_text_y = (BAR_HEIGHT - 16 * TEXT_SCALE) / 2;
    bar_text(font, time_text_x, time_text_y, time_str, TEXT_SCALE, COLOR_TEXT);

    /* Update display */
    XPutImage(dpy, bar_win, gc, img, 0, 0, 0, 0, screen_width, BAR_HEIGHT);
    XFlush(dpy);
}

/* X error handler */
static int x_error_handler(Display *d, XErrorEvent *e) {
    (void)d;
    (void)e;
    return 0;
}

/* Activate a window */
static void activate_window(Window win) {
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(dpy, win, &attrs)) {
        return;
    }

    if (attrs.map_state != IsViewable) {
        XMapWindow(dpy, win);
    }

    /* Check if window is off-screen or too small to see, and move it into view */
    int new_x = attrs.x;
    int new_y = attrs.y;
    int new_w = attrs.width;
    int new_h = attrs.height;
    int needs_move = 0;
    int needs_resize = 0;

    /* Window completely off right edge */
    if (attrs.x >= screen_width) {
        new_x = screen_width / 4;
        needs_move = 1;
    }
    /* Window completely off left edge */
    else if (attrs.x + attrs.width <= 0) {
        new_x = screen_width / 4;
        needs_move = 1;
    }
    /* Window completely off bottom edge */
    if (attrs.y >= screen_height - BAR_HEIGHT) {
        new_y = 100;
        needs_move = 1;
    }
    /* Window completely off top edge */
    else if (attrs.y + attrs.height <= 0) {
        new_y = 100;
        needs_move = 1;
    }

    /* Window too small to see (some popups are 1x1 or 0x0) */
    if (attrs.width < 100 || attrs.height < 100) {
        new_w = screen_width / 2;
        new_h = (screen_height - BAR_HEIGHT) / 2;
        new_x = screen_width / 4;
        new_y = 100;
        needs_resize = 1;
        needs_move = 1;
    }

    if (needs_move || needs_resize) {
        if (needs_resize) {
            XMoveResizeWindow(dpy, win, new_x, new_y, new_w, new_h);
        } else {
            XMoveWindow(dpy, win, new_x, new_y);
        }
    }

    XRaiseWindow(dpy, win);
    XSetInputFocus(dpy, win, RevertToPointerRoot, CurrentTime);
    XFlush(dpy);
    XSync(dpy, False);
}

/* Handle click on bar */
static void handle_click(int x) {
    /* Calculate which tab was clicked */
    int tabs_area = screen_width - TIME_WIDTH - TAB_PADDING;
    int tab_width = TAB_MAX_WIDTH;

    if (num_windows > 0) {
        tab_width = (tabs_area - TAB_PADDING) / num_windows - TAB_PADDING;
        if (tab_width > TAB_MAX_WIDTH) tab_width = TAB_MAX_WIDTH;
        if (tab_width < TAB_MIN_WIDTH) tab_width = TAB_MIN_WIDTH;
    }

    int tab_x = TAB_PADDING;
    for (int i = 0; i < num_windows; i++) {
        if (tab_x + tab_width > tabs_area) break;

        if (x >= tab_x && x < tab_x + tab_width) {
            if (windows[i].win == active_window) {
                XIconifyWindow(dpy, windows[i].win, DefaultScreen(dpy));
            } else {
                activate_window(windows[i].win);
            }
            break;
        }

        tab_x += tab_width + TAB_PADDING;
    }
}

int main(void) {
    /* Open display */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open display\n");
        return 1;
    }

    /* Set error handler */
    XSetErrorHandler(x_error_handler);

    int screen = DefaultScreen(dpy);
    screen_width = DisplayWidth(dpy, screen);
    screen_height = DisplayHeight(dpy, screen);
    bar_y = screen_height - BAR_HEIGHT;

    /* Initialize atoms */
    init_atoms();

    /* Allocate buffer */
    buf = malloc(screen_width * BAR_HEIGHT * sizeof(uint32_t));
    if (!buf) {
        fprintf(stderr, "Cannot allocate buffer\n");
        XCloseDisplay(dpy);
        return 1;
    }

    /* Create window */
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;

    bar_win = XCreateWindow(dpy, DefaultRootWindow(dpy),
                            0, bar_y, screen_width, BAR_HEIGHT,
                            0, CopyFromParent, InputOutput, CopyFromParent,
                            CWOverrideRedirect | CWEventMask, &attrs);

    /* Create GC and image */
    gc = XCreateGC(dpy, bar_win, 0, NULL);
    img = XCreateImage(dpy, DefaultVisual(dpy, screen), 24, ZPixmap, 0,
                       (char *)buf, screen_width, BAR_HEIGHT, 32, 0);

    /* Map window */
    XMapWindow(dpy, bar_win);
    XRaiseWindow(dpy, bar_win);

    /* Initial draw */
    update_windows();
    draw_bar();

    /* Main loop */
    XEvent ev;
    time_t last_time = 0;

    while (1) {
        /* Check for events with timeout */
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);

            switch (ev.type) {
            case Expose:
                draw_bar();
                break;

            case ButtonPress:
                if (ev.xbutton.button == Button1) {
                    handle_click(ev.xbutton.x);
                }
                break;
            }
        }

        /* Update time every second and keep bar on top */
        time_t now = time(NULL);
        if (now != last_time) {
            last_time = now;
            XRaiseWindow(dpy, bar_win);
            update_windows();
            draw_bar();
        }

        /* Small sleep to avoid busy loop */
        usleep(50000);  /* 50ms */
    }

    /* Cleanup (unreachable) */
    XDestroyWindow(dpy, bar_win);
    XCloseDisplay(dpy);
    free(buf);

    return 0;
}
