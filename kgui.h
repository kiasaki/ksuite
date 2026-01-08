#ifndef KGUI_H
#define KGUI_H

/*
 * kgui.h - Lightweight GUI framework for KSuite applications
 *
 * Provides utilities and GUI abstractions on top of fenster.h:
 *   - Scale parsing (K_SCALE environment variable)
 *   - Frame timing (60fps target)
 *   - Key repeat with configurable delay/rate
 *   - Clipboard (via xclip on Linux)
 *   - Layout regions with padding
 *   - Text rendering with alignment
 *   - Scrollable views
 *   - Click/double-click handling
 */

#include "fenster.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * SCALING
 * ============================================================================ */

typedef struct {
    int scale;       /* K_SCALE value (default 1) */
    int font_scale;  /* Derived font scale */
} kg_scale;

static inline kg_scale kg_scale_init(void) {
    kg_scale s = {1, 1};
    char *env = getenv("K_SCALE");
    if (env) {
        s.scale = atoi(env);
        if (s.scale < 1) s.scale = 1;
    }
    s.font_scale = s.scale;
    return s;
}

#define KG_SCALED(base, sc) ((base) * (sc).scale)

/* ============================================================================
 * TIMING
 * ============================================================================ */

typedef struct {
    int64_t last_frame;
    int64_t target_ms;  /* e.g., 16 for 60fps */
} kg_frame_timer;

static inline kg_frame_timer kg_frame_timer_init(int fps) {
    kg_frame_timer t;
    t.last_frame = fenster_time();
    t.target_ms = 1000 / fps;
    return t;
}

static inline void kg_frame_wait(kg_frame_timer *t) {
    int64_t now = fenster_time();
    int64_t elapsed = now - t->last_frame;
    if (elapsed < t->target_ms) {
        fenster_sleep(t->target_ms - elapsed);
    }
    t->last_frame = fenster_time();
}

/* ============================================================================
 * KEY REPEAT
 * ============================================================================ */

#define KG_KEY_REPEAT_DELAY_MS 400
#define KG_KEY_REPEAT_RATE_MS  30

typedef struct {
    int prev_keys[256];
    int64_t press_time[256];
    int64_t repeat_time[256];
    int delay_ms;
    int rate_ms;
} kg_key_repeat;

static inline kg_key_repeat kg_key_repeat_init(void) {
    kg_key_repeat kr = {0};
    kr.delay_ms = KG_KEY_REPEAT_DELAY_MS;
    kr.rate_ms = KG_KEY_REPEAT_RATE_MS;
    return kr;
}

static inline kg_key_repeat kg_key_repeat_init_custom(int delay_ms, int rate_ms) {
    kg_key_repeat kr = {0};
    kr.delay_ms = delay_ms;
    kr.rate_ms = rate_ms;
    return kr;
}

/* Returns: 0=no event, 1=key pressed, 2=key repeated */
static inline int kg_key_check(kg_key_repeat *kr, int *keys, int key) {
    int64_t now = fenster_time();
    int result = 0;

    if (keys[key] && !kr->prev_keys[key]) {
        /* Fresh press */
        kr->press_time[key] = now;
        kr->repeat_time[key] = now;
        result = 1;
    } else if (keys[key] && kr->prev_keys[key]) {
        /* Held - check for repeat */
        if (now - kr->press_time[key] > kr->delay_ms) {
            if (now - kr->repeat_time[key] > kr->rate_ms) {
                kr->repeat_time[key] = now;
                result = 2;
            }
        }
    }
    kr->prev_keys[key] = keys[key];
    return result;
}

/* Process all keys, call handler for each event */
typedef void (*kg_key_handler)(int key, int mod, void *userdata);

static inline void kg_key_process(kg_key_repeat *kr, int *keys, int mod,
                                  kg_key_handler handler, void *userdata) {
    for (int k = 0; k < 256; k++) {
        int event = kg_key_check(kr, keys, k);
        if (event) {
            handler(k, mod, userdata);
        }
    }
}

/* Common key constants */
#define KG_KEY_UP        17
#define KG_KEY_DOWN      18
#define KG_KEY_RIGHT     19
#define KG_KEY_LEFT      20
#define KG_KEY_BACKSPACE 8
#define KG_KEY_DELETE    127
#define KG_KEY_RETURN    10
#define KG_KEY_TAB       9
#define KG_KEY_ESCAPE    27
#define KG_KEY_HOME      2
#define KG_KEY_END       5
#define KG_KEY_PAGEUP    3
#define KG_KEY_PAGEDOWN  4
#define KG_KEY_INSERT    26

/* Modifier masks */
#define KG_MOD_CTRL  1
#define KG_MOD_SHIFT 2
#define KG_MOD_ALT   4
#define KG_MOD_META  8

/* ============================================================================
 * CLIPBOARD (Linux/xclip)
 * ============================================================================ */

static inline void kg_clipboard_copy(const char *text) {
    if (!text) return;
    FILE *p = popen("xclip", "w");
    if (p) {
        fputs(text, p);
        pclose(p);
    }
}

static inline char *kg_clipboard_paste_sel(const char *sel) {
    char cmd[64];
    if (sel) {
        snprintf(cmd, sizeof(cmd), "xclip -sel %s -o 2>/dev/null", sel);
    } else {
        snprintf(cmd, sizeof(cmd), "xclip -o 2>/dev/null");
    }
    FILE *p = popen(cmd, "r");
    if (!p) return NULL;

    char *buf = NULL;
    size_t len = 0, cap = 0;
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

static inline char *kg_clipboard_paste(void) {
    char *buf = kg_clipboard_paste_sel(NULL);
    if (buf && buf[0] != '\0') return buf;
    free(buf);
    return kg_clipboard_paste_sel("clipboard");
}

/* ============================================================================
 * TEXT UTILITIES
 * ============================================================================ */

/* Font format: first 256 bytes = character widths, then 32 bytes per char */
static inline int kg_char_width(unsigned char *font, char c, int scale) {
    return font[(unsigned char)c] * scale;
}

static inline int kg_text_width(unsigned char *font, const char *s, int scale) {
    int w = 0;
    while (*s) {
        w += kg_char_width(font, *s++, scale);
    }
    return w;
}

/* ============================================================================
 * CONTEXT
 * ============================================================================ */

#define KG_DOUBLE_CLICK_MS   400
#define KG_DOUBLE_CLICK_DIST 5

typedef struct {
    struct fenster *f;
    kg_scale scale;
    kg_key_repeat key_repeat;
    kg_frame_timer frame_timer;
    unsigned char *font;

    /* Mouse state */
    int mouse_x, mouse_y;
    int mouse_down;
    int mouse_pressed;   /* Just pressed this frame */
    int mouse_released;  /* Just released this frame */
    int prev_mouse;

    /* Double-click detection */
    int64_t last_click_time;
    int last_click_x, last_click_y;
    int double_clicked;

    /* Scroll wheel: -1 down, 0 none, +1 up */
    int scroll;
} kg_ctx;

static inline kg_ctx kg_init(struct fenster *f, unsigned char *font) {
    kg_ctx ctx = {0};
    ctx.f = f;
    ctx.scale = kg_scale_init();
    ctx.key_repeat = kg_key_repeat_init();
    ctx.frame_timer = kg_frame_timer_init(60);
    ctx.font = font;
    return ctx;
}

/* Call at start of each frame */
static inline void kg_frame_begin(kg_ctx *ctx) {
    /* Update mouse state */
    ctx->mouse_x = ctx->f->x;
    ctx->mouse_y = ctx->f->y;
    ctx->mouse_down = ctx->f->mouse;
    ctx->mouse_pressed = ctx->mouse_down && !ctx->prev_mouse;
    ctx->mouse_released = !ctx->mouse_down && ctx->prev_mouse;

    /* Double-click detection */
    ctx->double_clicked = 0;
    if (ctx->mouse_pressed) {
        int64_t now = fenster_time();
        int dx = ctx->mouse_x - ctx->last_click_x;
        int dy = ctx->mouse_y - ctx->last_click_y;
        if (now - ctx->last_click_time < KG_DOUBLE_CLICK_MS &&
            dx * dx + dy * dy < KG_DOUBLE_CLICK_DIST * KG_DOUBLE_CLICK_DIST) {
            ctx->double_clicked = 1;
        }
        ctx->last_click_time = now;
        ctx->last_click_x = ctx->mouse_x;
        ctx->last_click_y = ctx->mouse_y;
    }

    ctx->prev_mouse = ctx->mouse_down;

    /* Capture scroll wheel and reset for next frame */
    ctx->scroll = ctx->f->scroll;
    ctx->f->scroll = 0;
}

/* Call at end of each frame */
static inline void kg_frame_end(kg_ctx *ctx) {
    kg_frame_wait(&ctx->frame_timer);
}

/* ============================================================================
 * REGIONS / LAYOUT
 * ============================================================================ */

typedef struct {
    int x, y, w, h;
    int padding;
    int cursor_x, cursor_y;  /* Current position within region */
} kg_region;

static inline kg_region kg_region_full(kg_ctx *ctx, int padding) {
    return (kg_region){
        .x = 0, .y = 0,
        .w = ctx->f->width, .h = ctx->f->height,
        .padding = padding,
        .cursor_x = padding, .cursor_y = padding
    };
}

static inline kg_region kg_region_create(int x, int y, int w, int h, int padding) {
    return (kg_region){
        .x = x, .y = y, .w = w, .h = h,
        .padding = padding,
        .cursor_x = x + padding, .cursor_y = y + padding
    };
}

static inline kg_region kg_region_inset(kg_region *r, int inset) {
    return kg_region_create(
        r->x + inset, r->y + inset,
        r->w - inset * 2, r->h - inset * 2,
        r->padding
    );
}

/* Split region horizontally, returning left portion */
static inline kg_region kg_region_split_left(kg_region *r, int width) {
    kg_region left = kg_region_create(r->x, r->y, width, r->h, r->padding);
    r->x += width;
    r->w -= width;
    r->cursor_x = r->x + r->padding;
    return left;
}

/* Split region horizontally, returning right portion */
static inline kg_region kg_region_split_right(kg_region *r, int width) {
    kg_region right = kg_region_create(r->x + r->w - width, r->y, width, r->h, r->padding);
    r->w -= width;
    return right;
}

/* Split region vertically, returning top portion */
static inline kg_region kg_region_split_top(kg_region *r, int height) {
    kg_region top = kg_region_create(r->x, r->y, r->w, height, r->padding);
    r->y += height;
    r->h -= height;
    r->cursor_y = r->y + r->padding;
    return top;
}

/* Split region vertically, returning bottom portion */
static inline kg_region kg_region_split_bottom(kg_region *r, int height) {
    kg_region bottom = kg_region_create(r->x, r->y + r->h - height, r->w, height, r->padding);
    r->h -= height;
    return bottom;
}

/* Inner dimensions (excluding padding) */
static inline int kg_region_inner_w(kg_region *r) {
    return r->w - r->padding * 2;
}

static inline int kg_region_inner_h(kg_region *r) {
    return r->h - r->padding * 2;
}

/* ============================================================================
 * DRAWING
 * ============================================================================ */

static inline void kg_fill(kg_ctx *ctx, uint32_t color) {
    struct fenster *f = ctx->f;
    for (int i = 0; i < f->width * f->height; i++) {
        f->buf[i] = color;
    }
}

static inline void kg_fill_region(kg_ctx *ctx, kg_region *r, uint32_t color) {
    fenster_rect(ctx->f, r->x, r->y, r->w, r->h, color);
}

static inline void kg_border(kg_ctx *ctx, kg_region *r, int width, uint32_t color) {
    struct fenster *f = ctx->f;
    /* Top */
    fenster_rect(f, r->x, r->y, r->w, width, color);
    /* Bottom */
    fenster_rect(f, r->x, r->y + r->h - width, r->w, width, color);
    /* Left */
    fenster_rect(f, r->x, r->y, width, r->h, color);
    /* Right */
    fenster_rect(f, r->x + r->w - width, r->y, width, r->h, color);
}

/* Horizontal line */
static inline void kg_hline(kg_ctx *ctx, int x, int y, int w, int thickness, uint32_t color) {
    fenster_rect(ctx->f, x, y, w, thickness, color);
}

/* Vertical line */
static inline void kg_vline(kg_ctx *ctx, int x, int y, int h, int thickness, uint32_t color) {
    fenster_rect(ctx->f, x, y, thickness, h, color);
}

/* ============================================================================
 * TEXT RENDERING
 * ============================================================================ */

typedef enum {
    KG_ALIGN_LEFT,
    KG_ALIGN_CENTER,
    KG_ALIGN_RIGHT
} kg_align;

/* Draw text at absolute position */
static inline void kg_text_at(kg_ctx *ctx, int x, int y, const char *text, uint32_t color) {
    fenster_text(ctx->f, ctx->font, x, y, (char *)text, ctx->scale.font_scale, color);
}

/* Draw text aligned within a region */
static inline void kg_text(kg_ctx *ctx, kg_region *r, const char *text,
                           kg_align align, uint32_t color) {
    int scale = ctx->scale.font_scale;
    int tw = kg_text_width(ctx->font, text, scale);
    int x;

    switch (align) {
        case KG_ALIGN_CENTER:
            x = r->x + (r->w - tw) / 2;
            break;
        case KG_ALIGN_RIGHT:
            x = r->x + r->w - r->padding - tw;
            break;
        default: /* KG_ALIGN_LEFT */
            x = r->x + r->padding;
            break;
    }

    fenster_text(ctx->f, ctx->font, x, r->cursor_y, (char *)text, scale, color);
}

/* Draw text with clipping (character-level) */
static inline void kg_text_clipped(kg_ctx *ctx, int x, int y, const char *text,
                                   int max_w, uint32_t color) {
    struct fenster *f = ctx->f;
    int scale = ctx->scale.font_scale;
    int w = 0;
    char tmp[2] = {0, 0};

    while (*text) {
        int cw = kg_char_width(ctx->font, *text, scale);
        if (w + cw > max_w) break;
        tmp[0] = *text;
        fenster_text(f, ctx->font, x + w, y, tmp, scale, color);
        w += cw;
        text++;
    }
}

/* Draw text with ellipsis truncation */
static inline void kg_text_truncated(kg_ctx *ctx, int x, int y, const char *text,
                                     int max_w, uint32_t color) {
    int scale = ctx->scale.font_scale;
    int tw = kg_text_width(ctx->font, text, scale);

    if (tw <= max_w) {
        fenster_text(ctx->f, ctx->font, x, y, (char *)text, scale, color);
    } else {
        int ellipsis_w = kg_text_width(ctx->font, "...", scale);
        int target_w = max_w - ellipsis_w;

        if (target_w <= 0) {
            kg_text_clipped(ctx, x, y, "...", max_w, color);
            return;
        }

        /* Find how many characters fit */
        int w = 0;
        int len = 0;
        while (text[len]) {
            int cw = kg_char_width(ctx->font, text[len], scale);
            if (w + cw > target_w) break;
            w += cw;
            len++;
        }

        /* Draw truncated text + ellipsis */
        static char buf[256];
        if (len > 255) len = 255;
        memcpy(buf, text, len);
        buf[len] = '\0';
        strcat(buf, "...");
        fenster_text(ctx->f, ctx->font, x, y, buf, scale, color);
    }
}

/* ============================================================================
 * SCROLLABLE VIEWS
 * ============================================================================ */

typedef struct {
    int offset;          /* Current scroll position */
    int content_height;  /* Total scrollable content */
    int visible_height;  /* Visible area */
} kg_scroll;

static inline kg_scroll kg_scroll_init(void) {
    return (kg_scroll){0, 0, 0};
}

static inline void kg_scroll_update(kg_scroll *s, int content_h, int visible_h) {
    s->content_height = content_h;
    s->visible_height = visible_h;
    int max_scroll = content_h > visible_h ? content_h - visible_h : 0;
    if (s->offset > max_scroll) s->offset = max_scroll;
    if (s->offset < 0) s->offset = 0;
}

static inline int kg_scroll_max(kg_scroll *s) {
    return s->content_height > s->visible_height
        ? s->content_height - s->visible_height : 0;
}

static inline void kg_scroll_by(kg_scroll *s, int delta) {
    s->offset += delta;
    int max = kg_scroll_max(s);
    if (s->offset > max) s->offset = max;
    if (s->offset < 0) s->offset = 0;
}

static inline void kg_scroll_to(kg_scroll *s, int pos) {
    s->offset = pos;
    int max = kg_scroll_max(s);
    if (s->offset > max) s->offset = max;
    if (s->offset < 0) s->offset = 0;
}

/* Scroll to make an item visible */
static inline void kg_scroll_to_visible(kg_scroll *s, int item_y, int item_h) {
    if (item_y < s->offset) {
        s->offset = item_y;
    } else if (item_y + item_h > s->offset + s->visible_height) {
        s->offset = item_y + item_h - s->visible_height;
    }
}

/* Check if item is visible */
static inline int kg_scroll_is_visible(kg_scroll *s, int item_y, int item_h) {
    return (item_y + item_h > s->offset && item_y < s->offset + s->visible_height);
}

/* Convert content Y to screen Y */
static inline int kg_scroll_to_screen(kg_scroll *s, int content_y, int region_y) {
    return region_y + content_y - s->offset;
}

/* Convert screen Y to content Y */
static inline int kg_scroll_to_content(kg_scroll *s, int screen_y, int region_y) {
    return screen_y - region_y + s->offset;
}

/* ============================================================================
 * CLICK HANDLING
 * ============================================================================ */

static inline int kg_point_in_region(kg_region *r, int x, int y) {
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static inline int kg_point_in_rect(int rx, int ry, int rw, int rh, int x, int y) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static inline int kg_clicked(kg_ctx *ctx, kg_region *r) {
    return ctx->mouse_pressed && kg_point_in_region(r, ctx->mouse_x, ctx->mouse_y);
}

static inline int kg_clicked_rect(kg_ctx *ctx, int x, int y, int w, int h) {
    return ctx->mouse_pressed && kg_point_in_rect(x, y, w, h, ctx->mouse_x, ctx->mouse_y);
}

static inline int kg_double_clicked(kg_ctx *ctx, kg_region *r) {
    return ctx->double_clicked && kg_point_in_region(r, ctx->mouse_x, ctx->mouse_y);
}

static inline int kg_double_clicked_rect(kg_ctx *ctx, int x, int y, int w, int h) {
    return ctx->double_clicked && kg_point_in_rect(x, y, w, h, ctx->mouse_x, ctx->mouse_y);
}

static inline int kg_hovered(kg_ctx *ctx, kg_region *r) {
    return kg_point_in_region(r, ctx->mouse_x, ctx->mouse_y);
}

static inline int kg_hovered_rect(kg_ctx *ctx, int x, int y, int w, int h) {
    return kg_point_in_rect(x, y, w, h, ctx->mouse_x, ctx->mouse_y);
}

/* ============================================================================
 * SHORTCUT HELPERS
 * ============================================================================ */

/* Check if a key+mod combination was just pressed or repeated */
static inline int kg_shortcut(kg_ctx *ctx, int key, int mod_mask, int mod_value) {
    int event = kg_key_check(&ctx->key_repeat, ctx->f->keys, key);
    if (event) {
        int mod = ctx->f->mod;
        if ((mod & mod_mask) == mod_value) {
            return event;
        }
    }
    return 0;
}

/* Convenience macros for common shortcuts */
#define kg_shortcut_ctrl(ctx, key) kg_shortcut(ctx, key, KG_MOD_CTRL, KG_MOD_CTRL)
#define kg_shortcut_shift(ctx, key) kg_shortcut(ctx, key, KG_MOD_SHIFT, KG_MOD_SHIFT)
#define kg_shortcut_plain(ctx, key) kg_shortcut(ctx, key, 0, 0)

#endif /* KGUI_H */
