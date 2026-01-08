#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#include "kgui.h"
#include "fonts/terminus16.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tsm/libtsm.h"
#include "tsm/xkbcommon/xkbcommon-keysyms.h"

#define W 1000
#define H 1000
#define BASE_CHAR_W 8
#define BASE_CHAR_H 16
#define BASE_PADDING 2

static kg_ctx ctx;
static int char_w = 8;
static int char_h = 16;
static int padding = 2;

static uint32_t default_bg = 0xffffff;

static int master_fd = -1;
static pid_t child_pid = -1;
static int quit_requested = 0;
static int needs_redraw = 1;
static int cols = 80, rows = 24;

static struct tsm_screen *screen = NULL;
static struct tsm_vte *vte = NULL;

/* Mouse selection state */
static int mouse_pressed = 0;
static int selection_active = 0;
static char *clipboard_text = NULL;
static int idle_frames = 0;

static uint32_t palette[TSM_COLOR_NUM] = {
  [TSM_COLOR_BLACK]         = 0x1d1f21,
  [TSM_COLOR_RED]           = 0xcc6666,
  [TSM_COLOR_GREEN]         = 0xb5bd68,
  [TSM_COLOR_YELLOW]        = 0xf0c674,
  [TSM_COLOR_BLUE]          = 0x81a2be,
  [TSM_COLOR_MAGENTA]       = 0xb294bb,
  [TSM_COLOR_CYAN]          = 0x8abeb7,
  [TSM_COLOR_LIGHT_GREY]    = 0xc5c8c6,
  [TSM_COLOR_DARK_GREY]     = 0x222222,
  [TSM_COLOR_LIGHT_RED]     = 0xcc6666,
  [TSM_COLOR_LIGHT_GREEN]   = 0xb5bd68,
  [TSM_COLOR_LIGHT_YELLOW]  = 0xf0c674,
  [TSM_COLOR_LIGHT_BLUE]    = 0x81a2be,
  [TSM_COLOR_LIGHT_MAGENTA] = 0xb294bb,
  [TSM_COLOR_LIGHT_CYAN]    = 0x8abeb7,
  [TSM_COLOR_WHITE]         = 0xffffff,
  [TSM_COLOR_FOREGROUND]    = 0x222222,
  [TSM_COLOR_BACKGROUND]    = 0xffffff,
};

/* RGB palette for VTE (r, g, b for each color) */
static uint8_t vte_palette[TSM_COLOR_NUM][3] = {
  [TSM_COLOR_BLACK]         = { 0x1d, 0x1f, 0x21 },
  [TSM_COLOR_RED]           = { 0xcc, 0x66, 0x66 },
  [TSM_COLOR_GREEN]         = { 0xb5, 0xbd, 0x68 },
  [TSM_COLOR_YELLOW]        = { 0xf0, 0xc6, 0x74 },
  [TSM_COLOR_BLUE]          = { 0x81, 0xa2, 0xbe },
  [TSM_COLOR_MAGENTA]       = { 0xb2, 0x94, 0xbb },
  [TSM_COLOR_CYAN]          = { 0x8a, 0xbe, 0xb7 },
  [TSM_COLOR_LIGHT_GREY]    = { 0xc5, 0xc8, 0xc6 },
  [TSM_COLOR_DARK_GREY]     = { 0x22, 0x22, 0x22 },
  [TSM_COLOR_LIGHT_RED]     = { 0xcc, 0x66, 0x66 },
  [TSM_COLOR_LIGHT_GREEN]   = { 0xb5, 0xbd, 0x68 },
  [TSM_COLOR_LIGHT_YELLOW]  = { 0xf0, 0xc6, 0x74 },
  [TSM_COLOR_LIGHT_BLUE]    = { 0x81, 0xa2, 0xbe },
  [TSM_COLOR_LIGHT_MAGENTA] = { 0xb2, 0x94, 0xbb },
  [TSM_COLOR_LIGHT_CYAN]    = { 0x8a, 0xbe, 0xb7 },
  [TSM_COLOR_WHITE]         = { 0xff, 0xff, 0xff },
  [TSM_COLOR_FOREGROUND]    = { 0x22, 0x22, 0x22 },
  [TSM_COLOR_BACKGROUND]    = { 0xff, 0xff, 0xff },
};

static void vte_write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data) {
  (void)vte;
  (void)data;
  if (master_fd >= 0) {
    write(master_fd, u8, len);
  }
}

static void pixel_to_cell(struct fenster *f, int px, int py, int *cx, int *cy) {
  (void)f;
  *cx = (px - padding) / char_w;
  *cy = (py - padding) / char_h;
  if (*cx < 0) *cx = 0;
  if (*cy < 0) *cy = 0;
  if (*cx >= cols) *cx = cols - 1;
  if (*cy >= rows) *cy = rows - 1;
}

static void handle_mouse(void) {
  int cx, cy;
  pixel_to_cell(ctx.f, ctx.mouse_x, ctx.mouse_y, &cx, &cy);

  if (ctx.mouse_pressed) {
    /* Mouse button pressed - start selection */
    mouse_pressed = 1;
    selection_active = 1;
    tsm_screen_selection_reset(screen);
    tsm_screen_selection_start(screen, cx, cy);
    needs_redraw = 1;
  } else if (ctx.mouse_down && mouse_pressed) {
    /* Mouse dragging - update selection */
    tsm_screen_selection_target(screen, cx, cy);
    needs_redraw = 1;
  } else if (ctx.mouse_released && mouse_pressed) {
    /* Mouse button released - copy selection */
    mouse_pressed = 0;
    if (selection_active) {
      char *sel = NULL;
      if (tsm_screen_selection_copy(screen, &sel) >= 0 && sel) {
        kg_clipboard_copy(sel);
        free(clipboard_text);
        clipboard_text = sel;
      }
    }
  }
}

static uint32_t attr_to_color(const struct tsm_screen_attr *attr, int is_fg) {
  if (is_fg) {
    if (attr->fccode >= 0 && attr->fccode < TSM_COLOR_NUM) {
      return palette[attr->fccode];
    }
    /* If RGB is all zero, use default foreground */
    if (attr->fr == 0 && attr->fg == 0 && attr->fb == 0) {
      return palette[TSM_COLOR_FOREGROUND];
    }
    return (attr->fr << 16) | (attr->fg << 8) | attr->fb;
  } else {
    if (attr->bccode >= 0 && attr->bccode < TSM_COLOR_NUM) {
      return palette[attr->bccode];
    }
    /* If RGB is all zero, use default background */
    if (attr->br == 0 && attr->bg == 0 && attr->bb == 0) {
      return palette[TSM_COLOR_BACKGROUND];
    }
    return (attr->br << 16) | (attr->bg << 8) | attr->bb;
  }
}

static char box_to_ascii(uint32_t c) {
  if (c >= 0x2500 && c <= 0x257F) {
    switch (c) {
      /* Horizontal lines */
      case 0x2500: case 0x2501: case 0x2504: case 0x2505:
      case 0x2508: case 0x2509: case 0x254C: case 0x254D:
      case 0x2574: case 0x2576: case 0x2578: case 0x257A:
      case 0x257C: case 0x257E:
        return '-';
      /* Vertical lines */
      case 0x2502: case 0x2503: case 0x2506: case 0x2507:
      case 0x250A: case 0x250B: case 0x254E: case 0x254F:
      case 0x2575: case 0x2577: case 0x2579: case 0x257B:
      case 0x257D: case 0x257F:
        return '|';
      /* Corners and intersections */
      default:
        return '+';
    }
  }
  return 0;
}

static int draw_cb(struct tsm_screen *con, uint64_t id, const uint32_t *ch,
                   size_t len, unsigned int width, unsigned int posx,
                   unsigned int posy, const struct tsm_screen_attr *attr,
                   tsm_age_t age, void *data) {
  (void)con;
  (void)id;
  (void)width;
  (void)age;
  (void)data;

  struct fenster *f = ctx.f;
  int x = padding + posx * char_w;
  int y = padding + posy * char_h;

  uint32_t fg = attr_to_color(attr, 1);
  uint32_t bg = attr_to_color(attr, 0);

  if (attr->inverse) {
    uint32_t tmp = fg;
    fg = bg;
    bg = tmp;
  }

  fenster_rect(f, x, y, char_w, char_h, bg);

  if (len > 0) {
    uint32_t c = ch[0];
    char ascii = box_to_ascii(c);
    if (ascii) {
      char tmp[2] = { ascii, 0 };
      fenster_text(f, terminus, x, y, tmp, ctx.scale.font_scale, fg);
    } else if (c > 32 && c < 127) {
      char tmp[2] = { (char)c, 0 };
      fenster_text(f, terminus, x, y, tmp, ctx.scale.font_scale, fg);
    }
  }

  return 0;
}

static int spawn_shell(void) {
  struct winsize ws = { .ws_row = rows, .ws_col = cols };

  child_pid = forkpty(&master_fd, NULL, NULL, &ws);
  if (child_pid < 0) return -1;

  if (child_pid == 0) {
    char *shell = getenv("SHELL");
    if (!shell) shell = "/bin/sh";
    setenv("TERM", "xterm-256color", 1);
    execlp(shell, shell, NULL);
    _exit(1);
  }

  int flags = fcntl(master_fd, F_GETFL);
  fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
  return 0;
}

static uint32_t fenster_key_to_xkb(int k, int shift) {
  switch (k) {
    case 17: return XKB_KEY_Up;
    case 18: return XKB_KEY_Down;
    case 19: return XKB_KEY_Right;
    case 20: return XKB_KEY_Left;
    case 8:  return XKB_KEY_BackSpace;
    case 127: return XKB_KEY_Delete;
    case 10: return XKB_KEY_Return;
    case 9:  return XKB_KEY_Tab;
    case 27: return XKB_KEY_Escape;
    case 2:  return XKB_KEY_Home;
    case 5:  return XKB_KEY_End;
    case 3:  return XKB_KEY_Page_Up;
    case 4:  return XKB_KEY_Page_Down;
  }

  /* F1-F12 (fenster uses 1-12 for function keys) */
  if (k >= 129 && k <= 140) {
    return XKB_KEY_F1 + (k - 129);
  }

  /* Regular ASCII characters */
  if (k >= 32 && k < 127) {
    if (shift && k >= 'a' && k <= 'z') {
      return XKB_KEY_A + (k - 'a');
    }
    if (!shift && k >= 'A' && k <= 'Z') {
      return XKB_KEY_a + (k - 'A');
    }
    return k;
  }

  return XKB_KEY_NoSymbol;
}

static uint32_t get_unicode(int k, int shift) {
  if (k >= 32 && k < 127) {
    if (shift && k >= 'a' && k <= 'z') return k - 32;
    if (!shift && k >= 'A' && k <= 'Z') return k + 32;
    if (shift) {
      static const char *sh = ")!@#$%^&*(";
      if (k >= '0' && k <= '9') return sh[k - '0'];
      switch(k) {
        case '-': return '_';
        case '=': return '+';
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case ';': return ':';
        case '\'': return '"';
        case ',': return '<';
        case '.': return '>';
        case '/': return '?';
        case '`': return '~';
      }
    }
    return k;
  }
  return 0;
}

static void cancel_selection(void) {
  if (selection_active) {
    tsm_screen_selection_reset(screen);
    selection_active = 0;
    needs_redraw = 1;
  }
}

static void handle_key(int k, int mod, void *userdata) {
  (void)userdata;
  int ctrl = mod & KG_MOD_CTRL;
  int shift = mod & KG_MOD_SHIFT;

  if (ctrl && (k == 'Q' || k == 'q')) {
    quit_requested = 1;
    return;
  }

  /* Paste with Ctrl+Shift+V */
  if (ctrl && shift && (k == 'V' || k == 'v')) {
    char *paste = kg_clipboard_paste();
    if (paste) {
      write(master_fd, paste, strlen(paste));
      free(paste);
    }
    return;
  }

  /* Scrollback navigation with shift+arrows */
  if (shift && k == KG_KEY_UP) {
    tsm_screen_sb_up(screen, 1);
    needs_redraw = 1;
    return;
  }
  if (shift && k == KG_KEY_DOWN) {
    tsm_screen_sb_down(screen, 1);
    needs_redraw = 1;
    return;
  }
  if (shift && k == KG_KEY_PAGEUP) {
    tsm_screen_sb_page_up(screen, 1);
    needs_redraw = 1;
    return;
  }
  if (shift && k == KG_KEY_PAGEDOWN) {
    tsm_screen_sb_page_down(screen, 1);
    needs_redraw = 1;
    return;
  }

  cancel_selection();

  uint32_t keysym = fenster_key_to_xkb(k, shift);
  uint32_t unicode = get_unicode(k, shift);
  unsigned int mods = 0;

  if (ctrl) mods |= TSM_CONTROL_MASK;
  if (shift) mods |= TSM_SHIFT_MASK;

  tsm_vte_handle_keyboard(vte, keysym, keysym, mods, unicode);
}

static void handle_resize(void) {
  struct fenster *f = ctx.f;
  int new_cols = (f->width - padding * 2) / char_w;
  int new_rows = (f->height - padding * 2) / char_h;

  if (f->size_changed) {
    needs_redraw = 1;
  }

  if (new_cols != cols || new_rows != rows) {
    cols = new_cols;
    rows = new_rows;

    tsm_screen_resize(screen, cols, rows);

    struct winsize ws = { .ws_row = rows, .ws_col = cols };
    ioctl(master_fd, TIOCSWINSZ, &ws);
  }
}

static void draw(void) {
  struct fenster *f = ctx.f;
  int w = f->width;
  int h = f->height;

  for (int i = 0; i < w * h; i++) f->buf[i] = default_bg;

  tsm_screen_draw(screen, draw_cb, NULL);
}

static int run(void) {
  uint32_t buf[W * H];
  struct fenster f = { .title = "term", .width = W, .height = H, .buf = buf };

  /* Initialize kgui context */
  ctx = kg_init(&f, terminus);

  /* Apply scale to dimensions */
  char_w = KG_SCALED(BASE_CHAR_W, ctx.scale);
  char_h = KG_SCALED(BASE_CHAR_H, ctx.scale);
  padding = KG_SCALED(BASE_PADDING, ctx.scale);

  cols = (W - padding * 2) / char_w;
  rows = (H - padding * 2) / char_h;

  /* Initialize TSM screen */
  if (tsm_screen_new(&screen, NULL, NULL) < 0) {
    fprintf(stderr, "Failed to create TSM screen\n");
    return 1;
  }

  /* Set default attributes to use our foreground/background colors */
  struct tsm_screen_attr def_attr = {
    .fccode = TSM_COLOR_FOREGROUND,
    .bccode = TSM_COLOR_BACKGROUND,
  };
  tsm_screen_set_def_attr(screen, &def_attr);

  tsm_screen_resize(screen, cols, rows);
  tsm_screen_set_max_sb(screen, 5000);

  /* Initialize TSM VTE */
  if (tsm_vte_new(&vte, screen, vte_write_cb, NULL, NULL, NULL) < 0) {
    fprintf(stderr, "Failed to create TSM VTE\n");
    tsm_screen_unref(screen);
    return 1;
  }
  tsm_vte_set_custom_palette(vte, vte_palette);
  tsm_vte_set_palette(vte, "custom");
  tsm_vte_set_backspace_sends_delete(vte, true);

  if (spawn_shell() < 0) {
    tsm_vte_unref(vte);
    tsm_screen_unref(screen);
    return 1;
  }

  fenster_open(&f);

  while (fenster_loop(&f) == 0 && !quit_requested) {
    /* Use longer timeout when idle to reduce CPU usage */
    fd_set fds;
    int timeout_us = (idle_frames > 30) ? 100000 : 16000;  /* 100ms idle, 16ms active */
    struct timeval tv = { .tv_sec = 0, .tv_usec = timeout_us };
    FD_ZERO(&fds);
    FD_SET(master_fd, &fds);

    int had_activity = 0;
    if (select(master_fd + 1, &fds, NULL, NULL, &tv) > 0) {
      char rd[4096];
      ssize_t n;
      while ((n = read(master_fd, rd, sizeof(rd))) > 0) {
        tsm_vte_input(vte, rd, n);
        needs_redraw = 1;
        had_activity = 1;
      }
      if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        int status;
        if (waitpid(child_pid, &status, WNOHANG) != 0) break;
      }
    }

    /* Update mouse state */
    kg_frame_begin(&ctx);

    handle_resize();
    handle_mouse();

    /* Handle scroll wheel for scrollback */
    if (ctx.scroll > 0) {
      tsm_screen_sb_up(screen, 3);
      needs_redraw = 1;
    } else if (ctx.scroll < 0) {
      tsm_screen_sb_down(screen, 3);
      needs_redraw = 1;
    }

    /* Process keyboard with key repeat */
    kg_key_process(&ctx.key_repeat, f.keys, f.mod, handle_key, NULL);

    /* Detect input activity */
    if (f.keys[0] || ctx.mouse_pressed || ctx.mouse_released || ctx.mouse_down) {
      had_activity = 1;
    }

    /* Track idle state */
    if (had_activity || needs_redraw) {
      idle_frames = 0;
    } else if (idle_frames < 100) {
      idle_frames++;
    }

    if (needs_redraw) {
      draw();
      f.dirty = true;
      needs_redraw = 0;
    }
  }

  if (child_pid > 0) { kill(child_pid, SIGHUP); waitpid(child_pid, NULL, 0); }
  if (master_fd >= 0) close(master_fd);
  free(clipboard_text);
  tsm_vte_unref(vte);
  tsm_screen_unref(screen);
  fenster_close(&f);
  return 0;
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
  (void)hInstance, (void)hPrevInstance, (void)pCmdLine, (void)nCmdShow;
  return run();
}
#else
int main(int argc, char **argv) {
  int detached = 0;
  
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--detached") == 0) {
      detached = 1;
    }
  }
  
  if (!detached) {
    pid_t pid = fork();
    if (pid < 0) {
      return 1;
    }
    if (pid > 0) {
      return 0;
    }
    setsid();
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    char *args[3];
    args[0] = argv[0];
    args[1] = "--detached";
    args[2] = NULL;
    execvp(argv[0], args);
    return 1;
  }
  
  return run();
}
#endif
