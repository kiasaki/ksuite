#include "fenster.h"
#include "newyork14.h"
#include "terminus16.h"
#include "times15.h"
#include "chicago12.h"

#define W 800*2
#define H 600*2

static int run() {
  uint32_t buf[W * H];
  struct fenster f = { .title = "term", .width = W, .height = H, .buf = buf, };
  fenster_open(&f);
  uint32_t t = 0;
  int64_t now = fenster_time();
  while (fenster_loop(&f) == 0) {
    t++;
    for (int i = 0; i < W; i++) {
      for (int j = 0; j < H; j++) {
        /* Colourful and moving: */
        //fenster_pixel(&f, i, j) = i * j * t;
        /* Munching squares: */
        fenster_pixel(&f, i, j) = i ^ j ^ t;
        /* White noise: */
        //fenster_pixel(&f, i, j) = (rand() << 16) ^ (rand() << 8) ^ rand();
      }
    }
    fenster_text(&f, newyork, 16, 16+(0*4*16), "The brown fox jumps over the lazy dog. 01234567890!@#$%^&*()_+{}:<>?,", 4, 0xffffff);
    fenster_text(&f, terminus, 16, 16+(1*4*16), "The brown fox jumps over the lazy dog. 01234567890!@#$%^&*()_+{}:<>?,", 4, 0xffffff);
    fenster_text(&f, times, 16, 16+(2*4*16), "The brown fox jumps over the lazy dog. 01234567890!@#$%^&*()_+{}:<>?,", 4, 0xffffff);
    fenster_text(&f, chicago, 16, 16+(3*4*16), "The brown fox jumps over the lazy dog. 01234567890!@#$%^&*()_+{}:<>?,", 4, 0xffffff);
    int64_t time = fenster_time();
    if (time - now < 1000 / 60) {
      fenster_sleep(time - now);
    }
    now = time;
  }
  fenster_close(&f);
  return 0;
}

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow) {
  (void)hInstance, (void)hPrevInstance, (void)pCmdLine, (void)nCmdShow;
  return run();
}
#else
int main() { return run(); }
#endif
