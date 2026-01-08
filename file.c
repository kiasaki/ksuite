#define _DEFAULT_SOURCE 1
#include "kgui.h"
#include "fonts/chicago12.h"
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#define W 800
#define H 800
#define MAX_ENTRIES 4096
#define MAX_PATH_LEN 4096
#define BASE_CHAR_H 16
#define BASE_PADDING 8
#define BASE_COL_SIZE_W 80
#define BASE_COL_DATE_W 140
#define BG_COLOR 0xffffff
#define FG_COLOR 0x000000
#define SEL_COLOR 0x000000
#define SEL_TEXT_COLOR 0xffffff
#define HEADER_COLOR 0xffffff

static kg_ctx ctx;
static kg_scroll scroll;
static int char_h = 16;
static int padding = 8;
static int col_size_w = 80;
static int col_date_w = 140;

typedef struct {
  char name[256];
  int is_dir;
  off_t size;
  time_t mtime;
  int selected;
} Entry;

static Entry entries[MAX_ENTRIES];
static int entry_count = 0;
static char current_path[MAX_PATH_LEN];

/* Input mode states */
#define MODE_NORMAL 0
#define MODE_RENAME 1
#define MODE_FILTER 2
static int input_mode = MODE_NORMAL;
static char input_buf[256];
static int input_len = 0;
static int rename_entry_idx = -1;
static char filter_buf[256];
static int filter_len = 0;
static int filtered_indices[MAX_ENTRIES];
static int filtered_count = 0;

/* Forward declarations */
static void update_filter(void);

static int text_width(const char *s) {
  return kg_text_width(ctx.font, s, ctx.scale.font_scale);
}

static int compare_entries(const void *a, const void *b) {
  const Entry *ea = (const Entry *)a;
  const Entry *eb = (const Entry *)b;
  
  if (strcmp(ea->name, "../") == 0) return -1;
  if (strcmp(eb->name, "../") == 0) return 1;
  
  if (ea->is_dir && !eb->is_dir) return -1;
  if (!ea->is_dir && eb->is_dir) return 1;
  
  return strcasecmp(ea->name, eb->name);
}

static void load_directory(const char *path) {
  entry_count = 0;
  scroll = kg_scroll_init();
  
  if (realpath(path, current_path) == NULL) {
    strcpy(current_path, path);
  }
  
  if (strlen(current_path) > 1) {
    strcpy(entries[entry_count].name, "../");
    entries[entry_count].is_dir = 1;
    entries[entry_count].size = 0;
    entries[entry_count].mtime = 0;
    entries[entry_count].selected = 0;
    entry_count++;
  }
  
  DIR *dir = opendir(current_path);
  if (!dir) return;
  
  struct dirent *de;
  while ((de = readdir(dir)) && entry_count < MAX_ENTRIES) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
    
    char fullpath[MAX_PATH_LEN];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, de->d_name);
    
    struct stat st;
    if (stat(fullpath, &st) != 0) continue;
    
    Entry *e = &entries[entry_count];
    if (S_ISDIR(st.st_mode)) {
      snprintf(e->name, sizeof(e->name), "%s/", de->d_name);
      e->is_dir = 1;
    } else {
      strncpy(e->name, de->d_name, sizeof(e->name) - 1);
      e->name[sizeof(e->name) - 1] = '\0';
      e->is_dir = 0;
    }
    e->size = st.st_size;
    e->mtime = st.st_mtime;
    e->selected = 0;
    entry_count++;
  }
  closedir(dir);
  
  qsort(entries, entry_count, sizeof(Entry), compare_entries);
  update_filter();
}

static void format_size(off_t size, char *buf, size_t len) {
  if (size < 1024) {
    snprintf(buf, len, "%ld B", (long)size);
  } else if (size < 1024 * 1024) {
    snprintf(buf, len, "%.1f KB", size / 1024.0);
  } else if (size < 1024 * 1024 * 1024) {
    snprintf(buf, len, "%.1f MB", size / (1024.0 * 1024.0));
  } else {
    snprintf(buf, len, "%.1f GB", size / (1024.0 * 1024.0 * 1024.0));
  }
}

static void format_date(time_t t, char *buf, size_t len) {
  struct tm *tm = localtime(&t);
  strftime(buf, len, "%Y-%m-%d %H:%M", tm);
}

static void draw_text_clipped(int x, int y, const char *s, int max_w, uint32_t color) {
  if (x < 0 || y < 0 || x >= ctx.f->width || y >= ctx.f->height) return;
  kg_text_clipped(&ctx, x, y, s, max_w, color);
}

static void draw(void) {
  struct fenster *f = ctx.f;
  int w = f->width;
  int h = f->height;
  int col_name_w = w - col_size_w - col_date_w;
  if (col_name_w < 100) col_name_w = 100;
  int scale = ctx.scale.scale;

  int display_count = (input_mode == MODE_FILTER) ? filtered_count : entry_count;

  /* Update scroll with current content/viewport sizes */
  int footer_h = char_h + padding / 2 + scale;
  int visible_h = h - padding - footer_h;
  kg_scroll_update(&scroll, display_count * char_h, visible_h);

  fenster_rect(f, 0, 0, w, h, BG_COLOR);

  int y = padding - scroll.offset;
  for (int i = 0; i < display_count; i++) {
    if (y + char_h < padding) {
      y += char_h;
      continue;
    }
    if (y >= h - footer_h) break;

    int entry_idx = (input_mode == MODE_FILTER) ? filtered_indices[i] : i;
    Entry *e = &entries[entry_idx];
    uint32_t bg = e->selected ? SEL_COLOR : BG_COLOR;
    uint32_t fg = e->selected ? SEL_TEXT_COLOR : FG_COLOR;

    if (y >= 0 && y < h) {
      int row_h = (y + char_h > h) ? h - y : char_h;
      fenster_rect(f, 0, y, w, row_h, bg);
    }

    int x = padding;
    draw_text_clipped(x, y, e->name, col_name_w - padding, fg);
    x += col_name_w;

    if (!e->is_dir || strcmp(e->name, "../") == 0) {
      if (strcmp(e->name, "../") != 0) {
        char size_str[32];
        format_size(e->size, size_str, sizeof(size_str));
        draw_text_clipped(x - padding + (col_size_w - text_width(size_str)), y, size_str, col_size_w - padding, fg);
      }
    } else {
      draw_text_clipped(x - padding + (col_size_w - text_width("--")), y, "--", col_size_w - padding, fg);
    }
    x += col_size_w;

    if (e->mtime > 0) {
      char date_str[32];
      format_date(e->mtime, date_str, sizeof(date_str));
      draw_text_clipped(x - padding*2 + (col_date_w - text_width(date_str)), y, date_str, col_date_w - padding, fg);
    }

    y += char_h;
  }

  fenster_rect(f, 0, h - char_h - padding/2, w, scale, FG_COLOR);
  fenster_rect(f, 0, h - char_h - padding/2+scale, w, char_h + padding/2 - scale, HEADER_COLOR);

  if (input_mode == MODE_RENAME) {
    char status[512];
    snprintf(status, sizeof(status), "rename: %s", input_buf);
    draw_text_clipped(padding, h - char_h, status, w - padding*2, FG_COLOR);
  } else if (input_mode == MODE_FILTER) {
    char status[512];
    snprintf(status, sizeof(status), "/%s (%d matches)", filter_buf, filtered_count > 0 ? filtered_count - 1 : 0);
    draw_text_clipped(padding, h - char_h, status, w - padding*2, FG_COLOR);
  } else {
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%d items", entry_count > 0 ? entry_count - 1 : 0);
    int count_w = text_width(count_str);

    draw_text_clipped(padding, h - char_h, current_path, w - padding*3 - count_w, FG_COLOR);
    draw_text_clipped(w - padding - count_w, h - char_h, count_str, count_w, FG_COLOR);
  }
}

static int y_to_entry(int y) {
  if (y < padding) return -1;
  int display_idx = (y - padding + scroll.offset) / char_h;
  if (input_mode == MODE_FILTER) {
    if (display_idx < 0 || display_idx >= filtered_count) return -1;
    return filtered_indices[display_idx];
  }
  if (display_idx < 0 || display_idx >= entry_count) return -1;
  return display_idx;
}

static void clear_selection(void) {
  for (int i = 0; i < entry_count; i++) {
    entries[i].selected = 0;
  }
}

static const char *get_extension(const char *path) {
  const char *dot = strrchr(path, '.');
  if (!dot || dot == path) return "";
  return dot;
}

static int is_plaintext(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return 0;
  unsigned char buf[256];
  size_t n = fread(buf, 1, sizeof(buf), fp);
  fclose(fp);
  if (n == 0) return 1; /* empty file counts as text */
  for (size_t i = 0; i < n; i++) {
    unsigned char c = buf[i];
    /* printable ASCII, tab, newline, carriage return */
    if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r') continue;
    return 0;
  }
  return 1;
}

static void open_file(const char *path) {
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    const char *ext = get_extension(path);
    if (strcasecmp(ext, ".pdf") == 0) {
      execlp("mupdf", "mupdf", path, NULL);
    } else if (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".jpg") == 0 || 
               strcasecmp(ext, ".jpeg") == 0 || strcasecmp(ext, ".gif") == 0) {
      execlp("feh", "feh", path, NULL);
    } else if (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".md") == 0) {
      execlp("knote", "knote", path, NULL);
    } else if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".svg") == 0) {
      execlp("surf", "surf", path, NULL);
    } else if (is_plaintext(path)) {
      execlp("knote", "knote", path, NULL);
    }
    execlp("xdg-open", "xdg-open", path, NULL);
    exit(1);
  }
}

static void copy_selected(void) {
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;

  for (int i = 0; i < entry_count; i++) {
    if (!entries[i].selected) continue;
    if (strcmp(entries[i].name, "../") == 0) continue;

    char fullpath[MAX_PATH_LEN];
    char name[256];
    strncpy(name, entries[i].name, sizeof(name));
    int namelen = strlen(name);
    if (namelen > 0 && name[namelen-1] == '/') {
      name[namelen-1] = '\0';
    }
    snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, name);

    size_t pathlen = strlen(fullpath);
    if (len + pathlen + 2 >= cap) {
      cap = cap ? cap * 2 : 1024;
      buf = realloc(buf, cap);
    }
    if (len > 0) buf[len++] = '\n';
    memcpy(buf + len, fullpath, pathlen);
    len += pathlen;
  }

  if (buf) {
    buf[len] = '\0';
    kg_clipboard_copy(buf);
    free(buf);
  }
}

static void paste_files(void) {
  char *clip = kg_clipboard_paste();
  if (!clip) return;

  char *line = strtok(clip, "\n");
  while (line) {
    while (*line == ' ' || *line == '\t') line++;
    size_t linelen = strlen(line);
    while (linelen > 0 && (line[linelen-1] == ' ' || line[linelen-1] == '\t' || line[linelen-1] == '\r')) {
      line[--linelen] = '\0';
    }

    struct stat st;
    if (stat(line, &st) == 0) {
      char *basename = strrchr(line, '/');
      basename = basename ? basename + 1 : line;

      char destpath[MAX_PATH_LEN];
      snprintf(destpath, sizeof(destpath), "%s/%s", current_path, basename);

      char cmd[MAX_PATH_LEN * 2 + 32];
      snprintf(cmd, sizeof(cmd), "cp -r \"%s\" \"%s\"", line, destpath);
      system(cmd);
    }

    line = strtok(NULL, "\n");
  }

  free(clip);
  load_directory(current_path);
}

static int last_click_entry = -1;
static int quit_requested = 0;

static int visible_rows(void) {
  return scroll.visible_height / char_h;
}

static void update_filter(void) {
  filtered_count = 0;
  if (filter_len == 0) {
    for (int i = 0; i < entry_count; i++) {
      filtered_indices[filtered_count++] = i;
    }
    return;
  }
  for (int i = 0; i < entry_count; i++) {
    if (strcasestr(entries[i].name, filter_buf) != NULL) {
      filtered_indices[filtered_count++] = i;
    }
  }
}

static void delete_selected(void) {
  for (int i = 0; i < entry_count; i++) {
    if (!entries[i].selected) continue;
    if (strcmp(entries[i].name, "../") == 0) continue;
    
    char fullpath[MAX_PATH_LEN];
    char name[256];
    strncpy(name, entries[i].name, sizeof(name));
    int namelen = strlen(name);
    if (namelen > 0 && name[namelen-1] == '/') {
      name[namelen-1] = '\0';
    }
    snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, name);
    
    struct stat st;
    if (stat(fullpath, &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        char cmd[MAX_PATH_LEN + 32];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", fullpath);
        system(cmd);
      } else {
        unlink(fullpath);
      }
    }
  }
  load_directory(current_path);
}

static void do_rename(void) {
  if (rename_entry_idx < 0 || rename_entry_idx >= entry_count) return;
  if (input_len == 0) return;
  
  Entry *e = &entries[rename_entry_idx];
  char oldpath[MAX_PATH_LEN], newpath[MAX_PATH_LEN];
  char oldname[256];
  strncpy(oldname, e->name, sizeof(oldname));
  int namelen = strlen(oldname);
  if (namelen > 0 && oldname[namelen-1] == '/') {
    oldname[namelen-1] = '\0';
  }
  snprintf(oldpath, sizeof(oldpath), "%s/%s", current_path, oldname);
  snprintf(newpath, sizeof(newpath), "%s/%s", current_path, input_buf);
  
  rename(oldpath, newpath);
  load_directory(current_path);
}

static void create_new_file(void) {
  char filepath[MAX_PATH_LEN];
  snprintf(filepath, sizeof(filepath), "%s/untitled.txt", current_path);
  
  int n = 1;
  while (access(filepath, F_OK) == 0) {
    snprintf(filepath, sizeof(filepath), "%s/untitled%d.txt", current_path, n++);
  }
  
  FILE *fp = fopen(filepath, "w");
  if (fp) fclose(fp);
  load_directory(current_path);
}

static void create_new_folder(void) {
  char folderpath[MAX_PATH_LEN];
  snprintf(folderpath, sizeof(folderpath), "%s/untitled", current_path);
  
  int n = 1;
  while (access(folderpath, F_OK) == 0) {
    snprintf(folderpath, sizeof(folderpath), "%s/untitled%d", current_path, n++);
  }
  
  mkdir(folderpath, 0755);
  load_directory(current_path);
}

static void start_rename(void) {
  for (int i = 0; i < entry_count; i++) {
    if (entries[i].selected && strcmp(entries[i].name, "../") != 0) {
      rename_entry_idx = i;
      char name[256];
      strncpy(name, entries[i].name, sizeof(name));
      int namelen = strlen(name);
      if (namelen > 0 && name[namelen-1] == '/') {
        name[namelen-1] = '\0';
      }
      strncpy(input_buf, name, sizeof(input_buf));
      input_len = strlen(input_buf);
      input_mode = MODE_RENAME;
      return;
    }
  }
}

static char map_key(int k, int shift) {
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
  return c;
}

static void handle_key(int k, int mod, void *userdata) {
  (void)userdata;
  int ctrl = mod & KG_MOD_CTRL;
  int shift = mod & KG_MOD_SHIFT;

  if (input_mode == MODE_RENAME) {
    if (k == 27) { /* Escape */
      input_mode = MODE_NORMAL;
      input_len = 0;
      input_buf[0] = '\0';
    } else if (k == KG_KEY_RETURN) {
      do_rename();
      input_mode = MODE_NORMAL;
      input_len = 0;
      input_buf[0] = '\0';
    } else if (k == KG_KEY_BACKSPACE) {
      if (input_len > 0) {
        input_buf[--input_len] = '\0';
      }
    } else if (k >= 32 && k < 127 && input_len < 254) {
      input_buf[input_len++] = map_key(k, shift);
      input_buf[input_len] = '\0';
    }
    return;
  }

  if (input_mode == MODE_FILTER) {
    if (k == 27) { /* Escape */
      input_mode = MODE_NORMAL;
      filter_len = 0;
      filter_buf[0] = '\0';
      update_filter();
    } else if (k == KG_KEY_BACKSPACE) {
      if (filter_len > 0) {
        filter_buf[--filter_len] = '\0';
        update_filter();
      }
    } else if (k >= 32 && k < 127 && filter_len < 254) {
      filter_buf[filter_len++] = map_key(k, shift);
      filter_buf[filter_len] = '\0';
      update_filter();
    }
    return;
  }

  /* Normal mode */
  if (ctrl && (k == 'Q' || k == 'q')) {
    quit_requested = 1;
  } else if (ctrl && (k == 'C' || k == 'c')) {
    copy_selected();
  } else if (ctrl && (k == 'V' || k == 'v')) {
    paste_files();
  } else if (ctrl && (k == 'A' || k == 'a')) {
    for (int i = 0; i < entry_count; i++) {
      if (strcmp(entries[i].name, "../") != 0) {
        entries[i].selected = 1;
      }
    }
  } else if (ctrl && k == KG_KEY_BACKSPACE) {
    delete_selected();
  } else if (ctrl && (k == 'R' || k == 'r')) {
    start_rename();
  } else if (ctrl && (k == 'N' || k == 'n')) {
    create_new_file();
  } else if (ctrl && (k == 'D' || k == 'd')) {
    create_new_folder();
  } else if (k == '/') {
    input_mode = MODE_FILTER;
    filter_len = 0;
    filter_buf[0] = '\0';
    update_filter();
  } else if (shift && k == KG_KEY_UP) {
    kg_scroll_by(&scroll, -char_h);
  } else if (shift && k == KG_KEY_DOWN) {
    kg_scroll_by(&scroll, char_h);
  } else if (shift && k == KG_KEY_PAGEUP) {
    kg_scroll_by(&scroll, -visible_rows() * char_h);
  } else if (shift && k == KG_KEY_PAGEDOWN) {
    kg_scroll_by(&scroll, visible_rows() * char_h);
  }
}

static int run(const char *path) {
  uint32_t *buf = malloc(W * H * sizeof(uint32_t));
  if (!buf) return 1;
  struct fenster f = { .title = "file", .width = W, .height = H, .buf = buf };

  /* Initialize kgui context */
  ctx = kg_init(&f, chicago);
  ctx.key_repeat = kg_key_repeat_init_custom(300, 30);
  scroll = kg_scroll_init();

  /* Apply scale to dimensions */
  char_h = KG_SCALED(BASE_CHAR_H, ctx.scale);
  padding = KG_SCALED(BASE_PADDING, ctx.scale);
  col_size_w = KG_SCALED(BASE_COL_SIZE_W, ctx.scale);
  col_date_w = KG_SCALED(BASE_COL_DATE_W, ctx.scale);

  if (path) {
    load_directory(path);
  } else {
    char cwd[MAX_PATH_LEN];
    if (getcwd(cwd, sizeof(cwd))) {
      load_directory(cwd);
    } else {
      load_directory("/");
    }
  }

  fenster_open(&f);

  while (fenster_loop(&f) == 0 && !quit_requested) {
    kg_frame_begin(&ctx);

    /* Handle mouse clicks */
    if (ctx.mouse_pressed) {
      int idx = y_to_entry(ctx.mouse_y);

      if (ctx.double_clicked && idx >= 0 && idx == last_click_entry) {
        /* Double-click: open directory or file */
        Entry *e = &entries[idx];
        if (e->is_dir) {
          /* Reset filter mode when navigating */
          input_mode = MODE_NORMAL;
          filter_len = 0;
          filter_buf[0] = '\0';
          if (strcmp(e->name, "../") == 0) {
            char *parent = strrchr(current_path, '/');
            if (parent && parent != current_path) {
              *parent = '\0';
              load_directory(current_path);
            } else if (parent == current_path) {
              load_directory("/");
            }
          } else {
            char newpath[MAX_PATH_LEN];
            char name[256];
            strncpy(name, e->name, sizeof(name));
            int namelen = strlen(name);
            if (namelen > 0 && name[namelen-1] == '/') {
              name[namelen-1] = '\0';
            }
            snprintf(newpath, sizeof(newpath), "%s/%s", current_path, name);
            load_directory(newpath);
          }
        } else {
          char fullpath[MAX_PATH_LEN];
          snprintf(fullpath, sizeof(fullpath), "%s/%s", current_path, e->name);
          open_file(fullpath);
        }
        last_click_entry = -1;
      } else {
        /* Single click: select/toggle */
        if (idx >= 0) {
          int ctrl = ctx.f->mod & KG_MOD_CTRL;
          if (!ctrl) {
            clear_selection();
          }
          entries[idx].selected = !entries[idx].selected;
        } else {
          if (!(ctx.f->mod & KG_MOD_CTRL)) {
            clear_selection();
          }
        }
        last_click_entry = idx;
      }
    }

    /* Handle scroll wheel */
    if (ctx.scroll != 0) {
      kg_scroll_by(&scroll, -ctx.scroll * char_h * 3);
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
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
  (void)hInstance, (void)hPrevInstance, (void)nCmdShow;
  return run(pCmdLine[0] ? pCmdLine : NULL);
}
#else
int main(int argc, char **argv) {
  int detached = 0;
  const char *path = NULL;
  
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--detached") == 0) {
      detached = 1;
    } else {
      path = argv[i];
    }
  }
  
  if (!detached) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) return 0;
    setsid();
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
    char *args[4];
    args[0] = argv[0];
    args[1] = "--detached";
    args[2] = (char *)path;
    args[3] = NULL;
    execvp(argv[0], args);
    return 1;
  }
  
  return run(path);
}
#endif
