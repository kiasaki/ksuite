#ifndef FENSTER_H
#define FENSTER_H

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <objc/NSObjCRuntime.h>
#include <objc/objc-runtime.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#define _DEFAULT_SOURCE 1
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include <time.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct fenster {
  const char *title;
  bool size_changed;
  bool dirty; /* set to true when buffer needs to be redrawn to screen */
  int width;
  int height;
  uint32_t *buf;
  int keys[256]; /* keys are mostly ASCII, but arrows are 17..20 */
  int mod;       /* mod is 4 bits mask, ctrl=1, shift=2, alt=4, meta=8 */
  int x;
  int y;
  int mouse;
  int scroll; /* scroll wheel: -1 down, 0 none, +1 up (X11 only for now) */
#if defined(__APPLE__)
  id wnd;
#elif defined(_WIN32)
  HWND hwnd;
#else
  Display *dpy;
  Window w;
  GC gc;
  XImage *img;
  XShmSegmentInfo shminfo;
  bool use_shm;
#endif
};

#ifndef FENSTER_API
#define FENSTER_API extern
#endif
FENSTER_API int fenster_open(struct fenster *f);
FENSTER_API int fenster_loop(struct fenster *f);
FENSTER_API void fenster_close(struct fenster *f);
FENSTER_API void fenster_sleep(int64_t ms);
FENSTER_API int64_t fenster_time(void);
#define fenster_pixel(f, x, y) ((f)->buf[((y) * (f)->width) + (x)])

#ifndef FENSTER_HEADER
#if defined(__APPLE__)
#define msg(r, o, s) ((r(*)(id, SEL))objc_msgSend)(o, sel_getUid(s))
#define msg1(r, o, s, A, a)                                                    \
  ((r(*)(id, SEL, A))objc_msgSend)(o, sel_getUid(s), a)
#define msg2(r, o, s, A, a, B, b)                                              \
  ((r(*)(id, SEL, A, B))objc_msgSend)(o, sel_getUid(s), a, b)
#define msg3(r, o, s, A, a, B, b, C, c)                                        \
  ((r(*)(id, SEL, A, B, C))objc_msgSend)(o, sel_getUid(s), a, b, c)
#define msg4(r, o, s, A, a, B, b, C, c, D, d)                                  \
  ((r(*)(id, SEL, A, B, C, D))objc_msgSend)(o, sel_getUid(s), a, b, c, d)

#define cls(x) ((id)objc_getClass(x))

extern id const NSDefaultRunLoopMode;
extern id const NSApp;

static void fenster_draw_rect(id v, SEL s, CGRect r) {
  (void)r, (void)s;
  struct fenster *f = (struct fenster *)objc_getAssociatedObject(v, "fenster");
  CGContextRef context =
      msg(CGContextRef, msg(id, cls("NSGraphicsContext"), "currentContext"),
          "graphicsPort");
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGDataProviderRef provider = CGDataProviderCreateWithData(
      NULL, f->buf, f->width * f->height * 4, NULL);
  CGImageRef img =
      CGImageCreate(f->width, f->height, 8, 32, f->width * 4, space,
                    kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little,
                    provider, NULL, false, kCGRenderingIntentDefault);
  CGColorSpaceRelease(space);
  CGDataProviderRelease(provider);
  CGContextDrawImage(context, CGRectMake(0, 0, f->width, f->height), img);
  CGImageRelease(img);
}

static BOOL fenster_should_close(id v, SEL s, id w) {
  (void)v, (void)s, (void)w;
  msg1(void, NSApp, "terminate:", id, NSApp);
  return YES;
}

FENSTER_API int fenster_open(struct fenster *f) {
  msg(id, cls("NSApplication"), "sharedApplication");
  msg1(void, NSApp, "setActivationPolicy:", NSInteger, 0);
  f->wnd = msg4(id, msg(id, cls("NSWindow"), "alloc"),
                "initWithContentRect:styleMask:backing:defer:", CGRect,
                CGRectMake(0, 0, f->width, f->height), NSUInteger, 3,
                NSUInteger, 2, BOOL, NO);
  Class windelegate =
      objc_allocateClassPair((Class)cls("NSObject"), "FensterDelegate", 0);
  class_addMethod(windelegate, sel_getUid("windowShouldClose:"),
                  (IMP)fenster_should_close, "c@:@");
  objc_registerClassPair(windelegate);
  msg1(void, f->wnd, "setDelegate:", id,
       msg(id, msg(id, (id)windelegate, "alloc"), "init"));
  Class c = objc_allocateClassPair((Class)cls("NSView"), "FensterView", 0);
  class_addMethod(c, sel_getUid("drawRect:"), (IMP)fenster_draw_rect, "i@:@@");
  objc_registerClassPair(c);

  id v = msg(id, msg(id, (id)c, "alloc"), "init");
  msg1(void, f->wnd, "setContentView:", id, v);
  objc_setAssociatedObject(v, "fenster", (id)f, OBJC_ASSOCIATION_ASSIGN);

  id title = msg1(id, cls("NSString"), "stringWithUTF8String:", const char *,
                  f->title);
  msg1(void, f->wnd, "setTitle:", id, title);
  msg1(void, f->wnd, "makeKeyAndOrderFront:", id, nil);
  msg(void, f->wnd, "center");
  msg1(void, NSApp, "activateIgnoringOtherApps:", BOOL, YES);
  return 0;
}

FENSTER_API void fenster_close(struct fenster *f) {
  msg(void, f->wnd, "close");
}

// clang-format off
static const uint8_t FENSTER_KEYCODES[128] = {65,83,68,70,72,71,90,88,67,86,0,66,81,87,69,82,89,84,49,50,51,52,54,53,61,57,55,45,56,48,93,79,85,91,73,80,10,76,74,39,75,59,92,44,47,78,77,46,9,32,96,8,0,27,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,26,2,3,127,0,5,0,4,0,20,19,18,17,0};
// clang-format on
FENSTER_API int fenster_loop(struct fenster *f) {
  msg1(void, msg(id, f->wnd, "contentView"), "setNeedsDisplay:", BOOL, YES);
  id ev = msg4(id, NSApp,
               "nextEventMatchingMask:untilDate:inMode:dequeue:", NSUInteger,
               NSUIntegerMax, id, NULL, id, NSDefaultRunLoopMode, BOOL, YES);
  if (!ev)
    return 0;
  NSUInteger evtype = msg(NSUInteger, ev, "type");
  switch (evtype) {
  case 1: /* NSEventTypeMouseDown */
    f->mouse |= 1;
    break;
  case 2: /* NSEventTypeMouseUp*/
    f->mouse &= ~1;
    break;
  case 5:
  case 6: { /* NSEventTypeMouseMoved */
    CGPoint xy = msg(CGPoint, ev, "locationInWindow");
    f->x = (int)xy.x;
    f->y = (int)(f->height - xy.y);
    return 0;
  }
  case 10: /*NSEventTypeKeyDown*/
  case 11: /*NSEventTypeKeyUp:*/ {
    NSUInteger k = msg(NSUInteger, ev, "keyCode");
    f->keys[k < 127 ? FENSTER_KEYCODES[k] : 0] = evtype == 10;
    NSUInteger mod = msg(NSUInteger, ev, "modifierFlags") >> 17;
    f->mod = (mod & 0xc) | ((mod & 1) << 1) | ((mod >> 1) & 1);
    return 0;
  }
  }
  msg1(void, NSApp, "sendEvent:", id, ev);
  return 0;
}
#elif defined(_WIN32)
// clang-format off
static const uint8_t FENSTER_KEYCODES[] = {0,27,49,50,51,52,53,54,55,56,57,48,45,61,8,9,81,87,69,82,84,89,85,73,79,80,91,93,10,0,65,83,68,70,71,72,74,75,76,59,39,96,0,92,90,88,67,86,66,78,77,44,46,47,0,0,0,32,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,17,3,0,20,0,19,0,5,18,4,26,127};
// clang-format on
typedef struct BINFO{
    BITMAPINFOHEADER    bmiHeader;
    RGBQUAD             bmiColors[3];
}BINFO;
static LRESULT CALLBACK fenster_wndproc(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam) {
  struct fenster *f = (struct fenster *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  switch (msg) {
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    HDC memdc = CreateCompatibleDC(hdc);
    HBITMAP hbmp = CreateCompatibleBitmap(hdc, f->width, f->height);
    HBITMAP oldbmp = SelectObject(memdc, hbmp);
    BINFO bi = {{sizeof(bi), f->width, -f->height, 1, 32, BI_BITFIELDS}};
    bi.bmiColors[0].rgbRed = 0xff;
    bi.bmiColors[1].rgbGreen = 0xff;
    bi.bmiColors[2].rgbBlue = 0xff;
    SetDIBitsToDevice(memdc, 0, 0, f->width, f->height, 0, 0, 0, f->height,
                      f->buf, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    BitBlt(hdc, 0, 0, f->width, f->height, memdc, 0, 0, SRCCOPY);
    SelectObject(memdc, oldbmp);
    DeleteObject(hbmp);
    DeleteDC(memdc);
    EndPaint(hwnd, &ps);
  } break;
  case WM_CLOSE:
    DestroyWindow(hwnd);
    break;
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
    f->mouse = (msg == WM_LBUTTONDOWN);
    break;
  case WM_MOUSEMOVE:
    f->y = HIWORD(lParam), f->x = LOWORD(lParam);
    break;
  case WM_KEYDOWN:
  case WM_KEYUP: {
    f->mod = ((GetKeyState(VK_CONTROL) & 0x8000) >> 15) |
             ((GetKeyState(VK_SHIFT) & 0x8000) >> 14) |
             ((GetKeyState(VK_MENU) & 0x8000) >> 13) |
             (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) >> 12);
    f->keys[FENSTER_KEYCODES[HIWORD(lParam) & 0x1ff]] = !((lParam >> 31) & 1);
  } break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }
  return 0;
}

FENSTER_API int fenster_open(struct fenster *f) {
  HINSTANCE hInstance = GetModuleHandle(NULL);
  WNDCLASSEX wc = {0};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.style = CS_VREDRAW | CS_HREDRAW;
  wc.lpfnWndProc = fenster_wndproc;
  wc.hInstance = hInstance;
  wc.lpszClassName = f->title;
  RegisterClassEx(&wc);
  f->hwnd = CreateWindowEx(WS_EX_CLIENTEDGE, f->title, f->title,
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           f->width, f->height, NULL, NULL, hInstance, NULL);

  if (f->hwnd == NULL)
    return -1;
  SetWindowLongPtr(f->hwnd, GWLP_USERDATA, (LONG_PTR)f);
  ShowWindow(f->hwnd, SW_NORMAL);
  UpdateWindow(f->hwnd);
  return 0;
}

FENSTER_API void fenster_close(struct fenster *f) { (void)f; }

FENSTER_API int fenster_loop(struct fenster *f) {
  MSG msg;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT)
      return -1;
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  InvalidateRect(f->hwnd, NULL, TRUE);
  return 0;
}
#else
// clang-format off
static int FENSTER_KEYCODES[124] = {XK_BackSpace,8,XK_Delete,127,XK_Down,18,XK_End,5,XK_Escape,27,XK_Home,2,XK_Insert,26,XK_Left,20,XK_Page_Down,4,XK_Page_Up,3,XK_Return,10,XK_Right,19,XK_Tab,9,XK_Up,17,XK_apostrophe,39,XK_backslash,92,XK_bracketleft,91,XK_bracketright,93,XK_comma,44,XK_equal,61,XK_grave,96,XK_minus,45,XK_period,46,XK_semicolon,59,XK_slash,47,XK_space,32,XK_a,65,XK_b,66,XK_c,67,XK_d,68,XK_e,69,XK_f,70,XK_g,71,XK_h,72,XK_i,73,XK_j,74,XK_k,75,XK_l,76,XK_m,77,XK_n,78,XK_o,79,XK_p,80,XK_q,81,XK_r,82,XK_s,83,XK_t,84,XK_u,85,XK_v,86,XK_w,87,XK_x,88,XK_y,89,XK_z,90,XK_0,48,XK_1,49,XK_2,50,XK_3,51,XK_4,52,XK_5,53,XK_6,54,XK_7,55,XK_8,56,XK_9,57};
// clang-format on
static int fenster_shm_error = 0;
static int fenster_shm_handler(Display *dpy, XErrorEvent *ev) {
  (void)dpy; (void)ev;
  fenster_shm_error = 1;
  return 0;
}

static void fenster_create_shm_image(struct fenster *f) {
  f->use_shm = false;
  if (!XShmQueryExtension(f->dpy)) return;
  
  f->img = XShmCreateImage(f->dpy, DefaultVisual(f->dpy, 0), 24, ZPixmap, NULL,
                           &f->shminfo, f->width, f->height);
  if (!f->img) return;
  
  f->shminfo.shmid = shmget(IPC_PRIVATE, f->img->bytes_per_line * f->img->height,
                            IPC_CREAT | 0777);
  if (f->shminfo.shmid < 0) {
    XDestroyImage(f->img);
    f->img = NULL;
    return;
  }
  
  f->shminfo.shmaddr = f->img->data = shmat(f->shminfo.shmid, NULL, 0);
  if (f->shminfo.shmaddr == (char *)-1) {
    shmctl(f->shminfo.shmid, IPC_RMID, NULL);
    XDestroyImage(f->img);
    f->img = NULL;
    return;
  }
  f->shminfo.readOnly = False;
  
  fenster_shm_error = 0;
  XErrorHandler old_handler = XSetErrorHandler(fenster_shm_handler);
  if (!XShmAttach(f->dpy, &f->shminfo)) {
    XSetErrorHandler(old_handler);
    shmdt(f->shminfo.shmaddr);
    shmctl(f->shminfo.shmid, IPC_RMID, NULL);
    XDestroyImage(f->img);
    f->img = NULL;
    return;
  }
  XSync(f->dpy, False);
  XSetErrorHandler(old_handler);
  
  if (fenster_shm_error) {
    shmdt(f->shminfo.shmaddr);
    shmctl(f->shminfo.shmid, IPC_RMID, NULL);
    XDestroyImage(f->img);
    f->img = NULL;
    return;
  }
  
  shmctl(f->shminfo.shmid, IPC_RMID, NULL);
  f->buf = (uint32_t *)f->img->data;
  f->use_shm = true;
}

FENSTER_API int fenster_open(struct fenster *f) {
  f->dpy = XOpenDisplay(NULL);
  int screen = DefaultScreen(f->dpy);
  f->w = XCreateSimpleWindow(f->dpy, RootWindow(f->dpy, screen), 0, 0, f->width,
                             f->height, 0, BlackPixel(f->dpy, screen),
                             WhitePixel(f->dpy, screen));
  f->gc = XCreateGC(f->dpy, f->w, 0, 0);
  XSelectInput(f->dpy, f->w, StructureNotifyMask | ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask);
  XStoreName(f->dpy, f->w, f->title);
  XMapWindow(f->dpy, f->w);
  XSync(f->dpy, f->w);
  
  fenster_create_shm_image(f);
  if (!f->use_shm) {
    f->img = XCreateImage(f->dpy, DefaultVisual(f->dpy, 0), 24, ZPixmap, 0,
                          (char *)f->buf, f->width, f->height, 32, 0);
  }
  f->dirty = true;
  return 0;
}
FENSTER_API void fenster_close(struct fenster *f) {
  if (f->use_shm) {
    XShmDetach(f->dpy, &f->shminfo);
    shmdt(f->shminfo.shmaddr);
  }
  if (f->img) XDestroyImage(f->img);
  XCloseDisplay(f->dpy);
}
FENSTER_API int fenster_loop(struct fenster *f) {
  XEvent ev;
  if (f->dirty) {
    if (f->use_shm) {
      XShmPutImage(f->dpy, f->w, f->gc, f->img, 0, 0, 0, 0, f->width, f->height, False);
    } else {
      XPutImage(f->dpy, f->w, f->gc, f->img, 0, 0, 0, 0, f->width, f->height);
    }
    f->dirty = false;
  }
  XFlush(f->dpy);
  while (XPending(f->dpy)) {
    f->size_changed = false;
    XNextEvent(f->dpy, &ev);
    switch (ev.type) {
    case ConfigureNotify:
      f->size_changed = true;
      f->width = ev.xconfigure.width;
      f->height = ev.xconfigure.height;
      if (f->use_shm) {
        XShmDetach(f->dpy, &f->shminfo);
        shmdt(f->shminfo.shmaddr);
        XDestroyImage(f->img);
        f->img = NULL;
        fenster_create_shm_image(f);
      } else {
        free(f->img);
        f->buf = (uint32_t*)malloc(f->width * f->height * sizeof(uint32_t));
        f->img = XCreateImage(f->dpy, DefaultVisual(f->dpy, 0), 24, ZPixmap, 0,
                              (char *)f->buf, f->width, f->height, 32, 0);
      }
      f->dirty = true;
      break;
    case ButtonPress: {
      int m = ev.xbutton.state;
      f->mod = (!!(m & ControlMask)) | (!!(m & ShiftMask) << 1) |
               (!!(m & Mod1Mask) << 2) | (!!(m & Mod4Mask) << 3);
      if (ev.xbutton.button == Button4) {
        f->scroll = 1; /* scroll up */
      } else if (ev.xbutton.button == Button5) {
        f->scroll = -1; /* scroll down */
      } else {
        f->mouse = 1;
      }
    } break;
    case ButtonRelease:
      if (ev.xbutton.button != Button4 && ev.xbutton.button != Button5) {
        f->mouse = 0;
      }
      break;
    case MotionNotify:
      f->x = ev.xmotion.x, f->y = ev.xmotion.y;
      break;
    case KeyPress:
    case KeyRelease: {
      int m = ev.xkey.state;
      int k = XkbKeycodeToKeysym(f->dpy, ev.xkey.keycode, 0, 0);
      for (unsigned int i = 0; i < 124; i += 2) {
        if (FENSTER_KEYCODES[i] == k) {
          f->keys[FENSTER_KEYCODES[i + 1]] = (ev.type == KeyPress);
          break;
        }
      }
      f->mod = (!!(m & ControlMask)) | (!!(m & ShiftMask) << 1) |
               (!!(m & Mod1Mask) << 2) | (!!(m & Mod4Mask) << 3);
    } break;
    case FocusOut:
      for (int i = 0; i < 256; i++) f->keys[i] = 0;
      f->mod = 0;
      f->mouse = 0;
      break;
    }
  }
  return 0;
}
#endif

#ifdef _WIN32
FENSTER_API void fenster_sleep(int64_t ms) { Sleep(ms); }
FENSTER_API int64_t fenster_time() {
  LARGE_INTEGER freq, count;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&count);
  return (int64_t)(count.QuadPart * 1000.0 / freq.QuadPart);
}
#else
FENSTER_API void fenster_sleep(int64_t ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
}
FENSTER_API int64_t fenster_time(void) {
  struct timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  return time.tv_sec * 1000 + (time.tv_nsec / 1000000);
}
#endif

/*
static void fenster_line(struct fenster *f, int x0, int y0, int x1, int y1, uint32_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = (dx > dy ? dx : -dy) / 2, e2;
  for (;;) {
    fenster_pixel(f, x0, y0) = c;
    if (x0 == x1 && y0 == y1) {
      break;
    }
    e2 = err;
    if (e2 > -dx) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dy) {
      err += dx;
      y0 += sy;
    }
  }
}
*/

static void fenster_rect(struct fenster *f, int x, int y, int w, int h, uint32_t c) {
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      if (x+col > f->width || y+row > f->height) continue;
      fenster_pixel(f, x + col, y + row) = c;
    }
  }
}

/*
static void fenster_circle(struct fenster *f, int x, int y, int r, uint32_t c) {
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r * r) {
        fenster_pixel(f, x + dx, y + dy) = c;
      }
    }
  }
}
*/

void draw_icn(struct fenster *f, char *sprite, int x, int y, int scale, uint32_t color) {
  for (int dy = 0; dy < 8; dy++) {
    for (int dx = 0; dx < 8; dx++) {
			if (*(sprite+dy) << dx & 0x80) {
				fenster_rect(f, x + dx*scale, y + dy*scale, scale, scale, color);
      }
    }
  }
}

static void fenster_text(struct fenster *f, unsigned char *font, int x, int y, char *s, int scale, uint32_t c) {
  while (*s) {
    char chr = *s++;
    int size = font[(int)chr];
    if (chr > 32) {
      char *sprite = (char*)&font[chr*8*4 + 256];
      draw_icn(f, sprite, x, y, scale, c);
      draw_icn(f, sprite+8, x, y+8*scale, scale, c);
      if (size > 8) {
        draw_icn(f, sprite+16, x+8*scale, y, scale, c);
        draw_icn(f, sprite+24, x+8*scale, y+8*scale, scale, c);
      }
    }
    x = x + size * scale;
  }
}

#endif /* !FENSTER_HEADER */
#endif /* FENSTER_H */
