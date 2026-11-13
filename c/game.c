/* ==========================================================================
   BANNERS OF PEAKKK — simulation core in C

   Map generation, A* pathfinding, units, buildings, economy, combat and the
   enemy AI. Freestanding C99: no libc, no malloc, only static arrays, so it
   compiles to a small WebAssembly module (see Makefile) and also builds
   natively for the tests in tests/sim_test.c.

   The renderer (js/ui.js) never touches this state directly. After each
   update it calls g_snapshot(), which copies everything it needs into flat
   float arrays that JavaScript reads (see js/game_wasm.js).
   ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "defs.h"

#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(n), used))
#else
#define EXPORT(n)
#endif

/* ------------------------------- Limits ---------------------------------- */

#define MAXU 640
#define MAXB 256
#define MAXN 1300
#define MAXP 256
#define MAXE 640
#define MAXID 65536
#define MAXPATH 128
#define RAWMAX 1024
#define HEAPMAX 90000
#define MAXORD 640
#define MAXEV 96

enum { T_GRASS = 0, T_WATER = 1, T_SAND = 2 };
enum { S_IDLE = 0, S_MOVE, S_ATTACKMOVE, S_ATTACK, S_GATHER, S_BUILD };
enum { SUB_NONE = 0, SUB_TORES, SUB_WORK, SUB_TODROP };
enum { K_UNIT = 1, K_BLD = 2, K_NODE = 3 };
enum { R_FOOD = 0, R_WOOD, R_GOLD, R_STONE };
enum { EV_ALERT = 1, EV_BUILT, EV_DESTROYED, EV_TECH, EV_MARCH };
enum { ERR_NONE = 0, ERR_NOTREADY, ERR_CANTTRAIN, ERR_QFULL, ERR_HOUSES, ERR_POPLIMIT, ERR_RES0 /* +res idx */ = 6, ERR_MAXED = 10, ERR_CANTBUILD = 11, ERR_SHORE = 12 };

/* ------------------------------- Helpers --------------------------------- */

static double d_sqrt(double x) { return __builtin_sqrt(x); }
static double d_floor(double x) { return __builtin_floor(x); }
static double d_abs(double x) { return x < 0 ? -x : x; }
static double d_hypot(double a, double b) { return d_sqrt(a * a + b * b); }
static double d_min(double a, double b) { return a < b ? a : b; }
static double d_max(double a, double b) { return a > b ? a : b; }
static int i_min(int a, int b) { return a < b ? a : b; }
static int i_max(int a, int b) { return a > b ? a : b; }
static int i_abs(int a) { return a < 0 ? -a : a; }
static double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int fl(double x) { return (int)d_floor(x); }

static double d_sin(double x) {
  const double PI = 3.14159265358979323846;
  x = x - 2 * PI * d_floor((x + PI) / (2 * PI));
  double x2 = x * x;
  return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110 * (1 - x2 / 156 * (1 - x2 / 210)))))));
}
static double d_cos(double x) { return d_sin(x + 1.57079632679489661923); }

/* ---------------------------------- RNG ---------------------------------- */

static uint32_t rng_state;
static double rnd(void) {                       /* mulberry32 -> [0,1) */
  rng_state += 0x6D2B79F5u;
  uint32_t t = rng_state;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return (double)(t ^ (t >> 14)) / 4294967296.0;
}

/* ------------------------------- Entities -------------------------------- */

typedef struct {
  int id, type, team, dead, state, sub;
  int target;                       /* entity id (unit / building / node), 0 = none */
  double x, y, hp, maxHp, tx, ty, cd, atkAnim, anim, scan, repath, carry;
  int face, moving, working, carryType, gatherKind, lastKind, farm, drop, autoAtk, partial, ptx, pty, wave;
  double homeX, homeY; int hasHome;
  int haspath, pi, plen; float px[MAXPATH], py[MAXPATH];
  int ign_id[6]; double ign_t[6]; int ign_n;
  double stk, sx, sy;
} Unit;

typedef struct { int kind, type; double time; int cost[4]; } QItem;   /* kind 0 = unit, 1 = tech */

typedef struct {
  int id, type, team, dead, done, x, y, w, h;
  double hp, maxHp, progress, qt, cd, scan;
  int qlen; QItem q[5];
  int hasRally; double rx, ry; int rtarget;
  int target;
  int users[FARM_SLOTS], nusers;
} Bld;

typedef struct { int id, type, x, y, dead; double amt, max; } Node;

typedef struct { int akind, atype, ateam, aid, target; double sx, sy, t, dur; int stone; int done; double lx, ly; } Proj;
typedef struct { int type; double x, y, z, vx, vy, vz, life, max; int color, amt, res; } Eff;
typedef struct { int type, team; double x, y; int a, b; } Event;

typedef struct {
  double res[4]; int popUsed, popCap; int up[2]; double gatherMult, lastAlert;
} Team;

typedef struct {                    /* uniform view of any entity, used for targets */
  int kind, id, team, type, dom;
  double x, y, w, h;
} TV;

typedef struct {
  int active, team; int diff;
  double t, next; int waveSize, waveId, waveStart, attacking; double portTry;
  int hasRally; double rx, ry;
} AI;

static Unit units[MAXU]; static Bld blds[MAXB]; static Node nodes[MAXN];
static Proj projs[MAXP]; static Eff effs[MAXE]; static Event evs[MAXEV];
static int nev, nproj, neff;
static int idmap[MAXID];             /* id -> (kind << 20) | slot */
static int nextId;
static Team teams[2];
static AI ais[2];
static double game_time, win_check, path_budget;
static int winner, difficulty;

static uint8_t terrain[MAP_W * MAP_H], lake[MAP_W * MAP_H], shade[MAP_W * MAP_H];
static int occ[MAP_W * MAP_H];       /* id of the building / node on a tile, 0 = free */
static int base_x[2] = { 11, 60 }, base_y[2] = { 36, 36 };
static int mapType;                  /* 0 lake, 1 plains, 2 river, 3 ponds, 4 forest; set by g_set_map before g_init */
static float noiseF[MAP_W * MAP_H];  /* smooth 0..1 noise kept for the forest map */
static double pond[8][3]; static int npond;

static Unit *U(int id) {
  if (id <= 0 || id >= MAXID) return 0;
  int m = idmap[id];
  if ((m >> 20) != K_UNIT) return 0;
  Unit *u = &units[m & 0xFFFFF];
  return u->dead ? 0 : u;
}
static Bld *B(int id) {
  if (id <= 0 || id >= MAXID) return 0;
  int m = idmap[id];
  if ((m >> 20) != K_BLD) return 0;
  Bld *b = &blds[m & 0xFFFFF];
  return b->dead ? 0 : b;
}
static Node *N(int id) {
  if (id <= 0 || id >= MAXID) return 0;
  int m = idmap[id];
  if ((m >> 20) != K_NODE) return 0;
  Node *n = &nodes[m & 0xFFFFF];
  return n->dead ? 0 : n;
}

static void tv_unit(const Unit *u, TV *t) { t->kind = K_UNIT; t->id = u->id; t->team = u->team; t->type = u->type; t->dom = UDEF[u->type].water; t->x = u->x; t->y = u->y; t->w = 0; t->h = 0; }
static void tv_bld(const Bld *b, TV *t) { t->kind = K_BLD; t->id = b->id; t->team = b->team; t->type = b->type; t->dom = 0; t->x = b->x; t->y = b->y; t->w = b->w; t->h = b->h; }
static void tv_node(const Node *n, TV *t) { t->kind = K_NODE; t->id = n->id; t->team = -1; t->type = n->type; t->dom = 0; t->x = n->x; t->y = n->y; t->w = 1; t->h = 1; }
static int resolve(int id, TV *t) {
  Unit *u = U(id); if (u) { tv_unit(u, t); return 1; }
  Bld *b = B(id); if (b) { tv_bld(b, t); return 1; }
  Node *n = N(id); if (n) { tv_node(n, t); return 1; }
  return 0;
}
static void centerTV(const TV *t, double *cx, double *cy) {
  if (t->kind == K_UNIT) { *cx = t->x; *cy = t->y; } else { *cx = t->x + t->w / 2; *cy = t->y + t->h / 2; }
}
static double distPointRect(double px, double py, double rx, double ry, double rw, double rh) {
  double dx = d_max(d_max(rx - px, 0), px - (rx + rw));
  double dy = d_max(d_max(ry - py, 0), py - (ry + rh));
  return d_hypot(dx, dy);
}
static double distToTV(const Unit *u, const TV *t) {
  if (t->kind == K_UNIT) return d_hypot(u->x - t->x, u->y - t->y);
  return distPointRect(u->x, u->y, t->x, t->y, t->w, t->h);
}

static int canAfford(const double *res, const int *cost) { for (int k = 0; k < 4; k++) if (res[k] < cost[k]) return 0; return 1; }
static int missingRes(const double *res, const int *cost) { for (int k = 0; k < 4; k++) if (res[k] < cost[k]) return k; return -1; }
static void payCost(double *res, const int *cost) { for (int k = 0; k < 4; k++) res[k] -= cost[k]; }
static void refundCost(double *res, const int *cost) { for (int k = 0; k < 4; k++) res[k] += cost[k]; }

static int newId(int kind, int slot) {
  if (nextId >= MAXID - 1) return 0;
  int id = nextId++;
  idmap[id] = (kind << 20) | slot;
  return id;
}

/* ------------------------------- Terrain --------------------------------- */

static int occFarm(int id) { Bld *b = B(id); return b && b->type == B_FARM; }
static int passLand(int x, int y) {
  if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;
  int i = y * MAP_W + x;
  if (terrain[i] == T_WATER) return 0;
  int o = occ[i];
  return !o || occFarm(o);
}
static int passWater(int x, int y) {
  if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;
  return terrain[y * MAP_W + x] == T_WATER;
}
static int passFor(int water, int x, int y) { return water ? passWater(x, y) : passLand(x, y); }

static int isFreeLand(int x, int y) {
  if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return 0;
  int i = y * MAP_W + x;
  return terrain[i] == T_GRASS && !occ[i];
}
static int nearBase(int x, int y, double r) {
  for (int i = 0; i < 2; i++) if (d_hypot(x + 0.5 - (base_x[i] + 0.5), y + 0.5 - (base_y[i] + 0.5)) < r) return 1;
  return 0;
}

static int addNode(int type, int x, int y) {
  int slot = -1;
  for (int i = 0; i < MAXN; i++) if (nodes[i].dead || nodes[i].id == 0) { slot = i; break; }
  if (slot < 0) return 0;
  int id = newId(K_NODE, slot);
  if (!id) return 0;
  Node *n = &nodes[slot];
  n->id = id; n->type = type; n->x = x; n->y = y; n->dead = 0; n->amt = NDEF[type].amt; n->max = NDEF[type].amt;
  occ[y * MAP_W + x] = id;
  return id;
}
static void cluster(int type, double cx, double cy, int count, double radius) {
  int placed = 0;
  for (int t = 0; t < count * 12 && placed < count; t++) {
    double a = rnd() * 6.283185307179586, r = d_sqrt(rnd()) * radius;
    int x = fl(cx + d_cos(a) * r), y = fl(cy + d_sin(a) * r);
    if (!isFreeLand(x, y) || nearBase(x, y, 5.5)) continue;
    addNode(type, x, y); placed++;
  }
}

static void generateMap(void) {
  enum { NS = 12 };
  static double lat[(NS + 2) * (NS + 2)];
  for (int i = 0; i < (NS + 2) * (NS + 2); i++) lat[i] = rnd();
  npond = 0;
  if (mapType == 3) {
    for (int t = 0; t < 200 && npond < 7; t++) {
      double px = 6 + rnd() * (MAP_W - 12), py = 6 + rnd() * (MAP_H - 12), pr = 3 + rnd() * 2.5;
      int ok = 1;
      for (int b = 0; b < 2; b++) if (d_hypot(px - base_x[b], py - base_y[b]) < pr + 13) ok = 0;
      for (int k = 0; k < npond; k++) if (d_hypot(px - pond[k][0], py - pond[k][1]) < pr + pond[k][2] + 6) ok = 0;
      if (!ok) continue;
      pond[npond][0] = px; pond[npond][1] = py; pond[npond][2] = pr; npond++;
    }
  }
  for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
    double u = x / (double)(MAP_W + 1), v = y / (double)(MAP_H + 1);
    double gx = u * NS, gy = v * NS; int ix = fl(gx), iy = fl(gy);
    double fx = gx - ix, fy = gy - iy; fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    double a = lat[iy * (NS + 2) + ix], b = lat[iy * (NS + 2) + ix + 1], c = lat[(iy + 1) * (NS + 2) + ix], d = lat[(iy + 1) * (NS + 2) + ix + 1];
    double nz = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
    double wob = (nz - 0.5) * 0.4;
    double dd = d_hypot((x - 36) / 12.0, (y - 36) / 20.0);
    int water = 0;
    if (mapType == 0) water = dd < 1 + wob;
    else if (mapType == 2) {              /* river running north-south with three fords */
      double cx = 36 + 7 * d_sin(y * 0.14 + nz * 2.0) + wob * 10;
      water = d_abs(x - cx) < 1.7 + nz * 1.2;
      if (d_abs(y - 36) < 3.2 || d_abs(y - 12) < 2.6 || d_abs(y - 60) < 2.6) water = 0;
    } else if (mapType == 3) {
      for (int k = 0; k < npond; k++) if (d_hypot((x - pond[k][0]) / pond[k][2], (y - pond[k][1]) / (pond[k][2] * 0.75)) < 1 + wob * 1.3) water = 1;
    }
    terrain[y * MAP_W + x] = water ? T_WATER : T_GRASS;
    noiseF[y * MAP_W + x] = (float)nz;
    shade[y * MAP_W + x] = (uint8_t)(int)(rnd() * 6);
  }
  for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
    if (terrain[y * MAP_W + x] == T_WATER) continue;
    int near = 0;
    for (int dy = -1; dy <= 1 && !near; dy++) for (int dx = -1; dx <= 1; dx++) {
      int nx = x + dx, ny = y + dy;
      if (nx >= 0 && ny >= 0 && nx < MAP_W && ny < MAP_H && terrain[ny * MAP_W + nx] == T_WATER) { near = 1; break; }
    }
    if (near) terrain[y * MAP_W + x] = T_SAND;
  }
  if (mapType != 0) {                    /* every body of water counts as a harbour */
    for (int i = 0; i < MAP_W * MAP_H; i++) lake[i] = terrain[i] == T_WATER;
  } else {
  /* flood-fill the central lake */
  static int stack[MAP_W * MAP_H * 2]; int sp = 0;
  if (terrain[36 * MAP_W + 36] == T_WATER) stack[sp++] = 36 * MAP_W + 36;
  while (sp > 0) {
    int i = stack[--sp];
    if (lake[i]) continue;
    lake[i] = 1;
    int x = i % MAP_W, y = i / MAP_W;
    static const int D4[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
    for (int k = 0; k < 4; k++) {
      int nx = x + D4[k][0], ny = y + D4[k][1];
      if (nx >= 0 && ny >= 0 && nx < MAP_W && ny < MAP_H && terrain[ny * MAP_W + nx] == T_WATER && !lake[ny * MAP_W + nx] && sp < MAP_W * MAP_H * 2 - 1) stack[sp++] = ny * MAP_W + nx;
    }
  }
  }
  /* base resources, mirrored for the second team */
  for (int i = 0; i < 2; i++) {
    int s = i == 0 ? 1 : -1, bx = base_x[i], by = base_y[i];
    cluster(N_TREE, bx + -1 * s, by - 9, 24, 3.6);
    cluster(N_TREE, bx + -2 * s, by + 10, 26, 3.6);
    cluster(N_TREE, bx + 6 * s, by - 4, 10, 2.4);
    cluster(N_GOLD, bx + 3 * s, by - 8, 5, 1.8);
    cluster(N_STONE, bx + 3 * s, by + 8, 4, 1.7);
  }
  if (mapType == 0) {
  cluster(N_GOLD, 36, 8, 6, 2.2); cluster(N_GOLD, 36, 64, 6, 2.2);
  cluster(N_STONE, 30, 12, 4, 1.8); cluster(N_STONE, 42, 60, 4, 1.8);
  cluster(N_GOLD, 24, 10, 4, 1.8); cluster(N_GOLD, 48, 62, 4, 1.8);
  } else {
    /* contested mid-map deposits, mirrored top/bottom so neither side is favoured */
    cluster(N_GOLD, 36, 22, 6, 2.2); cluster(N_GOLD, 36, 50, 6, 2.2);
    cluster(N_STONE, 28, 24, 4, 1.8); cluster(N_STONE, 44, 48, 4, 1.8);
    cluster(N_GOLD, 22, 12, 4, 1.8); cluster(N_GOLD, 50, 60, 4, 1.8);
    cluster(N_STONE, 36, 36, 3, 1.5);
  }
  if (mapType == 4) {
    /* forest density is normalised so every seed gets about the same number of trees (well under the node cap) */
    int q = 0;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++)
      if (noiseF[y * MAP_W + x] >= 0.55f && isFreeLand(x, y) && !nearBase(x, y, 11) && !(d_abs(y - 36) < 3 && x > 20 && x < 52)) q++;
    double keep = q > 0 ? 950.0 / q : 0;
    if (keep > 0.85) keep = 0.85;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) {
      if (noiseF[y * MAP_W + x] < 0.55f || !isFreeLand(x, y) || nearBase(x, y, 11)) continue;
      if (d_abs(y - 36) < 3 && x > 20 && x < 52) continue;      /* keep a road between the bases */
      if (rnd() < keep) addNode(N_TREE, x, y);
    }
  }
  if (mapType == 1) {                    /* plains: a few lone groves instead of the usual forests */
    for (int n = 0, tries = 0; n < 12 && tries < 300; tries++) {
      double cx = 4 + rnd() * (MAP_W - 8), cy = 4 + rnd() * (MAP_H - 8);
      if (!isFreeLand(fl(cx), fl(cy))) continue;
      if (d_hypot(cx - base_x[0], cy - base_y[0]) < 12 || d_hypot(cx - base_x[1], cy - base_y[1]) < 12) continue;
      int count = 8 + (int)(rnd() * 10);
      cluster(N_TREE, cx, cy, count, 2.5);
      n++;
    }
    return;
  }
  if (mapType == 4) return;
  for (int n = 0, tries = 0; n < 30 && tries < 400; tries++) {
    double cx = 2 + rnd() * (MAP_W - 4), cy = 2 + rnd() * (MAP_H - 4);
    if (!isFreeLand(fl(cx), fl(cy))) continue;
    if (d_hypot(cx - base_x[0], cy - base_y[0]) < 11 || d_hypot(cx - base_x[1], cy - base_y[1]) < 11) continue;
    /* separate statements: C does not define the order in which call arguments are evaluated,
       and the random sequence must be identical across compilers (native test vs wasm) */
    int count = 10 + (int)(rnd() * 30);
    double radius = 2.5 + rnd() * 2.5;
    cluster(N_TREE, cx, cy, count, radius);
    n++;
  }
  for (int n = 0, tries = 0; n < 70 && tries < 600; tries++) {
    int x = (int)(rnd() * MAP_W), y = (int)(rnd() * MAP_H);
    if (!isFreeLand(x, y) || nearBase(x, y, 6)) continue;
    addNode(N_TREE, x, y); n++;
  }
}

/* ----------------------------- Pathfinding ------------------------------- */

static float pg[MAP_W * MAP_H];
static int pp[MAP_W * MAP_H], ps[MAP_W * MAP_H], pc[MAP_W * MAP_H];
static int pstamp;
static float hk[HEAPMAX]; static int hv[HEAPMAX]; static int hn;
static float rawx[RAWMAX], rawy[RAWMAX]; static int rawn;

static void heapPush(float k, int v) {
  if (hn >= HEAPMAX) return;
  int i = hn++;
  while (i > 0) {
    int p = (i - 1) >> 1;
    if (hk[p] <= k) break;
    hk[i] = hk[p]; hv[i] = hv[p]; i = p;
  }
  hk[i] = k; hv[i] = v;
}
static int heapPop(void) {
  int top = hv[0];
  float lk = hk[hn - 1]; int lv = hv[hn - 1]; hn--;
  if (hn > 0) {
    int i = 0;
    for (;;) {
      int c = 2 * i + 1;
      if (c >= hn) break;
      if (c + 1 < hn && hk[c + 1] < hk[c]) c++;
      if (hk[c] >= lk) break;
      hk[i] = hk[c]; hv[i] = hv[c]; i = c;
    }
    hk[i] = lk; hv[i] = lv;
  }
  return top;
}

typedef struct { int mode; int gx, gy; double rx, ry, rw, rh, lim; } Goal;   /* mode 0: exact tile, 1: within reach of rect */
static int goalHit(const Goal *g, int x, int y) {
  if (g->mode == 0) return x == g->gx && y == g->gy;
  return distPointRect(x + 0.5, y + 0.5, g->rx, g->ry, g->rw, g->rh) <= g->lim;
}

/* A* from tile (sx,sy). Fills rawx/rawy/rawn with waypoints (centres of tiles), returns 1 if the goal was reached, 0 if partial. */
static int findPath(int sx, int sy, int water, const Goal *goal, double tx, double ty, int maxNodes) {
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  sx = clampi(sx, 0, MAP_W - 1); sy = clampi(sy, 0, MAP_H - 1);
  int st = ++pstamp;
  hn = 0;
  int start = sy * MAP_W + sx;
  pg[start] = 0; pp[start] = -1; ps[start] = st;
#define HEUR(x, y) (d_abs((x) - tx) + d_abs((y) - ty) - 0.5858 * d_min(d_abs((x) - tx), d_abs((y) - ty)))
  heapPush((float)HEUR(sx, sy), start);
  int best = start; double bestH = HEUR(sx, sy); int expanded = 0, found = -1;
  while (hn > 0) {
    int cur = heapPop();
    if (pc[cur] == st) continue;
    pc[cur] = st;
    int cx = cur % MAP_W, cy = cur / MAP_W;
    if (goalHit(goal, cx, cy)) { found = cur; break; }
    if (++expanded > maxNodes) break;
    for (int d = 0; d < 8; d++) {
      int nx = cx + DX[d], ny = cy + DY[d];
      if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
      if (!passFor(water, nx, ny)) continue;
      int diag = d >= 4;
      if (diag && (!passFor(water, cx + DX[d], cy) || !passFor(water, cx, cy + DY[d]))) continue;
      int ni = ny * MAP_W + nx;
      if (pc[ni] == st) continue;
      float ng = pg[cur] + (diag ? 1.4142f : 1.0f);
      if (ps[ni] == st && ng >= pg[ni]) continue;
      ps[ni] = st; pg[ni] = ng; pp[ni] = cur;
      double h = HEUR(nx, ny);
      heapPush(ng + (float)h, ni);
      if (h < bestH) { bestH = h; best = ni; }
    }
  }
#undef HEUR
  int partial = found < 0;
  int n = partial ? best : found;
  rawn = 0;
  static float revx[RAWMAX], revy[RAWMAX]; int rn = 0;
  while (n != start && n >= 0 && rn < RAWMAX) { revx[rn] = (float)(n % MAP_W + 0.5); revy[rn] = (float)(n / MAP_W + 0.5); rn++; n = pp[n]; }
  for (int i = rn - 1; i >= 0; i--) { rawx[rawn] = revx[i]; rawy[rawn] = revy[i]; rawn++; }
  return !partial;
}

static int los(double x0, double y0, double x1, double y1, int water) {
  double d = d_hypot(x1 - x0, y1 - y0);
  int steps = (int)__builtin_ceil(d / 0.3);
  double nx = d > 0 ? -(y1 - y0) / d : 0, ny = d > 0 ? (x1 - x0) / d : 0;
  for (int i = 1; i <= steps; i++) {
    double t = (double)i / steps, px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
    if (!passFor(water, fl(px), fl(py))) return 0;
    if (!passFor(water, fl(px + nx * 0.22), fl(py + ny * 0.22))) return 0;
    if (!passFor(water, fl(px - nx * 0.22), fl(py - ny * 0.22))) return 0;
  }
  return 1;
}

static void smoothInto(Unit *u, int water) {
  double cx = u->x, cy = u->y; int i = 0, out = 0;
  while (i < rawn && out < MAXPATH) {
    int j = rawn - 1;
    while (j > i && !los(cx, cy, rawx[j], rawy[j], water)) j--;
    u->px[out] = rawx[j]; u->py[out] = rawy[j]; out++;
    cx = rawx[j]; cy = rawy[j]; i = j + 1;
  }
  u->plen = out; u->pi = 0; u->haspath = 1;
}

static int computePathPoint(Unit *u) {
  if (path_budget <= 0) return 0;
  path_budget--;
  Goal g; g.mode = 0; g.gx = clampi(fl(u->tx), 0, MAP_W - 1); g.gy = clampi(fl(u->ty), 0, MAP_H - 1);
  int ok = findPath(fl(u->x), fl(u->y), UDEF[u->type].water, &g, g.gx, g.gy, 4500);
  smoothInto(u, UDEF[u->type].water);
  u->partial = !ok;
  return 1;
}
static int computePathTo(Unit *u, const TV *t, double reach) {
  if (path_budget <= 0) return 0;
  path_budget--;
  Goal g; g.mode = 1;
  if (t->kind == K_UNIT) { g.rx = fl(t->x); g.ry = fl(t->y); g.rw = 1; g.rh = 1; }
  else { g.rx = t->x; g.ry = t->y; g.rw = t->w; g.rh = t->h; }
  g.lim = reach - 0.05;
  double cx, cy; centerTV(t, &cx, &cy);
  u->ptx = fl(t->x); u->pty = fl(t->y);
  int ok = findPath(fl(u->x), fl(u->y), UDEF[u->type].water, &g, cx - 0.5, cy - 0.5, 4500);
  smoothInto(u, UDEF[u->type].water);
  u->partial = !ok;
  return 1;
}

static int followPath(Unit *u, double dt) {
  if (!u->haspath) { u->stk = 0; return 0; }
  u->stk += dt;
  if (u->stk >= 1) {
    double moved = d_hypot(u->x - u->sx, u->y - u->sy);
    u->stk = 0; u->sx = u->x; u->sy = u->y;
    if (moved < 0.25) { u->haspath = 0; return 0; }
  }
  double step = UDEF[u->type].speed * dt;
  while (step > 0 && u->pi < u->plen) {
    double wx = u->px[u->pi], wy = u->py[u->pi];
    double dx = wx - u->x, dy = wy - u->y, d = d_hypot(dx, dy);
    if (u->pi < u->plen - 1 && d < 0.4) { u->pi++; continue; }
    if (d <= step) {
      u->x = wx; u->y = wy; step -= d; u->pi++;
      if (d > 0.001) u->face = (dx - dy) >= 0 ? 1 : -1;
    } else {
      u->x += dx / d * step; u->y += dy / d * step; u->face = (dx - dy) >= 0 ? 1 : -1; step = 0;
    }
    u->moving = 1;
  }
  if (u->pi >= u->plen) { u->haspath = 0; return 0; }
  return 1;
}

static double reachOf(const Unit *u, const TV *t) { return UDEF[u->type].range + (t->kind == K_UNIT ? 0.35 : 0.15); }
static void faceTo(Unit *u, const TV *t) {
  double cx, cy; centerTV(t, &cx, &cy);
  double sx = (cx - cy) - (u->x - u->y);
  if (d_abs(sx) > 0.05) u->face = sx >= 0 ? 1 : -1;
}

/* Move u until within `reach` of t. Returns 1 arrived, 0 still moving, -1 unreachable. */
static int approach(Unit *u, const TV *t, double reach, double dt) {
  if (distToTV(u, t) <= reach) { u->haspath = 0; return 1; }
  u->repath -= dt;
  int moved = t->kind == K_UNIT && (u->ptx != fl(t->x) || u->pty != fl(t->y));
  if ((!u->haspath || moved) && u->repath <= 0) {
    if (computePathTo(u, t, reach)) u->repath = 0.5;
  }
  if (!followPath(u, dt)) {
    if (u->partial && !u->haspath && u->repath > 0.25 && distToTV(u, t) > reach) return -1;
    if (distToTV(u, t) <= 3) {
      double cx, cy; centerTV(t, &cx, &cy);
      double dx = cx - u->x, dy = cy - u->y, d = d_hypot(dx, dy); if (d == 0) d = 1;
      double st = d_min(d, UDEF[u->type].speed * dt);
      double nx = u->x + dx / d * st, ny = u->y + dy / d * st;
      if (passFor(UDEF[u->type].water, fl(nx), fl(ny))) { u->x = nx; u->y = ny; u->moving = 1; u->face = (dx - dy) >= 0 ? 1 : -1; }
    }
  }
  return 0;
}

/* ------------------------------ Entity creation -------------------------- */

static Unit *createUnit(int team, int type, double x, double y) {
  int slot = -1;
  for (int i = 0; i < MAXU; i++) if (units[i].dead || units[i].id == 0) { slot = i; break; }
  if (slot < 0) return 0;
  int id = newId(K_UNIT, slot);
  if (!id) return 0;
  Unit *u = &units[slot];
  __builtin_memset(u, 0, sizeof(Unit));
  u->id = id; u->type = type; u->team = team; u->x = x; u->y = y; u->hp = u->maxHp = UDEF[type].hp;
  u->face = team == 0 ? 1 : -1; u->anim = rnd() * 10; u->carryType = -1; u->gatherKind = -1; u->lastKind = -1;
  u->scan = rnd() * 0.5; u->ptx = -1; u->pty = -1; u->sx = -9999; u->sy = -9999;
  return u;
}

static void recalcPop(int team) {
  int cap = 0;
  for (int i = 0; i < MAXB; i++) { Bld *b = &blds[i]; if (b->id && !b->dead && b->team == team && b->done) cap += BDEF[b->type].pop; }
  teams[team].popCap = i_min(MAX_POP, cap);
}

static Bld *createBuilding(int team, int type, int x, int y, int done) {
  int slot = -1;
  for (int i = 0; i < MAXB; i++) if (blds[i].dead || blds[i].id == 0) { slot = i; break; }
  if (slot < 0) return 0;
  int id = newId(K_BLD, slot);
  if (!id) return 0;
  const BldDef *d = &BDEF[type];
  Bld *b = &blds[slot];
  __builtin_memset(b, 0, sizeof(Bld));
  b->id = id; b->type = type; b->team = team; b->x = x; b->y = y; b->w = d->w; b->h = d->h;
  b->hp = done ? d->hp : d_max(10, d->hp * 0.1); b->maxHp = d->hp; b->done = done; b->progress = done ? 1 : 0;
  for (int j = 0; j < b->h; j++) for (int i = 0; i < b->w; i++) occ[(y + j) * MAP_W + x + i] = id;
  if (done) recalcPop(team);
  return b;
}

/* ------------------------------ Placement -------------------------------- */

static int shoreOk(int x, int y, int w, int h) {
  for (int ty = y - 2; ty < y + h + 2; ty++) for (int tx = x - 2; tx < x + w + 2; tx++) {
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) continue;
    if (lake[ty * MAP_W + tx]) return 1;
  }
  return 0;
}
static int canPlace(int type, int x, int y, int margin) {
  const BldDef *d = &BDEF[type];
  for (int j = -margin; j < d->h + margin; j++) for (int i = -margin; i < d->w + margin; i++) {
    int tx = x + i, ty = y + j;
    int inside = i >= 0 && j >= 0 && i < d->w && j < d->h;
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) { if (inside) return 0; continue; }
    int idx = ty * MAP_W + tx;
    if (inside) {
      if (terrain[idx] == T_WATER || occ[idx]) return 0;
    } else {
      Bld *o = B(occ[idx]);
      if (o && o->type != B_FARM) return 0;
    }
  }
  if (d->shore && !shoreOk(x, y, d->w, d->h)) return 0;
  return 1;
}

static int findPlacement(int type, double cx, double cy, int rmin, int rmax, int margin, int *ox_, int *oy_) {
  const BldDef *d = &BDEF[type];
  int ox = d->w / 2, oy = d->h / 2;
  static int cand[600][2];
  for (int r = rmin; r <= rmax; r++) {
    int nc = 0;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
      if (i_max(i_abs(dx), i_abs(dy)) != r) continue;
      if (nc >= 600) break;
      cand[nc][0] = fl(cx + 0.5) + dx - ox; cand[nc][1] = fl(cy + 0.5) + dy - oy; nc++;
    }
    for (int i = nc - 1; i > 0; i--) {
      int j = (int)(rnd() * (i + 1));
      int tx = cand[i][0], ty = cand[i][1]; cand[i][0] = cand[j][0]; cand[i][1] = cand[j][1]; cand[j][0] = tx; cand[j][1] = ty;
    }
    for (int i = 0; i < nc; i++) if (canPlace(type, cand[i][0], cand[i][1], margin)) { *ox_ = cand[i][0]; *oy_ = cand[i][1]; return 1; }
  }
  return 0;
}

static int nearestFree(double x, double y, int water, double *ox, double *oy) {
  int sx = fl(x), sy = fl(y);
  for (int r = 0; r < 8; r++) {
    int found = 0; double bd = 1e9; int bx = 0, by = 0;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
      if (i_max(i_abs(dx), i_abs(dy)) != r) continue;
      if (!passFor(water, sx + dx, sy + dy)) continue;
      double d = dx * dx + dy * dy;
      if (d < bd) { bd = d; bx = sx + dx; by = sy + dy; found = 1; }
    }
    if (found) { *ox = bx + 0.5; *oy = by + 0.5; return 1; }
  }
  return 0;
}

static void ejectUnits(const Bld *b) {
  for (int i = 0; i < MAXU; i++) {
    Unit *u = &units[i];
    if (!u->id || u->dead || UDEF[u->type].water) continue;
    int tx = fl(u->x), ty = fl(u->y);
    if (tx >= b->x && tx < b->x + b->w && ty >= b->y && ty < b->y + b->h) {
      double nx, ny;
      if (nearestFree(u->x, u->y, 0, &nx, &ny)) { u->x = nx; u->y = ny; u->haspath = 0; }
    }
  }
}

static int findSpawn(const Bld *b, int water, int hasToward, double twx, double twy, double *ox, double *oy) {
  double tx = hasToward ? twx : MAP_W / 2.0, ty = hasToward ? twy : MAP_H / 2.0;
  int maxR = water ? 5 : 3;
  for (int r = 1; r <= maxR; r++) {
    int found = 0; double bd = 1e9; int bx = 0, by = 0;
    for (int y = b->y - r; y <= b->y + b->h - 1 + r; y++) for (int x = b->x - r; x <= b->x + b->w - 1 + r; x++) {
      if (!(x == b->x - r || x == b->x + b->w - 1 + r || y == b->y - r || y == b->y + b->h - 1 + r)) continue;
      if (!passFor(water, x, y)) continue;
      int busy = 0;
      for (int i = 0; i < MAXU; i++) {
        const Unit *u = &units[i];
        if (!u->id || u->dead) continue;
        if (d_abs(u->x - x - 0.5) < 0.45 && d_abs(u->y - y - 0.5) < 0.45) { busy = 1; break; }
      }
      if (busy) continue;
      double d = d_hypot(x + 0.5 - tx, y + 0.5 - ty);
      if (d < bd) { bd = d; bx = x; by = y; found = 1; }
    }
    if (found) { *ox = bx + 0.5; *oy = by + 0.5; return 1; }
  }
  return 0;
}

static Unit *spawnUnitAt(Bld *b, int type) {
  double x, y;
  if (!findSpawn(b, UDEF[type].water, 0, 0, 0, &x, &y)) return 0;
  Unit *u = createUnit(b->team, type, x, y);
  if (u) teams[b->team].popUsed += UDEF[type].pop;
  return u;
}

/* -------------------------------- Orders --------------------------------- */

static void farmRemove(Bld *f, int uid) {
  for (int i = 0; i < f->nusers; i++) if (f->users[i] == uid) { f->users[i] = f->users[--f->nusers]; return; }
}
static int farmHas(const Bld *f, int uid) { for (int i = 0; i < f->nusers; i++) if (f->users[i] == uid) return 1; return 0; }
static void farmAdd(Bld *f, int uid) { if (!farmHas(f, uid) && f->nusers < FARM_SLOTS) f->users[f->nusers++] = uid; }
static void release(Unit *u) {
  if (u->farm) { Bld *f = B(u->farm); if (f) farmRemove(f, u->id); u->farm = 0; }
}
static void setOrder(Unit *u, int state, int target, int hasXY, double tx, double ty) {
  release(u);
  u->state = state; u->target = target;
  if (hasXY) { u->tx = tx; u->ty = ty; }
  u->haspath = 0; u->pi = 0; u->sub = SUB_NONE; u->repath = 0; u->autoAtk = 0; u->partial = 0;
}

static int canAttackTV(const Unit *u, const TV *t) {
  if (t->team == u->team) return 0;
  const UnitDef *ud = &UDEF[u->type];
  if (t->kind == K_NODE) return 0;
  if (t->kind == K_UNIT && t->dom && !ud->water && ud->range < 2) return 0;
  if (t->kind == K_BLD && ud->water && !ud->soldier) return 0;
  return 1;
}

static int formationSpots(int n, double x, double y, int water, double *sxo, double *syo) {
  int sx = clampi(fl(x), 0, MAP_W - 1), sy = clampi(fl(y), 0, MAP_H - 1);
  static int q[400][2]; static uint8_t seen[MAP_W * MAP_H];
  __builtin_memset(seen, 0, sizeof(seen));
  int qn = 0, qi = 0, cnt = 0;
  q[qn][0] = sx; q[qn][1] = sy; qn++; seen[sy * MAP_W + sx] = 1;
  static const int D8[8][2] = { {1,0}, {-1,0}, {0,1}, {0,-1}, {1,1}, {-1,-1}, {1,-1}, {-1,1} };
  while (qi < qn && cnt < n) {
    int cx = q[qi][0], cy = q[qi][1]; qi++;
    if (passFor(water, cx, cy)) { sxo[cnt] = cx + 0.5; syo[cnt] = cy + 0.5; cnt++; }
    for (int k = 0; k < 8; k++) {
      int nx = cx + D8[k][0], ny = cy + D8[k][1];
      if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H || seen[ny * MAP_W + nx]) continue;
      if (i_abs(nx - sx) > 9 || i_abs(ny - sy) > 9 || qn >= 400) continue;
      seen[ny * MAP_W + nx] = 1; q[qn][0] = nx; q[qn][1] = ny; qn++;
    }
  }
  return cnt;
}

static void orderMove(const int *ids, int n, double x, double y, int attackMove) {
  static Unit *grp[MAXORD]; static double fx[MAXORD], fy[MAXORD]; static uint8_t used[MAXORD];
  for (int dom = 0; dom < 2; dom++) {
    int g = 0;
    for (int i = 0; i < n && g < MAXORD; i++) { Unit *u = U(ids[i]); if (u && UDEF[u->type].water == dom) grp[g++] = u; }
    if (!g) continue;
    int ns;
    if (g == 1) { fx[0] = x; fy[0] = y; ns = 1; } else ns = formationSpots(g, x, y, dom, fx, fy);
    for (int i = 0; i < ns; i++) used[i] = 0;
    /* closest-first assignment */
    for (int i = 1; i < g; i++) {                       /* insertion sort by distance to the target */
      Unit *k = grp[i]; double kd = d_hypot(k->x - x, k->y - y); int j = i - 1;
      while (j >= 0 && d_hypot(grp[j]->x - x, grp[j]->y - y) > kd) { grp[j + 1] = grp[j]; j--; }
      grp[j + 1] = k;
    }
    for (int i = 0; i < g; i++) {
      Unit *u = grp[i]; int bi = -1; double bd = 1e9;
      for (int s = 0; s < ns; s++) if (!used[s]) { double d = d_hypot(u->x - fx[s], u->y - fy[s]); if (d < bd) { bd = d; bi = s; } }
      double tx = x, ty = y;
      if (bi >= 0) { used[bi] = 1; tx = fx[bi]; ty = fy[bi]; }
      setOrder(u, attackMove ? S_ATTACKMOVE : S_MOVE, 0, 1, tx, ty);
      if (attackMove && !UDEF[u->type].soldier) u->state = S_MOVE;
    }
  }
}
static void orderStop(const int *ids, int n) { for (int i = 0; i < n; i++) { Unit *u = U(ids[i]); if (u) setOrder(u, S_IDLE, 0, 0, 0, 0); } }
static void orderAttack(const int *ids, int n, int tid) {
  TV t; if (!resolve(tid, &t)) return;
  for (int i = 0; i < n; i++) {
    Unit *u = U(ids[i]); if (!u) continue;
    if (canAttackTV(u, &t)) setOrder(u, S_ATTACK, tid, 0, 0, 0);
    else { double cx, cy; centerTV(&t, &cx, &cy); setOrder(u, S_MOVE, 0, 1, cx, cy); }
  }
}
static void orderBuild(const int *ids, int n, int bid) {
  for (int i = 0; i < n; i++) {
    Unit *u = U(ids[i]); if (!u || UDEF[u->type].soldier) continue;
    if (u->state == S_GATHER && u->gatherKind >= 0) u->lastKind = u->gatherKind;
    setOrder(u, S_BUILD, bid, 0, 0, 0);
  }
}
static double distPointBld(double x, double y, const Bld *b) { return distPointRect(x, y, b->x, b->y, b->w, b->h); }
static Bld *nearestFarm(double x, double y, int team, double maxd) {
  Bld *best = 0; double bd = maxd;
  for (int i = 0; i < MAXB; i++) {
    Bld *b = &blds[i];
    if (!b->id || b->dead || b->team != team || b->type != B_FARM || !b->done || b->nusers >= FARM_SLOTS) continue;
    double d = distPointBld(x, y, b);
    if (d < bd) { bd = d; best = b; }
  }
  return best;
}
static Node *findNearestNode(int kind, double x, double y, double maxd) {
  Node *best = 0; double bd = maxd;
  for (int i = 0; i < MAXN; i++) {
    Node *n = &nodes[i];
    if (!n->id || n->dead || n->amt <= 0 || NDEF[n->type].res != kind) continue;
    double d = d_hypot(n->x + 0.5 - x, n->y + 0.5 - y);
    if (d < bd) { bd = d; best = n; }
  }
  return best;
}
static void orderGather(const int *ids, int n, int tid) {
  Node *tn = N(tid); Bld *tb = B(tid);
  if (!tn && !tb) return;
  int kind = tn ? NDEF[tn->type].res : R_FOOD;
  for (int i = 0; i < n; i++) {
    Unit *u = U(ids[i]); if (!u || UDEF[u->type].soldier) continue;
    int tt = tid; Bld *f = tb;
    if (tb && tb->type == B_FARM) {
      if (!tb->done) { int one[1] = { u->id }; orderBuild(one, 1, tid); continue; }
      if (tb->nusers >= FARM_SLOTS && !farmHas(tb, u->id)) {
        f = nearestFarm(u->x, u->y, u->team, 14);
        if (!f) continue;
        tt = f->id;
      }
    }
    setOrder(u, S_GATHER, tt, 0, 0, 0);
    u->gatherKind = kind; u->sub = SUB_TORES;
    if (u->carryType != kind) { u->carry = 0; u->carryType = kind; }
    if (f && f->type == B_FARM) { farmAdd(f, u->id); u->farm = f->id; }
  }
}

static Bld *findDropoff(const Unit *u) {
  Bld *best = 0; double bd = 1e9;
  for (int i = 0; i < MAXB; i++) {
    Bld *b = &blds[i];
    if (!b->id || b->dead || b->team != u->team || !b->done || !BDEF[b->type].drop) continue;
    double d = distPointBld(u->x, u->y, b);
    if (d < bd) { bd = d; best = b; }
  }
  return best;
}
static int retarget(Unit *u) {
  int tid = 0;
  if (u->gatherKind == R_FOOD) { Bld *f = nearestFarm(u->x, u->y, u->team, 16); if (f) tid = f->id; }
  else { Node *n = findNearestNode(u->gatherKind, u->x, u->y, 28); if (n) tid = n->id; }
  if (!tid) return 0;
  u->target = tid; u->sub = SUB_TORES; u->haspath = 0; u->repath = 0;
  Bld *f = B(tid);
  if (f && f->type == B_FARM) { release(u); farmAdd(f, u->id); u->farm = f->id; }
  return 1;
}

/* ----------------------------- Production -------------------------------- */

static int techLevelPending(int team, int tech) {
  int n = 0;
  for (int i = 0; i < MAXB; i++) {
    Bld *b = &blds[i];
    if (!b->id || b->dead || b->team != team) continue;
    for (int q = 0; q < b->qlen; q++) if (b->q[q].kind == 1 && b->q[q].type == tech) n++;
  }
  return n;
}
static int queueUnit(Bld *b, int type) {
  const UnitDef *d = &UDEF[type]; Team *team = &teams[b->team];
  if (!b->done || b->dead) return ERR_NOTREADY;
  const BldDef *bd = &BDEF[b->type];
  if (bd->trains[0] != type && bd->trains[1] != type) return ERR_CANTTRAIN;
  if (b->qlen >= 5) return ERR_QFULL;
  if (team->popUsed + d->pop > team->popCap) return team->popCap >= MAX_POP ? ERR_POPLIMIT : ERR_HOUSES;
  int miss = missingRes(team->res, d->cost);
  if (miss >= 0) return ERR_RES0 + miss;
  payCost(team->res, d->cost);
  team->popUsed += d->pop;
  QItem *q = &b->q[b->qlen++];
  q->kind = 0; q->type = type; q->time = d->time;
  for (int k = 0; k < 4; k++) q->cost[k] = d->cost[k];
  return ERR_NONE;
}
static void techCost(int tech, int lvl, int *cost) {
  const TechDef *t = &TDEF[tech];
  cost[R_FOOD] = t->base[0] + t->step[0] * lvl; cost[R_WOOD] = 0; cost[R_GOLD] = t->base[1] + t->step[1] * lvl; cost[R_STONE] = 0;
}
static int queueTech(Bld *b, int tech) {
  const TechDef *T = &TDEF[tech]; Team *team = &teams[b->team];
  if (!b->done || b->dead) return ERR_NOTREADY;
  if (b->qlen >= 5) return ERR_QFULL;
  int lvl = team->up[tech] + techLevelPending(b->team, tech);
  if (lvl >= T->max) return ERR_MAXED;
  int cost[4]; techCost(tech, lvl, cost);
  int miss = missingRes(team->res, cost);
  if (miss >= 0) return ERR_RES0 + miss;
  payCost(team->res, cost);
  QItem *q = &b->q[b->qlen++];
  q->kind = 1; q->type = tech; q->time = T->tbase + T->tstep * lvl;
  for (int k = 0; k < 4; k++) q->cost[k] = cost[k];
  return ERR_NONE;
}
static void cancelQueue(Bld *b, int i) {
  if (i < 0 || i >= b->qlen) return;
  Team *team = &teams[b->team];
  QItem *q = &b->q[i];
  refundCost(team->res, q->cost);
  if (q->kind == 0) team->popUsed -= UDEF[q->type].pop;
  for (int k = i; k < b->qlen - 1; k++) b->q[k] = b->q[k + 1];
  b->qlen--;
  if (i == 0) b->qt = 0;
}

static int placeBuilding(int team, int type, int x, int y, const int *builders, int nb) {
  const BldDef *d = &BDEF[type]; Team *t = &teams[team];
  if (!canPlace(type, x, y, 0)) return d->shore ? -ERR_SHORE : -ERR_CANTBUILD;
  int miss = missingRes(t->res, d->cost);
  if (miss >= 0) return -(ERR_RES0 + miss);
  payCost(t->res, d->cost);
  Bld *b = createBuilding(team, type, x, y, 0);
  if (!b) { refundCost(t->res, d->cost); return -ERR_CANTBUILD; }
  ejectUnits(b);
  if (nb > 0) orderBuild(builders, nb, b->id);
  return b->id;
}

/* -------------------------------- Effects -------------------------------- */

static void addEffect(const Eff *e) { if (neff < MAXE) effs[neff++] = *e; }
static void burst(double x, double y, int n, int color, double spread) {
  for (int i = 0; i < n; i++) {
    double a = rnd() * 6.283185307179586, s = (0.3 + rnd()) * spread;
    Eff e; __builtin_memset(&e, 0, sizeof(e));
    e.type = 0; e.x = x; e.y = y; e.vx = d_cos(a) * s; e.vy = d_sin(a) * s; e.z = 4 + rnd() * 8; e.vz = 20 + rnd() * 30;
    e.life = 0.7 + rnd() * 0.5; e.max = 1.2; e.color = color;
    addEffect(&e);
  }
}
static void floatText(double x, double y, int amt, int res) {
  Eff e; __builtin_memset(&e, 0, sizeof(e));
  e.type = 1; e.x = x; e.y = y; e.z = 24; e.amt = amt; e.res = res; e.life = 1.1; e.max = 1.1;
  addEffect(&e);
}
static void pushEvent(int type, int team, double x, double y, int a, int b) {
  if (nev >= MAXEV) return;
  Event *e = &evs[nev++]; e->type = type; e->team = team; e->x = x; e->y = y; e->a = a; e->b = b;
}

/* ------------------------------- Combat ---------------------------------- */

static void kill(const TV *t);

static void damage(int akind, int atype, int aid, int ateam, const TV *t) {
  double atk; int atkType; const double *bonus = 0; int isSoldierUnit = 0;
  if (akind == K_UNIT) { const UnitDef *ad = &UDEF[atype]; atk = ad->atk; atkType = ad->atkType; bonus = ad->bonus; isSoldierUnit = ad->soldier && atkType != 2; }
  else { const BldDef *bd = &BDEF[atype]; atk = bd->atk; atkType = bd->atkType; }
  Team *team = &teams[ateam];
  double dmg = atk;
  if (isSoldierUnit) dmg += 2 * team->up[TECH_WEAPONS];
  int armor = 0;
  if (t->kind == K_UNIT) {
    const UnitDef *td = &UDEF[t->type];
    if (bonus && bonus[td->cls] > 0) dmg += bonus[td->cls];
    armor = td->armor[atkType];
    if (td->soldier && atkType != 2) armor += teams[t->team].up[TECH_ARMOR];
  } else {
    static const int BUILDING_ARMOR[3] = { 4, 12, 0 };      /* melee / pierce / siege; matches js/defs.js */
    armor = BUILDING_ARMOR[atkType];
  }
  dmg = d_max(1, dmg - armor);
  Unit *tu = t->kind == K_UNIT ? U(t->id) : 0; Bld *tb = t->kind == K_BLD ? B(t->id) : 0;
  if (!tu && !tb) return;
  double *hp = tu ? &tu->hp : &tb->hp;
  *hp -= dmg;
  Team *tt = &teams[t->team];
  if (t->team == 0 && game_time - tt->lastAlert > 8) {
    tt->lastAlert = game_time;
    double cx, cy; centerTV(t, &cx, &cy);
    pushEvent(EV_ALERT, 0, cx, cy, 0, 0);
  }
  /* idle defenders strike back */
  if (tu && tu->state == S_IDLE && UDEF[tu->type].soldier && akind == K_UNIT) {
    Unit *au = U(aid);
    if (au) {
      TV atv; tv_unit(au, &atv);
      if (canAttackTV(tu, &atv)) { double hx = tu->x, hy = tu->y; setOrder(tu, S_ATTACK, au->id, 0, 0, 0); tu->autoAtk = 1; tu->hasHome = 1; tu->homeX = hx; tu->homeY = hy; }
    }
  }
  if (*hp <= 0) kill(t);
}

static void kill(const TV *t) {
  if (t->kind == K_UNIT) {
    Unit *u = U(t->id); if (!u) return;
    u->dead = 1;
    teams[u->team].popUsed = i_max(0, teams[u->team].popUsed - UDEF[u->type].pop);
    release(u);
    burst(u->x, u->y, u->type == U_WARSHIP ? 14 : 5, u->type == U_WARSHIP ? 0 : 1, 0.6);
  } else if (t->kind == K_BLD) {
    Bld *b = B(t->id); if (!b) return;
    b->dead = 1;
    for (int j = 0; j < b->h; j++) for (int i = 0; i < b->w; i++) { int k = (b->y + j) * MAP_W + b->x + i; if (occ[k] == b->id) occ[k] = 0; }
    Team *team = &teams[b->team];
    for (int q = 0; q < b->qlen; q++) if (b->q[q].kind == 0) team->popUsed = i_max(0, team->popUsed - UDEF[b->q[q].type].pop);
    b->qlen = 0;
    recalcPop(b->team);
    double cx = b->x + b->w / 2.0, cy = b->y + b->h / 2.0;
    burst(cx, cy, 10 + b->w * 6, 2, 1.4);
    pushEvent(EV_DESTROYED, b->team, cx, cy, b->type, 0);
  }
}

static void fireAt(int akind, int atype, int aid, int ateam, int tid, double fx, double fy, int stone) {
  TV t; if (!resolve(tid, &t) || nproj >= MAXP) return;
  double cx, cy; centerTV(&t, &cx, &cy);
  double d = d_hypot(cx - fx, cy - fy), speed = stone ? 9 : 14;
  Proj *p = &projs[nproj++];
  p->akind = akind; p->atype = atype; p->aid = aid; p->ateam = ateam; p->target = tid; p->sx = fx; p->sy = fy; p->t = 0;
  p->dur = d_max(0.15, d / speed); p->stone = stone; p->done = 0; p->lx = cx; p->ly = cy;
}

static void doAttack(Unit *u, const TV *t) {
  const UnitDef *d = &UDEF[u->type];
  u->cd = d->cd; u->atkAnim = 0.28;
  if (d->range > 2) fireAt(K_UNIT, u->type, u->id, u->team, t->id, u->x, u->y, d->atkType == 2);
  else damage(K_UNIT, u->type, u->id, u->team, t);
}

/* ------------------------------ Unit behaviour --------------------------- */

static int ignored(const Unit *u, int id) {
  for (int i = 0; i < u->ign_n; i++) if (u->ign_id[i] == id && u->ign_t[i] > game_time) return 1;
  return 0;
}
static void setIgnore(Unit *u, int id, double until) {
  for (int i = 0; i < u->ign_n; i++) if (u->ign_id[i] == id) { u->ign_t[i] = until; return; }
  if (u->ign_n < 6) { u->ign_id[u->ign_n] = id; u->ign_t[u->ign_n] = until; u->ign_n++; return; }
  int o = 0; for (int i = 1; i < 6; i++) if (u->ign_t[i] < u->ign_t[o]) o = i;
  u->ign_id[o] = id; u->ign_t[o] = until;
}

static int findTarget(const Unit *u, double radius, double bRadius) {
  int best = 0; double bs = 1e9;
  for (int i = 0; i < MAXU; i++) {
    const Unit *e = &units[i];
    if (!e->id || e->dead || e->team == u->team) continue;
    if (d_abs(e->x - u->x) > radius || d_abs(e->y - u->y) > radius) continue;
    double d = d_hypot(e->x - u->x, e->y - u->y);
    if (d > radius) continue;
    TV t; tv_unit(e, &t);
    if (!canAttackTV(u, &t) || ignored(u, e->id)) continue;
    double s = d - (UDEF[e->type].soldier ? 2 : 0) + (!UDEF[e->type].soldier ? 1.5 : 0);
    if (s < bs) { bs = s; best = e->id; }
  }
  if (best) return best;
  for (int i = 0; i < MAXB; i++) {
    const Bld *e = &blds[i];
    if (!e->id || e->dead || e->team == u->team) continue;
    double d = distPointBld(u->x, u->y, e);
    if (d > bRadius) continue;
    TV t; tv_bld(e, &t);
    if (!canAttackTV(u, &t) || ignored(u, e->id)) continue;
    double s = d + 3 - (BDEF[e->type].atk > 0 ? 4 : 0);
    if (s < bs) { bs = s; best = e->id; }
  }
  return best;
}

static void updateIdle(Unit *u, double dt) {
  if (!UDEF[u->type].soldier) return;
  u->scan -= dt;
  if (u->scan > 0) return;
  u->scan = 0.4 + rnd() * 0.2;
  int t = findTarget(u, UDEF[u->type].sight + (UDEF[u->type].range > 2 ? 1 : 0), 5);
  if (t) {
    double hx = u->x, hy = u->y;
    setOrder(u, S_ATTACK, t, 0, 0, 0);
    u->autoAtk = 1; u->hasHome = 1; u->homeX = hx; u->homeY = hy;
  }
}

/* Chase and hit. Returns 1 if the target should be dropped. */
static int engage(Unit *u, const TV *t, double dt) {
  int r = approach(u, t, reachOf(u, t), dt);
  if (r < 0) { setIgnore(u, t->id, game_time + 10); return 1; }
  if (r == 1) {
    faceTo(u, t);
    if (u->cd <= 0) doAttack(u, t);
  }
  return 0;
}

static void endAttack(Unit *u) {
  int autoA = u->autoAtk, hasHome = u->hasHome; double hx = u->homeX, hy = u->homeY;
  u->target = 0;
  if (autoA && hasHome) {
    int near = d_hypot(u->x - hx, u->y - hy) < 14;
    int t = near ? findTarget(u, UDEF[u->type].sight + 1, 5) : 0;
    if (t) { u->target = t; u->haspath = 0; u->repath = 0; return; }
    if (d_hypot(u->x - hx, u->y - hy) > 3) { setOrder(u, S_MOVE, 0, 1, hx, hy); return; }
  }
  setOrder(u, S_IDLE, 0, 0, 0, 0);
}

static void updateAttack(Unit *u, double dt) {
  TV t;
  if (!u->target || !resolve(u->target, &t) || !canAttackTV(u, &t)) { endAttack(u); return; }
  if (u->autoAtk && u->hasHome && d_hypot(u->x - u->homeX, u->y - u->homeY) > 14) { u->target = 0; endAttack(u); return; }
  if (engage(u, &t, dt)) endAttack(u);
}

static void updateAttackMove(Unit *u, double dt) {
  TV t;
  if (u->target && (!resolve(u->target, &t) || !canAttackTV(u, &t))) { u->target = 0; u->haspath = 0; }
  if (!u->target) {
    u->scan -= dt;
    if (u->scan <= 0) {
      u->scan = 0.35;
      int f = findTarget(u, UDEF[u->type].sight + 1.5, UDEF[u->type].sight + 1.5);
      if (f) { u->target = f; u->haspath = 0; u->repath = 0; }
    }
  }
  if (u->target) {
    if (!resolve(u->target, &t)) { u->target = 0; return; }
    if (engage(u, &t, dt)) { u->target = 0; u->haspath = 0; }
    return;
  }
  if (!u->haspath && !computePathPoint(u)) return;
  if (!followPath(u, dt)) setOrder(u, S_IDLE, 0, 0, 0, 0);
}

static void depleteNode(Node *n) {
  n->dead = 1;
  int k = n->y * MAP_W + n->x;
  if (occ[k] == n->id) occ[k] = 0;
}

static void updateGather(Unit *u, double dt) {
  Team *team = &teams[u->team];
  int kind = u->gatherKind;
  TV t; int have = u->target && resolve(u->target, &t);
  Node *tn = have && t.kind == K_NODE ? N(t.id) : 0;
  int gone = !have || (tn && tn->amt <= 0);
  if (u->sub == SUB_WORK) {
    if (gone) {
      if (u->carry >= 1) { u->sub = SUB_TODROP; u->haspath = 0; u->repath = 0; }
      else if (!retarget(u)) setOrder(u, S_IDLE, 0, 0, 0, 0);
      return;
    }
    if (distToTV(u, &t) > 1.35) { u->sub = SUB_TORES; u->haspath = 0; u->repath = 0; return; }
    u->working = 1; faceTo(u, &t);
    double amt;
    if (tn) {
      amt = d_min(NDEF[tn->type].rate * team->gatherMult * dt, tn->amt);
      tn->amt -= amt;
      if (tn->amt <= 0.001) depleteNode(tn);
    } else amt = FARM_RATE * team->gatherMult * dt;
    u->carry += amt; u->carryType = kind;
    if (u->carry >= UDEF[u->type].carry) { u->sub = SUB_TODROP; u->haspath = 0; u->repath = 0; u->drop = 0; }
    return;
  }
  if (u->sub == SUB_TORES) {
    if (gone) {
      if (!retarget(u)) {
        if (u->carry >= 1) { u->sub = SUB_TODROP; u->haspath = 0; u->repath = 0; } else setOrder(u, S_IDLE, 0, 0, 0, 0);
      }
      return;
    }
    int r = approach(u, &t, 0.95, dt);
    if (r == 1) { u->sub = SUB_WORK; u->haspath = 0; if (u->carryType != kind) { u->carry = 0; u->carryType = kind; } }
    else if (r < 0) { int old = u->target; if (!retarget(u) || u->target == old) setOrder(u, S_IDLE, 0, 0, 0, 0); }
    return;
  }
  if (u->sub == SUB_TODROP) {
    Bld *d = B(u->drop);
    if (!d || !d->done) { d = findDropoff(u); u->drop = d ? d->id : 0; }
    if (!d) { setOrder(u, S_IDLE, 0, 0, 0, 0); return; }
    TV dt_; tv_bld(d, &dt_);
    int r = approach(u, &dt_, 1.0, dt);
    if (r == 1) {
      int n = (int)u->carry;
      if (n > 0) {
        team->res[u->carryType] += n;
        if (u->team == 0) floatText(u->x, u->y, n, u->carryType);
      }
      u->carry -= n;
      u->sub = SUB_TORES; u->haspath = 0; u->repath = 0; u->drop = 0;
    } else if (r < 0) { u->drop = 0; u->haspath = 0; u->repath = 0.6; }
  }
}

static void completeBuilding(Bld *b) {
  b->done = 1; b->progress = 1; b->hp = b->maxHp;
  recalcPop(b->team);
  pushEvent(EV_BUILT, b->team, b->x, b->y, b->type, 0);
}

static void afterBuild(Unit *u, Bld *b) {
  if (b && b->type == B_FARM && b->nusers < FARM_SLOTS) { int one[1] = { u->id }; orderGather(one, 1, b->id); return; }
  if (u->lastKind >= 0) {
    int kind = u->lastKind;
    int tid = 0;
    if (kind == R_FOOD) { Bld *f = nearestFarm(u->x, u->y, u->team, 16); if (f) tid = f->id; }
    else { Node *n = findNearestNode(kind, u->x, u->y, 20); if (n) tid = n->id; }
    if (tid) { int one[1] = { u->id }; orderGather(one, 1, tid); return; }
  }
  setOrder(u, S_IDLE, 0, 0, 0, 0);
}

static void updateBuild(Unit *u, double dt) {
  Bld *b = B(u->target);
  if (!b) { afterBuild(u, 0); return; }
  if (b->done && b->hp >= b->maxHp) { afterBuild(u, b); return; }
  TV t; tv_bld(b, &t);
  int r = approach(u, &t, 0.95, dt);
  if (r < 0) { setOrder(u, S_IDLE, 0, 0, 0, 0); return; }
  if (r != 1) return;
  u->working = 1; faceTo(u, &t);
  if (!b->done) {
    b->progress += dt / BDEF[b->type].time;
    b->hp = d_min(b->maxHp, b->hp + b->maxHp * 0.9 * dt / BDEF[b->type].time);
    if (b->progress >= 1) completeBuilding(b);
  } else {
    b->hp = d_min(b->maxHp, b->hp + b->maxHp * dt / (BDEF[b->type].time * 1.5));
  }
}

static void updateUnit(Unit *u, double dt) {
  if (u->dead) return;
  u->cd = d_max(0, u->cd - dt);
  u->atkAnim = d_max(0, u->atkAnim - dt);
  u->moving = 0; u->working = 0; u->anim += dt;
  switch (u->state) {
    case S_IDLE: updateIdle(u, dt); break;
    case S_MOVE:
      if (!u->haspath && !computePathPoint(u)) break;
      if (!followPath(u, dt)) setOrder(u, S_IDLE, 0, 0, 0, 0);
      break;
    case S_ATTACKMOVE: updateAttackMove(u, dt); break;
    case S_ATTACK: updateAttack(u, dt); break;
    case S_GATHER: updateGather(u, dt); break;
    case S_BUILD: updateBuild(u, dt); break;
  }
}

/* ----------------------------- Building behaviour ------------------------ */

static int finishQueueItem(Bld *b, QItem *q) {
  Team *team = &teams[b->team];
  if (q->kind == 1) {
    team->up[q->type]++;
    pushEvent(EV_TECH, b->team, 0, 0, q->type, 0);
    return 1;
  }
  double x, y;
  if (!findSpawn(b, UDEF[q->type].water, b->hasRally, b->rx, b->ry, &x, &y)) return 0;
  Unit *u = createUnit(b->team, q->type, x, y);
  if (!u) return 0;
  if (b->hasRally) {
    int one[1] = { u->id };
    TV rt;
    if (b->rtarget && resolve(b->rtarget, &rt) && !UDEF[u->type].soldier && (rt.kind == K_NODE || (rt.kind == K_BLD && rt.type == B_FARM))) orderGather(one, 1, b->rtarget);
    else orderMove(one, 1, b->rx, b->ry, 0);
  }
  return 1;
}

static void updateBuilding(Bld *b, double dt) {
  if (b->dead || !b->done) return;
  const BldDef *d = &BDEF[b->type];
  if (d->atk > 0) {
    b->cd = d_max(0, b->cd - dt);
    b->scan -= dt;
    if (b->scan <= 0) {
      b->scan = 0.4;
      int best = 0; double bd = d->range + 1;
      for (int i = 0; i < MAXU; i++) {
        const Unit *u = &units[i];
        if (!u->id || u->dead || u->team == b->team) continue;
        double dd = distPointBld(u->x, u->y, b);
        if (dd < bd && dd <= d->range) { bd = dd; best = u->id; }
      }
      b->target = best;
    }
    Unit *tu = U(b->target);
    if (tu && b->cd <= 0) {
      if (distPointBld(tu->x, tu->y, b) <= d->range + 0.5) {
        b->cd = d->cd;
        fireAt(K_BLD, b->type, b->id, b->team, tu->id, b->x + b->w / 2.0, b->y + b->h / 2.0, 0);
      } else b->target = 0;
    }
  }
  if (b->qlen) {
    QItem *q = &b->q[0];
    b->qt += dt;
    if (b->qt >= q->time) {
      if (finishQueueItem(b, q)) {
        for (int k = 0; k < b->qlen - 1; k++) b->q[k] = b->q[k + 1];
        b->qlen--; b->qt = 0;
      } else b->qt = q->time;
    }
  }
}

/* ---------------------------------- AI ----------------------------------- */

static Unit *pickBuilder(Unit **bl, int n, double x, double y) {
  Unit *best = 0; double bd = 1e9;
  for (int i = 0; i < n; i++) {
    Unit *u = bl[i];
    if (u->state == S_BUILD) continue;
    double d = d_hypot(u->x - x, u->y - y) + (u->state == S_IDLE ? -8 : 0) + (u->carry > 6 ? 6 : 0);
    if (d < bd) { bd = d; best = u; }
  }
  return best;
}

static int aiTryBuild(AI *ai, int type, double cx, double cy, int rmin, int rmax, int margin, Unit **bl, int nb) {
  Team *team = &teams[ai->team];
  if (!canAfford(team->res, BDEF[type].cost)) return 0;
  int px, py;
  if (!findPlacement(type, cx, cy, rmin, rmax, margin, &px, &py)) return 0;
  Unit *bu = pickBuilder(bl, nb, px, py);
  if (!bu) return 0;
  if (bu->state == S_GATHER && bu->gatherKind >= 0) bu->lastKind = bu->gatherKind;
  int one[1] = { bu->id };
  return placeBuilding(ai->team, type, px, py, one, 1) > 0;
}

static double aiPickTarget(const AI *ai, double fx, double fy, double *ox, double *oy) {
  int found = 0; double bd = 1e9;
  for (int i = 0; i < MAXB; i++) {
    const Bld *b = &blds[i];
    if (!b->id || b->dead || b->team == ai->team) continue;
    double cx = b->x + b->w / 2.0, cy = b->y + b->h / 2.0;
    double d = d_hypot(fx - cx, fy - cy) - (b->type == B_TOWNCENTER ? 6 : 0);
    if (d < bd) { bd = d; *ox = cx; *oy = cy; found = 1; }
  }
  if (found) return 1;
  for (int i = 0; i < MAXU; i++) {
    const Unit *u = &units[i];
    if (!u->id || u->dead || u->team == ai->team) continue;
    double d = d_hypot(fx - u->x, fy - u->y);
    if (d < bd) { bd = d; *ox = u->x; *oy = u->y; found = 1; }
  }
  return found;
}

static void aiThink(AI *ai) {
  const DiffDef *df = &DIFFD[ai->diff];
  int id = ai->team; Team *team = &teams[id]; double *R = team->res;
  static Unit *builders[MAXU], *army[MAXU], *ships[MAXU];
  static Bld *myB[MAXB];
  int nB = 0, nbld = 0, nA = 0, nS = 0;
  int nbType[NUM_BLD] = {0}, ndType[NUM_BLD] = {0};
  Bld *tc = 0; int buildingNow = 0, housesBuilding = 0, prod = 0, farmsTotal = 0;
  int queuedB = 0;
  for (int i = 0; i < MAXB; i++) {
    Bld *b = &blds[i];
    if (!b->id || b->dead || b->team != id) continue;
    myB[nbld++] = b; nbType[b->type]++;
    if (b->done) { ndType[b->type]++; if (b->type == B_TOWNCENTER && !tc) tc = b; }
    if (!b->done && b->type != B_HOUSE && b->type != B_FARM && b->type != B_STOREHOUSE) buildingNow++;
    if (!b->done && b->type == B_HOUSE) housesBuilding++;
    if (BDEF[b->type].trains[0] >= 0 && b->type != B_TOWNCENTER) prod++;
    if (b->type == B_FARM) farmsTotal++;
    for (int q = 0; q < b->qlen; q++) if (b->q[q].kind == 0 && b->q[q].type == U_BUILDER) queuedB++;
  }
  if (!nbld) return;
  int nSword = 0, nSpear = 0;
  for (int i = 0; i < MAXU; i++) {
    Unit *u = &units[i];
    if (!u->id || u->dead || u->team != id) continue;
    if (u->type == U_BUILDER) builders[nB++] = u;
    else if (UDEF[u->type].soldier && !UDEF[u->type].water) { army[nA++] = u; if (u->type == U_SWORDSMAN) nSword++; if (u->type == U_SPEARMAN) nSpear++; }
    if (u->type == U_WARSHIP) ships[nS++] = u;
  }
  Bld *baseB = tc ? tc : myB[0];
  double bcx = baseB->x + baseB->w / 2.0, bcy = baseB->y + baseB->h / 2.0;
  double bt = game_time / df->pace;
  int dirX = base_x[1 - id] > bcx ? 1 : -1;

  if (!ai->hasRally) {
    double rx, ry;
    if (nearestFree(bcx + dirX * 8, bcy, 0, &rx, &ry)) { ai->rx = rx; ai->ry = ry; } else { ai->rx = bcx; ai->ry = bcy; }
    ai->hasRally = 1;
  }

  /* economy: builders */
  int maxB = i_min(df->maxB, 6 + (int)(bt / 25));
  if (tc && nB + queuedB < maxB && tc->qlen < 2) queueUnit(tc, U_BUILDER);

  /* houses */
  int room = team->popCap - team->popUsed;
  if (room <= 3 + prod * 2 && housesBuilding == 0 && team->popCap < MAX_POP) aiTryBuild(ai, B_HOUSE, bcx, bcy, 5, 14, 1, builders, nB);

  /* builder jobs */
  int cnt[4] = {0, 0, 0, 0};
  for (int i = 0; i < nB; i++) if (builders[i]->state == S_GATHER && builders[i]->gatherKind >= 0) cnt[builders[i]->gatherKind]++;
  double w[4];
  if (nB < 10) { w[0] = 0.38; w[1] = 0.52; w[2] = 0.1; w[3] = 0; } else { w[0] = 0.3; w[1] = 0.38; w[2] = 0.17; w[3] = 0.15; }
  if (nB >= 6 && R[R_STONE] < 120 && nbType[B_BARRACKS] < 3) w[3] += 0.1;
  if (R[R_GOLD] < 150 && nB >= 8) w[2] += 0.25;
  if (R[R_WOOD] < 100) w[1] += 0.15;
  if (R[R_FOOD] < 100) w[0] += 0.15;
  static const double pile[4] = { 900, 900, 900, 350 };
  for (int k = 0; k < 4; k++) if (R[k] > pile[k]) w[k] *= 0.2;
  int total = i_max(1, nB);
  for (int k = 0; k < 4; k++) {
    if (R[k] > pile[k] + 150 && cnt[k] > 2) {
      for (int i = 0; i < nB; i++) if (builders[i]->state == S_GATHER && builders[i]->gatherKind == k) { setOrder(builders[i], S_IDLE, 0, 0, 0, 0); cnt[k]--; break; }
    }
  }
  for (int i = 0; i < nB; i++) {
    Unit *u = builders[i];
    if (u->state != S_IDLE) continue;
    int kind = 0; double bestDef = -1e9;
    for (int k = 0; k < 4; k++) { double def = w[k] * total - cnt[k]; if (def > bestDef) { bestDef = def; kind = k; } }
    if (kind == R_FOOD) {
      Bld *farm = nearestFarm(u->x, u->y, id, 30);
      if (farm) { int one[1] = { u->id }; orderGather(one, 1, farm->id); cnt[R_FOOD]++; continue; }
      Unit *one[1] = { u };
      if (farmsTotal < 9 && farmsTotal * FARM_SLOTS < cnt[R_FOOD] + 3 && R[R_WOOD] >= 60 && aiTryBuild(ai, B_FARM, bcx, bcy, 3, 9, 0, one, 1)) {
        farmsTotal++; u->lastKind = R_FOOD; cnt[R_FOOD]++; continue;
      }
      kind = R_WOOD;
    }
    Node *node = findNearestNode(kind, u->x, u->y, 80);
    if (!node) node = findNearestNode(kind, bcx, bcy, 120);
    if (!node) continue;
    int dropNear = 0;
    for (int j = 0; j < nbld; j++) if (BDEF[myB[j]->type].drop && distPointBld(node->x + 0.5, node->y + 0.5, myB[j]) < 8) { dropNear = 1; break; }
    if (!dropNear && R[R_WOOD] >= 80) {
      u->lastKind = kind;
      Unit *one[1] = { u };
      if (aiTryBuild(ai, B_STOREHOUSE, node->x, node->y, 2, 6, 0, one, 1)) { cnt[kind]++; continue; }
    }
    { int one[1] = { u->id }; orderGather(one, 1, node->id); cnt[kind]++; }
  }

  /* rebuild a lost town center */
  if (!nbType[B_TOWNCENTER] && R[R_WOOD] >= 300 && R[R_STONE] >= 200) aiTryBuild(ai, B_TOWNCENTER, bcx, bcy, 5, 14, 1, builders, nB);

  /* build order */
  if (buildingNow < 2) {
    struct { int type; int cond; } want[8] = {
      { B_BARRACKS,   nB >= 6 && nbType[B_BARRACKS] < 1 },
      { B_ARCHERY,    ndType[B_BARRACKS] > 0 && nbType[B_ARCHERY] < 1 },
      { B_STABLE,     bt > 140 && nbType[B_STABLE] < 1 },
      { B_BLACKSMITH, bt > 220 && nbType[B_BLACKSMITH] < 1 },
      { B_BARRACKS,   bt > 280 && nB >= 14 && nbType[B_BARRACKS] < 2 },
      { B_ARCHERY,    bt > 380 && nbType[B_ARCHERY] < 2 },
      { B_TOWER,      bt > 330 && nbType[B_TOWER] < i_min(5, 2 + (int)((bt - 330) / 240)) },
      { B_STABLE,     bt > 450 && nbType[B_STABLE] < 2 },
    };
    for (int i = 0; i < 8; i++) {
      if (!want[i].cond) continue;
      int ok;
      if (want[i].type == B_TOWER) ok = aiTryBuild(ai, B_TOWER, bcx + dirX * 7, bcy, 0, 6, 1, builders, nB);
      else ok = aiTryBuild(ai, want[i].type, bcx, bcy, 6, 15, 1, builders, nB);
      if (ok) break;
    }
  }
  if (bt > 450 && !nbType[B_PORT] && ai->t > ai->portTry && nB >= 8) {
    ai->portTry = ai->t + 20;
    if (canAfford(R, BDEF[B_PORT].cost)) aiTryBuild(ai, B_PORT, bcx, bcy, 4, 34, 1, builders, nB);
  }

  /* training */
  for (int i = 0; i < nbld; i++) {
    Bld *b = myB[i];
    if (!b->done || b->type == B_TOWNCENTER || BDEF[b->type].trains[0] < 0 || b->qlen >= 2) continue;
    int type = -1;
    if (b->type == B_BARRACKS) type = nSword <= nSpear ? U_SWORDSMAN : U_SPEARMAN;
    else if (b->type == B_ARCHERY) type = U_ARCHER;
    else if (b->type == B_STABLE) type = U_CAVALRY;
    else if (b->type == B_PORT) { if (nS + b->qlen < (id == 1 ? 3 : 2)) type = U_WARSHIP; }
    if (type >= 0) queueUnit(b, type);
    b->hasRally = 1; b->rx = ai->rx; b->ry = ai->ry; b->rtarget = 0;
  }
  for (int i = 0; i < nbld; i++) {
    Bld *b = myB[i];
    if (b->type != B_BLACKSMITH || !b->done || b->qlen) continue;
    int order[2] = { TECH_WEAPONS, TECH_ARMOR };
    if (team->up[TECH_WEAPONS] > team->up[TECH_ARMOR]) { order[0] = TECH_ARMOR; order[1] = TECH_WEAPONS; }
    for (int k = 0; k < 2; k++) {
      int t = order[k];
      int lvl = team->up[t] + techLevelPending(id, t);
      if (lvl >= TDEF[t].max) continue;
      int c[4]; techCost(t, lvl, c);
      if (R[R_FOOD] >= c[R_FOOD] + 150 && R[R_GOLD] >= c[R_GOLD] + 100) { queueTech(b, t); break; }
    }
  }

  /* defense */
  int threat = 0; double td = 1e9; double thx = 0, thy = 0, thWater = 0;
  for (int i = 0; i < MAXU; i++) {
    const Unit *e = &units[i];
    if (!e->id || e->dead || e->team == id) continue;
    for (int j = 0; j < nbld; j++) {
      double d = distPointBld(e->x, e->y, myB[j]);
      if (d < 12 && d < td) { td = d; threat = e->id; thx = e->x; thy = e->y; thWater = UDEF[e->type].water; }
    }
  }
  if (threat && !ai->attacking) {
    for (int i = 0; i < nA; i++) { Unit *u = army[i]; if (u->state == S_IDLE || (u->state == S_MOVE && !u->target)) { int one[1] = { u->id }; orderMove(one, 1, thx, thy, 1); } }
    for (int i = 0; i < nS; i++) { Unit *u = ships[i]; if (u->state == S_IDLE && thWater) { int one[1] = { u->id }; orderMove(one, 1, thx, thy, 1); } }
  } else if (!ai->attacking) {
    for (int i = 0; i < nA; i++) { Unit *u = army[i]; if (u->state == S_IDLE && d_hypot(u->x - ai->rx, u->y - ai->ry) > 8) { int one[1] = { u->id }; orderMove(one, 1, ai->rx, ai->ry, 0); } }
  }

  /* offense */
  double tx = 0, ty = 0;
  int haveTarget = (int)aiPickTarget(ai, bcx, bcy, &tx, &ty);
  static int ids[MAXORD];
  if (!ai->attacking && nA >= ai->waveSize && haveTarget) {
    ai->attacking = 1; ai->waveId++; ai->waveStart = nA;
    int n = 0;
    for (int i = 0; i < nA && n < MAXORD; i++) { army[i]->wave = ai->waveId; ids[n++] = army[i]->id; }
    orderMove(ids, n, tx, ty, 1);
    pushEvent(EV_MARCH, 0, 0, 0, 0, 0);
  } else if (ai->attacking) {
    int n = 0;
    for (int i = 0; i < nA && n < MAXORD; i++) if (army[i]->wave == ai->waveId) ids[n++] = army[i]->id;
    if ((double)n < d_max(2, ai->waveStart * 0.25) || !haveTarget) {
      ai->attacking = 0; ai->waveSize += df->grow;
      orderMove(ids, n, ai->rx, ai->ry, 0);
    } else {
      for (int i = 0; i < n; i++) { Unit *u = U(ids[i]); if (u && u->state == S_IDLE) { int one[1] = { ids[i] }; orderMove(one, 1, tx, ty, 1); } }
    }
  }
  if (nS >= 2 && haveTarget) {
    for (int i = 0; i < nS; i++) if (ships[i]->state == S_IDLE) { int one[1] = { ships[i]->id }; orderMove(one, 1, tx, ty, 1); }
  }
}

static void aiInit(AI *ai, int team, int diff) {
  __builtin_memset(ai, 0, sizeof(AI));
  ai->active = 1; ai->team = team; ai->diff = diff;
  ai->next = 0.5; ai->waveSize = DIFFD[diff].wave;
  teams[team].gatherMult = DIFFD[diff].gather;
  for (int k = 0; k < 4; k++) teams[team].res[k] += DIFFD[diff].bonus;
}
static void aiUpdate(AI *ai, double dt) {
  ai->t += dt;
  if (ai->t < ai->next) return;
  ai->next = ai->t + DIFFD[ai->diff].think;
  aiThink(ai);
}

/* ------------------------- Projectiles / separation ---------------------- */

static void updateProjectiles(double dt) {
  int w = 0;
  for (int i = 0; i < nproj; i++) {
    Proj *p = &projs[i];
    p->t += dt;
    TV t;
    if (resolve(p->target, &t)) centerTV(&t, &p->lx, &p->ly);
    if (p->t >= p->dur) {
      if (resolve(p->target, &t)) damage(p->akind, p->atype, p->aid, p->ateam, &t);
      continue;
    }
    projs[w++] = *p;
  }
  nproj = w;
}
static void updateEffects(double dt) {
  int w = 0;
  for (int i = 0; i < neff; i++) {
    Eff *e = &effs[i];
    e->life -= dt;
    if (e->type == 0) { e->x += e->vx * dt; e->y += e->vy * dt; e->z += e->vz * dt; e->vz -= 60 * dt; if (e->z < 0) { e->z = 0; e->vz = 0; } }
    else e->z += 14 * dt;
    if (e->life > 0) effs[w++] = *e;
  }
  neff = w;
}

static void shiftUnit(Unit *u, double dx, double dy) {
  double nx = u->x + dx, ny = u->y + dy;
  if (passFor(UDEF[u->type].water, fl(nx), fl(ny))) { u->x = nx; u->y = ny; }
}
static void separate(double dt) {
  double k = d_min(1, dt * 10);
  for (int i = 0; i < MAXU; i++) {
    Unit *a = &units[i];
    if (!a->id || a->dead) continue;
    for (int j = i + 1; j < MAXU; j++) {
      Unit *b = &units[j];
      if (!b->id || b->dead || UDEF[a->type].water != UDEF[b->type].water) continue;
      double dx = b->x - a->x, dy = b->y - a->y;
      if (dx > 0.55 || dx < -0.55 || dy > 0.55 || dy < -0.55) continue;
      double d = d_hypot(dx, dy);
      if (d >= 0.55) continue;
      double nx, ny;
      if (d < 0.001) { nx = rnd() - 0.5; ny = rnd() - 0.5; double l = d_hypot(nx, ny); if (l == 0) l = 1; nx /= l; ny /= l; d = 0; }
      else { nx = dx / d; ny = dy / d; }
      double wa = a->moving ? 0.2 : 1, wb = b->moving ? 0.2 : 1;
      double push = (0.55 - d) * k, sa = push * wa / (wa + wb), sb = push * wb / (wa + wb);
      shiftUnit(a, -nx * sa, -ny * sa);
      shiftUnit(b, nx * sb, ny * sb);
    }
  }
}

/* --------------------------------- Update -------------------------------- */

static void freeDead(void) {
  for (int i = 0; i < MAXU; i++) if (units[i].id && units[i].dead) { idmap[units[i].id] = 0; units[i].id = 0; units[i].dead = 0; }
  for (int i = 0; i < MAXB; i++) if (blds[i].id && blds[i].dead) { idmap[blds[i].id] = 0; blds[i].id = 0; blds[i].dead = 0; }
  for (int i = 0; i < MAXN; i++) if (nodes[i].id && nodes[i].dead) { idmap[nodes[i].id] = 0; nodes[i].id = 0; nodes[i].dead = 0; }
}

EXPORT("g_update") void g_update(double dt) {
  if (winner >= 0) { updateEffects(dt); return; }
  game_time += dt;
  path_budget = 14;
  for (int i = 0; i < 2; i++) if (ais[i].active) aiUpdate(&ais[i], dt);
  for (int i = 0; i < MAXB; i++) if (blds[i].id && !blds[i].dead) updateBuilding(&blds[i], dt);
  for (int i = 0; i < MAXU; i++) if (units[i].id && !units[i].dead) updateUnit(&units[i], dt);
  separate(dt);
  updateProjectiles(dt);
  updateEffects(dt);
  freeDead();
  win_check -= dt;
  if (win_check <= 0) {
    win_check = 1;
    int alive[2] = {0, 0};
    for (int i = 0; i < MAXB; i++) if (blds[i].id && !blds[i].dead) alive[blds[i].team] = 1;
    if (!alive[0]) winner = 1; else if (!alive[1]) winner = 0;
  }
}

/* Selects the terrain for the next g_init: 0 central lake (default), 1 open plains, 2 river with fords, 3 ponds, 4 deep forest. */
EXPORT("g_set_map") void g_set_map(int m) { mapType = clampi(m, 0, 4); }
EXPORT("g_init") void g_init(uint32_t seed, int diff, int aiMask) {
  __builtin_memset(units, 0, sizeof(units)); __builtin_memset(blds, 0, sizeof(blds)); __builtin_memset(nodes, 0, sizeof(nodes));
  __builtin_memset(idmap, 0, sizeof(idmap)); __builtin_memset(occ, 0, sizeof(occ)); __builtin_memset(lake, 0, sizeof(lake));
  __builtin_memset(pc, 0, sizeof(pc)); __builtin_memset(ps, 0, sizeof(ps)); pstamp = 0;
  __builtin_memset(ais, 0, sizeof(ais));
  nproj = neff = nev = 0; nextId = 1; game_time = 0; win_check = 0; winner = -1; path_budget = 0;
  difficulty = clampi(diff, 0, 2);
  rng_state = seed;
  for (int i = 0; i < 2; i++) {
    __builtin_memset(&teams[i], 0, sizeof(Team));
    teams[i].res[R_FOOD] = 300; teams[i].res[R_WOOD] = 300; teams[i].res[R_GOLD] = 150; teams[i].res[R_STONE] = 100;
    teams[i].gatherMult = 1; teams[i].lastAlert = -99;
  }
  generateMap();
  for (int i = 0; i < 2; i++) {
    Bld *tc = createBuilding(i, B_TOWNCENTER, base_x[i] - 1, base_y[i] - 1, 1);
    for (int k = 0; k < 5; k++) spawnUnitAt(tc, U_BUILDER);
    spawnUnitAt(tc, U_SPEARMAN);
  }
  for (int i = 0; i < 2; i++) if (aiMask & (1 << i)) aiInit(&ais[i], i, difficulty);
}

/* ====================== Exported API for JavaScript ======================== */

static int cmdIds[MAXORD];
EXPORT("g_idbuf") int *g_idbuf(void) { return cmdIds; }
EXPORT("g_idbuf_len") int g_idbuf_len(void) { return MAXORD; }

EXPORT("g_order_move") void g_order_move(int n, double x, double y, int attackMove) { orderMove(cmdIds, n, x, y, attackMove); }
EXPORT("g_order_stop") void g_order_stop(int n) { orderStop(cmdIds, n); }
EXPORT("g_order_attack") void g_order_attack(int n, int target) { orderAttack(cmdIds, n, target); }
EXPORT("g_order_build") void g_order_build(int n, int bid) { orderBuild(cmdIds, n, bid); }
EXPORT("g_order_gather") void g_order_gather(int n, int target) { orderGather(cmdIds, n, target); }
/* returns the new building id, or a negative error code */
EXPORT("g_place") int g_place(int team, int type, int x, int y, int n) { return placeBuilding(team, type, x, y, cmdIds, n); }
EXPORT("g_can_place") int g_can_place(int type, int x, int y, int margin) { return canPlace(type, x, y, margin); }
EXPORT("g_queue_unit") int g_queue_unit(int bid, int type) { Bld *b = B(bid); return b ? queueUnit(b, type) : ERR_NOTREADY; }
EXPORT("g_queue_tech") int g_queue_tech(int bid, int tech) { Bld *b = B(bid); return b ? queueTech(b, tech) : ERR_NOTREADY; }
EXPORT("g_cancel_queue") void g_cancel_queue(int bid, int i) { Bld *b = B(bid); if (b) cancelQueue(b, i); }
EXPORT("g_tech_pending") int g_tech_pending(int team, int tech) { return techLevelPending(team, tech); }
EXPORT("g_set_rally") void g_set_rally(int bid, double x, double y, int target) { Bld *b = B(bid); if (b) { b->hasRally = 1; b->rx = x; b->ry = y; b->rtarget = target; } }
EXPORT("g_winner") int g_winner(void) { return winner; }

/* ---- snapshot: flat float arrays the renderer reads ---- */
#define U_STRIDE 20
#define B_STRIDE 34
#define N_STRIDE 6
#define P_STRIDE 8
#define E_STRIDE 9
#define EV_STRIDE 6
#define T_STRIDE 8
static float snapU[MAXU * U_STRIDE], snapB[MAXB * B_STRIDE], snapN[MAXN * N_STRIDE];
static float snapP[MAXP * P_STRIDE], snapE[MAXE * E_STRIDE], snapEv[MAXEV * EV_STRIDE], snapT[2 * T_STRIDE + 2];
static int snapCount[6];

EXPORT("g_snapshot") void g_snapshot(void) {
  int nu = 0, nb = 0, nn = 0;
  for (int i = 0; i < MAXU; i++) {
    const Unit *u = &units[i];
    if (!u->id || u->dead) continue;
    float *o = &snapU[nu++ * U_STRIDE];
    TV t; int td = 0;
    if (u->state == S_BUILD) { Bld *b = B(u->target); td = b && b->done; }
    (void)t;
    o[0] = u->id; o[1] = u->type; o[2] = u->team; o[3] = (float)u->x; o[4] = (float)u->y; o[5] = (float)u->hp; o[6] = (float)u->maxHp;
    o[7] = u->state; o[8] = u->face; o[9] = (float)u->anim; o[10] = u->moving; o[11] = u->working; o[12] = (float)u->carry;
    o[13] = u->carryType; o[14] = u->gatherKind; o[15] = u->sub; o[16] = (float)u->atkAnim; o[17] = (float)u->cd; o[18] = td; o[19] = u->target;
  }
  for (int i = 0; i < MAXB; i++) {
    const Bld *b = &blds[i];
    if (!b->id || b->dead) continue;
    float *o = &snapB[nb++ * B_STRIDE];
    o[0] = b->id; o[1] = b->type; o[2] = b->team; o[3] = b->x; o[4] = b->y; o[5] = b->w; o[6] = b->h; o[7] = (float)b->hp; o[8] = (float)b->maxHp;
    o[9] = b->done; o[10] = (float)b->progress; o[11] = (float)b->qt; o[12] = b->qlen; o[13] = b->hasRally; o[14] = (float)b->rx; o[15] = (float)b->ry;
    o[16] = b->rtarget; o[17] = b->nusers;
    for (int q = 0; q < 5; q++) {
      if (q < b->qlen) { o[18 + q * 3] = b->q[q].kind; o[19 + q * 3] = b->q[q].type; o[20 + q * 3] = (float)b->q[q].time; }
      else { o[18 + q * 3] = -1; o[19 + q * 3] = 0; o[20 + q * 3] = 0; }
    }
    o[33] = 0;
  }
  for (int i = 0; i < MAXN; i++) {
    const Node *n = &nodes[i];
    if (!n->id || n->dead) continue;
    float *o = &snapN[nn++ * N_STRIDE];
    o[0] = n->id; o[1] = n->type; o[2] = n->x; o[3] = n->y; o[4] = (float)n->amt; o[5] = (float)n->max;
  }
  for (int i = 0; i < nproj; i++) {
    const Proj *p = &projs[i]; float *o = &snapP[i * P_STRIDE];
    o[0] = (float)p->sx; o[1] = (float)p->sy; o[2] = (float)p->t; o[3] = (float)p->dur; o[4] = p->stone; o[5] = (float)p->lx; o[6] = (float)p->ly; o[7] = p->ateam;
  }
  for (int i = 0; i < neff; i++) {
    const Eff *e = &effs[i]; float *o = &snapE[i * E_STRIDE];
    o[0] = e->type; o[1] = (float)e->x; o[2] = (float)e->y; o[3] = (float)e->z; o[4] = (float)e->life; o[5] = (float)e->max; o[6] = e->color; o[7] = e->amt; o[8] = e->res;
  }
  for (int i = 0; i < nev; i++) {
    const Event *e = &evs[i]; float *o = &snapEv[i * EV_STRIDE];
    o[0] = e->type; o[1] = e->team; o[2] = (float)e->x; o[3] = (float)e->y; o[4] = e->a; o[5] = e->b;
  }
  for (int t = 0; t < 2; t++) {
    float *o = &snapT[t * T_STRIDE];
    o[0] = (float)teams[t].res[0]; o[1] = (float)teams[t].res[1]; o[2] = (float)teams[t].res[2]; o[3] = (float)teams[t].res[3];
    o[4] = teams[t].popUsed; o[5] = teams[t].popCap; o[6] = teams[t].up[0]; o[7] = teams[t].up[1];
  }
  snapT[2 * T_STRIDE] = (float)game_time; snapT[2 * T_STRIDE + 1] = (float)winner;
  snapCount[0] = nu; snapCount[1] = nb; snapCount[2] = nn; snapCount[3] = nproj; snapCount[4] = neff; snapCount[5] = nev;
  nev = 0;                                     /* events are delivered once */
}
EXPORT("g_snap_counts") int *g_snap_counts(void) { return snapCount; }
EXPORT("g_snap_units") float *g_snap_units(void) { return snapU; }
EXPORT("g_snap_blds") float *g_snap_blds(void) { return snapB; }
EXPORT("g_snap_nodes") float *g_snap_nodes(void) { return snapN; }
EXPORT("g_snap_projs") float *g_snap_projs(void) { return snapP; }
EXPORT("g_snap_effs") float *g_snap_effs(void) { return snapE; }
EXPORT("g_snap_events") float *g_snap_events(void) { return snapEv; }
EXPORT("g_snap_teams") float *g_snap_teams(void) { return snapT; }
EXPORT("g_terrain") uint8_t *g_terrain(void) { return terrain; }
EXPORT("g_lake") uint8_t *g_lake(void) { return lake; }
EXPORT("g_shade") uint8_t *g_shade(void) { return shade; }
EXPORT("g_layout") int g_layout(int which) {
  switch (which) { case 0: return U_STRIDE; case 1: return B_STRIDE; case 2: return N_STRIDE; case 3: return P_STRIDE; case 4: return E_STRIDE; case 5: return EV_STRIDE; case 6: return T_STRIDE; case 7: return MAXORD; }
  return -1;
}
