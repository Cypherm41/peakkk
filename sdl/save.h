/* Save files for the SDL front end: the game state from g_save() is almost all zeros, so it is stored with a simple
 * zero-run encoding (a zero byte is followed by the length of its run). No SDL here, so the tests can use it too. */
#ifndef SAVE_H
#define SAVE_H
#include <stdint.h>

/* dst needs room for 2 * n bytes in the worst case; returns the encoded length */
static int rle_encode(const uint8_t *src, int n, uint8_t *dst) {
  int o = 0;
  for (int i = 0; i < n;) {
    if (src[i]) { dst[o++] = src[i++]; continue; }
    int run = 0; while (i < n && !src[i] && run < 255) { run++; i++; }
    dst[o++] = 0; dst[o++] = (uint8_t)run;
  }
  return o;
}
/* returns the decoded length, or -1 if the data is malformed or longer than cap; the output is exactly `want` bytes */
static int rle_decode(const uint8_t *src, int n, uint8_t *dst, int cap) {
  int o = 0;
  for (int i = 0; i < n; i++) {
    if (src[i]) { if (o >= cap) return -1; dst[o++] = src[i]; continue; }
    if (++i >= n) return -1;
    int run = src[i]; if (o + run > cap) return -1;
    for (int k = 0; k < run; k++) dst[o++] = 0;
  }
  return o;
}
#endif
