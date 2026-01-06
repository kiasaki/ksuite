#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>

#include "chicago12.h"

static Atom wm_change_state;
static Atom wm_state;
static Atom wm_name;
static Atom net_wm_name;
static Atom utf8_string;

static const char *ct[]  = {"st", NULL};
static const char *cw[]  = {"mychromium", NULL};
static const char *cs[]  = {"dmenu_run", NULL};
static const char *cy[] = {"amixer", "-q", "set", "Master", "toggle", NULL};
static const char *cu[] = {"amixer", "-q", "set", "Master", "5%-", "unmute", NULL};
static const char *ci[] = {"amixer", "-q", "set", "Master", "5%+", "unmute", NULL};
static const char *co[] = {"bri", "-", NULL};
static const char *cp[] = {"bri", "+", NULL};

#define stk(s)      XKeysymToKeycode(dpy, XStringToKeysym(s))
#define keys(k, _)  XGrabKey(dpy, stk(k), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
#define map(k, x)   if (ev.xkey.keycode == stk(k)) { x; }
#define TBL(x)  x("F1", XCloseDisplay(dpy);free(clients);exit(0)) \
  x("Tab", focus_next()) \
  x("q", kill_window()) \
  x("m", maximize_window()) \
  x("t", start(ct)) \
  x("w", start(cw)) \
  x("space", start(cs)) \
  x("y", start(cy)) \
  x("u", start(cu)) \
  x("i", start(ci)) \
  x("o", start(co)) \
  x("p", start(cp))

/* Scaling */
static int k_scale = 1;
#define BASE_TITLE_HEIGHT 20
#define BASE_BORDER 1
static int title_height;
static int border_width;

/* Colors */
#define COLOR_BG         0xffffff
#define COLOR_TEXT       0x000000
#define COLOR_BORDER     0x000000

typedef struct {
  Window win;
  Window frame;
  int iconified;
  GC gc;
  XImage *img;
  uint32_t *buf;
  int title_w;
} Client;

static Display *dpy;
static Window root;
static int screen;
static Client *clients = NULL;
static int nclients = 0;
static int current = -1;
static int drag_start_x, drag_start_y;
static int win_start_x, win_start_y;
static unsigned int win_start_w, win_start_h;
static Window drag_win = None;
static int resizing = 0;
static int drag_is_frame = 0;

static int x_error_occurred = 0;

static int x_error_handler(Display *d, XErrorEvent *e) {
  (void)d;
  (void)e;
  x_error_occurred = 1;
  return 0;
}

static void start(const char **cmd) {
  if (fork() == 0) {
    if (dpy) close(ConnectionNumber(dpy));
    setsid();
    execvp(cmd[0], (char **)cmd);
    _exit(0);
  }
}

static void set_wm_state(Window w, long state) {
  long data[2] = {state, None};
  XChangeProperty(dpy, w, wm_state, wm_state, 32, PropModeReplace,
                  (unsigned char *)data, 2);
}

/* Text rendering (same as bar.c) */
static void draw_rect(uint32_t *buf, int buf_w, int buf_h, int x, int y, int w, int h, uint32_t c) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < buf_w && py >= 0 && py < buf_h) {
                buf[py * buf_w + px] = c;
            }
        }
    }
}

static void draw_icn(uint32_t *buf, int buf_w, int buf_h, char *sprite, int x, int y, int scale, uint32_t color) {
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            if (*(sprite + dy) << dx & 0x80) {
                draw_rect(buf, buf_w, buf_h, x + dx * scale, y + dy * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(uint32_t *buf, int buf_w, int buf_h, unsigned char *font, int x, int y, char *s, int scale, uint32_t c) {
    while (*s) {
        char chr = *s++;
        int size = font[(unsigned char)chr];
        if (chr > 32) {
            char *sprite = (char *)&font[(unsigned char)chr * 8 * 4 + 256];
            draw_icn(buf, buf_w, buf_h, sprite, x, y, scale, c);
            draw_icn(buf, buf_w, buf_h, sprite + 8, x, y + 8 * scale, scale, c);
            if (size > 8) {
                draw_icn(buf, buf_w, buf_h, sprite + 16, x + 8 * scale, y, scale, c);
                draw_icn(buf, buf_w, buf_h, sprite + 24, x + 8 * scale, y + 8 * scale, scale, c);
            }
        }
        x = x + size * scale;
    }
}

static int text_width(unsigned char *font, char *s, int scale) {
    int w = 0;
    while (*s) {
        w += font[(unsigned char)*s++] * scale;
    }
    return w;
}

/* Get window title */
static void get_window_title(Window win, char *title, size_t maxlen) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    title[0] = '\0';

    if (XGetWindowProperty(dpy, win, net_wm_name, 0, 256, False, utf8_string,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop) {
        strncpy(title, (char *)prop, maxlen - 1);
        title[maxlen - 1] = '\0';
        XFree(prop);
        return;
    }

    if (XGetWindowProperty(dpy, win, wm_name, 0, 256, False, XA_STRING,
                           &actual_type, &actual_format, &nitems, &bytes_after,
                           &prop) == Success && prop) {
        strncpy(title, (char *)prop, maxlen - 1);
        title[maxlen - 1] = '\0';
        XFree(prop);
    }
}

static void draw_title_bar(Client *c, int width);

static void add_client(Window w) {
  XWindowAttributes attr;
  if (!XGetWindowAttributes(dpy, w, &attr)) return;

  int client_w = attr.width;
  int client_h = attr.height;
  int client_x = attr.x;
  int client_y = attr.y;

  int frame_w = client_w + 2 * border_width;
  int frame_h = client_h + title_height + 2 * border_width;

  Window frame = XCreateSimpleWindow(dpy, root,
    client_x, client_y, frame_w, frame_h,
    0, BlackPixel(dpy, screen), WhitePixel(dpy, screen));

  XSelectInput(dpy, frame, SubstructureRedirectMask | SubstructureNotifyMask |
    ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask);

  XReparentWindow(dpy, w, frame, border_width, title_height + border_width);
  XMapWindow(dpy, frame);
  XMapWindow(dpy, w);
  XSync(dpy, False);

  clients = realloc(clients, sizeof(Client) * (nclients + 1));
  clients[nclients].win = w;
  clients[nclients].frame = frame;
  clients[nclients].iconified = 0;

  clients[nclients].title_w = 0;
  clients[nclients].buf = NULL;
  clients[nclients].gc = XCreateGC(dpy, frame, 0, NULL);
  clients[nclients].img = NULL;

  nclients++;
  current = nclients - 1;

  XSetWindowBorderWidth(dpy, w, 0);
  XSelectInput(dpy, w, EnterWindowMask | PropertyChangeMask);
  XGrabButton(dpy, 1, AnyModifier, w, True, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
  set_wm_state(w, NormalState);
}

static void draw_title_bar(Client *c, int width) {
  if (width <= 0 || width > 10000) return;
  
  x_error_occurred = 0;
  XWindowAttributes attr;
  XGetWindowAttributes(dpy, c->frame, &attr);
  XSync(dpy, False);
  if (x_error_occurred) return;
  
  if (width != c->title_w) {
    if (c->img) {
      c->img->data = NULL;
      XDestroyImage(c->img);
      c->img = NULL;
    }
    if (c->buf) { free(c->buf); c->buf = NULL; }
    c->title_w = width;
    c->buf = malloc(width * title_height * sizeof(uint32_t));
    if (!c->buf) return;
    c->img = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen), ZPixmap, 0,
                          (char *)c->buf, width, title_height, 32, 0);
    if (!c->img) { free(c->buf); c->buf = NULL; return; }
  }
  if (!c->buf || !c->img) return;

  draw_rect(c->buf, width, title_height, 0, 0, width, title_height, COLOR_BG);

  draw_rect(c->buf, width, title_height, 0, 0, width, border_width, COLOR_BORDER);
  draw_rect(c->buf, width, title_height, 0, title_height - border_width, width, border_width, COLOR_BORDER);
  draw_rect(c->buf, width, title_height, 0, 0, border_width, title_height, COLOR_BORDER);
  draw_rect(c->buf, width, title_height, width - border_width, 0, border_width, title_height, COLOR_BORDER);

  char title[256];
  get_window_title(c->win, title, sizeof(title));

  int tw = text_width(chicago, title, k_scale);
  int tx = (width - tw) / 2;
  int ty = (title_height - 16 * k_scale) / 2;
  if (tx < border_width) tx = border_width;

  draw_text(c->buf, width, title_height, chicago, tx, ty, title, k_scale, COLOR_TEXT);

  x_error_occurred = 0;
  XPutImage(dpy, c->frame, c->gc, c->img, 0, 0, 0, 0, width, title_height);
  XSync(dpy, False);
}

static int find_client(Window w) {
  for (int i = 0; i < nclients; i++) {
    if (clients[i].win == w) return i;
  }
  return -1;
}

static int find_client_by_frame(Window f) {
  for (int i = 0; i < nclients; i++) {
    if (clients[i].frame == f) return i;
  }
  return -1;
}

static void remove_client(Window w) {
  int i = find_client(w);
  if (i >= 0) {
    if (clients[i].img) {
      clients[i].img->data = NULL;
      XDestroyImage(clients[i].img);
    }
    if (clients[i].gc) XFreeGC(dpy, clients[i].gc);
    if (clients[i].buf) free(clients[i].buf);
    XDestroyWindow(dpy, clients[i].frame);
    XSync(dpy, False);
    for (int j = i; j < nclients - 1; j++)
      clients[j] = clients[j + 1];
    nclients--;
    if (current >= nclients) current = nclients - 1;
  }
}

static void maximize_window() {
  Window focused;
  int revert;
  XGetInputFocus(dpy, &focused, &revert);
  int idx = find_client(focused);
  if (idx < 0) return;

  int mx = 0, my = 0, mw = DisplayWidth(dpy, screen), mh = DisplayHeight(dpy, screen);
  int nmonitors;
  XineramaScreenInfo *monitors = XineramaQueryScreens(dpy, &nmonitors);
  if (monitors) {
    XWindowAttributes attr;
    if (XGetWindowAttributes(dpy, clients[idx].frame, &attr)) {
      for (int i = 0; i < nmonitors; i++) {
        if (attr.x >= monitors[i].x_org && attr.x < monitors[i].x_org + monitors[i].width &&
            attr.y >= monitors[i].y_org && attr.y < monitors[i].y_org + monitors[i].height) {
          mx = monitors[i].x_org;
          my = monitors[i].y_org;
          mw = monitors[i].width;
          mh = monitors[i].height;
          break;
        }
      }
    }
    XFree(monitors);
  }

  int frame_w = mw;
  int frame_h = mh;
  int client_w = frame_w - 2 * border_width;
  int client_h = frame_h - title_height - 2 * border_width;

  XMoveResizeWindow(dpy, clients[idx].frame, mx, my, frame_w, frame_h);
  XResizeWindow(dpy, clients[idx].win, client_w, client_h);
  draw_title_bar(&clients[idx], frame_w);
}

static void kill_window() {
  Window focused;
  int revert;
  XGetInputFocus(dpy, &focused, &revert);
  if (focused != root && focused != PointerRoot)
    XKillClient(dpy, focused);
}

static void focus_next() {
  if (nclients == 0) return;
  current = (current + 1) % nclients;
  if (clients[current].iconified) {
    XMapWindow(dpy, clients[current].frame);
    clients[current].iconified = 0;
    set_wm_state(clients[current].win, NormalState);
  }
  XRaiseWindow(dpy, clients[current].frame);
  XSetInputFocus(dpy, clients[current].win, RevertToPointerRoot, CurrentTime);
}

int main() {
  XEvent ev;
  XWindowAttributes attr;

  char *scale_env = getenv("K_SCALE");
  if (scale_env) {
    k_scale = atoi(scale_env);
    if (k_scale < 1) k_scale = 1;
  }
  title_height = BASE_TITLE_HEIGHT * k_scale;
  border_width = BASE_BORDER * k_scale;

  if (!(dpy = XOpenDisplay(NULL))) return 1;
  XSetErrorHandler(x_error_handler);
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);

  wm_change_state = XInternAtom(dpy, "WM_CHANGE_STATE", False);
  wm_state = XInternAtom(dpy, "WM_STATE", False);
  wm_name = XInternAtom(dpy, "WM_NAME", False);
  net_wm_name = XInternAtom(dpy, "_NET_WM_NAME", False);
  utf8_string = XInternAtom(dpy, "UTF8_STRING", False);

  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);
  XDefineCursor(dpy, root, XCreateFontCursor(dpy, XC_left_ptr));

  TBL(keys);
  XGrabButton(dpy, 1, Mod4Mask, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
              GrabModeAsync, GrabModeAsync, None, None);
  XGrabButton(dpy, 1, Mod4Mask | ShiftMask, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
              GrabModeAsync, GrabModeAsync, None, None);

  Window root_ret, parent_ret;
  Window *children;
  unsigned int nchildren;
  if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
    for (unsigned int i = 0; i < nchildren; i++) {
      XWindowAttributes attr;
      if (XGetWindowAttributes(dpy, children[i], &attr)
          && attr.map_state == IsViewable && !attr.override_redirect) {
        add_client(children[i]);
      }
    }
    if (children) XFree(children);
  }
  
  while (1) {
    XNextEvent(dpy, &ev);
    switch (ev.type) {
    case MapRequest:
        add_client(ev.xmaprequest.window);
        XSetInputFocus(dpy, ev.xmaprequest.window, RevertToPointerRoot, CurrentTime);
        break;
    case DestroyNotify:
        remove_client(ev.xdestroywindow.window);
        break;
    case UnmapNotify: {
        int idx = find_client(ev.xunmap.window);
        if (idx >= 0 && !clients[idx].iconified) {
          remove_client(ev.xunmap.window);
        }
        break;
    }
    case KeyPress:
        TBL(map);
        break;
    case Expose: {
        int idx = find_client_by_frame(ev.xexpose.window);
        if (idx >= 0 && ev.xexpose.count == 0) {
          XWindowAttributes attr;
          if (XGetWindowAttributes(dpy, clients[idx].frame, &attr)) {
            draw_title_bar(&clients[idx], attr.width);
          }
        }
        break;
    }
    case PropertyNotify: {
        if (ev.xproperty.atom == wm_name || ev.xproperty.atom == net_wm_name) {
          int idx = find_client(ev.xproperty.window);
          if (idx >= 0) {
            XWindowAttributes attr;
            if (XGetWindowAttributes(dpy, clients[idx].frame, &attr)) {
              draw_title_bar(&clients[idx], attr.width);
            }
          }
        }
        break;
    }
    case ButtonPress: {
        int frame_idx = find_client_by_frame(ev.xbutton.window);
        if (frame_idx >= 0) {
          XGetWindowAttributes(dpy, clients[frame_idx].frame, &attr);
          drag_start_x = ev.xbutton.x_root;
          drag_start_y = ev.xbutton.y_root;
          win_start_x = attr.x;
          win_start_y = attr.y;
          win_start_w = attr.width;
          win_start_h = attr.height;
          drag_win = clients[frame_idx].frame;
          drag_is_frame = 1;
          resizing = (ev.xbutton.y > (int)title_height) ? 0 : 0;
          XRaiseWindow(dpy, drag_win);
          XSetInputFocus(dpy, clients[frame_idx].win, RevertToPointerRoot, CurrentTime);
        } else if (ev.xbutton.subwindow != None) {
          int idx = find_client_by_frame(ev.xbutton.subwindow);
          if (idx >= 0) {
            XGetWindowAttributes(dpy, clients[idx].frame, &attr);
          } else {
            XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
          }
          if (attr.override_redirect) {
            break;
          }
          drag_start_x = ev.xbutton.x_root;
          drag_start_y = ev.xbutton.y_root;
          win_start_x = attr.x;
          win_start_y = attr.y;
          win_start_w = attr.width;
          win_start_h = attr.height;
          drag_win = (idx >= 0) ? clients[idx].frame : ev.xbutton.subwindow;
          drag_is_frame = (idx >= 0);
          resizing = (ev.xbutton.state & ShiftMask) ? 1 : 0;
          XRaiseWindow(dpy, drag_win);
        } else if (ev.xbutton.window != root) {
          int idx = find_client(ev.xbutton.window);
          if (idx >= 0) {
            XRaiseWindow(dpy, clients[idx].frame);
            XSetInputFocus(dpy, clients[idx].win, RevertToPointerRoot, CurrentTime);
          } else {
            XRaiseWindow(dpy, ev.xbutton.window);
            XSetInputFocus(dpy, ev.xbutton.window, RevertToPointerRoot, CurrentTime);
          }
          XAllowEvents(dpy, ReplayPointer, CurrentTime);
        }
        break;
    }
    case MotionNotify:
      if (drag_win != None) {
        int xdiff = ev.xmotion.x_root - drag_start_x;
        int ydiff = ev.xmotion.y_root - drag_start_y;
        if (resizing && drag_is_frame) {
          int neww = win_start_w + xdiff;
          int newh = win_start_h + ydiff;
          if (neww > 50 && newh > title_height + 20) {
            XResizeWindow(dpy, drag_win, neww, newh);
            int idx = find_client_by_frame(drag_win);
            if (idx >= 0) {
              int cw = neww - 2 * border_width;
              int ch = newh - title_height - 2 * border_width;
              if (cw > 0 && ch > 0) {
                XResizeWindow(dpy, clients[idx].win, cw, ch);
              }
              draw_title_bar(&clients[idx], neww);
            }
          }
        } else if (resizing) {
          int neww = win_start_w + xdiff;
          int newh = win_start_h + ydiff;
          if (neww > 10 && newh > 10)
            XResizeWindow(dpy, drag_win, neww, newh);
        } else {
          XMoveWindow(dpy, drag_win, win_start_x + xdiff, win_start_y + ydiff);
        }
      }
      break;
    case ButtonRelease:
      drag_win = None;
      drag_is_frame = 0;
      break;
    case EnterNotify:
      if (ev.xcrossing.window != root) {
        int idx = find_client(ev.xcrossing.window);
        if (idx >= 0) {
          XSetInputFocus(dpy, clients[idx].win, RevertToPointerRoot, CurrentTime);
        } else {
          XSetInputFocus(dpy, ev.xcrossing.window, RevertToPointerRoot, CurrentTime);
        }
      }
      break;
    case ConfigureRequest: {
      int idx = find_client(ev.xconfigurerequest.window);
      if (idx >= 0) {
        int new_w = ev.xconfigurerequest.width;
        int new_h = ev.xconfigurerequest.height;
        int frame_w = new_w + 2 * border_width;
        int frame_h = new_h + title_height + 2 * border_width;

        if (ev.xconfigurerequest.value_mask & (CWWidth | CWHeight)) {
          XResizeWindow(dpy, clients[idx].win, new_w, new_h);
          XResizeWindow(dpy, clients[idx].frame, frame_w, frame_h);
          draw_title_bar(&clients[idx], frame_w);
        }
        if (ev.xconfigurerequest.value_mask & (CWX | CWY)) {
          XMoveWindow(dpy, clients[idx].frame, ev.xconfigurerequest.x, ev.xconfigurerequest.y);
        }
      } else {
        XWindowChanges changes;
        changes.x = ev.xconfigurerequest.x;
        changes.y = ev.xconfigurerequest.y;
        changes.width = ev.xconfigurerequest.width;
        changes.height = ev.xconfigurerequest.height;
        changes.border_width = ev.xconfigurerequest.border_width;
        changes.sibling = ev.xconfigurerequest.above;
        changes.stack_mode = ev.xconfigurerequest.detail;
        XConfigureWindow(dpy, ev.xconfigurerequest.window,
                         ev.xconfigurerequest.value_mask, &changes);
      }
      break;
    }
    case ClientMessage:
      if (ev.xclient.message_type == wm_change_state &&
          ev.xclient.data.l[0] == IconicState) {
        int idx = find_client(ev.xclient.window);
        if (idx >= 0) {
          clients[idx].iconified = 1;
          set_wm_state(ev.xclient.window, IconicState);
          XUnmapWindow(dpy, clients[idx].frame);
        }
      }
      break;
    }
  }
  return 0;
}
