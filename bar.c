/*
 * bar.c - X11 taskbar/toolbar
 * Shows open windows as tabs and time on the right
 * Compile: gcc -o kbar bar.c -lX11
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

/* Base dimensions (unscaled) */
#define BASE_BAR_HEIGHT 24
#define BASE_PADDING 8
#define BASE_PADDING_TIGHT 4
#define BASE_TAB_MAX_WIDTH 150
#define BASE_TAB_MIN_WIDTH 75
#define BASE_TIME_WIDTH 70
#define BASE_BORDER 1

/* Scaled dimensions (computed at runtime) */
static int k_scale = 1;
static int bar_height;
static int padding;
static int padding_tight;
static int tab_max_width;
static int tab_min_width;
static int time_width;
static int border_width;
static int text_scale;

/* Colors */
#define COLOR_BG         0xffffff
#define COLOR_TEXT       0x000000
#define COLOR_TEXT_INV   0xffffff
#define COLOR_ACTIVE_BG  0x000000
#define COLOR_BORDER     0x000000

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

/* Drawing primitives */
static void bar_rect(int x, int y, int w, int h, uint32_t c) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < screen_width && py >= 0 && py < bar_height) {
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

/* Get root window name (for status text) */
static void get_root_name(char *name, size_t maxlen) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    name[0] = '\0';

    Window root = DefaultRootWindow(dpy);

    if (XGetWindowProperty(dpy, root, net_wm_name, 0, 256, False, utf8_string,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop) {
        strncpy(name, (char *)prop, maxlen - 1);
        name[maxlen - 1] = '\0';
        XFree(prop);
        return;
    }

    if (XGetWindowProperty(dpy, root, wm_name, 0, 256, False, XA_STRING,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop) {
        strncpy(name, (char *)prop, maxlen - 1);
        name[maxlen - 1] = '\0';
        XFree(prop);
    }
}

/* Get title from a single window (no recursion) */
static int get_title_from_window(Window win, char *title, size_t maxlen) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(dpy, win, net_wm_name, 0, 256, False, utf8_string,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop && nitems > 0) {
        strncpy(title, (char *)prop, maxlen - 1);
        title[maxlen - 1] = '\0';
        XFree(prop);
        return 1;
    }

    if (XGetWindowProperty(dpy, win, wm_name, 0, 256, False, XA_STRING,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop && nitems > 0) {
        strncpy(title, (char *)prop, maxlen - 1);
        title[maxlen - 1] = '\0';
        XFree(prop);
        return 1;
    }

    return 0;
}

/* Get window title - checks window and its children (for reparenting WMs) */
static void get_window_title(Window win, char *title, size_t maxlen) {
    title[0] = '\0';

    if (get_title_from_window(win, title, maxlen)) return;

    Window root_ret, parent_ret;
    Window *children = NULL;
    unsigned int nchildren = 0;

    if (XQueryTree(dpy, win, &root_ret, &parent_ret, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            if (get_title_from_window(children[i], title, maxlen)) {
                XFree(children);
                return;
            }
        }
        if (children) XFree(children);
    }
}

/* Get active window using XGetInputFocus, returning the top-level frame */
static Window get_active_window(void) {
    Window focus;
    int revert_to;
    XGetInputFocus(dpy, &focus, &revert_to);
    if (focus == None || focus == PointerRoot || focus == DefaultRootWindow(dpy)) {
        return None;
    }

    /* Walk up to find the direct child of root (the frame) */
    Window current = focus;
    Window parent, root_ret;
    Window *children;
    unsigned int nchildren;

    while (current != None && current != DefaultRootWindow(dpy)) {
        if (!XQueryTree(dpy, current, &root_ret, &parent, &children, &nchildren)) {
            break;
        }
        if (children) XFree(children);

        if (parent == DefaultRootWindow(dpy)) {
            return current;
        }
        current = parent;
    }

    return focus;
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

        if (windows[num_windows].title[0] == '\0') {
            strncpy(windows[num_windows].title, "(Untitled)", sizeof(windows[num_windows].title) - 1);
        }
        num_windows++;
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
    char status_str[256];

    get_root_name(status_str, sizeof(status_str));
    if (status_str[0] == '\0') {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        snprintf(status_str, sizeof(status_str), "%02d:%02d", tm->tm_hour, tm->tm_min);
    }

    /* Clear background */
    bar_rect(0, 0, screen_width, bar_height, COLOR_BG);

    /* Draw top border */
    bar_rect(0, 0, screen_width, border_width, COLOR_BORDER);

    /* Calculate tab width */
    int tabs_area = screen_width - time_width - padding;
    int tab_width = tab_max_width;

    if (num_windows > 0) {
        tab_width = (tabs_area - padding) / num_windows;
        if (tab_width > tab_max_width) tab_width = tab_max_width;
        if (tab_width < tab_min_width) tab_width = tab_min_width;
    }

    /* Draw tabs */
    int tab_x = padding;
    unsigned char *font = chicago;
    int text_y = (bar_height - 16 * text_scale) / 2;

    for (int i = 0; i < num_windows; i++) {
        if (tab_x + tab_width > tabs_area) break;

        char truncated[256];
        truncate_title(truncated, windows[i].title, tab_width - padding * 2, font, text_scale);

        if (windows[i].active) {
            /* Active: black background, white text, full height */
            bar_rect(tab_x, border_width, tab_width, bar_height - border_width, COLOR_ACTIVE_BG);
            bar_text(font, tab_x + padding, text_y, truncated, text_scale, COLOR_TEXT_INV);
        } else {
            /* Inactive: just text on white background */
            bar_text(font, tab_x + padding, text_y, truncated, text_scale, COLOR_TEXT);
        }

        tab_x += tab_width;
    }

    /* Draw status (root window name or time) */
    int status_text_w = text_width(font, status_str, text_scale);
    int status_text_x = screen_width - status_text_w - padding;
    bar_text(font, status_text_x, text_y, status_str, text_scale, COLOR_TEXT);

    /* Update display */
    XPutImage(dpy, bar_win, gc, img, 0, 0, 0, 0, screen_width, bar_height);
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
    if (attrs.y >= screen_height - bar_height) {
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
        new_h = (screen_height - bar_height) / 2;
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
    int tabs_area = screen_width - time_width - padding;
    int tab_width = tab_max_width;

    if (num_windows > 0) {
        tab_width = (tabs_area - padding) / num_windows;
        if (tab_width > tab_max_width) tab_width = tab_max_width;
        if (tab_width < tab_min_width) tab_width = tab_min_width;
    }

    int tab_x = padding;
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

        tab_x += tab_width;
    }
}

int64_t get_time() {
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  return time.tv_sec * 1000 + (time.tv_nsec / 1000000);
}

int main(void) {
    /* Read K_SCALE from environment (default 1) */
    char *scale_env = getenv("K_SCALE");
    if (scale_env) {
        k_scale = atoi(scale_env);
        if (k_scale < 1) k_scale = 1;
    }

    /* Initialize scaled dimensions */
    bar_height = BASE_BAR_HEIGHT * k_scale;
    padding = BASE_PADDING * k_scale;
    padding_tight = BASE_PADDING_TIGHT * k_scale;
    tab_max_width = BASE_TAB_MAX_WIDTH * k_scale;
    tab_min_width = BASE_TAB_MIN_WIDTH * k_scale;
    time_width = BASE_TIME_WIDTH * k_scale;
    border_width = BASE_BORDER * k_scale;
    text_scale = k_scale;

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
    bar_y = screen_height - bar_height;

    /* Initialize atoms */
    init_atoms();

    /* Allocate buffer */
    buf = malloc(screen_width * bar_height * sizeof(uint32_t));
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
                            0, bar_y, screen_width, bar_height,
                            0, CopyFromParent, InputOutput, CopyFromParent,
                            CWOverrideRedirect | CWEventMask, &attrs);

    /* Create GC and image */
    gc = XCreateGC(dpy, bar_win, 0, NULL);
    img = XCreateImage(dpy, DefaultVisual(dpy, screen), 24, ZPixmap, 0,
                       (char *)buf, screen_width, bar_height, 32, 0);

    /* Subscribe to root window events for screen size changes */
    XSelectInput(dpy, DefaultRootWindow(dpy), StructureNotifyMask);

    /* Ensure bar window receives ConfigureNotify to prevent moving */
    XSelectInput(dpy, bar_win, ExposureMask | ButtonPressMask | StructureNotifyMask);

    /* Map window */
    XMapWindow(dpy, bar_win);
    XRaiseWindow(dpy, bar_win);

    /* Initial draw */
    update_windows();
    draw_bar();

    /* Main loop */
    XEvent ev;
    int64_t last_time = 0;

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

            case ConfigureNotify:
                if (ev.xconfigure.window == bar_win) {
                    if (ev.xconfigure.x != 0 || ev.xconfigure.y != bar_y) {
                        XMoveWindow(dpy, bar_win, 0, bar_y);
                    }
                } else if (ev.xconfigure.window == DefaultRootWindow(dpy)) {
                    int new_width = ev.xconfigure.width;
                    int new_height = ev.xconfigure.height;
                    if (new_width != screen_width || new_height != screen_height) {
                        screen_width = new_width;
                        screen_height = new_height;
                        bar_y = screen_height - bar_height;

                        img->data = NULL;
                        XDestroyImage(img);
                        free(buf);

                        buf = malloc(screen_width * bar_height * sizeof(uint32_t));
                        img = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), 24, ZPixmap, 0,
                                           (char *)buf, screen_width, bar_height, 32, 0);

                        XMoveResizeWindow(dpy, bar_win, 0, bar_y, screen_width, bar_height);
                        draw_bar();
                    }
                }
                break;
            }
        }

        /* Update time every second and keep bar on top */
        int64_t now = get_time();
        if (now > last_time + 100) {
            last_time = now;

            /* Ensure bar stays in correct position */
            //XWindowAttributes attrs;
            //if (XGetWindowAttributes(dpy, bar_win, &attrs)) {
            //    if (attrs.x != 0 || attrs.y != bar_y) {
            //        XMoveWindow(dpy, bar_win, 0, bar_y);
            //    }
            //}

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
