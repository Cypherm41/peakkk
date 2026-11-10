/* Minimal 2D drawing layer for the SDL2 front end.
 *
 * Everything is batched into one vertex buffer and submitted with SDL_RenderGeometry (SDL 2.0.18+), so a whole
 * frame of polygons costs a handful of draw calls. It offers the few Canvas-2D-style operations the renderer
 * needs: filled polygons / ellipses / arcs, thick lines, rounded panels, gradients and bitmap-font text.
 *
 * Colours are 0xRRGGBB plus a separate alpha. gAlpha multiplies every alpha (like ctx.globalAlpha) and
 * (gOX, gOY) is added to every vertex (the world camera offset; zero while drawing the HUD).
 */
#ifndef GFX_H
#define GFX_H

#include <SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font5x7.h"

#define PI_F 3.14159265358979f
#define GFX_MAXV 98304                     /* multiple of 3 */

static SDL_Renderer *gR;
static SDL_Texture *gFont;                 /* white-on-transparent glyph atlas */
static SDL_Texture *gVTex;                 /* texture of the batch currently being built (NULL = untextured) */
static SDL_Vertex gV[GFX_MAXV];
static int gVN;
static float gAlpha = 1.0f;
static float gOX, gOY;
static float gDetail = 1.0f;               /* on-screen size of one drawing unit; picks curve smoothness */

static void gfx_flush(void) {
  if (gVN) { SDL_RenderGeometry(gR, gVTex, gV, gVN, NULL, 0); gVN = 0; }
}

static SDL_Vertex *gfx_alloc(int n, SDL_Texture *tex) {
  if (tex != gVTex || gVN + n > GFX_MAXV) { gfx_flush(); gVTex = tex; }
  SDL_Vertex *v = gV + gVN;
  gVN += n;
  return v;
}

static SDL_Color mkcol(unsigned rgb, float a) {
  SDL_Color c;
  float al = a * gAlpha;
  c.r = (Uint8)((rgb >> 16) & 255); c.g = (Uint8)((rgb >> 8) & 255); c.b = (Uint8)(rgb & 255);
  c.a = (Uint8)(al <= 0 ? 0 : al >= 1 ? 255 : al * 255.0f + 0.5f);
  return c;
}

static unsigned shade_rgb(unsigned rgb, float f) {      /* multiply each channel, clamped */
  float r = ((rgb >> 16) & 255) * f, g = ((rgb >> 8) & 255) * f, b = (rgb & 255) * f;
  unsigned R_ = r > 255 ? 255 : (unsigned)r, G_ = g > 255 ? 255 : (unsigned)g, B_ = b > 255 ? 255 : (unsigned)b;
  return (R_ << 16) | (G_ << 8) | B_;
}

static void vtx(SDL_Vertex *v, float x, float y, SDL_Color c) {
  v->position.x = x + gOX; v->position.y = y + gOY; v->color = c; v->tex_coord.x = 0; v->tex_coord.y = 0;
}

/* ------------------------------ filled shapes ------------------------------ */

static void fill_tri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned rgb, float a) {
  SDL_Color c = mkcol(rgb, a);
  SDL_Vertex *v = gfx_alloc(3, NULL);
  vtx(v, x0, y0, c); vtx(v + 1, x1, y1, c); vtx(v + 2, x2, y2, c);
}

/* convex polygon as a triangle fan; p = x0,y0,x1,y1,... */
static void fill_poly(const float *p, int n, unsigned rgb, float a) {
  if (n < 3) return;
  SDL_Color c = mkcol(rgb, a);
  SDL_Vertex *v = gfx_alloc((n - 2) * 3, NULL);
  for (int i = 1; i < n - 1; i++) {
    vtx(v++, p[0], p[1], c); vtx(v++, p[2 * i], p[2 * i + 1], c); vtx(v++, p[2 * i + 2], p[2 * i + 3], c);
  }
}

static void fill_rect(float x, float y, float w, float h, unsigned rgb, float a) {
  SDL_Color c = mkcol(rgb, a);
  SDL_Vertex *v = gfx_alloc(6, NULL);
  vtx(v, x, y, c); vtx(v + 1, x + w, y, c); vtx(v + 2, x + w, y + h, c);
  vtx(v + 3, x, y, c); vtx(v + 4, x + w, y + h, c); vtx(v + 5, x, y + h, c);
}

static void fill_vgrad(float x, float y, float w, float h, unsigned top, unsigned bot, float a) {
  SDL_Color ct = mkcol(top, a), cb = mkcol(bot, a);
  SDL_Vertex *v = gfx_alloc(6, NULL);
  vtx(v, x, y, ct); vtx(v + 1, x + w, y, ct); vtx(v + 2, x + w, y + h, cb);
  vtx(v + 3, x, y, ct); vtx(v + 4, x + w, y + h, cb); vtx(v + 5, x, y + h, cb);
}

static int arc_segments(float rx, float ry, float span) {
  float r = (rx > ry ? rx : ry) * gDetail;
  int n = (int)(r * 0.7f) + 8;
  if (n > 56) n = 56;
  n = (int)(n * (span / (2 * PI_F)) + 0.999f);
  return n < 2 ? 2 : n;
}

/* pie slice (angles as in Canvas: 0 = +x, increasing towards +y) */
static void fill_arc(float cx, float cy, float rx, float ry, float a0, float a1, unsigned rgb, float a) {
  int n = arc_segments(rx, ry, fabsf(a1 - a0));
  SDL_Color c = mkcol(rgb, a);
  SDL_Vertex *v = gfx_alloc(n * 3, NULL);
  for (int i = 0; i < n; i++) {
    float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n;
    vtx(v++, cx, cy, c);
    vtx(v++, cx + cosf(t0) * rx, cy + sinf(t0) * ry, c);
    vtx(v++, cx + cosf(t1) * rx, cy + sinf(t1) * ry, c);
  }
}

static void fill_ellipse(float cx, float cy, float rx, float ry, unsigned rgb, float a) {
  fill_arc(cx, cy, rx, ry, 0, 2 * PI_F, rgb, a);
}

/* ---------------------------------- lines ---------------------------------- */

static void line(float x0, float y0, float x1, float y1, float w, unsigned rgb, float a) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-5f) { dx = 1; dy = 0; l = 1; }
  float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
  SDL_Color c = mkcol(rgb, a);
  SDL_Vertex *v = gfx_alloc(6, NULL);
  vtx(v, x0 + nx, y0 + ny, c); vtx(v + 1, x1 + nx, y1 + ny, c); vtx(v + 2, x1 - nx, y1 - ny, c);
  vtx(v + 3, x0 + nx, y0 + ny, c); vtx(v + 4, x1 - nx, y1 - ny, c); vtx(v + 5, x0 - nx, y0 - ny, c);
}

/* line with square caps that overshoot by half the width (stands in for lineCap = 'round') */
static void line_cap(float x0, float y0, float x1, float y1, float w, unsigned rgb, float a) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l > 1e-5f) { dx = dx / l * w * 0.5f; dy = dy / l * w * 0.5f; x0 -= dx; y0 -= dy; x1 += dx; y1 += dy; }
  line(x0, y0, x1, y1, w, rgb, a);
}

static void stroke_poly(const float *p, int n, float w, unsigned rgb, float a) {
  for (int i = 0; i < n; i++) { int j = (i + 1) % n; line(p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], w, rgb, a); }
}

static void stroke_arc(float cx, float cy, float rx, float ry, float a0, float a1, float w, unsigned rgb, float a) {
  int n = arc_segments(rx, ry, fabsf(a1 - a0)) + 2;
  float px = cx + cosf(a0) * rx, py = cy + sinf(a0) * ry;
  for (int i = 1; i <= n; i++) {
    float t = a0 + (a1 - a0) * i / n, x = cx + cosf(t) * rx, y = cy + sinf(t) * ry;
    line(px, py, x, y, w, rgb, a);
    px = x; py = y;
  }
}

static void stroke_ellipse(float cx, float cy, float rx, float ry, float w, unsigned rgb, float a) {
  stroke_arc(cx, cy, rx, ry, 0, 2 * PI_F, w, rgb, a);
}

static void stroke_rect(float x, float y, float w, float h, float lw, unsigned rgb, float a) {
  fill_rect(x, y, w, lw, rgb, a); fill_rect(x, y + h - lw, w, lw, rgb, a);
  fill_rect(x, y + lw, lw, h - 2 * lw, rgb, a); fill_rect(x + w - lw, y + lw, lw, h - 2 * lw, rgb, a);
}

/* -------------------------------- UI panels --------------------------------- */

static void fill_rrect(float x, float y, float w, float h, float r, unsigned rgb, float a) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r < 1) { fill_rect(x, y, w, h, rgb, a); return; }
  fill_rect(x + r, y, w - 2 * r, h, rgb, a);
  fill_rect(x, y + r, r, h - 2 * r, rgb, a);
  fill_rect(x + w - r, y + r, r, h - 2 * r, rgb, a);
  fill_arc(x + r, y + r, r, r, PI_F, 1.5f * PI_F, rgb, a);
  fill_arc(x + w - r, y + r, r, r, 1.5f * PI_F, 2 * PI_F, rgb, a);
  fill_arc(x + w - r, y + h - r, r, r, 0, 0.5f * PI_F, rgb, a);
  fill_arc(x + r, y + h - r, r, r, 0.5f * PI_F, PI_F, rgb, a);
}

/* bordered rounded box */
static void panel(float x, float y, float w, float h, float r, unsigned bg, unsigned border, float bw) {
  fill_rrect(x, y, w, h, r, border, 1);
  fill_rrect(x + bw, y + bw, w - 2 * bw, h - 2 * bw, r > bw ? r - bw : 0, bg, 1);
}

/* ----------------------------------- text ----------------------------------- */

#define FONT_CELL_W 6
#define FONT_CELL_H 8
#define FONT_COLS 16
#define FONT_ROWS 6

static void font_init(void) {
  int tw = FONT_COLS * FONT_CELL_W, th = FONT_ROWS * FONT_CELL_H;
  Uint32 *px = (Uint32 *)calloc((size_t)tw * th, sizeof(Uint32));
  for (int g = 0; g < FONT_COUNT; g++) {
    int ox = (g % FONT_COLS) * FONT_CELL_W, oy = (g / FONT_COLS) * FONT_CELL_H;
    for (int col = 0; col < 5; col++)
      for (int row = 0; row < 8; row++)
        if (FONT5X7[g][col] & (1 << row)) px[(oy + row) * tw + ox + col] = 0xFFFFFFFFu;   /* RGBA32 on little endian = ABGR8888 word */
  }
  gFont = SDL_CreateTexture(gR, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, tw, th);
  if (gFont) {
    /* RGBA32 byte order is R,G,B,A in memory regardless of endianness; 0xFFFFFFFF is the same either way */
    SDL_UpdateTexture(gFont, NULL, px, tw * 4);
    SDL_SetTextureBlendMode(gFont, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(gFont, SDL_ScaleModeNearest);
  }
  free(px);
}

static float text_width(const char *s, float scale) {
  int n = (int)strlen(s);
  return n ? (n * FONT_CELL_W - 1) * scale : 0;
}

static void text(float x, float y, float scale, unsigned rgb, float a, const char *s) {
  if (!gFont) return;
  SDL_Color c = mkcol(rgb, a);
  const float tw = FONT_COLS * FONT_CELL_W, th = FONT_ROWS * FONT_CELL_H;
  float cx = x;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '\n') { cx = x; y += 10 * scale; continue; }
    int ch = *p;
    if (ch < FONT_FIRST || ch >= FONT_FIRST + FONT_COUNT) ch = '?';
    if (ch != ' ') {
      int g = ch - FONT_FIRST;
      float u0 = (g % FONT_COLS) * FONT_CELL_W / tw, v0 = (g / FONT_COLS) * FONT_CELL_H / th;
      float u1 = u0 + 5 / tw, v1 = v0 + 8 / th;
      float x1 = cx + 5 * scale, y1 = y + 8 * scale;
      SDL_Vertex *v = gfx_alloc(6, gFont);
      vtx(v, cx, y, c); vtx(v + 1, x1, y, c); vtx(v + 2, x1, y1, c);
      vtx(v + 3, cx, y, c); vtx(v + 4, x1, y1, c); vtx(v + 5, cx, y1, c);
      v[0].tex_coord.x = u0; v[0].tex_coord.y = v0; v[1].tex_coord.x = u1; v[1].tex_coord.y = v0; v[2].tex_coord.x = u1; v[2].tex_coord.y = v1;
      v[3].tex_coord.x = u0; v[3].tex_coord.y = v0; v[4].tex_coord.x = u1; v[4].tex_coord.y = v1; v[5].tex_coord.x = u0; v[5].tex_coord.y = v1;
    }
    cx += FONT_CELL_W * scale;
  }
}

static void textf(float x, float y, float scale, unsigned rgb, float a, const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
  text(x, y, scale, rgb, a, buf);
}

static void text_center(float cx, float y, float scale, unsigned rgb, float a, const char *s) {
  text(cx - text_width(s, scale) / 2, y, scale, rgb, a, s);
}

static void text_right(float rx, float y, float scale, unsigned rgb, float a, const char *s) {
  text(rx - text_width(s, scale), y, scale, rgb, a, s);
}

static void text_outline(float x, float y, float scale, unsigned rgb, float a, unsigned outline, const char *s) {
  float d = scale * 0.6f;
  text(x - d, y, scale, outline, a * 0.8f, s); text(x + d, y, scale, outline, a * 0.8f, s);
  text(x, y - d, scale, outline, a * 0.8f, s); text(x, y + d, scale, outline, a * 0.8f, s);
  text(x, y, scale, rgb, a, s);
}

/* Greedy word wrap into a box of width w. Returns the number of lines; draws only when draw is non-zero. */
static int text_wrap(float x, float y, float w, float scale, unsigned rgb, float a, const char *s, int draw) {
  int maxc = (int)((w + scale) / (FONT_CELL_W * scale));
  if (maxc < 4) maxc = 4;
  int lines = 0;
  const char *p = s;
  char buf[256];
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    int len = 0, lastsp = -1;
    while (p[len] && p[len] != '\n' && len < maxc) { if (p[len] == ' ') lastsp = len; len++; }
    if (p[len] && p[len] != '\n' && p[len] != ' ' && lastsp > 0) len = lastsp;   /* break at the last space */
    if (len > 255) len = 255;
    memcpy(buf, p, (size_t)len); buf[len] = 0;
    while (len > 0 && buf[len - 1] == ' ') buf[--len] = 0;
    if (draw) text(x, y + lines * 10 * scale, scale, rgb, a, buf);
    lines++;
    p += len;
    if (*p == '\n') p++;
  }
  return lines;
}

/* ------------------------------ render passes ------------------------------ */

static void gfx_set_scale(float s) { gfx_flush(); SDL_RenderSetScale(gR, s, s); }

static void gfx_clip(float x, float y, float w, float h) {
  gfx_flush();
  SDL_Rect r; r.x = (int)x; r.y = (int)y; r.w = (int)w; r.h = (int)h;
  SDL_RenderSetClipRect(gR, &r);
}
static void gfx_unclip(void) { gfx_flush(); SDL_RenderSetClipRect(gR, NULL); }

#endif
