#define _DEFAULT_SOURCE 1
#include "fenster.h"
#include "chicago12.h"
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define W 800
#define H 800
#define MAX_ENTRIES 4096
#define MAX_PATH_LEN 4096
#define BASE_FONT_SCALE 1
#define BASE_CHAR_H 16
#define BASE_PADDING 8
#define BG_COLOR 0xffffff
#define FG_COLOR 0x000000
#define SEL_COLOR 0x000000
#define SEL_TEXT_COLOR 0xffffff
#define HEADER_COLOR 0xffffff

static int k_scale = 1;
static int font_scale = 1;
static int char_h = 16;
static int padding = 8;

static unsigned char *font = chicago;

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
static int scroll_y = 0;

static int col_size_w = 80;
static int col_date_w = 140;

static void clipboard_copy(const char *text) {
  if (!text) return;
  FILE *p = popen("xclip -selection clipboard", "w");
  if (p) {
    fputs(text, p);
    pclose(p);
  }
}

static char *clipboard_paste(void) {
  FILE *p = popen("xclip -selection clipboard -o 2>/dev/null", "r");
  if (!p) return NULL;

  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;
  char tmp[256];

  while (fgets(tmp, sizeof(tmp), p)) {
    size_t n = strlen(tmp);
    if (len + n >= cap) {
      cap = cap ? cap * 2 : 256;
      buf = realloc(buf, cap);
    }
    memcpy(buf + len, tmp, n);
    len += n;
  }
  if (buf) buf[len] = '\0';
  pclose(p);
  return buf;
}

static int char_width(char c) {
  return font[(unsigned char)c] * font_scale;
}

static int text_width(const char *s) {
  int w = 0;
  for (int i = 0; s[i]; i++) {
    w += char_width(s[i]);
  }
  return w;
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
  scroll_y = 0;
  
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

static void draw_text_clipped(struct fenster *f, int x, int y, const char *s, int max_w, uint32_t color) {
  if (x < 0 || y < 0 || x >= f->width || y >= f->height) return;
  int w = 0;
  char tmp[2] = {0, 0};
  while (*s) {
    int cw = char_width(*s);
    if (w + cw > max_w) break;
    if (x + w >= f->width) break;
    tmp[0] = *s;
    fenster_text(f, font, x + w, y, tmp, font_scale, color);
    w += cw;
    s++;
  }
}

static void draw(struct fenster *f) {
  int w = f->width;
  int h = f->height;
  int col_name_w = w - col_size_w - col_date_w;
  if (col_name_w < 100) col_name_w = 100;
  
  fenster_rect(f, 0, 0, w, h, BG_COLOR);
  
  int y = padding - scroll_y;
  for (int i = 0; i < entry_count; i++) {
    if (y + char_h < padding) {
      y += char_h;
      continue;
    }
    if (y >= h) break;
    
    Entry *e = &entries[i];
    uint32_t bg = e->selected ? SEL_COLOR : BG_COLOR;
    uint32_t fg = e->selected ? SEL_TEXT_COLOR : FG_COLOR;
    
    if (y >= 0 && y < h) {
      int row_h = (y + char_h > h) ? h - y : char_h;
      fenster_rect(f, 0, y, w, row_h, bg);
    }
    
    int x = padding;
    draw_text_clipped(f, x, y, e->name, col_name_w - padding, fg);
    x += col_name_w;
    
    if (!e->is_dir || strcmp(e->name, "../") == 0) {
      if (strcmp(e->name, "../") != 0) {
        char size_str[32];
        format_size(e->size, size_str, sizeof(size_str));
        draw_text_clipped(f, x - padding + (col_size_w - text_width(size_str)), y, size_str, col_size_w - padding, fg);
      }
    } else {
      draw_text_clipped(f, x - padding + (col_size_w - text_width("--")), y, "--", col_size_w - padding, fg);
    }
    x += col_size_w;
    
    if (e->mtime > 0) {
      char date_str[32];
      format_date(e->mtime, date_str, sizeof(date_str));
      draw_text_clipped(f, x - padding*2 + (col_date_w - text_width(date_str)), y, date_str, col_date_w - padding, fg);
    }
    
    y += char_h;
  }
  
  fenster_rect(f, 0, h - char_h - padding/2, w, k_scale, FG_COLOR);
  fenster_rect(f, 0, h - char_h - padding/2+k_scale, w, char_h + padding/2 - k_scale, HEADER_COLOR);
  
  char count_str[32];
  snprintf(count_str, sizeof(count_str), "%d items", entry_count > 0 ? entry_count - 1 : 0);
  int count_w = text_width(count_str);
  
  draw_text_clipped(f, padding, h - char_h, current_path, w - padding*3 - count_w, FG_COLOR);
  draw_text_clipped(f, w - padding - count_w, h - char_h, count_str, count_w, FG_COLOR);
}

static int y_to_entry(int y) {
  if (y < padding) return -1;
  int idx = (y - padding + scroll_y) / char_h;
  if (idx < 0 || idx >= entry_count) return -1;
  return idx;
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
    clipboard_copy(buf);
    free(buf);
  }
}

static void paste_files(void) {
  char *clip = clipboard_paste();
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
      snprintf(cmd, sizeof(cmd), "mv \"%s\" \"%s\"", line, destpath);
      system(cmd);
    }
    
    line = strtok(NULL, "\n");
  }
  
  free(clip);
  load_directory(current_path);
}

static int prev_keys[256];
static int64_t key_press_time[256];
static int64_t key_repeat_time[256];
#define KEY_REPEAT_DELAY 300
#define KEY_REPEAT_RATE 30

static int prev_mouse = 0;
static int64_t last_click_time = 0;
static int last_click_entry = -1;
#define DOUBLE_CLICK_MS 400

static int quit_requested = 0;
static int window_h = H;

static int max_scroll(void) {
  int footer_h = char_h + padding / 2 + 1;
  int visible_h = window_h - padding - footer_h;
  int content_h = entry_count * char_h;
  return content_h > visible_h ? content_h - visible_h : 0;
}

static int visible_rows(void) {
  int footer_h = char_h + padding / 2 + 1;
  int visible_h = window_h - padding - footer_h;
  return visible_h / char_h;
}

static void handle_key(int k, int mod) {
  int ctrl = mod & 1;
  int shift = (mod >> 1) & 1;
  
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
  } else if (shift && k == 17) { /* Up */
    scroll_y -= char_h;
    if (scroll_y < 0) scroll_y = 0;
  } else if (shift && k == 18) { /* Down */
    scroll_y += char_h;
    int ms = max_scroll();
    if (scroll_y > ms) scroll_y = ms;
  } else if (shift && k == 3) { /* Page Up */
    scroll_y -= visible_rows() * char_h;
    if (scroll_y < 0) scroll_y = 0;
  } else if (shift && k == 4) { /* Page Down */
    scroll_y += visible_rows() * char_h;
    int ms = max_scroll();
    if (scroll_y > ms) scroll_y = ms;
  }
}

static int run(const char *path) {
  char *scale_env = getenv("K_SCALE");
  if (scale_env) {
    k_scale = atoi(scale_env);
    if (k_scale < 1) k_scale = 1;
  }
  font_scale = BASE_FONT_SCALE * k_scale;
  char_h = BASE_CHAR_H * k_scale;
  padding = BASE_PADDING * k_scale;
  col_size_w = col_size_w * k_scale;
  col_date_w = col_date_w * k_scale;
  
  uint32_t *buf = malloc(W * H * sizeof(uint32_t));
  if (!buf) return 1;
  struct fenster f = { .title = "file", .width = W, .height = H, .buf = buf };
  
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
  int64_t now = fenster_time();
  
  while (fenster_loop(&f) == 0 && !quit_requested) {
    if (f.size_changed) {
      f.size_changed = 0;
      buf = f.buf;
      window_h = f.height;
    }
    
    if (f.mouse && !prev_mouse) {
      int64_t click_time = fenster_time();
      int idx = y_to_entry(f.y);
      
      if (idx >= 0 && idx == last_click_entry && 
          click_time - last_click_time < DOUBLE_CLICK_MS) {
        Entry *e = &entries[idx];
        if (e->is_dir) {
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
        if (idx >= 0) {
          int ctrl = f.mod & 1;
          if (!ctrl) {
            clear_selection();
          }
          entries[idx].selected = !entries[idx].selected;
        } else {
          if (!(f.mod & 1)) {
            clear_selection();
          }
        }
        last_click_entry = idx;
        last_click_time = click_time;
      }
    }
    prev_mouse = f.mouse;
    
    int64_t now_time = fenster_time();
    for (int k = 0; k < 256; k++) {
      if (f.keys[k] && !prev_keys[k]) {
        handle_key(k, f.mod);
        key_press_time[k] = now_time;
        key_repeat_time[k] = now_time;
      } else if (f.keys[k] && prev_keys[k]) {
        int64_t held = now_time - key_press_time[k];
        if (held > KEY_REPEAT_DELAY) {
          int64_t since_repeat = now_time - key_repeat_time[k];
          if (since_repeat > KEY_REPEAT_RATE) {
            handle_key(k, f.mod);
            key_repeat_time[k] = now_time;
          }
        }
      }
      prev_keys[k] = f.keys[k];
    }
    
    draw(&f);
    
    int64_t time = fenster_time();
    if (time - now < 1000 / 60) {
      fenster_sleep(1000 / 60 - (time - now));
    }
    now = time;
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
