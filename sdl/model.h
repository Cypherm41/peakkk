/* The front end's view of the game: typed copies of the flat snapshot arrays that the C core publishes.
 * model_sync() is the native counterpart of Game.sync() in js/game_wasm.js. */
#ifndef MODEL_H
#define MODEL_H

#include <string.h>
#include "sim.h"
#include "defs.h"        /* c/defs.h: generated unit / building / tech tables shared with the core */
#include "gamedata.h"    /* sdl/gamedata.h: generated names, descriptions, draw heights, team colours */

typedef struct {
  int id, type, team;
  float x, y, hp, maxHp;
  int state, face;
  float anim;
  int moving, working;
  float carry;
  int carryType, gatherKind, sub;
  float atkAnim, cd;
  int targetDone, targetId;
} FUnit;

typedef struct { int kind, idx; float time; } FQItem;          /* kind 0 = unit, 1 = tech */

typedef struct {
  int id, type, team, x, y, w, h;
  float hp, maxHp;
  int done;
  float progress, qt;
  int qlen;
  FQItem q[5];
  int hasRally;
  float rx, ry;
  int nusers;
} FBld;

typedef struct { int id, type, x, y; float amt, max; } FNode;
typedef struct { float sx, sy, t, dur; int stone; float tx, ty; } FProj;
typedef struct { int type; float x, y, z, life, max; int color, amt, res; } FEff;
typedef struct { int type, team; float x, y; int a, b; } FEvent;

enum { K_NONE = 0, K_UNIT = 1, K_BLD = 2, K_NODE = 3 };

static FUnit mUnits[SIM_MAXU];
static FBld mBlds[SIM_MAXB];
static FNode mNodes[SIM_MAXN];
static FProj mProjs[SIM_MAXP];
static FEff mEffs[SIM_MAXE];
static int nUnits, nBlds, nNodes, nProjs, nEffs;

static FEvent evQueue[SIM_MAXEV * 4];
static int nEvQueue;

static struct { float res[4]; int popUsed, popCap, upWeapons, upArmor; } mTeam[2];
static float mTime;
static int mWinner = -1;

static const uint8_t *mTerrain, *mShade;
static unsigned char idKind[SIM_MAXID];          /* id -> entity kind */
static unsigned short idIdx[SIM_MAXID];          /* id -> index into the matching array */
static int mOcc[MAP_W * MAP_H];                  /* tile -> id of the building / resource node on it (0 = free) */

static FUnit *get_unit(int id) { return id > 0 && id < SIM_MAXID && idKind[id] == K_UNIT ? &mUnits[idIdx[id]] : NULL; }
static FBld *get_bld(int id) { return id > 0 && id < SIM_MAXID && idKind[id] == K_BLD ? &mBlds[idIdx[id]] : NULL; }
static FNode *get_node(int id) { return id > 0 && id < SIM_MAXID && idKind[id] == K_NODE ? &mNodes[idIdx[id]] : NULL; }

static int model_init(void) {
  const int expect[7] = { U_STRIDE, B_STRIDE, N_STRIDE, P_STRIDE, E_STRIDE, EV_STRIDE, T_STRIDE };
  for (int i = 0; i < 7; i++) if (g_layout(i) != expect[i]) return 0;     /* snapshot layout must match the core */
  if (g_layout(7) > SIM_MAXU) return 0;
  return 1;
}

static void model_bind_terrain(void) { mTerrain = g_terrain(); mShade = g_shade(); }

static void model_sync(void) {
  g_snapshot();
  const int *cnt = g_snap_counts();
  const float *U = g_snap_units(), *B = g_snap_blds(), *N = g_snap_nodes(), *P = g_snap_projs(), *E = g_snap_effs(), *EV = g_snap_events(), *T = g_snap_teams();
  int nu = cnt[0] < SIM_MAXU ? cnt[0] : SIM_MAXU, nb = cnt[1] < SIM_MAXB ? cnt[1] : SIM_MAXB, nn = cnt[2] < SIM_MAXN ? cnt[2] : SIM_MAXN;
  int np = cnt[3] < SIM_MAXP ? cnt[3] : SIM_MAXP, ne = cnt[4] < SIM_MAXE ? cnt[4] : SIM_MAXE, nev = cnt[5] < SIM_MAXEV ? cnt[5] : SIM_MAXEV;

  memset(idKind, 0, sizeof idKind);

  for (int t = 0; t < 2; t++) {
    const float *o = T + t * T_STRIDE;
    for (int r = 0; r < 4; r++) mTeam[t].res[r] = o[r];
    mTeam[t].popUsed = (int)o[4]; mTeam[t].popCap = (int)o[5]; mTeam[t].upWeapons = (int)o[6]; mTeam[t].upArmor = (int)o[7];
  }
  mTime = T[2 * T_STRIDE];
  mWinner = (int)T[2 * T_STRIDE + 1];

  nUnits = nu;
  for (int i = 0; i < nu; i++) {
    const float *o = U + i * U_STRIDE;
    FUnit *u = &mUnits[i];
    u->id = (int)o[0]; u->type = (int)o[1]; u->team = (int)o[2];
    u->x = o[3]; u->y = o[4]; u->hp = o[5]; u->maxHp = o[6];
    u->state = (int)o[7]; u->face = o[8] < 0 ? -1 : 1; u->anim = o[9]; u->moving = o[10] != 0; u->working = o[11] != 0;
    u->carry = o[12]; u->carryType = (int)o[13]; u->gatherKind = (int)o[14]; u->sub = (int)o[15];
    u->atkAnim = o[16]; u->cd = o[17]; u->targetDone = o[18] != 0; u->targetId = (int)o[19];
    if (u->id > 0 && u->id < SIM_MAXID) { idKind[u->id] = K_UNIT; idIdx[u->id] = (unsigned short)i; }
  }

  nBlds = nb;
  for (int i = 0; i < nb; i++) {
    const float *o = B + i * B_STRIDE;
    FBld *b = &mBlds[i];
    b->id = (int)o[0]; b->type = (int)o[1]; b->team = (int)o[2];
    b->x = (int)o[3]; b->y = (int)o[4]; b->w = (int)o[5]; b->h = (int)o[6];
    b->hp = o[7]; b->maxHp = o[8]; b->done = o[9] != 0; b->progress = o[10]; b->qt = o[11];
    b->qlen = (int)o[12]; if (b->qlen > 5) b->qlen = 5;
    for (int q = 0; q < b->qlen; q++) {
      b->q[q].kind = (int)o[18 + q * 3]; b->q[q].idx = (int)o[19 + q * 3]; b->q[q].time = o[20 + q * 3];
    }
    b->hasRally = o[13] != 0; b->rx = o[14]; b->ry = o[15]; b->nusers = (int)o[17];
    if (b->id > 0 && b->id < SIM_MAXID) { idKind[b->id] = K_BLD; idIdx[b->id] = (unsigned short)i; }
  }

  nNodes = nn;
  for (int i = 0; i < nn; i++) {
    const float *o = N + i * N_STRIDE;
    FNode *n = &mNodes[i];
    n->id = (int)o[0]; n->type = (int)o[1]; n->x = (int)o[2]; n->y = (int)o[3]; n->amt = o[4]; n->max = o[5];
    if (n->id > 0 && n->id < SIM_MAXID) { idKind[n->id] = K_NODE; idIdx[n->id] = (unsigned short)i; }
  }

  memset(mOcc, 0, sizeof mOcc);
  for (int i = 0; i < nn; i++) {
    const FNode *n = &mNodes[i];
    if (n->x >= 0 && n->y >= 0 && n->x < MAP_W && n->y < MAP_H) mOcc[n->y * MAP_W + n->x] = n->id;
  }
  for (int i = 0; i < nb; i++) {
    const FBld *b = &mBlds[i];
    for (int j = 0; j < b->h; j++) for (int k = 0; k < b->w; k++) {
      int tx = b->x + k, ty = b->y + j;
      if (tx >= 0 && ty >= 0 && tx < MAP_W && ty < MAP_H) mOcc[ty * MAP_W + tx] = b->id;
    }
  }

  nProjs = np;
  for (int i = 0; i < np; i++) {
    const float *o = P + i * P_STRIDE;
    mProjs[i].sx = o[0]; mProjs[i].sy = o[1]; mProjs[i].t = o[2]; mProjs[i].dur = o[3] > 0.001f ? o[3] : 0.001f;
    mProjs[i].stone = o[4] != 0; mProjs[i].tx = o[5]; mProjs[i].ty = o[6];
  }
  nEffs = ne;
  for (int i = 0; i < ne; i++) {
    const float *o = E + i * E_STRIDE;
    FEff *e = &mEffs[i];
    e->type = (int)o[0]; e->x = o[1]; e->y = o[2]; e->z = o[3]; e->life = o[4]; e->max = o[5] > 0.001f ? o[5] : 0.001f;
    e->color = (int)o[6]; e->amt = (int)o[7]; e->res = (int)o[8];
  }
  for (int i = 0; i < nev; i++) {                 /* events are delivered once per snapshot, so queue them */
    if (nEvQueue >= (int)(sizeof evQueue / sizeof evQueue[0])) break;
    const float *o = EV + i * EV_STRIDE;
    FEvent *e = &evQueue[nEvQueue++];
    e->type = (int)o[0]; e->team = (int)o[1]; e->x = o[2]; e->y = o[3]; e->a = (int)o[4]; e->b = (int)o[5];
  }
}

static void entity_center(int kind, int id, float *cx, float *cy) {
  if (kind == K_UNIT) { FUnit *u = get_unit(id); if (u) { *cx = u->x; *cy = u->y; return; } }
  else if (kind == K_BLD) { FBld *b = get_bld(id); if (b) { *cx = b->x + b->w / 2.0f; *cy = b->y + b->h / 2.0f; return; } }
  else if (kind == K_NODE) { FNode *n = get_node(id); if (n) { *cx = n->x + 0.5f; *cy = n->y + 0.5f; return; } }
  *cx = *cy = 0;
}

#endif
