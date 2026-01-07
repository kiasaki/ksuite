#include "kgui.h"
#include "fonts/chicago12.h"

#define W 340
#define H 420
#define MAX_DISPLAY 20
#define BG_COLOR 0xffffff
#define FG_COLOR 0x000000

static kg_ctx ctx;
static int char_h = 16;
static int display_char_h = 32;
static int padding = 16;
static int btn_w, btn_h, btn_gap;

static char input1[MAX_DISPLAY + 1] = "";
static char input2[MAX_DISPLAY + 1] = "";
static char pending_op = 0;
static int editing_second = 0;

static int quit_requested = 0;

static char *current_input(void) {
  return editing_second ? input2 : input1;
}

static void clear_all(void) {
  input1[0] = '\0';
  input2[0] = '\0';
  pending_op = 0;
  editing_second = 0;
}

static void clear_entry(void) {
  char *cur = current_input();
  cur[0] = '\0';
}

static void append_digit(char d) {
  char *cur = current_input();
  int len = strlen(cur);
  if (len < MAX_DISPLAY) {
    if (len == 0 && d == '0') return;
    if (len == 1 && cur[0] == '0' && d != '.') {
      cur[0] = d;
    } else {
      cur[len] = d;
      cur[len + 1] = '\0';
    }
  }
}

static void append_dot(void) {
  char *cur = current_input();
  int len = strlen(cur);
  if (strchr(cur, '.') == NULL && len < MAX_DISPLAY) {
    if (len == 0) {
      strcpy(cur, "0.");
    } else {
      cur[len] = '.';
      cur[len + 1] = '\0';
    }
  }
}

static void backspace(void) {
  char *cur = current_input();
  int len = strlen(cur);
  if (len > 0) {
    cur[len - 1] = '\0';
  } else if (editing_second) {
    editing_second = 0;
    pending_op = 0;
  }
}

static void negate(void) {
  char *cur = current_input();
  int len = strlen(cur);
  if (len == 0) return;
  if (cur[0] == '-') {
    memmove(cur, cur + 1, len);
  } else if (len < MAX_DISPLAY) {
    memmove(cur + 1, cur, len + 1);
    cur[0] = '-';
  }
}

static double parse_input(const char *s) {
  if (s[0] == '\0') return 0;
  return atof(s);
}

static void format_result(double val, char *buf) {
  if (val == (long long)val && val >= -999999999999LL && val <= 999999999999LL) {
    snprintf(buf, MAX_DISPLAY, "%lld", (long long)val);
  } else {
    snprintf(buf, MAX_DISPLAY, "%.8g", val);
  }
}

static double compute_result(int *error) {
  double a = parse_input(input1);
  double b = parse_input(input2);
  *error = 0;
  switch (pending_op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/':
      if (b == 0) { *error = 1; return 0; }
      return a / b;
    default: return a;
  }
}

static void set_operation(char op) {
  if (editing_second && input2[0] != '\0') {
    int err;
    double r = compute_result(&err);
    if (!err) {
      format_result(r, input1);
      input2[0] = '\0';
    }
  }
  if (input1[0] == '\0') strcpy(input1, "0");
  pending_op = op;
  editing_second = 1;
}

static void equals(void) {
  if (!pending_op) return;
  int err;
  double r = compute_result(&err);
  if (err) {
    strcpy(input1, "Error");
  } else {
    format_result(r, input1);
  }
  input2[0] = '\0';
  pending_op = 0;
  editing_second = 0;
}

/* Button definition */
typedef struct {
  const char *label;
  char key;      /* Keyboard shortcut */
  int col, row;
  int width;     /* In columns (default 1) */
} Button;

static Button buttons[] = {
  { "C",  'c', 0, 0, 1 },
  { "CE", 'e', 1, 0, 1 },
  { "+/-",'n', 2, 0, 1 },
  { "/",  '/', 3, 0, 1 },

  { "7",  '7', 0, 1, 1 },
  { "8",  '8', 1, 1, 1 },
  { "9",  '9', 2, 1, 1 },
  { "*",  '*', 3, 1, 1 },

  { "4",  '4', 0, 2, 1 },
  { "5",  '5', 1, 2, 1 },
  { "6",  '6', 2, 2, 1 },
  { "-",  '-', 3, 2, 1 },

  { "1",  '1', 0, 3, 1 },
  { "2",  '2', 1, 3, 1 },
  { "3",  '3', 2, 3, 1 },
  { "+",  '+', 3, 3, 1 },

  { "0",  '0', 0, 4, 2 },
  { ".",  '.', 2, 4, 1 },
  { "=",  '=', 3, 4, 1 },
};

#define NUM_BUTTONS (sizeof(buttons) / sizeof(buttons[0]))

static void get_button_rect(Button *b, int *x, int *y, int *w, int *h) {
  int grid_x = padding;
  int display_padding = KG_SCALED(8, ctx.scale);
  int display_h = display_char_h * 2 + display_padding * 3;
  int grid_y = padding + display_h + padding;

  *x = grid_x + b->col * (btn_w + btn_gap);
  *y = grid_y + b->row * (btn_h + btn_gap);
  *w = b->width * btn_w + (b->width - 1) * btn_gap;
  *h = btn_h;
}

static void handle_button(Button *b) {
  const char *lbl = b->label;

  if (strcmp(lbl, "C") == 0) clear_all();
  else if (strcmp(lbl, "CE") == 0) clear_entry();
  else if (strcmp(lbl, "+/-") == 0) negate();
  else if (strcmp(lbl, "+") == 0) set_operation('+');
  else if (strcmp(lbl, "-") == 0) set_operation('-');
  else if (strcmp(lbl, "*") == 0) set_operation('*');
  else if (strcmp(lbl, "/") == 0) set_operation('/');
  else if (strcmp(lbl, "=") == 0) equals();
  else if (strcmp(lbl, ".") == 0) append_dot();
  else if (lbl[0] >= '0' && lbl[0] <= '9') append_digit(lbl[0]);
}

static void handle_key(int k, int mod, void *userdata) {
  (void)userdata;
  (void)mod;

  if (k == 'q' || k == 'Q' || k == KG_KEY_ESCAPE) {
    quit_requested = 1;
    return;
  }

  /* Backspace */
  if (k == KG_KEY_BACKSPACE) {
    backspace();
    return;
  }

  /* Enter = equals */
  if (k == KG_KEY_RETURN) {
    equals();
    return;
  }

  /* Map keys to buttons */
  char key = 0;
  if (k >= '0' && k <= '9') key = k;
  else if (k == '+' || k == '=' ) key = '+';  /* Shift+= is + on US keyboard */
  else if (k == '-') key = '-';
  else if (k == '*' || k == '8') {
    if (mod & KG_MOD_SHIFT) key = '*';
    else if (k == '*') key = '*';
    else key = '8';
  }
  else if (k == '/') key = '/';
  else if (k == '.') key = '.';
  else if (k == 'c' || k == 'C') key = 'c';
  else if (k == 'e' || k == 'E') key = 'e';
  else if (k == 'n' || k == 'N') key = 'n';

  if (key) {
    for (int i = 0; i < (int)NUM_BUTTONS; i++) {
      if (buttons[i].key == key) {
        handle_button(&buttons[i]);
        return;
      }
    }
  }
}

static void draw(void) {
  struct fenster *f = ctx.f;
  int w = f->width;
  int h = f->height;

  /* Clear background */
  for (int i = 0; i < w * h; i++) f->buf[i] = BG_COLOR;

  /* Draw display area */
  int display_padding = KG_SCALED(8, ctx.scale);
  int display_h = display_char_h * 2 + display_padding * 3;
  kg_region display_r = kg_region_create(padding, padding, w - padding * 2, display_h, display_padding);
  kg_border(&ctx, &display_r, ctx.scale.scale, FG_COLOR);

  /* Build expression string: "10 + 5" */
  char expr[64] = "";
  if (input1[0]) strcat(expr, input1);
  else strcat(expr, "0");
  if (pending_op) {
    char ops[4] = " x ";
    ops[1] = pending_op;
    strcat(expr, ops);
    if (input2[0]) strcat(expr, input2);
  }

  /* Build result preview */
  char result_str[32] = "";
  if (pending_op) {
    int err;
    double r = compute_result(&err);
    if (!err) {
      strcpy(result_str, "= ");
      format_result(r, result_str + 2);
    } else {
      strcpy(result_str, "= Error");
    }
  }

  /* Draw expression (top line) - 2x scale */
  int display_font_scale = ctx.scale.font_scale * 2;
  int ew = kg_text_width(ctx.font, expr, display_font_scale);
  int ex = display_r.x + display_r.w - display_r.padding - ew;
  int ey = display_r.y + display_r.padding;
  fenster_text(f, ctx.font, ex, ey, expr, display_font_scale, FG_COLOR);

  /* Draw result preview (bottom line) - 2x scale */
  if (result_str[0]) {
    int rw = kg_text_width(ctx.font, result_str, display_font_scale);
    int rx = display_r.x + display_r.w - display_r.padding - rw;
    int ry = display_r.y + display_r.h - display_r.padding - display_char_h;
    fenster_text(f, ctx.font, rx, ry, result_str, display_font_scale, FG_COLOR);
  }

  /* Draw buttons */
  for (int i = 0; i < (int)NUM_BUTTONS; i++) {
    Button *b = &buttons[i];
    int bx, by, bw, bh;
    get_button_rect(b, &bx, &by, &bw, &bh);

    /* Determine button state */
    int hovered = kg_hovered_rect(&ctx, bx, by, bw, bh);
    int pressed = hovered && ctx.mouse_down;

    uint32_t bg = pressed ? FG_COLOR : BG_COLOR;
    uint32_t fg = pressed ? BG_COLOR : FG_COLOR;

    /* Draw button background */
    fenster_rect(f, bx, by, bw, bh, bg);

    /* Draw button border */
    kg_region btn_r = kg_region_create(bx, by, bw, bh, 0);
    kg_border(&ctx, &btn_r, ctx.scale.scale, FG_COLOR);

    /* Center text in button */
    int ltw = kg_text_width(ctx.font, b->label, ctx.scale.font_scale);
    int ltx = bx + (bw - ltw) / 2;
    int lty = by + (bh - char_h) / 2;
    kg_text_at(&ctx, ltx, lty, b->label, fg);
  }
}

static int run(void) {
  uint32_t buf[W * H];
  struct fenster f = { .title = "calc", .width = W, .height = H, .buf = buf };

  /* Initialize kgui context */
  ctx = kg_init(&f, chicago);

  /* Apply scale to dimensions */
  char_h = KG_SCALED(16, ctx.scale);
  display_char_h = KG_SCALED(32, ctx.scale);
  padding = KG_SCALED(16, ctx.scale);
  btn_gap = KG_SCALED(6, ctx.scale);

  /* Calculate button sizes (4 columns) */
  int grid_w = f.width - padding * 2;
  btn_w = (grid_w - btn_gap * 3) / 4;
  btn_h = KG_SCALED(48, ctx.scale);

  fenster_open(&f);

  while (fenster_loop(&f) == 0 && !quit_requested) {
    kg_frame_begin(&ctx);

    /* Handle button clicks */
    if (ctx.mouse_pressed) {
      for (int i = 0; i < (int)NUM_BUTTONS; i++) {
        int bx, by, bw, bh;
        get_button_rect(&buttons[i], &bx, &by, &bw, &bh);
        if (kg_clicked_rect(&ctx, bx, by, bw, bh)) {
          handle_button(&buttons[i]);
          break;
        }
      }
    }

    /* Handle keyboard */
    kg_key_process(&ctx.key_repeat, f.keys, f.mod, handle_key, NULL);

    draw();

    kg_frame_end(&ctx);
  }

  fenster_close(&f);
  return 0;
}

#if defined(_WIN32)
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
  (void)hInstance, (void)hPrevInstance, (void)pCmdLine, (void)nCmdShow;
  return run();
}
#else
int main(void) {
  return run();
}
#endif
