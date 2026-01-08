#include "kgui.h"
#include "fonts/newyork14.h"

#define W 800
#define H 600
#define MAX_LINES 4096
#define MAX_LINE_LEN 1024
#define BASE_CHAR_H 16
#define BASE_PADDING 8
#define BG_COLOR 0xffffff
#define FG_COLOR 0x000000
#define SEL_COLOR 0x3399ff
#define CURSOR_COLOR 0x000000

static kg_ctx ctx;
static int char_h = 16;
static int padding = 8;
static char *lines[MAX_LINES];
static int line_count = 1;
static int cursor_line = 0;
static int cursor_col = 0;
static int sel_start_line = -1, sel_start_col = -1;
static int sel_end_line = -1, sel_end_col = -1;
static int selecting = 0;
static char *filename = NULL;
static int scroll_y = 0;

static int char_width(char c) {
  return kg_char_width(ctx.font, c, ctx.scale.font_scale);
}

static int text_width(const char *s, int len) {
  int w = 0;
  for (int i = 0; i < len && s[i]; i++) {
    w += char_width(s[i]);
  }
  return w;
}

static int col_to_x(const char *line, int col) {
  return padding + text_width(line, col);
}

static int x_to_col(const char *line, int x) {
  int px = padding;
  int len = strlen(line);
  for (int i = 0; i < len; i++) {
    int cw = char_width(line[i]);
    if (x < px + cw / 2) return i;
    px += cw;
  }
  return len;
}

static int y_to_line(int y) {
  int l = (y - padding + scroll_y) / char_h;
  if (l < 0) l = 0;
  if (l >= line_count) l = line_count - 1;
  return l;
}

static void ensure_line(int n) {
  while (line_count <= n) {
    lines[line_count] = calloc(MAX_LINE_LEN, 1);
    line_count++;
  }
}

static void insert_char(char c) {
  ensure_line(cursor_line);
  char *line = lines[cursor_line];
  int len = strlen(line);
  if (len >= MAX_LINE_LEN - 1) return;
  memmove(line + cursor_col + 1, line + cursor_col, len - cursor_col + 1);
  line[cursor_col] = c;
  cursor_col++;
}

static void insert_newline(void) {
  ensure_line(cursor_line);
  if (line_count >= MAX_LINES - 1) return;
  for (int i = line_count; i > cursor_line + 1; i--) {
    lines[i] = lines[i - 1];
  }
  line_count++;
  char *old = lines[cursor_line];
  char *newline = calloc(MAX_LINE_LEN, 1);
  strcpy(newline, old + cursor_col);
  old[cursor_col] = '\0';
  lines[cursor_line + 1] = newline;
  cursor_line++;
  cursor_col = 0;
}

static void delete_char(void) {
  ensure_line(cursor_line);
  char *line = lines[cursor_line];
  if (cursor_col > 0) {
    int len = strlen(line);
    memmove(line + cursor_col - 1, line + cursor_col, len - cursor_col + 1);
    cursor_col--;
  } else if (cursor_line > 0) {
    char *prev = lines[cursor_line - 1];
    int prev_len = strlen(prev);
    strcat(prev, line);
    free(lines[cursor_line]);
    for (int i = cursor_line; i < line_count - 1; i++) {
      lines[i] = lines[i + 1];
    }
    line_count--;
    cursor_line--;
    cursor_col = prev_len;
  }
}

static void delete_forward(void) {
  ensure_line(cursor_line);
  char *line = lines[cursor_line];
  int len = strlen(line);
  if (cursor_col < len) {
    memmove(line + cursor_col, line + cursor_col + 1, len - cursor_col);
  } else if (cursor_line < line_count - 1) {
    strcat(line, lines[cursor_line + 1]);
    free(lines[cursor_line + 1]);
    for (int i = cursor_line + 1; i < line_count - 1; i++) {
      lines[i] = lines[i + 1];
    }
    line_count--;
  }
}

static void normalize_selection(int *sl, int *sc, int *el, int *ec) {
  if (sel_start_line < 0) { *sl = *sc = *el = *ec = -1; return; }
  if (sel_start_line < sel_end_line || 
      (sel_start_line == sel_end_line && sel_start_col <= sel_end_col)) {
    *sl = sel_start_line; *sc = sel_start_col;
    *el = sel_end_line; *ec = sel_end_col;
  } else {
    *sl = sel_end_line; *sc = sel_end_col;
    *el = sel_start_line; *ec = sel_start_col;
  }
}

static int in_selection(int line, int col) {
  int sl, sc, el, ec;
  normalize_selection(&sl, &sc, &el, &ec);
  if (sl < 0) return 0;
  if (line < sl || line > el) return 0;
  if (line == sl && col < sc) return 0;
  if (line == el && col >= ec) return 0;
  return 1;
}

static void delete_selection(void) {
  int sl, sc, el, ec;
  normalize_selection(&sl, &sc, &el, &ec);
  if (sl < 0) return;
  
  if (sl == el) {
    char *line = lines[sl];
    int len = strlen(line);
    memmove(line + sc, line + ec, len - ec + 1);
  } else {
    char *first = lines[sl];
    char *last = lines[el];
    first[sc] = '\0';
    strcat(first, last + ec);
    for (int i = sl + 1; i <= el; i++) {
      free(lines[i]);
    }
    for (int i = sl + 1; i < line_count - (el - sl); i++) {
      lines[i] = lines[i + (el - sl)];
    }
    line_count -= (el - sl);
  }
  cursor_line = sl;
  cursor_col = sc;
  sel_start_line = sel_end_line = -1;
}

static char *get_selection_text(void) {
  int sl, sc, el, ec;
  normalize_selection(&sl, &sc, &el, &ec);
  if (sl < 0) return NULL;
  
  int total = 0;
  for (int i = sl; i <= el; i++) {
    int start = (i == sl) ? sc : 0;
    int end = (i == el) ? ec : (int)strlen(lines[i]);
    total += end - start;
    if (i < el) total++;
  }
  
  char *text = malloc(total + 1);
  char *p = text;
  for (int i = sl; i <= el; i++) {
    int start = (i == sl) ? sc : 0;
    int end = (i == el) ? ec : (int)strlen(lines[i]);
    memcpy(p, lines[i] + start, end - start);
    p += end - start;
    if (i < el) *p++ = '\n';
  }
  *p = '\0';
  return text;
}

static void paste_text(const char *text) {
  if (!text) return;
  if (sel_start_line >= 0) delete_selection();
  while (*text) {
    if (*text == '\n') {
      insert_newline();
    } else if (*text >= 32 || *text == '\t') {
      insert_char(*text);
    }
    text++;
  }
}

static void save_file(void) {
  if (!filename) return;
  FILE *f = fopen(filename, "w");
  if (!f) return;
  for (int i = 0; i < line_count; i++) {
    fputs(lines[i], f);
    if (i < line_count - 1) fputc('\n', f);
  }
  fclose(f);
}

static void load_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    lines[0] = calloc(MAX_LINE_LEN, 1);
    line_count = 1;
    return;
  }
  line_count = 0;
  char buf[MAX_LINE_LEN];
  while (fgets(buf, MAX_LINE_LEN, f) && line_count < MAX_LINES) {
    int len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
    lines[line_count] = calloc(MAX_LINE_LEN, 1);
    strcpy(lines[line_count], buf);
    line_count++;
  }
  fclose(f);
  if (line_count == 0) {
    lines[0] = calloc(MAX_LINE_LEN, 1);
    line_count = 1;
  }
}

static void draw(void) {
  struct fenster *f = ctx.f;
  int w = f->width;
  int h = f->height;
  for (int i = 0; i < w * h; i++) f->buf[i] = BG_COLOR;

  int visible_lines = (h - padding * 2) / char_h;
  int start_line = scroll_y / char_h;

  for (int i = start_line; i < line_count && i < start_line + visible_lines + 1; i++) {
    int y = padding + i * char_h - scroll_y;
    if (y < -char_h || y > h) continue;

    char *line = lines[i];
    int x = padding;
    int len = strlen(line);

    for (int j = 0; j <= len; j++) {
      if (in_selection(i, j) && j < len) {
        int cw = char_width(line[j]);
        fenster_rect(f, x, y, cw, char_h, SEL_COLOR);
      }
      if (j < len) {
        char tmp[2] = { line[j], 0 };
        fenster_text(f, ctx.font, x, y, tmp, ctx.scale.font_scale, FG_COLOR);
        x += char_width(line[j]);
      }
    }
  }

  int cursor_y = padding + cursor_line * char_h - scroll_y;
  int cursor_x = col_to_x(lines[cursor_line], cursor_col);
  if (cursor_y >= 0 && cursor_y < h) {
    fenster_rect(f, cursor_x, cursor_y, 2, char_h, CURSOR_COLOR);
  }
}

static void scroll_to_cursor(void) {
  int cursor_y = cursor_line * char_h;
  int visible_h = ctx.f->height - padding * 2;
  if (cursor_y < scroll_y) {
    scroll_y = cursor_y;
  } else if (cursor_y + char_h > scroll_y + visible_h) {
    scroll_y = cursor_y + char_h - visible_h;
  }
  if (scroll_y < 0) scroll_y = 0;
}

static int quit_requested = 0;
static int cursor_moved = 0;

static void handle_key(int k, int mod, void *userdata) {
  (void)userdata;
  int ctrl = mod & KG_MOD_CTRL;
  int shift = mod & KG_MOD_SHIFT;
  cursor_moved = 1;

  if (ctrl && (k == 'Q' || k == 'q')) {
    quit_requested = 1;
  } else if (ctrl && (k == 'S' || k == 's')) {
    save_file();
  } else if (ctrl && (k == 'C' || k == 'c')) {
    char *sel = get_selection_text();
    kg_clipboard_copy(sel);
    free(sel);
  } else if (ctrl && (k == 'V' || k == 'v')) {
    char *text = kg_clipboard_paste();
    paste_text(text);
    free(text);
  } else if (ctrl && (k == 'A' || k == 'a')) {
    sel_start_line = 0;
    sel_start_col = 0;
    sel_end_line = line_count - 1;
    sel_end_col = strlen(lines[line_count - 1]);
    cursor_line = sel_end_line;
    cursor_col = sel_end_col;
  } else if (k == KG_KEY_UP) {
    if (cursor_line > 0) cursor_line--;
    if (cursor_col > (int)strlen(lines[cursor_line]))
      cursor_col = strlen(lines[cursor_line]);
    sel_start_line = -1;
  } else if (k == KG_KEY_DOWN) {
    if (cursor_line < line_count - 1) cursor_line++;
    if (cursor_col > (int)strlen(lines[cursor_line]))
      cursor_col = strlen(lines[cursor_line]);
    sel_start_line = -1;
  } else if (k == KG_KEY_LEFT) {
    if (cursor_col > 0) cursor_col--;
    else if (cursor_line > 0) {
      cursor_line--;
      cursor_col = strlen(lines[cursor_line]);
    }
    sel_start_line = -1;
  } else if (k == KG_KEY_RIGHT) {
    if (cursor_col < (int)strlen(lines[cursor_line])) cursor_col++;
    else if (cursor_line < line_count - 1) {
      cursor_line++;
      cursor_col = 0;
    }
    sel_start_line = -1;
  } else if (k == KG_KEY_BACKSPACE) {
    if (sel_start_line >= 0) delete_selection();
    else delete_char();
  } else if (k == KG_KEY_DELETE) {
    if (sel_start_line >= 0) delete_selection();
    else delete_forward();
  } else if (k == KG_KEY_RETURN) {
    if (sel_start_line >= 0) delete_selection();
    insert_newline();
  } else if (k >= 32 && k < 127) {
    if (sel_start_line >= 0) delete_selection();
    char c = k;
    if (shift) {
      static const char *shifted = ")!@#$%^&*(";
      if (k >= '0' && k <= '9') c = shifted[k - '0'];
      else if (k >= 'a' && k <= 'z') c = k - 32;
      else {
        switch(k) {
          case '-': c = '_'; break;
          case '=': c = '+'; break;
          case '[': c = '{'; break;
          case ']': c = '}'; break;
          case '\\': c = '|'; break;
          case ';': c = ':'; break;
          case '\'': c = '"'; break;
          case ',': c = '<'; break;
          case '.': c = '>'; break;
          case '/': c = '?'; break;
          case '`': c = '~'; break;
        }
      }
    } else if (k >= 'A' && k <= 'Z') {
      c = k + 32;
    }
    insert_char(c);
  }
}

static int run(const char *path) {
  uint32_t buf[W * H];
  struct fenster f = { .title = "note", .width = W, .height = H, .buf = buf };

  /* Initialize kgui context */
  ctx = kg_init(&f, newyork);

  /* Apply scale to dimensions */
  char_h = KG_SCALED(BASE_CHAR_H, ctx.scale);
  padding = KG_SCALED(BASE_PADDING, ctx.scale);

  if (path) {
    filename = strdup(path);
    load_file(path);
  } else {
    lines[0] = calloc(MAX_LINE_LEN, 1);
    line_count = 1;
  }

  fenster_open(&f);

  while (fenster_loop(&f) == 0 && !quit_requested) {
    kg_frame_begin(&ctx);

    /* Handle mouse for text selection */
    if (ctx.mouse_pressed) {
      int l = y_to_line(ctx.mouse_y);
      int c = x_to_col(lines[l], ctx.mouse_x);
      cursor_line = l;
      cursor_col = c;
      sel_start_line = l;
      sel_start_col = c;
      sel_end_line = l;
      sel_end_col = c;
      selecting = 1;
    } else if (ctx.mouse_down && selecting) {
      int l = y_to_line(ctx.mouse_y);
      int c = x_to_col(lines[l], ctx.mouse_x);
      sel_end_line = l;
      sel_end_col = c;
      cursor_line = l;
      cursor_col = c;
    } else if (ctx.mouse_released) {
      selecting = 0;
      if (sel_start_line == sel_end_line && sel_start_col == sel_end_col) {
        sel_start_line = -1;
      }
    }

    /* Handle scroll wheel */
    if (ctx.scroll != 0) {
      scroll_y -= ctx.scroll * char_h * 3;
      if (scroll_y < 0) scroll_y = 0;
      int max_scroll = line_count * char_h - (ctx.f->height - padding * 2);
      if (max_scroll < 0) max_scroll = 0;
      if (scroll_y > max_scroll) scroll_y = max_scroll;
    }

    /* Handle keyboard */
    cursor_moved = 0;
    kg_key_process(&ctx.key_repeat, f.keys, f.mod, handle_key, NULL);

    if (cursor_moved) scroll_to_cursor();
    draw();
    ctx.f->dirty = true;

    kg_frame_end(&ctx);
  }

  fenster_close(&f);
  return 0;
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
  (void)hInstance, (void)hPrevInstance, (void)nCmdShow;
  return run(pCmdLine[0] ? pCmdLine : NULL);
}
#else
#include <unistd.h>
int main(int argc, char **argv) {
  int detached = 0;
  const char *file = NULL;
  
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--detached") == 0) {
      detached = 1;
    } else {
      file = argv[i];
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
    char *args[4];
    args[0] = argv[0];
    args[1] = "--detached";
    args[2] = (char *)file;
    args[3] = NULL;
    execvp(argv[0], args);
    return 1;
  }
  
  return run(file);
}
#endif
