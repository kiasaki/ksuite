#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>

static Atom wm_change_state;
static Atom wm_state;

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

typedef struct {
  Window win;
  int iconified;
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

static void add_client(Window w) {
  clients = realloc(clients, sizeof(Client) * (nclients + 1));
  clients[nclients].win = w;
  clients[nclients].iconified = 0;
  nclients++;
  current = nclients - 1;
  XSetWindowBorderWidth(dpy, w, 1);
  XSetWindowBorder(dpy, w, BlackPixel(dpy, screen));
  XSelectInput(dpy, w, EnterWindowMask);
  XGrabButton(dpy, 1, AnyModifier, w, True, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
  set_wm_state(w, NormalState);
}

static int find_client(Window w) {
  for (int i = 0; i < nclients; i++) {
    if (clients[i].win == w) return i;
  }
  return -1;
}

static void remove_client(Window w) {
  int i = find_client(w);
  if (i >= 0) {
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
  if (focused != root && focused != PointerRoot) {
    int mx = 0, my = 0, mw = DisplayWidth(dpy, screen), mh = DisplayHeight(dpy, screen);
    int nmonitors;
    XineramaScreenInfo *monitors = XineramaQueryScreens(dpy, &nmonitors);
    if (monitors) {
      Window child;
      int wx, wy;
      XTranslateCoordinates(dpy, focused, root, 0, 0, &wx, &wy, &child);
      for (int i = 0; i < nmonitors; i++) {
        if (wx >= monitors[i].x_org && wx < monitors[i].x_org + monitors[i].width &&
            wy >= monitors[i].y_org && wy < monitors[i].y_org + monitors[i].height) {
          mx = monitors[i].x_org;
          my = monitors[i].y_org;
          mw = monitors[i].width;
          mh = monitors[i].height;
          break;
        }
      }
      XFree(monitors);
    }
    XMoveResizeWindow(dpy, focused, mx, my, mw, mh);
  }
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
    XMapWindow(dpy, clients[current].win);
    clients[current].iconified = 0;
    set_wm_state(clients[current].win, NormalState);
  }
  XRaiseWindow(dpy, clients[current].win);
  XSetInputFocus(dpy, clients[current].win, RevertToPointerRoot, CurrentTime);
}

int main() {
  XEvent ev;
  XWindowAttributes attr;
  if (!(dpy = XOpenDisplay(NULL))) return 1;
  screen = DefaultScreen(dpy);
  root = RootWindow(dpy, screen);
  wm_change_state = XInternAtom(dpy, "WM_CHANGE_STATE", False);
  wm_state = XInternAtom(dpy, "WM_STATE", False);
  XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | StructureNotifyMask);
  XDefineCursor(dpy, root, XCreateFontCursor(dpy, XC_left_ptr));
  // grab events
  TBL(keys);
  XGrabButton(dpy, 1, Mod4Mask, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
              GrabModeAsync, GrabModeAsync, None, None);
  XGrabButton(dpy, 1, Mod4Mask | ShiftMask, root, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
              GrabModeAsync, GrabModeAsync, None, None);
  // scan existing windows
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
        XMapWindow(dpy, ev.xmaprequest.window);
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
    case ButtonPress:
        if (ev.xbutton.subwindow != None) {
          XGetWindowAttributes(dpy, ev.xbutton.subwindow, &attr);
          drag_start_x = ev.xbutton.x_root;
          drag_start_y = ev.xbutton.y_root;
          win_start_x = attr.x;
          win_start_y = attr.y;
          win_start_w = attr.width;
          win_start_h = attr.height;
          drag_win = ev.xbutton.subwindow;
          resizing = (ev.xbutton.state & ShiftMask) ? 1 : 0;
          XRaiseWindow(dpy, drag_win);
        } else if (ev.xbutton.window != root) {
          XRaiseWindow(dpy, ev.xbutton.window);
          XSetInputFocus(dpy, ev.xbutton.window, RevertToPointerRoot, CurrentTime);
          XAllowEvents(dpy, ReplayPointer, CurrentTime);
        }
        break;
    case MotionNotify:
      if (drag_win != None) {
        int xdiff = ev.xmotion.x_root - drag_start_x;
        int ydiff = ev.xmotion.y_root - drag_start_y;
        if (resizing) {
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
      break;
    case EnterNotify:
      if (ev.xcrossing.window != root) {
        XSetInputFocus(dpy, ev.xcrossing.window, RevertToPointerRoot, CurrentTime);
      }
      break;
    case ConfigureRequest: {
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
      break;
    }
    case ClientMessage:
      if (ev.xclient.message_type == wm_change_state &&
          ev.xclient.data.l[0] == IconicState) {
        int idx = find_client(ev.xclient.window);
        if (idx >= 0) {
          clients[idx].iconified = 1;
          set_wm_state(ev.xclient.window, IconicState);
        }
        XUnmapWindow(dpy, ev.xclient.window);
      }
      break;
    }
  }
  return 0;
}
