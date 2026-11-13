/* A scripted "reasonable human" player for team 0, played against the built-in AI (team 1), headless.
 *
 *   make bot                          run the default sweep (3 difficulties x 6 seeds x 2 strategies)
 *   ./build/bot_player [seeds] [maxMinutes] [strategy: boom|rush|both] [difficulty 0|1|2|all] [map 0-4]
 *
 * It exists to measure game balance rather than to be clever: if a sensible bot cannot beat Easy, or beats Hard
 * every time, the tuning is off. It only uses what a human has: the public g_* API and the snapshot model, no
 * simulation internals. Decisions are made once per simulated second.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "model.h"
static int botMap;                    /* terrain for every game: 0 lake, 1 plains, 2 river, 3 ponds, 4 forest */

typedef struct { const char *name; int builders, attackAt, barracks, archery, stable, smith, towers; } Strat;
static const Strat STRATS[2] = {
  /* boom */ { "boom", 26, 34, 2, 2, 1, 1, 0 },
  /* rush */ { "rush", 14, 12, 2, 1, 0, 0, 0 },
};

static int attacking, lastWaveOrder;
static const Strat *S;

/* ---------------------------------- helpers ---------------------------------- */

static int afford(const int *c) { for (int k = 0; k < 4; k++) if (c[k] > 0 && mTeam[0].res[k] < c[k]) return 0; return 1; }

static float dist(float ax, float ay, float bx, float by) { return hypotf(ax - bx, ay - by); }
static float bx_c(const FBld *b) { return b->x + b->w / 2.0f; }
static float by_c(const FBld *b) { return b->y + b->h / 2.0f; }

static const FBld *my_tc(void) { for (int i = 0; i < nBlds; i++) if (mBlds[i].team == 0 && mBlds[i].type == B_TOWNCENTER) return &mBlds[i]; return NULL; }
static int cnt_units(int team, int type) { int n = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == team && (type < 0 || mUnits[i].type == type)) n++; return n; }
static int cnt_soldiers(int team) { int n = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == team && mUnits[i].type != U_BUILDER && mUnits[i].type != U_WARSHIP) n++; return n; }
static int cnt_blds(int team, int type, int doneOnly) { int n = 0; for (int i = 0; i < nBlds; i++) if (mBlds[i].team == team && mBlds[i].type == type && (!doneOnly || mBlds[i].done)) n++; return n; }
static int cnt_gatherers(int kind) { int n = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == 0 && mUnits[i].type == U_BUILDER && mUnits[i].state == ST_GATHER && mUnits[i].gatherKind == kind) n++; return n; }
static int cnt_building_units(void) { int n = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == 0 && mUnits[i].state == ST_BUILD) n++; return n; }

/* distance from a point to my nearest finished drop-off (town center / storehouse) */
static float dropsite_dist(float x, float y) {
  float best = 1e9f;
  for (int i = 0; i < nBlds; i++) { const FBld *b = &mBlds[i]; if (b->team == 0 && b->done && BDEF[b->type].drop) { float d = dist(x, y, bx_c(b), by_c(b)); if (d < best) best = d; } }
  return best;
}

static int find_spot(int type, float cx, float cy, int rmin, int rmax, int margin, int *ox, int *oy) {
  int w = BDEF[type].w, h = BDEF[type].h;
  for (int r = rmin; r <= rmax; r++)
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
      if (abs(dx) != r && abs(dy) != r) continue;
      int x = (int)cx + dx - w / 2, y = (int)cy + dy - h / 2;
      if (x < 1 || y < 1 || x + w >= MAP_W - 1 || y + h >= MAP_H - 1) continue;
      if (g_can_place(type, x, y, margin)) { *ox = x; *oy = y; return 1; }
    }
  return 0;
}

/* nearest builder that is not already constructing and is not hauling a full load */
static const FUnit *pick_builder(float x, float y) {
  const FUnit *best = NULL; float bd = 1e9f;
  for (int i = 0; i < nUnits; i++) {
    const FUnit *u = &mUnits[i];
    if (u->team != 0 || u->type != U_BUILDER || u->state == ST_BUILD || u->carry > 8) continue;
    float d = dist(u->x, u->y, x, y);
    if (d < bd) { bd = d; best = u; }
  }
  return best;
}

static int order_build(int type, int x, int y, int builderId) {
  int *buf = g_idbuf(); buf[0] = builderId;
  return g_place(0, type, x, y, 1) > 0;
}

static int try_build(int type, float cx, float cy, int rmin, int rmax, int margin) {
  if (!afford(BDEF[type].cost)) return 0;
  int x, y;
  if (!find_spot(type, cx, cy, rmin, rmax, margin, &x, &y)) return 0;
  const FUnit *b = pick_builder(x, y);
  if (!b) return 0;
  return order_build(type, x, y, b->id);
}

/* ---------------------------------- economy ---------------------------------- */

static void train(const FBld *b, int unit, int maxQueue) {
  if (b->qlen >= maxQueue || !afford(UDEF[unit].cost) || mTeam[0].popUsed + UDEF[unit].pop > mTeam[0].popCap) return;
  g_queue_unit(b->id, unit);
}

static const FNode *best_node(int type, const FUnit *u) {
  const FNode *best = NULL; float bs = 1e9f;
  for (int i = 0; i < nNodes; i++) {
    const FNode *n = &mNodes[i];
    if (n->type != type || n->amt <= 0) continue;
    float s = dropsite_dist(n->x + 0.5f, n->y + 0.5f) + 0.35f * dist(u->x, u->y, n->x + 0.5f, n->y + 0.5f);
    if (s < bs) { bs = s; best = n; }
  }
  return best;
}

static void assign_idle_builders(const FBld *tc) {
  int farms = 0; const FBld *freeFarm[32]; int nFree = 0;
  for (int i = 0; i < nBlds; i++) { const FBld *b = &mBlds[i]; if (b->team == 0 && b->type == B_FARM && b->done) { farms++; if (b->nusers < FARM_SLOTS && nFree < 32) freeFarm[nFree++] = b; } }
  int B = cnt_units(0, U_BUILDER);
  /* desired split of the work force */
  int wantFood = (int)(B * 0.52f), wantGold = (int)(B * 0.14f), wantStone = mTime > 300 ? (int)(B * 0.06f) : 0, wantWood = B - wantFood - wantGold - wantStone;
  if (mTime < 90) { wantGold = 0; wantStone = 0; wantWood = B - wantFood; }
  int have[4] = { cnt_gatherers(RES_FOOD), cnt_gatherers(RES_WOOD), cnt_gatherers(RES_GOLD), cnt_gatherers(RES_STONE) };
  int want[4] = { wantFood, wantWood, wantGold, wantStone };
  (void)farms;
  for (int i = 0; i < nUnits; i++) {
    const FUnit *u = &mUnits[i];
    if (u->team != 0 || u->type != U_BUILDER || u->state != ST_IDLE) continue;
    int pick = -1, bestDef = -1000;
    for (int k = 0; k < 4; k++) { int d = want[k] - have[k]; if (d > bestDef) { bestDef = d; pick = k; } }
    if (pick == RES_FOOD && !nFree) pick = RES_WOOD;
    const FNode *n = NULL;
    if (pick == RES_FOOD) {
      int *buf = g_idbuf(); buf[0] = u->id;
      g_order_gather(1, freeFarm[--nFree]->id); have[RES_FOOD]++;
      continue;
    }
    n = best_node(pick == RES_WOOD ? N_TREE : pick == RES_GOLD ? N_GOLD : N_STONE, u);
    if (!n) n = best_node(N_TREE, u);
    if (!n) continue;
    int *buf = g_idbuf(); buf[0] = u->id;
    g_order_gather(1, n->id); have[pick]++;
  }
  (void)tc;
}

static void economy_and_buildings(const FBld *tc) {
  float cx = bx_c(tc), cy = by_c(tc);
  int B = cnt_units(0, U_BUILDER), pending = cnt_building_units();
  if (pending >= 3) return;                                       /* do not pull the whole work force into construction */
  /* houses */
  int housesQueued = 0; for (int i = 0; i < nBlds; i++) if (mBlds[i].team == 0 && mBlds[i].type == B_HOUSE && !mBlds[i].done) housesQueued++;
  int room = mTeam[0].popCap + 5 * housesQueued - mTeam[0].popUsed;
  if (room < 4 && mTeam[0].popCap + 5 * housesQueued < MAX_POP) { if (try_build(B_HOUSE, cx, cy, 5, 12, 1)) return; }
  /* farms: one per three farmers we want */
  int farmsHave = cnt_blds(0, B_FARM, 0), farmsWant = (int)ceilf(B * 0.52f / FARM_SLOTS);
  if (farmsHave < farmsWant && B >= 5) { if (try_build(B_FARM, cx, cy, 3, 9, 0)) return; }
  /* storehouses next to resources that are far from any drop-off */
  if (mTime > 60 && cnt_blds(0, B_STOREHOUSE, 0) < (mTime < 240 ? 2 : 4) && cnt_blds(0, B_STOREHOUSE, 0) < B / 5) {
    for (int pass = 0; pass < 3; pass++) {
      int type = pass == 0 ? N_TREE : pass == 1 ? N_GOLD : N_STONE;
      if (type == N_STONE) continue;
      if (type != N_TREE && mTime < 150) continue;
      const FNode *far = NULL; float bd = 1e9f;
      for (int i = 0; i < nNodes; i++) {
        const FNode *n = &mNodes[i];
        if (n->type != type || n->amt <= 0) continue;
        float dd = dropsite_dist(n->x + 0.5f, n->y + 0.5f), dtc = dist(cx, cy, n->x, n->y);
        if (dd > 7 && dd < 1e8f && dtc < 30 && dtc < bd) { bd = dtc; far = n; }
      }
      if (far && cnt_gatherers(type == N_TREE ? RES_WOOD : type == N_GOLD ? RES_GOLD : RES_STONE) >= 2 && try_build(B_STOREHOUSE, far->x, far->y, 2, 4, 0)) return;
    }
  }
  /* military buildings, in a sensible order */
  if (B >= 9 && cnt_blds(0, B_BARRACKS, 0) < 1 && try_build(B_BARRACKS, cx, cy, 6, 14, 1)) return;
  if (B >= 14 && cnt_blds(0, B_ARCHERY, 0) < S->archery && cnt_blds(0, B_BARRACKS, 0) >= 1 && try_build(B_ARCHERY, cx, cy, 6, 14, 1)) return;
  if (B >= 14 && cnt_blds(0, B_BARRACKS, 0) < S->barracks && try_build(B_BARRACKS, cx, cy, 6, 14, 1)) return;
  if (S->smith && B >= 16 && cnt_blds(0, B_BLACKSMITH, 0) < 1 && try_build(B_BLACKSMITH, cx, cy, 6, 14, 1)) return;
  if (S->stable && B >= 18 && cnt_blds(0, B_STABLE, 0) < S->stable && try_build(B_STABLE, cx, cy, 6, 14, 1)) return;
}

static void production(const FBld *tc) {
  int B = cnt_units(0, U_BUILDER);
  int behind = mTime > 120 && B >= 12 && cnt_soldiers(0) < cnt_soldiers(1) + 2;
  if (B < S->builders && !behind) train(tc, U_BUILDER, 2);
  else if (B < 12) train(tc, U_BUILDER, 2);
  int sw = cnt_units(0, U_SWORDSMAN), sp = cnt_units(0, U_SPEARMAN), en_cav = cnt_units(1, U_CAVALRY), en_arch = cnt_units(1, U_ARCHER);
  for (int i = 0; i < nBlds; i++) {
    const FBld *b = &mBlds[i];
    if (b->team != 0 || !b->done) continue;
    if (b->type == B_BARRACKS) train(b, (en_cav * 2 > sw + sp || sp < sw) ? U_SPEARMAN : U_SWORDSMAN, 2), (void)en_arch;
    else if (b->type == B_ARCHERY) train(b, U_ARCHER, 2);
    else if (b->type == B_STABLE) train(b, U_CAVALRY, 1);
    else if (b->type == B_BLACKSMITH && b->qlen == 0 && B >= 14) {
      int w = mTeam[0].upWeapons + g_tech_pending(0, TECH_WEAPONS), a = mTeam[0].upArmor + g_tech_pending(0, TECH_ARMOR);
      int t = w <= a ? TECH_WEAPONS : TECH_ARMOR, lvl = t == TECH_WEAPONS ? w : a;
      if (lvl < TDEF[t].max) {
        int lc = lvl, cost[4] = { TDEF[t].base[0] + TDEF[t].step[0] * lc, 0, TDEF[t].base[1] + TDEF[t].step[1] * lc, 0 };
        if (afford(cost)) g_queue_tech(b->id, t);
      }
    }
  }
}

/* ---------------------------------- military ---------------------------------- */

static int army_ids(int *out) {
  int n = 0;
  for (int i = 0; i < nUnits; i++) { const FUnit *u = &mUnits[i]; if (u->team == 0 && u->type != U_BUILDER && u->type != U_WARSHIP) out[n++] = u->id; }
  return n;
}
static void send_army(const int *ids, int n, float x, float y) {
  int *buf = g_idbuf(), cap = g_idbuf_len(), m = 0;
  for (int i = 0; i < n && m < cap; i++) buf[m++] = ids[i];
  if (m) g_order_move(m, x, y, 1);
}

static void military(const FBld *tc) {
  int ids[SIM_MAXU], n = army_ids(ids);
  float cx = bx_c(tc), cy = by_c(tc);
  /* defend: any enemy near one of my buildings */
  const FUnit *threat = NULL; float td = 1e9f;
  for (int i = 0; i < nUnits; i++) {
    const FUnit *e = &mUnits[i];
    if (e->team != 1) continue;
    for (int j = 0; j < nBlds; j++) { const FBld *b = &mBlds[j]; if (b->team != 0) continue; float d = dist(e->x, e->y, bx_c(b), by_c(b)); if (d < 14 && d < td) { td = d; threat = e; } }
  }
  if (threat && n) { send_army(ids, n, threat->x, threat->y); lastWaveOrder = (int)mTime; return; }
  if (!n) { attacking = 0; return; }
  if (attacking && n < 6) { attacking = 0; send_army(ids, n, cx + 4, cy + 4); return; }       /* beaten back: regroup at home */
  if (!attacking && n >= S->attackAt && mTime > 150) attacking = 1;
  if (attacking && (int)mTime - lastWaveOrder >= 6) {
    /* target: the enemy building closest to my base, so the army meets resistance in order */
    const FBld *tgt = NULL; float bd = 1e9f;
    for (int j = 0; j < nBlds; j++) { const FBld *b = &mBlds[j]; if (b->team != 1) continue; float d = dist(cx, cy, bx_c(b), by_c(b)); if (d < bd) { bd = d; tgt = b; } }
    if (tgt) { send_army(ids, n, bx_c(tgt), by_c(tgt)); lastWaveOrder = (int)mTime; }
  }
}

static void think(void) {
  const FBld *tc = my_tc();
  if (!tc) return;
  production(tc);
  assign_idle_builders(tc);
  economy_and_buildings(tc);
  military(tc);
}

/* ---------------------------------- harness ---------------------------------- */

typedef struct { int winner; float time; int myArmy, enArmy, myBlds, enBlds, peakArmy; } Result;

static Result play(int seed, int diff, const Strat *strat, float maxMinutes) {
  S = strat; attacking = 0; lastWaveOrder = -100;
  g_set_map(botMap);
  g_init((unsigned)seed, diff, 1 << 1);
  model_sync();
  Result r = { -1, 0, 0, 0, 0, 0, 0 };
  int tick = 0;
  for (float t = 0; t < maxMinutes * 60; t += 0.05f) {
    g_update(0.05);
    if (++tick % 20) continue;
    model_sync();
    if (mWinner >= 0) break;
    think();
    int a = cnt_soldiers(0); if (a > r.peakArmy) r.peakArmy = a;
    if (getenv("BOT_TRACE") && tick % 400 == 0)                       /* every 20 simulated seconds */
      printf("  t=%3ds F%4d W%4d G%4d S%4d pop %2d/%2d builders %2d (food %d wood %d gold %d stone %d, idle %d) army %2d | houses %d farms %d store %d rax %d arch %d | enemy army %d\n",
        (int)mTime, (int)mTeam[0].res[0], (int)mTeam[0].res[1], (int)mTeam[0].res[2], (int)mTeam[0].res[3], mTeam[0].popUsed, mTeam[0].popCap, cnt_units(0, U_BUILDER),
        cnt_gatherers(RES_FOOD), cnt_gatherers(RES_WOOD), cnt_gatherers(RES_GOLD), cnt_gatherers(RES_STONE),
        ({ int q = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == 0 && mUnits[i].type == U_BUILDER && mUnits[i].state == ST_IDLE) q++; q; }),
        cnt_soldiers(0), cnt_blds(0, B_HOUSE, 0), cnt_blds(0, B_FARM, 0), cnt_blds(0, B_STOREHOUSE, 0), cnt_blds(0, B_BARRACKS, 0), cnt_blds(0, B_ARCHERY, 0), cnt_soldiers(1));
  }
  model_sync();
  r.winner = mWinner; r.time = mTime; r.myArmy = cnt_soldiers(0); r.enArmy = cnt_soldiers(1);
  for (int i = 0; i < nBlds; i++) { if (mBlds[i].team == 0) r.myBlds++; else r.enBlds++; }
  return r;
}

int main(int argc, char **argv) {
  int seeds = argc > 1 ? atoi(argv[1]) : 6;
  float maxMin = argc > 2 ? (float)atof(argv[2]) : 25.0f;
  const char *which = argc > 3 ? argv[3] : "both";
  const char *dsel = argc > 4 ? argv[4] : "all";
  botMap = argc > 5 ? atoi(argv[5]) : 0;
  if (!model_init()) { printf("snapshot layout mismatch\n"); return 2; }
  static const char *DN[3] = { "easy", "normal", "hard" };
  printf("%-6s %-5s %-5s %-8s %7s %9s %9s %9s\n", "diff", "strat", "seed", "result", "minutes", "peakArmy", "army/en", "blds/en");
  for (int d = 0; d < 3; d++) {
    if (strcmp(dsel, "all") && atoi(dsel) != d) continue;
    for (int si = 0; si < 2; si++) {
      if (strcmp(which, "both") && strcmp(which, STRATS[si].name)) continue;
      int wins = 0, losses = 0, draws = 0; float tw = 0;
      for (int seed = 1; seed <= seeds; seed++) {
        Result r = play(seed, d, &STRATS[si], maxMin);
        printf("%-6s %-5s %-5d %-8s %7.1f %9d %4d/%-4d %4d/%-4d\n", DN[d], STRATS[si].name, seed, r.winner == 0 ? "WIN" : r.winner == 1 ? "loss" : "timeout", r.time / 60, r.peakArmy, r.myArmy, r.enArmy, r.myBlds, r.enBlds);
        if (r.winner == 0) { wins++; tw += r.time; } else if (r.winner == 1) losses++; else draws++;
      }
      printf("  => %s / %s: %d wins (avg %.1f min), %d losses, %d timeouts\n\n", DN[d], STRATS[si].name, wins, wins ? tw / wins / 60 : 0.0f, losses, draws);
    }
  }
  return 0;
}
