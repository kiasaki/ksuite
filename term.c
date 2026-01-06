#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#include "fenster.h"
#include "terminus16.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tsm/libtsm.h"
#include "tsm/xkbcommon/xkbcommon-keysyms.h"

#define W 600
#define H 800
#define BASE_FONT_SCALE 1
#define BASE_CHAR_W 8
#define BASE_CHAR_H 16
#define BASE_PADDING 2

static int k_scale = 1;
static int font_scale = 1;
static int char_w = 8;
static int char_h = 16;
static int padding = 2;

static uint32_t default_bg = 0xffffff;

static int master_fd = -1;
static pid_t child_pid = -1;
static int quit_requested = 0;
static int cols = 80, rows = 24;

static struct tsm_screen *screen = NULL;
static struct tsm_vte *vte = NULL;

static struct fenster *g_fenster = NULL;

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

static void vte_write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data) {
  (void)vte;
  (void)data;
  if (master_fd >= 0) {
    write(master_fd, u8, len);
  }
}

static uint32_t attr_to_color(const struct tsm_screen_attr *attr, int is_fg) {
  if (is_fg) {
    if (attr->fccode >= 0 && attr->fccode < TSM_COLOR_NUM) {
      return palette[attr->fccode];
    }
    return (attr->fr << 16) | (attr->fg << 8) | attr->fb;
  } else {
    if (attr->bccode >= 0 && attr->bccode < TSM_COLOR_NUM) {
      return palette[attr->bccode];
    }
    return (attr->br << 16) | (attr->bg << 8) | attr->bb;
  }
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

  struct fenster *f = g_fenster;
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

  if (len > 0 && ch[0] > 32 && ch[0] < 127) {
    char tmp[2] = { (char)ch[0], 0 };
    fenster_text(f, terminus, x, y, tmp, font_scale, fg);
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

static int prev_keys[256];

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

static void handle_keyboard(struct fenster *f) {
  for (int k = 0; k < 256; k++) {
    if (f->keys[k] && !prev_keys[k]) {
      int ctrl = f->mod & 1;
      int shift = f->mod & 2;

      if (ctrl && (k == 'Q' || k == 'q')) {
        quit_requested = 1;
        break;
      }

      uint32_t keysym = fenster_key_to_xkb(k, shift);
      uint32_t unicode = get_unicode(k, shift);
      unsigned int mods = 0;

      if (ctrl) mods |= TSM_CONTROL_MASK;
      if (shift) mods |= TSM_SHIFT_MASK;

      tsm_vte_handle_keyboard(vte, keysym, keysym, mods, unicode);
    }
    prev_keys[k] = f->keys[k];
  }
}

static void handle_resize(struct fenster *f) {
  int new_cols = (f->width - padding * 2) / char_w;
  int new_rows = (f->height - padding * 2) / char_h;

  if (new_cols != cols || new_rows != rows) {
    cols = new_cols;
    rows = new_rows;

    tsm_screen_resize(screen, cols, rows);

    struct winsize ws = { .ws_row = rows, .ws_col = cols };
    ioctl(master_fd, TIOCSWINSZ, &ws);
  }
}

static void draw(struct fenster *f) {
  int w = f->width;
  int h = f->height;
  for (int i = 0; i < w * h; i++) f->buf[i] = default_bg;

  g_fenster = f;
  tsm_screen_draw(screen, draw_cb, NULL);
}

static int run(void) {
  char *scale_env = getenv("K_SCALE");
  if (scale_env) {
    k_scale = atoi(scale_env);
    if (k_scale < 1) k_scale = 1;
  }
  font_scale = BASE_FONT_SCALE * k_scale;
  char_w = BASE_CHAR_W * k_scale;
  char_h = BASE_CHAR_H * k_scale;
  padding = BASE_PADDING * k_scale;

  uint32_t buf[W * H];
  struct fenster f = { .title = "term", .width = W, .height = H, .buf = buf };

  cols = (W - padding * 2) / char_w;
  rows = (H - padding * 2) / char_h;

  /* Initialize TSM screen */
  if (tsm_screen_new(&screen, NULL, NULL) < 0) {
    fprintf(stderr, "Failed to create TSM screen\n");
    return 1;
  }
  tsm_screen_resize(screen, cols, rows);
  tsm_screen_set_max_sb(screen, 5000);

  /* Initialize TSM VTE */
  if (tsm_vte_new(&vte, screen, vte_write_cb, NULL, NULL, NULL) < 0) {
    fprintf(stderr, "Failed to create TSM VTE\n");
    tsm_screen_unref(screen);
    return 1;
  }
  tsm_vte_set_backspace_sends_delete(vte, true);

  if (spawn_shell() < 0) {
    tsm_vte_unref(vte);
    tsm_screen_unref(screen);
    return 1;
  }

  fenster_open(&f);
  int64_t now = fenster_time();

  while (fenster_loop(&f) == 0 && !quit_requested) {
    char rd[4096];
    ssize_t n;
    while ((n = read(master_fd, rd, sizeof(rd))) > 0) {
      tsm_vte_input(vte, rd, n);
    }
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
      int status;
      if (waitpid(child_pid, &status, WNOHANG) != 0) break;
    }

    handle_resize(&f);
    handle_keyboard(&f);
    draw(&f);

    int64_t time = fenster_time();
    if (time - now < 1000 / 60) {
      fenster_sleep(1000 / 60 - (time - now));
    }
    now = time;
  }

  if (child_pid > 0) { kill(child_pid, SIGHUP); waitpid(child_pid, NULL, 0); }
  if (master_fd >= 0) close(master_fd);
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
int main(void) { return run(); }
#endif
