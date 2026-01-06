#ifndef WCWIDTH_H
#define WCWIDTH_H

#include <wchar.h>

/* Map standard wcwidth to our implementation */
#define wcwidth mk_wcwidth

static inline int mk_wcwidth(wchar_t ucs) {
  if (ucs == 0) return 0;
  if (ucs < 32 || (ucs >= 0x7f && ucs < 0xa0)) return -1;
  if (ucs >= 0x1100 &&
      (ucs <= 0x115f ||
       ucs == 0x2329 || ucs == 0x232a ||
       (ucs >= 0x2e80 && ucs <= 0xa4cf && ucs != 0x303f) ||
       (ucs >= 0xac00 && ucs <= 0xd7a3) ||
       (ucs >= 0xf900 && ucs <= 0xfaff) ||
       (ucs >= 0xfe10 && ucs <= 0xfe19) ||
       (ucs >= 0xfe30 && ucs <= 0xfe6f) ||
       (ucs >= 0xff00 && ucs <= 0xff60) ||
       (ucs >= 0xffe0 && ucs <= 0xffe6) ||
       (ucs >= 0x20000 && ucs <= 0x2fffd) ||
       (ucs >= 0x30000 && ucs <= 0x3fffd)))
    return 2;
  return 1;
}

static inline int mk_wcswidth(const wchar_t *pwcs, size_t n) {
  int w, width = 0;
  for (;*pwcs && n-- > 0; pwcs++) {
    if ((w = mk_wcwidth(*pwcs)) < 0) return -1;
    width += w;
  }
  return width;
}

#endif /* WCWIDTH_H */
