/* World renderer: isometric terrain, resource nodes, buildings, units, projectiles, effects and the minimap.
 * A direct port of the Canvas-2D drawing code in js/ui.js onto the gfx.h batch layer. */
#ifndef RENDER_H
#define RENDER_H

#include "app.h"

/* ------------------------------ colours ------------------------------ */

static const unsigned GRASS[6] = { 0x4f8b3b, 0x4b8637, 0x53913e, 0x487f38, 0x579541, 0x4d8939 };
static const unsigned SAND[6] = { 0xcdb878, 0xc8b270, 0xd2bd80, 0xc9b474, 0xd0ba7c, 0xcbb676 };

typedef struct { float x, y; } V2;
static V2 P(float x, float y, float z) { V2 v = { isoX(x, y), isoY(x, y) - z }; return v; }

/* polygon with optional fill / outline (alpha 0 = skip) */
static void polyS(const V2 *p, int n, unsigned fill, float fa, unsigned stroke, float sa) {
  float f[16];
  if (n > 8) n = 8;
  for (int i = 0; i < n; i++) { f[2 * i] = p[i].x; f[2 * i + 1] = p[i].y; }
  if (fa > 0) fill_poly(f, n, fill, fa);
  if (sa > 0) stroke_poly(f, n, 1.0f, stroke, sa);
}
static void quad(V2 a, V2 b, V2 c, V2 d, unsigned fill, float fa, unsigned stroke, float sa) { V2 p[4] = { a, b, c, d }; polyS(p, 4, fill, fa, stroke, sa); }
static void tri(V2 a, V2 b, V2 c, unsigned fill, float fa, unsigned stroke, float sa) { V2 p[3] = { a, b, c }; polyS(p, 3, fill, fa, stroke, sa); }

/* ------------------------------ terrain ------------------------------ */

typedef struct { float l, r, t, b; int x0, x1, y0, y1; } View;

static void draw_terrain(const View *vis) {
  for (int y = vis->y0; y <= vis->y1; y++) {
    for (int x = vis->x0; x <= vis->x1; x++) {
      int i = y * MAP_W + x, t = mTerrain[i];
      float cx = isoX((float)x, (float)y), cy = isoY((float)x, (float)y);
      if (cx < vis->l - 40 || cx > vis->r + 40 || cy < vis->t - 20 || cy > vis->b + 20) continue;
      unsigned col = t == TILE_WATER ? (((x + y) & 1) ? 0x2b69a1 : 0x2d6fa9) : t == TILE_SAND ? SAND[mShade[i] % 6] : GRASS[mShade[i] % 6];
      float p[8] = { cx, cy - 0.6f, cx + 32.8f, cy + 16, cx, cy + 32.6f, cx - 32.8f, cy + 16 };
      fill_poly(p, 4, col, 1);
      if (t == TILE_WATER && ((x * 13 + y * 7) % 5 == 0)) {
        float s = sinf(gNow * 1.6f + x * 0.9f + y * 0.7f);
        if (s > 0.1f) line(cx - 10 + s * 4, cy + 14, cx + 6 + s * 4, cy + 14, 1.5f, 0xffffff, 0.18f * s);
      } else if (t == TILE_GRASS && ((x * 31 + y * 17) % 9 == 0)) {
        fill_rect(cx - 4, cy + 14, 2, 3, 0x1e4614, 0.35f); fill_rect(cx + 5, cy + 18, 2, 3, 0x1e4614, 0.35f);
      }
    }
  }
  /* map edge: south-west and south-east cliffs */
  float a[8] = { isoX(0, MAP_H), isoY(0, MAP_H), isoX(MAP_W, MAP_H), isoY(MAP_W, MAP_H), isoX(MAP_W, MAP_H), isoY(MAP_W, MAP_H) + 26, isoX(0, MAP_H), isoY(0, MAP_H) + 26 };
  fill_poly(a, 4, 0x4a3a22, 1);
  float b[8] = { isoX(MAP_W, 0), isoY(MAP_W, 0), isoX(MAP_W, MAP_H), isoY(MAP_W, MAP_H), isoX(MAP_W, MAP_H), isoY(MAP_W, MAP_H) + 26, isoX(MAP_W, 0), isoY(MAP_W, 0) + 26 };
  fill_poly(b, 4, 0x392c19, 1);
}

/* ------------------------------ resource nodes ------------------------------ */

static void draw_tree(const FNode *n) {
  float cx = isoX(n->x + 0.5f, n->y + 0.5f), cy = isoY(n->x + 0.5f, n->y + 0.5f) + 5;
  float s = 0.85f + ((n->id * 7) % 5) / 16.0f;
  fill_ellipse(cx + 2, cy, 11 * s, 4.5f * s, 0x000000, 0.22f);
  fill_rect(cx - 2.5f, cy - 9 * s, 5, 10 * s, 0x5b3b1d, 1);
  static const unsigned cols[3][2] = { { 0x2f6b2c, 0x3f8a38 }, { 0x2a6128, 0x388031 }, { 0x33732f, 0x459440 } };
  const unsigned *c = cols[n->id % 3];
  for (int i = 0; i < 3; i++) {
    float yb = cy - (6 + i * 9) * s, w = (16 - i * 4) * s, hh = 14 * s;
    fill_tri(cx - w, yb, cx + w, yb, cx, yb - hh, c[0], 1);
    fill_tri(cx, yb - hh, cx + w, yb, cx + w * 0.1f, yb, c[1], 1);
  }
}

static void draw_rocks(const FNode *n) {
  float cx = isoX(n->x + 0.5f, n->y + 0.5f), cy = isoY(n->x + 0.5f, n->y + 0.5f) + 6;
  float f = 0.55f + 0.45f * (n->amt / n->max);
  int gold = n->type == N_GOLD;
  fill_ellipse(cx, cy, 15 * f, 6 * f, 0x000000, 0.25f);
  static const float rocks[3][5] = { { -8, -2, 9, 11, 0 }, { 5, -1, 8, 9, 1 }, { -1, -8, 10, 12, 2 } };
  static const unsigned goldc[3] = { 0x6e6a66, 0x7b7771, 0x645f5b }, stonec[3] = { 0x8e8e97, 0xa3a3ac, 0x7c7c85 };
  for (int r = 0; r < 3; r++) {
    float bx = cx + rocks[r][0] * f, by = cy + rocks[r][1] * f * 0.6f, ww = rocks[r][2] * f, hh = rocks[r][3] * f;
    int k = (int)rocks[r][4];
    float p[8] = { bx - ww, by, bx - ww * 0.5f, by - hh, bx + ww * 0.4f, by - hh * 0.9f, bx + ww, by };
    fill_poly(p, 4, gold ? goldc[k] : stonec[k], 1);
    fill_tri(bx - ww * 0.5f, by - hh, bx + ww * 0.4f, by - hh * 0.9f, bx, by - hh * 0.4f, 0xffffff, gold ? 0.08f : 0.18f);
    if (gold) {
      float q[8] = { bx - 3, by - hh * 0.5f, bx, by - hh * 0.75f, bx + 3, by - hh * 0.45f, bx, by - hh * 0.3f };
      fill_poly(q, 4, 0xffd23a, 1);
      fill_rect(bx - 1, by - hh * 0.7f, 2, 2, 0xfff3a0, 1);
    }
  }
}

/* ------------------------------ buildings ------------------------------ */

static void box(float x, float y, float w, float h, float z0, float z1, unsigned wall, unsigned topCol) {
  quad(P(x, y + h, z0), P(x + w, y + h, z0), P(x + w, y + h, z1), P(x, y + h, z1), shade_rgb(wall, 0.84f), 1, 0, 0.25f);
  quad(P(x + w, y + h, z0), P(x + w, y, z0), P(x + w, y, z1), P(x + w, y + h, z1), shade_rgb(wall, 0.6f), 1, 0, 0.25f);
  quad(P(x, y, z1), P(x + w, y, z1), P(x + w, y + h, z1), P(x, y + h, z1), topCol ? topCol : shade_rgb(wall, 1.08f), 1, 0, 0.2f);
}
static void pyramid(float x, float y, float w, float h, float z, float hh, unsigned color) {
  V2 a = P(x + w / 2, y + h / 2, z + hh);
  V2 N = P(x, y, z), E = P(x + w, y, z), S = P(x + w, y + h, z), Wp = P(x, y + h, z);
  tri(N, E, a, shade_rgb(color, 0.7f), 1, 0, 0); tri(N, Wp, a, shade_rgb(color, 0.78f), 1, 0, 0);
  tri(Wp, S, a, shade_rgb(color, 0.95f), 1, 0, 0.25f); tri(S, E, a, shade_rgb(color, 0.72f), 1, 0, 0.25f);
}
static void flag(float x, float y, float z, int team, float len) {
  V2 p = P(x, y, z);
  float t = gNow / 0.3f;
  line(p.x, p.y, p.x, p.y - len, 1.6f, 0x3a2a18, 1);
  float w = 11 + sinf(t + x) * 1.5f;
  tri((V2){ p.x, p.y - len }, (V2){ p.x + w, p.y - len + 3 + sinf(t) }, (V2){ p.x, p.y - len + 8 }, TEAM_COLOR[team], 1, TEAM_DARK[team], 1);
}
static void door(float x, float y, float w, float h, float hh) {
  float cx = x + w / 2;
  quad(P(cx - 0.35f, y + h, 0), P(cx + 0.35f, y + h, 0), P(cx + 0.35f, y + h, hh), P(cx - 0.35f, y + h, hh), 0x2a1a0c, 1, 0, 0.5f);
}
static void window_at(float y, float h, float fx, float z) {
  quad(P(fx - 0.14f, y + h, z), P(fx + 0.14f, y + h, z), P(fx + 0.14f, y + h, z + 7), P(fx - 0.14f, y + h, z + 7), 0xf3da8a, 1, 0, 0.4f);
}

static void draw_building(const FBld *b, float ghostAlpha) {
  const int team = b->team;
  float x = (float)b->x, y = (float)b->y, w = (float)b->w, h = (float)b->h;
  float prog = b->done ? 1.0f : (b->progress > 0.14f ? b->progress : 0.14f);
  float Hh = BLD_HEIGHT[b->type] * prog;
  float oldAlpha = gAlpha;
  gAlpha = ghostAlpha > 0 ? ghostAlpha : !b->done ? 0.6f + 0.4f * prog : 1.0f;
  quad(P(x - 0.1f, y - 0.1f, 0), P(x + w + 0.1f, y - 0.1f, 0), P(x + w + 0.1f, y + h + 0.1f, 0), P(x - 0.1f, y + h + 0.1f, 0), 0x281c0c, 0.35f, 0, 0);
  int roofOn = b->done || prog > 0.7f;
  switch (b->type) {
    case B_FARM: {
      quad(P(x, y, 0), P(x + w, y, 0), P(x + w, y + h, 0), P(x, y + h, 0), 0x6b4a26, 1, 0, 0.3f);
      for (int i = 0; i < 5; i++) {
        float yy = y + (i + 0.5f) * h / 5;
        V2 a = P(x + 0.12f, yy, 0), c = P(x + w - 0.12f, yy, 0);
        line_cap(a.x, a.y, c.x, c.y, 3.2f, b->done ? ((i & 1) ? 0xd9b834 : 0x7db33a) : 0x5a3d1c, 1);
      }
      if (b->done && b->nusers) {
        V2 p = P(x + w - 0.2f, y + 0.2f, 0);
        fill_rect(p.x - 1, p.y - 12, 2, 12, 0xe8d6a0, 1); fill_rect(p.x - 5, p.y - 10, 10, 2, 0xe8d6a0, 1);
      }
      break;
    }
    case B_TOWNCENTER: {
      float h0 = Hh * 0.6f;
      box(x, y, w, h, 0, h0, 0xc7b68f, 0);
      door(x, y, w, h, h0 * 0.7f);
      window_at(y, h, x + 0.6f, h0 * 0.45f); window_at(y, h, x + w - 0.6f, h0 * 0.45f);
      for (int i = 0; i < 3; i++) { V2 p = P(x + w * ((i + 0.5f) / 3), y + h, h0); fill_rect(p.x - 3, p.y - 5, 6, 5, 0xb3a27d, 1); }
      float kx = x + w / 2 - 0.85f, ky = y + h / 2 - 0.85f;
      box(kx, ky, 1.7f, 1.7f, h0, Hh, 0xd3c39d, 0);
      if (roofOn) { pyramid(kx - 0.12f, ky - 0.12f, 1.94f, 1.94f, Hh, 20, TEAM_COLOR[team]); flag(kx + 0.85f, ky + 0.85f, Hh + 20, team, 16); }
      float bxs[2] = { x + 0.25f, x + w - 0.25f };
      for (int i = 0; i < 2; i++) {
        float bx = bxs[i];
        V2 p[5] = { P(bx - 0.12f, y + h, h0 * 0.85f), P(bx + 0.12f, y + h, h0 * 0.85f), P(bx + 0.12f, y + h, h0 * 0.35f), P(bx, y + h, h0 * 0.25f), P(bx - 0.12f, y + h, h0 * 0.35f) };
        polyS(p, 5, TEAM_COLOR[team], 1, TEAM_DARK[team], 1);
      }
      break;
    }
    case B_HOUSE: {
      box(x, y, w, h, 0, Hh, 0xe9dcb6, 0);
      door(x, y, w, h, Hh * 0.6f);
      window_at(y, h, x + 0.3f, Hh * 0.45f);
      if (roofOn) pyramid(x - 0.15f, y - 0.15f, w + 0.3f, h + 0.3f, Hh, 15, TEAM_COLOR[team]);
      break;
    }
    case B_STOREHOUSE: {
      box(x, y, w, h, 0, Hh, 0xa88b5e, 0);
      door(x, y, w, h, Hh * 0.7f);
      if (roofOn) pyramid(x - 0.12f, y - 0.12f, w + 0.24f, h + 0.24f, Hh, 11, TEAM_DARK[team]);
      V2 p = P(x + w + 0.15f, y + h - 0.2f, 0);
      fill_ellipse(p.x, p.y - 4, 4, 5, 0xc9b27a, 1);
      fill_rect(p.x - 1.5f, p.y - 9, 3, 2, TEAM_COLOR[team], 1);
      break;
    }
    case B_BARRACKS: case B_ARCHERY: case B_STABLE: case B_BLACKSMITH: {
      unsigned wallCol = b->type == B_BARRACKS ? 0x9b7650 : b->type == B_ARCHERY ? 0xa98557 : b->type == B_STABLE ? 0x8c6a45 : 0x85807a;
      box(x, y, w, h, 0, Hh, wallCol, 0);
      door(x, y, w, h, Hh * 0.62f);
      if (roofOn) { pyramid(x - 0.2f, y - 0.2f, w + 0.4f, h + 0.4f, Hh, 15, TEAM_COLOR[team]); flag(x + w * 0.5f, y + h * 0.5f, Hh + 15, team, 14); }
      V2 e = P(x + w * 0.72f, y + h, Hh * 0.55f);
      if (b->type == B_BARRACKS) {
        line(e.x - 6, e.y - 6, e.x + 6, e.y + 6, 2, 0xe6edf2, 1); line(e.x + 6, e.y - 6, e.x - 6, e.y + 6, 2, 0xe6edf2, 1);
        line(e.x - 3, e.y + 3, e.x + 3, e.y + 3, 2, 0xb08a3a, 1);
      } else if (b->type == B_ARCHERY) {
        fill_ellipse(e.x, e.y, 7, 5.6f, 0xf2f2f2, 1); fill_ellipse(e.x, e.y, 5, 4, 0xc93a2a, 1);
        fill_ellipse(e.x, e.y, 3, 2.4f, 0xf2f2f2, 1); fill_ellipse(e.x, e.y, 1.4f, 1.1f, 0xc93a2a, 1);
      } else if (b->type == B_STABLE) {
        stroke_arc(e.x, e.y, 5, 5, PI_F - 0.2f, 0.2f, 2.4f, 0xd9d9d9, 1);   /* horseshoe: opening faces down */
        stroke_arc(e.x, e.y, 5, 5, 0.2f, PI_F - 0.2f, 2.4f, 0xd9d9d9, 0);
      } else {
        fill_rect(e.x - 6, e.y - 2, 12, 3, 0x2b2b2f, 1); fill_rect(e.x - 3, e.y + 1, 6, 4, 0x2b2b2f, 1); fill_rect(e.x - 7, e.y - 3, 4, 2, 0x2b2b2f, 1);
        V2 q = P(x + 0.6f, y + 0.6f, Hh + 14);
        fill_rect(q.x - 2, q.y - 2, 4, 10, 0x585860, 1);
        float t = gNow / 0.4f;
        fill_ellipse(q.x + sinf(t) * 3, q.y - 8 - fmodf(t, 1.0f) * 6, 4, 4, 0xc8c8c8, 0.35f);
        fill_ellipse(q.x + sinf(t + 2) * 4, q.y - 16 - fmodf(t, 1.0f) * 6, 5, 5, 0xc8c8c8, 0.35f);
      }
      break;
    }
    case B_PORT: {
      float dz = 6 * prog + 2;
      box(x, y, w, h, 0, dz, 0x8a6a45, 0xa98a5a);
      for (int i = 1; i < 6; i++) { V2 a = P(x + w * i / 6, y, dz), c = P(x + w * i / 6, y + h, dz); line(a.x, a.y, c.x, c.y, 1, 0x000000, 0.25f); }
      float hx = x + 0.3f, hy = y + 0.3f;
      box(hx, hy, 1.4f, 1.4f, 6, 6 + Hh * 0.6f, 0xb49a6c, 0);
      if (roofOn) pyramid(hx - 0.1f, hy - 0.1f, 1.6f, 1.6f, 6 + Hh * 0.6f, 10, TEAM_COLOR[team]);
      V2 mp = P(x + w - 0.6f, y + h - 0.6f, 6);
      line(mp.x, mp.y, mp.x, mp.y - 34 * prog, 3, 0x4a3320, 1);
      if (roofOn) tri((V2){ mp.x, mp.y - 32 }, (V2){ mp.x + 16, mp.y - 20 }, (V2){ mp.x, mp.y - 10 }, TEAM_COLOR[team], 1, TEAM_DARK[team], 1);
      float cr[2][2] = { { x + w - 0.5f, y + 0.5f }, { x + w - 0.9f, y + 0.9f } };
      for (int i = 0; i < 2; i++) { V2 p = P(cr[i][0], cr[i][1], 8); fill_rect(p.x - 4, p.y - 6, 8, 8, 0x9b7a46, 1); stroke_rect(p.x - 4, p.y - 6, 8, 8, 1, 0x5a4020, 1); }
      break;
    }
    case B_TOWER: {
      float tx = x + 0.2f, ty = y + 0.2f, tw = w - 0.4f, th = h - 0.4f;
      box(tx, ty, tw, th, 0, Hh, 0xb3a98f, 0);
      quad(P(tx + tw * 0.5f - 0.1f, ty + th, Hh * 0.55f), P(tx + tw * 0.5f + 0.1f, ty + th, Hh * 0.55f), P(tx + tw * 0.5f + 0.1f, ty + th, Hh * 0.75f), P(tx + tw * 0.5f - 0.1f, ty + th, Hh * 0.75f), 0x1d1308, 1, 0, 0);
      door(tx, ty, tw, th, Hh * 0.28f);
      if (b->done) {
        box(x + 0.05f, y + 0.05f, w - 0.1f, h - 0.1f, Hh, Hh + 6, 0xa89d82, 0);
        for (int i = 0; i < 4; i++) { V2 p = P(x + 0.1f + (w - 0.2f) * (i / 3.0f), y + h - 0.05f, Hh + 6); fill_rect(p.x - 2, p.y - 4, 4, 4, 0x948a70, 1); }
        flag(x + w / 2, y + h / 2, Hh + 6, team, 18);
      }
      break;
    }
  }
  if (!b->done) {                                       /* scaffolding */
    float top = BLD_HEIGHT[b->type] + 8;
    float cs[3][2] = { { x, y + h }, { x + w, y + h }, { x + w, y } };
    for (int i = 0; i < 3; i++) { V2 a = P(cs[i][0], cs[i][1], 0), c = P(cs[i][0], cs[i][1], top * (0.5f + 0.5f * prog)); line(a.x, a.y, c.x, c.y, 1.6f, 0x7a5a2c, 1); }
    V2 a = P(x, y + h, top * 0.4f), c = P(x + w, y + h, top * 0.4f);
    line(a.x, a.y, c.x, c.y, 1.6f, 0x7a5a2c, 1);
  }
  if (b->done && b->hp < b->maxHp * 0.5f && b->type != B_FARM) {   /* burning */
    float t = gNow / 0.12f;
    for (int i = 0; i < 3; i++) {
      V2 p = P(x + w * (0.3f + 0.2f * i), y + h * (0.4f + 0.15f * i), Hh * 0.9f);
      float f = 6 + sinf(t + i * 2) * 2;
      fill_ellipse(p.x, p.y - f * 0.4f, 4, f, 0xff7a1a, 1);
      fill_ellipse(p.x, p.y - f * 0.2f, 2, f * 0.55f, 0xffd24a, 1);
    }
  }
  gAlpha = oldAlpha;
}

/* ------------------------------ units ------------------------------ */
/* A small matrix stack so the unit drawing code can stay close to the canvas translate / rotate / scale calls. */

typedef struct { float a, b, c, d, e, f; } Mx;
static Mx cm = { 1, 0, 0, 1, 0, 0 };
static Mx mxStack[8]; static int mxSp;
static void mx_push(void) { if (mxSp < 8) mxStack[mxSp++] = cm; }
static void mx_pop(void) { if (mxSp > 0) cm = mxStack[--mxSp]; }
static void mx_reset(float tx, float ty) { cm = (Mx){ 1, 0, 0, 1, tx, ty }; mxSp = 0; }
static void mx_translate(float tx, float ty) { cm.e += cm.a * tx + cm.c * ty; cm.f += cm.b * tx + cm.d * ty; }
static void mx_scale(float sx, float sy) { cm.a *= sx; cm.b *= sx; cm.c *= sy; cm.d *= sy; }
static void mx_rotate(float r) {
  float cs = cosf(r), sn = sinf(r), a = cm.a, b = cm.b, c = cm.c, d = cm.d;
  cm.a = a * cs + c * sn; cm.b = b * cs + d * sn; cm.c = -a * sn + c * cs; cm.d = -b * sn + d * cs;
}
static void tfp(float x, float y, float *ox, float *oy) { *ox = cm.a * x + cm.c * y + cm.e; *oy = cm.b * x + cm.d * y + cm.f; }

static void l_poly(const float *p, int n, unsigned rgb, float a) {
  float t[32];
  if (n > 16) n = 16;
  for (int i = 0; i < n; i++) tfp(p[2 * i], p[2 * i + 1], &t[2 * i], &t[2 * i + 1]);
  fill_poly(t, n, rgb, a);
}
static void l_rect(float x, float y, float w, float h, unsigned rgb, float a) { float p[8] = { x, y, x + w, y, x + w, y + h, x, y + h }; l_poly(p, 4, rgb, a); }
static void l_tri(float x0, float y0, float x1, float y1, float x2, float y2, unsigned rgb, float a) { float p[6] = { x0, y0, x1, y1, x2, y2 }; l_poly(p, 3, rgb, a); }
static void l_line(float x0, float y0, float x1, float y1, float w, unsigned rgb, float a) {
  float ax, ay, bx, by; tfp(x0, y0, &ax, &ay); tfp(x1, y1, &bx, &by); line(ax, ay, bx, by, w, rgb, a);
}
static void l_line_cap(float x0, float y0, float x1, float y1, float w, unsigned rgb, float a) {
  float ax, ay, bx, by; tfp(x0, y0, &ax, &ay); tfp(x1, y1, &bx, &by); line_cap(ax, ay, bx, by, w, rgb, a);
}
/* ellipse arc (filled as a closed segment, or stroked) in local space */
static int l_arc_pts(float cx, float cy, float rx, float ry, float a0, float a1, float *out, int maxn) {
  int n = arc_segments(rx, ry, fabsf(a1 - a0)); if (n + 1 > maxn) n = maxn - 1;
  for (int i = 0; i <= n; i++) { float t = a0 + (a1 - a0) * i / n; tfp(cx + cosf(t) * rx, cy + sinf(t) * ry, &out[2 * i], &out[2 * i + 1]); }
  return n + 1;
}
static void l_fill_arc(float cx, float cy, float rx, float ry, float a0, float a1, unsigned rgb, float a) {
  float pts[2 * 60]; int n = l_arc_pts(cx, cy, rx, ry, a0, a1, pts, 60);
  SDL_Color c = mkcol(rgb, a);
  if (n < 3) return;
  SDL_Vertex *v = gfx_alloc((n - 2) * 3, NULL);
  for (int i = 1; i < n - 1; i++) { vtx(v++, pts[0], pts[1], c); vtx(v++, pts[2 * i], pts[2 * i + 1], c); vtx(v++, pts[2 * i + 2], pts[2 * i + 3], c); }
}
static void l_ellipse(float cx, float cy, float rx, float ry, unsigned rgb, float a) { l_fill_arc(cx, cy, rx, ry, 0, 2 * PI_F, rgb, a); }
static void l_stroke_arc(float cx, float cy, float r, float a0, float a1, float w, unsigned rgb, float a) {
  float pts[2 * 60]; int n = l_arc_pts(cx, cy, r, r, a0, a1, pts, 60);
  for (int i = 1; i < n; i++) line(pts[2 * i - 2], pts[2 * i - 1], pts[2 * i], pts[2 * i + 1], w, rgb, a);
}
static void l_stroke_ellipse(float cx, float cy, float rx, float ry, float w, unsigned rgb, float a) {
  float pts[2 * 60]; int n = l_arc_pts(cx, cy, rx, ry, 0, 2 * PI_F, pts, 60);
  for (int i = 1; i < n; i++) line(pts[2 * i - 2], pts[2 * i - 1], pts[2 * i], pts[2 * i + 1], w, rgb, a);
}

static void u_legs(float t, int mv, unsigned col) {
  float l = mv ? sinf(t * 9) * 3 : 0;
  l_line_cap(-2, -6, -2 + l, 0, 2.6f, col, 1); l_line_cap(2, -6, 2 - l, 0, 2.6f, col, 1);
}
static void u_head(float y, int helmet) {
  l_ellipse(0, y, 3.4f, 3.4f, 0xe9c39b, 1);
  if (helmet) { l_fill_arc(0, y - 0.4f, 3.9f, 3.9f, PI_F, 2 * PI_F, 0x9aa0a8, 1); l_rect(-3.9f, y - 0.6f, 7.8f, 1.3f, 0x9aa0a8, 1); }
}

/* outline of the entity under the cursor: a ring at a unit's feet, the footprint of a building or resource tile */
static void draw_hover(int id) {
  const FUnit *u = get_unit(id); const FBld *b = get_bld(id); const FNode *n = get_node(id);
  if (u) {
    unsigned col = u->team == 0 ? 0xbff5c4 : 0xff8e7e;
    int big = UDEF[u->type].big;
    stroke_ellipse(isoX(u->x, u->y), isoY(u->x, u->y) + 1, big ? 17 : 11, big ? 8 : 5.5f, 2, col, 0.95f);
    return;
  }
  float x0, y0, w, h; unsigned col;
  if (b) { x0 = (float)b->x; y0 = (float)b->y; w = (float)b->w; h = (float)b->h; col = b->team == 0 ? 0xbff5c4 : 0xff8e7e; }
  else if (n) { x0 = (float)n->x; y0 = (float)n->y; w = h = 1; col = 0xfff3a0; }
  else return;
  float q[8] = { isoX(x0, y0), isoY(x0, y0), isoX(x0 + w, y0), isoY(x0 + w, y0), isoX(x0 + w, y0 + h), isoY(x0 + w, y0 + h), isoX(x0, y0 + h), isoY(x0, y0 + h) };
  stroke_poly(q, 4, 2, col, 0.95f);
}

static void draw_unit(const FUnit *u, int selected) {
  float x = isoX(u->x, u->y), y = isoY(u->x, u->y);
  const UnitDef *d = &UDEF[u->type];
  int team = u->team, big = d->big, mv = u->moving;
  unsigned TC = TEAM_COLOR[team], TD = TEAM_DARK[team], TL = TEAM_LIGHT[team];
  float t = u->anim, bob = mv ? fabsf(sinf(t * 9)) * 2 : 0;
  float sw = u->atkAnim > 0 ? sinf((1 - u->atkAnim / 0.28f) * PI_F) : 0;
  float oy = y + (u->type == U_WARSHIP ? sinf(t * 2) * 1.5f : 0);
  fill_ellipse(x, oy + 1, big ? 13 : 8, big ? 5.5f : 4, d->water ? 0x001e3c : 0x000000, 0.3f);
  if (selected) stroke_ellipse(x, oy + 1, big ? 17 : 11, big ? 8 : 5.5f, 2, TL, 1);
  mx_reset(x, oy);
  mx_scale((float)u->face, 1);
  switch (u->type) {
    case U_BUILDER: {
      u_legs(t, mv, 0x3b2a1a);
      l_rect(-4.5f, -15 - bob, 9, 10, TC, 1);
      l_rect(-4.5f, -9 - bob, 9, 1.3f, 0x000000, 0.3f);
      u_head(-18.5f - bob, 0);
      l_ellipse(0, -21 - bob, 6, 1.7f, 0x9b7a3e, 1);
      l_fill_arc(0, -21.3f - bob, 3.2f, 3.2f, PI_F, 2 * PI_F, 0x9b7a3e, 1);
      if (u->carry >= 1) {
        unsigned cc = u->carryType >= 0 && u->carryType < 4 ? RES_COLOR[u->carryType] : 0xaaaaaa;
        if (u->carryType == 1) cc = 0x7a4f25;
        l_rect(-9, -15 - bob, 5, 6, cc, 1);
        l_line(-9, -15 - bob, -4, -15 - bob, 1, 0x000000, 0.4f); l_line(-9, -9 - bob, -4, -9 - bob, 1, 0x000000, 0.4f);
        l_line(-9, -15 - bob, -9, -9 - bob, 1, 0x000000, 0.4f); l_line(-4, -15 - bob, -4, -9 - bob, 1, 0x000000, 0.4f);
      }
      mx_push();
      mx_translate(4, -11 - bob);
      mx_rotate(u->working ? sinf(t * 10) * 0.9f - 0.6f : -0.3f + (mv ? sinf(t * 9) * 0.2f : 0));
      l_line(0, 0, 0, -11, 1.8f, 0x7a5a30, 1);
      int tool = u->state == ST_BUILD ? 3 : u->gatherKind == 1 ? 0 : (u->gatherKind == 2 || u->gatherKind == 3) ? 1 : u->gatherKind == 0 ? 2 : 3;
      if (tool == 0) l_rect(0, -12, 5, 5, 0xb8bec4, 1);
      else if (tool == 1) l_rect(-5, -12, 10, 2, 0xb8bec4, 1);
      else if (tool == 2) { l_rect(0, -12, 4, 2, 0xb8bec4, 1); l_rect(3, -12, 2, 4, 0xb8bec4, 1); }
      else l_rect(-3, -13, 7, 4, 0xb8bec4, 1);
      mx_pop();
      break;
    }
    case U_SWORDSMAN: case U_SPEARMAN: {
      u_legs(t, mv, 0x3b2a1a);
      l_rect(-4.8f, -15 - bob, 9.6f, 10, TC, 1);
      l_rect(-4.8f, -15 - bob, 9.6f, 2, 0xffffff, 0.2f);
      u_head(-18.5f - bob, 1);
      l_ellipse(-5.5f, -10 - bob, 3.2f, 5, TC, 1); l_stroke_ellipse(-5.5f, -10 - bob, 3.2f, 5, 1.3f, 0xe8e0c0, 1);
      if (u->type == U_SWORDSMAN) {
        mx_push(); mx_translate(5, -10 - bob); mx_rotate(-0.8f + sw * 1.9f);
        l_line(0, 0, 0, -12, 2, 0xe4eaee, 1); l_line(-2.5f, -2, 2.5f, -2, 1.6f, 0xb08a3a, 1);
        mx_pop();
      } else {
        float th = sw * 7;
        l_line(4 + th * 0.3f, -3, 8 + th * 0.4f, -27 + th, 1.8f, 0x8a6a3a, 1);
        l_tri(8 + th * 0.4f, -27 + th, 10 + th * 0.4f, -31 + th, 6 + th * 0.4f, -31 + th, 0xe4eaee, 1);
      }
      break;
    }
    case U_ARCHER: {
      u_legs(t, mv, 0x2f3a22);
      l_rect(-4.2f, -15 - bob, 8.4f, 10, TC, 1);
      l_rect(-7, -16 - bob, 3.5f, 9, 0x6b4a2a, 1);
      u_head(-18.5f - bob, 0);
      l_tri(-4.4f, -17 - bob, 0, -25 - bob, 4.4f, -17 - bob, TD, 1);
      l_stroke_arc(5, -11 - bob, 7, -1.25f, 1.25f, 2, 0x6b4a2a, 1);
      float pull = sw > 0 ? 2.5f : 0;
      l_line(5 + cosf(-1.25f) * 7, -11 - bob + sinf(-1.25f) * 7, 5 - pull, -11 - bob, 0.8f, 0xe8e0c0, 1);
      l_line(5 + cosf(1.25f) * 7, -11 - bob + sinf(1.25f) * 7, 5 - pull, -11 - bob, 0.8f, 0xe8e0c0, 1);
      if (u->cd > 0.8f || sw > 0) l_line(5 - pull, -11 - bob, 13, -11 - bob, 1.2f, 0xcbb27a, 1);
      break;
    }
    case U_CAVALRY: {
      float gal = mv ? sinf(t * 10) : 0, ag = fabsf(gal);
      static const float legsx[4][2] = { { -8, 0 }, { -5, 1.6f }, { 6, 0.8f }, { 9, 2.4f } };
      for (int i = 0; i < 4; i++) { float sv = mv ? sinf(t * 10 + legsx[i][1]) * 4 : 0; l_line_cap(legsx[i][0], -8, legsx[i][0] + sv, 0, 2.6f, 0x3b2412, 1); }
      l_ellipse(0, -11 - ag, 12, 5.5f, 0x7a5230, 1);
      l_ellipse(-1, -13 - ag, 8, 4, TC, 1);
      l_tri(8, -14, 13, -23, 17, -21, 0x7a5230, 1); l_tri(8, -14, 17, -21, 13, -9, 0x7a5230, 1);
      mx_push(); mx_translate(16, -21); mx_rotate(0.5f); l_ellipse(0, 0, 4.5f, 2.6f, 0x7a5230, 1); mx_pop();
      l_line(-11, -13, -14.5f, -11.5f, 2, 0x2c1a0b, 1); l_line(-14.5f, -11.5f, -14, -5, 2, 0x2c1a0b, 1);
      float ry = -22 - ag;
      l_rect(-3.5f, ry - 1, 7, 9, TC, 1);
      mx_push(); mx_translate(0, ry + 1); u_head(-5, 1); mx_pop();
      float th = sw * 8;
      l_line(3, ry + 4, 22 + th, ry - 2 + th * 0.1f, 1.9f, 0x8a6a3a, 1);
      l_tri(22 + th, ry - 2 + th * 0.1f, 27 + th, ry - 1.5f + th * 0.1f, 22 + th, ry - 4 + th * 0.1f, 0xe4eaee, 1);
      break;
    }
    case U_WARSHIP: {
      if (mv) {
        l_ellipse(-20, 4, 9, 2.5f, 0xffffff, 0.35f); l_ellipse(-30, 5, 6, 1.8f, 0xffffff, 0.35f);
        float o = sinf(t * 6) * 4;
        for (int i = -1; i <= 1; i++) l_line(i * 8.0f, -1, i * 8.0f + o, 8, 1.5f, 0x4a3320, 1);
      }
      float hull[8] = { -19, -4, 21, -4, 14, 6, -13, 6 };
      l_poly(hull, 4, 0x6b4526, 1);
      l_line(-19, -4, 21, -4, 1.2f, 0x2f1d0e, 1); l_line(21, -4, 14, 6, 1.2f, 0x2f1d0e, 1); l_line(14, 6, -13, 6, 1.2f, 0x2f1d0e, 1); l_line(-13, 6, -19, -4, 1.2f, 0x2f1d0e, 1);
      l_rect(-18, -4, 38, 2.4f, TC, 1);
      l_rect(-14, -7, 28, 3, 0x8a5e34, 1);
      l_line(0, -6, 0, -36, 2.6f, 0x4a3320, 1);
      float sail[2 * 20]; int n = 0;
      sail[n++] = 1.5f; sail[n++] = -34; sail[n++] = 1.5f; sail[n++] = -10;
      for (int i = 1; i <= 5; i++) { float s = i / 5.0f, q = 1 - s; sail[n++] = q * q * 1.5f + 2 * q * s * 16 + s * s * 15; sail[n++] = q * q * -10 + 2 * q * s * -14 + s * s * -22; }
      for (int i = 1; i <= 4; i++) { float s = i / 5.0f, q = 1 - s; sail[n++] = q * q * 15 + 2 * q * s * 12 + s * s * 1.5f; sail[n++] = q * q * -22 + 2 * q * s * -29 + s * s * -34; }
      l_poly(sail, n / 2, TC, 1);
      for (int i = 0; i < n / 2; i++) { int j = (i + 1) % (n / 2); l_line(sail[2 * i], sail[2 * i + 1], sail[2 * j], sail[2 * j + 1], 1, TD, 1); }
      l_tri(0, -36, 9, -34, 0, -31, TL, 1);
      if (sw > 0) l_ellipse(23, -9, 5 * sw + 1, 5 * sw + 1, 0xffb02a, 1);
      break;
    }
  }
}

/* ------------------------------ overlays ------------------------------ */

static void draw_bar(float cx, float cy, float w, float frac, int prog) {
  frac = clampf(frac, 0, 1);
  fill_rect(cx - w / 2 - 1, cy - 1, w + 2, 5, 0x000000, 0.75f);
  fill_rect(cx - w / 2, cy, w * frac, 3, prog ? 0x4aa0e0 : frac > 0.6f ? 0x5fcf6b : frac > 0.3f ? 0xe2c23a : 0xd8493b, 1);
}

static void draw_projectile(const FProj *p) {
  float t = p->t / p->dur, ex = p->tx, ey = p->ty;
  float dd = hypotf(ex - p->sx, ey - p->sy);
  float pts[2][2];
  for (int k = 0; k < 2; k++) {
    float tt = k == 0 ? t : (t - 0.07f > 0 ? t - 0.07f : 0);
    float x = p->sx + (ex - p->sx) * tt, y = p->sy + (ey - p->sy) * tt;
    float z = 10 + sinf(tt * PI_F) * dd * (p->stone ? 7.0f : 3.5f);
    pts[k][0] = isoX(x, y); pts[k][1] = isoY(x, y) - z;
  }
  if (p->stone) {
    fill_ellipse(pts[0][0], pts[0][1], 3.6f, 3.6f, 0x2b2b2b, 1);
    fill_ellipse(pts[0][0] - 1, pts[0][1] - 1, 1.2f, 1.2f, 0xffffff, 0.25f);
  } else {
    line(pts[1][0], pts[1][1], pts[0][0], pts[0][1], 1.6f, 0xe8d9a8, 1);
    fill_ellipse(pts[0][0], pts[0][1], 1.3f, 1.3f, 0xcccccc, 1);
  }
}

static void draw_effect(const FEff *e) {
  float x = isoX(e->x, e->y), y = isoY(e->x, e->y) - e->z, a = clampf(e->life / e->max, 0, 1);
  if (e->type == 0) {
    static const unsigned dust[3] = { 0x6b4a2b, 0x8a7a5a, 0x7a6a52 };
    float r = 2 + (1 - a) * 3;
    fill_ellipse(x, y, r, r, dust[(e->color >= 0 && e->color < 3) ? e->color : 1], a * 0.8f);
  } else {
    char s[24];
    int r = e->res >= 0 && e->res < 4 ? e->res : 0;
    snprintf(s, sizeof s, "+%d %c", e->amt, RES_LETTER[r]);
    text_outline(x - text_width(s, 1.4f) / 2, y - 8, 1.4f, RES_COLOR[r], clampf(a * 2, 0, 1), 0x000000, s);
  }
}

/* ------------------------------ placement / picking helpers ------------------------------ */

static int placement_tile_ok(int tx, int ty) {
  return tx >= 0 && ty >= 0 && tx < MAP_W && ty < MAP_H && mTerrain[ty * MAP_W + tx] != TILE_WATER && !mOcc[ty * MAP_W + tx];
}

/* ------------------------------ main render ------------------------------ */

typedef struct { float key; int kind, id; } DrawItem;
static DrawItem items[SIM_MAXU + SIM_MAXB + SIM_MAXN];
static int cmp_item(const void *a, const void *b) {
  const DrawItem *x = a, *y = b;
  if (x->key != y->key) return x->key < y->key ? -1 : 1;
  return x->id - y->id;
}
static int entity_visible(float ix, float iy, const View *v, float pad) { return ix > v->l - pad && ix < v->r + pad && iy > v->t - pad - 60 && iy < v->b + pad; }

static void world_begin(void) { gfx_set_scale(dpr * zoom); gOX = VX / zoom - camX; gOY = VY / zoom - camY; gDetail = dpr * zoom; }
static void screen_begin(void) { gfx_set_scale(dpr); gOX = gOY = 0; gDetail = dpr; }

static void render_world(int mouseX, int mouseY, int dragging, float dragX, float dragY) {
  world_begin();
  View vis;
  vis.l = camX - VX / zoom; vis.r = camX + (W - VX) / zoom; vis.t = camY - VY / zoom; vis.b = camY + (H - VY) / zoom;
  float cx[4] = { vis.l, vis.r, vis.l, vis.r }, cy[4] = { vis.t, vis.t, vis.b + 40, vis.b + 40 };
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  for (int i = 0; i < 4; i++) {
    float a = cx[i] / 32, b = cy[i] / 16, wx = (a + b) / 2, wy = (b - a) / 2;
    if (wx < minx) minx = wx;
    if (wx > maxx) maxx = wx;
    if (wy < miny) miny = wy;
    if (wy > maxy) maxy = wy;
  }
  vis.x0 = (int)fmaxf(0, floorf(minx) - 1); vis.x1 = (int)fminf(MAP_W - 1, ceilf(maxx) + 1);
  vis.y0 = (int)fmaxf(0, floorf(miny) - 1); vis.y1 = (int)fminf(MAP_H - 1, ceilf(maxy) + 1);

  draw_terrain(&vis);

  /* selection footprints and the rally line, on the ground */
  for (int i = 0; i < nSel; i++) {
    const FBld *b = get_bld(sel[i]);
    if (b) {
      V2 p[4] = { P(b->x - 0.05f, b->y - 0.05f, 0), P(b->x + b->w + 0.05f, b->y - 0.05f, 0), P(b->x + b->w + 0.05f, b->y + b->h + 0.05f, 0), P(b->x - 0.05f, b->y + b->h + 0.05f, 0) };
      polyS(p, 4, 0xffffff, 0.1f, b->team == 0 ? 0x9af0a6 : 0xff8e7e, 1);
      float f[8]; for (int k = 0; k < 4; k++) { f[2 * k] = p[k].x; f[2 * k + 1] = p[k].y; }
      stroke_poly(f, 4, 1.5f, b->team == 0 ? 0x9af0a6 : 0xff8e7e, 1);
    }
    const FNode *n = get_node(sel[i]);
    if (n) {
      V2 p[4] = { P((float)n->x, (float)n->y, 0), P(n->x + 1.0f, (float)n->y, 0), P(n->x + 1.0f, n->y + 1.0f, 0), P((float)n->x, n->y + 1.0f, 0) };
      polyS(p, 4, 0xffffff, 0.12f, 0xffffff, 1);
      float f[8]; for (int k = 0; k < 4; k++) { f[2 * k] = p[k].x; f[2 * k + 1] = p[k].y; }
      stroke_poly(f, 4, 1.0f, 0xffffff, 1);
    }
  }
  if (nSel == 1) {
    const FBld *b = get_bld(sel[0]);
    if (b && b->team == 0 && b->hasRally) {
      float cxw = b->x + b->w / 2.0f, cyw = b->y + b->h / 2.0f;
      float ax = isoX(cxw, cyw), ay = isoY(cxw, cyw), bx = isoX(b->rx, b->ry), by = isoY(b->rx, b->ry);
      float len = hypotf(bx - ax, by - ay);
      for (float s = 0; s < len; s += 11) {                         /* dashed line */
        float s2 = fminf(len, s + 6);
        line(ax + (bx - ax) * s / len, ay + (by - ay) * s / len, ax + (bx - ax) * s2 / len, ay + (by - ay) * s2 / len, 1.5f, 0xffffff, 0.6f);
      }
      V2 p = P(b->rx, b->ry, 0);
      line(p.x, p.y, p.x, p.y - 20, 1.5f, 0xffffff, 1);
      fill_tri(p.x, p.y - 20, p.x + 10, p.y - 16, p.x, p.y - 12, TEAM_COLOR[0], 1);
    }
  }
  for (int i = 0; i < nClickFx; i++) {
    float k = clickFx[i].t / 0.7f, r = 6 + k * 14;
    stroke_ellipse(isoX(clickFx[i].x, clickFx[i].y), isoY(clickFx[i].x, clickFx[i].y), r * 1.6f, r * 0.8f, 2, clickFx[i].color, 1 - k);
  }

  /* placement footprint */
  if (placingOn) {
    for (int j = 0; j < BDEF[placeType].h; j++) for (int i = 0; i < BDEF[placeType].w; i++) {
      int tx = placeX + i, ty = placeY + j, ok = placement_tile_ok(tx, ty) && placeValid;
      quad(P((float)tx, (float)ty, 0), P(tx + 1.0f, (float)ty, 0), P(tx + 1.0f, ty + 1.0f, 0), P((float)tx, ty + 1.0f, 0), ok ? 0x50ff78 : 0xff463c, ok ? 0.38f : 0.45f, 0, 0);
    }
  }

  /* depth-sorted entities */
  int ni = 0;
  for (int i = 0; i < nNodes; i++) {
    const FNode *n = &mNodes[i];
    if (!entity_visible(isoX(n->x + 0.5f, n->y + 0.5f), isoY(n->x + 0.5f, n->y + 0.5f), &vis, 50)) continue;
    items[ni++] = (DrawItem){ (float)(n->x + n->y + 1), K_NODE, n->id };
  }
  for (int i = 0; i < nBlds; i++) {
    const FBld *b = &mBlds[i];
    float bx = b->x + b->w / 2.0f, by = b->y + b->h / 2.0f;
    if (!entity_visible(isoX(bx, by), isoY(bx, by), &vis, 160)) continue;
    items[ni++] = (DrawItem){ b->x + b->y + (b->w + b->h) / 2.0f - (b->type == B_FARM ? 100 : 0), K_BLD, b->id };
  }
  for (int i = 0; i < nUnits; i++) {
    const FUnit *u = &mUnits[i];
    if (!entity_visible(isoX(u->x, u->y), isoY(u->x, u->y), &vis, 60)) continue;
    items[ni++] = (DrawItem){ u->x + u->y, K_UNIT, u->id };
  }
  qsort(items, (size_t)ni, sizeof(DrawItem), cmp_item);
  for (int i = 0; i < ni; i++) {
    if (items[i].kind == K_NODE) { const FNode *n = get_node(items[i].id); if (n->type == N_TREE) draw_tree(n); else draw_rocks(n); }
    else if (items[i].kind == K_BLD) draw_building(get_bld(items[i].id), 0);
    else draw_unit(get_unit(items[i].id), sel_has(items[i].id));
  }

  if (hoverId && !sel_has(hoverId)) draw_hover(hoverId);
  for (int i = 0; i < nProjs; i++) draw_projectile(&mProjs[i]);
  for (int i = 0; i < nEffs; i++) draw_effect(&mEffs[i]);

  /* health bars */
  for (int i = 0; i < ni; i++) {
    if (items[i].kind == K_NODE) continue;
    int isSel = sel_has(items[i].id) || items[i].id == hoverId;   /* hovering shows the health bar too */
    if (items[i].kind == K_UNIT) {
      const FUnit *u = get_unit(items[i].id);
      if (!isSel && u->hp >= u->maxHp) continue;
      int big = UDEF[u->type].big;
      draw_bar(isoX(u->x, u->y), isoY(u->x, u->y) - (big ? 46 : 31), big ? 26.0f : 20.0f, u->hp / u->maxHp, 0);
    } else {
      const FBld *b = get_bld(items[i].id);
      if (!(isSel || b->hp < b->maxHp || !b->done)) continue;
      float cxw = b->x + b->w / 2.0f, cyw = b->y + b->h / 2.0f;
      float top = BLD_HEIGHT[b->type] * (b->done ? 1.0f : fmaxf(0.14f, b->progress)) + (b->type == B_TOWER || b->type == B_TOWNCENTER ? 30 : 22);
      float bx = isoX(cxw, cyw), by = isoY(cxw, cyw) - top, bw = 26.0f + b->w * 10;
      if (!b->done) { draw_bar(bx, by, bw, b->progress, 1); draw_bar(bx, by + 6, bw, b->hp / b->maxHp, 0); }
      else draw_bar(bx, by, bw, b->hp / b->maxHp, 0);
    }
  }

  /* building ghost */
  if (placingOn) {
    FBld g; memset(&g, 0, sizeof g);
    g.type = placeType; g.team = 0; g.x = placeX; g.y = placeY; g.w = BDEF[placeType].w; g.h = BDEF[placeType].h; g.done = 1; g.progress = 1;
    g.hp = g.maxHp = (float)BDEF[placeType].hp;
    draw_building(&g, placeValid ? 0.62f : 0.35f);
  }

  /* drag-selection box, in screen space */
  screen_begin();
  (void)mouseX; (void)mouseY;
  if (dragging) {
    float x = fminf(dragX, (float)mouseX), y = fminf(dragY, (float)mouseY), w = fabsf((float)mouseX - dragX), h = fabsf((float)mouseY - dragY);
    fill_rect(x, y, w, h, 0x64ff82, 0.12f);
    stroke_rect(x, y, w, h, 1.5f, 0x7dff91, 1);
  }
}

/* ------------------------------ minimap ------------------------------ */

static SDL_Texture *miniTex;

static void mini_pt(float x, float y, float *mx, float *my) { *mx = ((x - y) * 32 + MAP_H * 32) * MM_K; *my = (x + y) * 16 * MM_K; }

static void build_mini_terrain(void) {
  if (miniTex) SDL_DestroyTexture(miniTex);
  miniTex = SDL_CreateTexture(gR, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, (int)(MM_W * dpr), (int)(MM_H * dpr));
  if (!miniTex) return;
  gfx_flush();
  SDL_SetRenderTarget(gR, miniTex);
  SDL_SetRenderDrawColor(gR, 11, 15, 10, 255);
  SDL_RenderClear(gR);
  screen_begin();
  for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
    int t = mTerrain[y * MAP_W + x];
    unsigned col = t == TILE_WATER ? 0x2b69a1 : t == TILE_SAND ? 0xb9a566 : 0x3d6e2d;
    float ax, ay, bx, by, dx, dy, ex, ey;
    mini_pt((float)x, (float)y, &ax, &ay); mini_pt(x + 1.0f, (float)y, &bx, &by); mini_pt(x + 1.0f, y + 1.0f, &dx, &dy); mini_pt((float)x, y + 1.0f, &ex, &ey);
    float p[8] = { ax, ay - 0.3f, bx + 0.4f, by, dx, dy + 0.4f, ex - 0.4f, ey };
    fill_poly(p, 4, col, 1);
  }
  gfx_flush();
  SDL_SetRenderTarget(gR, NULL);
}

static void draw_minimap(float ox, float oy) {
  screen_begin();
  panel(ox - 3, oy - 3, MM_W + 6, MM_H + 6, 4, 0x0b0f0a, 0x6b5a34, 2);
  gfx_flush();
  if (miniTex) { SDL_FRect r = { ox, oy, MM_W, MM_H }; SDL_RenderCopyF(gR, miniTex, NULL, &r); }
  gOX = ox; gOY = oy;
  for (int i = 0; i < nNodes; i++) {
    const FNode *n = &mNodes[i];
    float px, py; mini_pt(n->x + 0.5f, n->y + 0.5f, &px, &py);
    float s = n->type == N_TREE ? 1.6f : 2.4f;
    fill_rect(px - s / 2, py - s / 2, s, s, n->type == N_TREE ? 0x1f4a1c : n->type == N_GOLD ? 0xffd23a : 0xb5b5bd, 1);
  }
  for (int i = 0; i < nBlds; i++) {
    const FBld *b = &mBlds[i];
    float px, py; mini_pt(b->x + b->w / 2.0f, b->y + b->h / 2.0f, &px, &py);
    float s = b->w + 1.5f;
    fill_rect(px - s / 2, py - s / 2, s, s, TEAM_COLOR[b->team], 1);
    stroke_rect(px - s / 2, py - s / 2, s, s, 0.6f, 0x000000, 1);
  }
  for (int i = 0; i < nUnits; i++) {
    const FUnit *u = &mUnits[i];
    float px, py; mini_pt(u->x, u->y, &px, &py);
    fill_rect(px - 1, py - 1, 2.2f, 2.2f, TEAM_COLOR[u->team], 1);
  }
  for (int i = 0; i < nPings; i++) {
    float px, py; mini_pt(pings[i].x, pings[i].y, &px, &py);
    stroke_ellipse(px, py, 3 + pings[i].t * 10, 3 + pings[i].t * 10, 2, 0xff503c, 1 - pings[i].t / 1.5f);
  }
  float sx[4] = { 0, W, W, 0 }, sy[4] = { TOP_H, TOP_H, H - BOTTOM_H, H - BOTTOM_H }, q[8];
  for (int i = 0; i < 4; i++) {
    float wx, wy; s2w(sx[i], sy[i], &wx, &wy);
    mini_pt(wx, wy, &q[2 * i], &q[2 * i + 1]);
    q[2 * i] = clampf(q[2 * i], 0, MM_W); q[2 * i + 1] = clampf(q[2 * i + 1], 0, MM_H);
  }
  stroke_poly(q, 4, 1.2f, 0xffffff, 1);
  gOX = gOY = 0;
}

#endif
