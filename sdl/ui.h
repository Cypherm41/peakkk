/* Interaction layer of the SDL2 front end: picking, selection, orders, command card, HUD, menus and input.
 * Counterpart of the second half of js/ui.js (everything after the renderer). The HUD is drawn immediate-mode with
 * gfx.h (the web version uses DOM elements), so the bitmap font is ASCII only and unit / building "icons" are
 * initial letters. Layout is a pure function of the window size so drawing and hit-testing always agree.
 *
 * Entry points used by main.c and tests/sdl_smoke.c:
 *   ui_init / ui_shutdown     window, renderer, font, sound
 *   ui_start_game             start a new game (resets all UI state)
 *   ui_event                  feed one SDL event
 *   ui_update / ui_draw       advance by dt seconds / draw one frame
 */
#ifndef UI_H
#define UI_H

#include <time.h>
#include "render.h"

typedef struct { float x, y, w, h; } Rc;
static int in_rc(Rc r, float px, float py) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }

static SDL_Window *gWin;
static int uiQuit, endShown;
static float diffToastTimer;
static SDL_Cursor *curArrow, *curCross;

static struct {
  float x, y, sx, sy;
  int inside, seen, down, mid, drag, onMini, lastType;   /* seen: a real motion event arrived (edge scroll needs it) */
  Uint32 lastClick;
} mouse = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0 };

/* ------------------------------ layout ------------------------------ */

#define CELL_W 72.0f
#define CELL_H 44.0f
#define CELL_G 4.0f
#define CMD_PAD 4.0f
#define CMD_PW (4 * CELL_W + 3 * CELL_G + 2 * CMD_PAD)
#define CMD_PH (3 * CELL_H + 2 * CELL_G + 2 * CMD_PAD)
#define MM_X 14.0f
#define BAR_PAD 10.0f

static float bottom_y(void) { return H - BOTTOM_H; }
static float mm_y(void) { return bottom_y() + 19; }
static Rc minimap_rc(void) { Rc r = { MM_X, mm_y(), MM_W, MM_H }; return r; }
static Rc cmds_rc(void) { Rc r = { W - 8 - CMD_PW, bottom_y() + BAR_PAD, CMD_PW, CMD_PH }; return r; }
static Rc info_rc(void) {
  float x = MM_X + MM_W + 11;
  Rc r = { x, bottom_y() + BAR_PAD, W - 8 - CMD_PW - 8 - x, CMD_PH };
  return r;
}
static Rc cell_rc(int i) {
  Rc c = cmds_rc();
  Rc r = { c.x + CMD_PAD + (i % 4) * (CELL_W + CELL_G), c.y + CMD_PAD + (i / 4) * (CELL_H + CELL_G), CELL_W, CELL_H };
  return r;
}
static int in_world(float y) { return y >= TOP_H && y < H - BOTTOM_H; }

static void top_rcs(Rc *idle, Rc *speed, Rc *pause, Rc *help) {
  float x = W - 12 - 72;
  *help = (Rc){ x - 30, 6, 30, 28 }; x -= 36;
  *pause = (Rc){ x - 34, 6, 34, 28 }; x -= 40;
  *speed = (Rc){ x - 50, 6, 50, 28 }; x -= 56;
  *idle = (Rc){ x - 100, 6, 100, 28 };
}

static int overlay_open(void) { return screen == SCR_MENU || overlayPanel != PANEL_NONE; }

/* ------------------------------ names for the HUD ------------------------------ */

static const char *UNIT_SHORT[NUM_UNIT] = { "Builder", "Sword", "Spear", "Archer", "Cavalry", "Ship" };
static const char *BLD_SHORT[NUM_BLD] = { "Town Ctr", "House", "Farm", "Storehs", "Barracks", "Archery", "Stable", "Smithy", "Port", "Tower" };
static const char *TECH_SHORT[NUM_TECH] = { "Weapons", "Armor" };
static const char *CLS_NAME[NUM_CLS] = { "worker", "infantry", "spear", "archer", "cavalry", "ship" };
static const int BUILD_SLOTS[10] = { B_HOUSE, B_FARM, B_STOREHOUSE, B_BARRACKS, B_ARCHERY, B_STABLE, B_BLACKSMITH, B_PORT, B_TOWER, B_TOWNCENTER };
static const char HK_CH[13] = "QWERASDFZXCV";
static const SDL_Scancode HK_SC[12] = {
  SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_A, SDL_SCANCODE_S,
  SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V };

static int team_of(int id) {
  const FUnit *u = get_unit(id); if (u) return u->team;
  const FBld *b = get_bld(id); if (b) return b->team;
  return -1;                                                  /* resource nodes and unknown ids are neutral */
}

/* ------------------------------ selection ------------------------------ */

static void set_sel(const int *ids, int n) {
  if (n > MAXSEL) n = MAXSEL;
  memmove(sel, ids, (size_t)n * sizeof(int));
  nSel = n;
}
static void sel_clear(void) { nSel = 0; }
static void sel_one(int id) { set_sel(&id, 1); }

static void set_mode(int attackMove) {
  cmdAttackMove = attackMove;
  if (curCross && curArrow) SDL_SetCursor(attackMove || placingOn ? curCross : curArrow);
}

/* ------------------------------ picking ------------------------------ */

static int building_hit(const FBld *b, float sx, float sy) {
  float hh = (BLD_HEIGHT[b->type] + 6) * zoom;
  float nx, ny, ex, ey, sx2, sy2, wx, wy;
  w2s((float)b->x, (float)b->y, &nx, &ny);
  w2s((float)(b->x + b->w), (float)b->y, &ex, &ey);
  w2s((float)(b->x + b->w), (float)(b->y + b->h), &sx2, &sy2);
  w2s((float)b->x, (float)(b->y + b->h), &wx, &wy);
  float poly[12] = { wx, wy - hh, nx, ny - hh, ex, ey - hh, ex, ey, sx2, sy2, wx, wy };
  return point_in_poly(sx, sy, poly, 6);
}

/* entity id under a screen point: units first, then buildings (farms are flat, so only via the ground), then nodes */
static int pick_at(float sx, float sy) {
  int best = 0; float bd = 1e9f;
  for (int i = 0; i < nUnits; i++) {
    const FUnit *u = &mUnits[i];
    float px, py; w2s(u->x, u->y, &px, &py);
    float r = (UDEF[u->type].big ? 17.0f : 12.0f) * zoom, d = hypotf(px - sx, py - 11 * zoom - sy);
    if (d < r && d < bd) { bd = d; best = u->id; }
  }
  if (best) return best;
  int bb = 0; float bk = -1e9f;
  for (int i = 0; i < nBlds; i++) {
    const FBld *b = &mBlds[i];
    if (b->type == B_FARM) continue;
    if (building_hit(b, sx, sy)) { float k = b->x + b->y + (b->w + b->h) / 2.0f; if (k > bk) { bk = k; bb = b->id; } }
  }
  if (bb) return bb;
  float wx, wy; s2w(sx, sy, &wx, &wy);
  int tx = (int)floorf(wx), ty = (int)floorf(wy);
  for (int dy = 0; dy <= 1; dy++) for (int dx = 0; dx <= 1; dx++) {
    int x = tx + dx, y = ty + dy;                               /* trees draw above their tile, so also test the tile behind */
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
    int o = mOcc[y * MAP_W + x];
    const FNode *n = get_node(o);
    if (!n) continue;
    if (dx == 0 && dy == 0) return o;
    if (n->type == N_TREE) {
      float px, py; w2s(n->x + 0.5f, n->y + 0.5f, &px, &py);
      if (hypotf(px - sx, py - 14 * zoom - sy) < 16 * zoom) return o;
    }
  }
  if (tx >= 0 && ty >= 0 && tx < MAP_W && ty < MAP_H && get_bld(mOcc[ty * MAP_W + tx])) return mOcc[ty * MAP_W + tx];
  return 0;
}

static void mini_to_world(float mx, float my, float *wx, float *wy) {
  float ix = mx / MM_K - MAP_H * 32.0f, iy = my / MM_K, a = ix / 32.0f, b = iy / 16.0f;
  *wx = clampf((a + b) / 2, 0, MAP_W - 1); *wy = clampf((b - a) / 2, 0, MAP_H - 1);
}

/* ------------------------------ orders ------------------------------ */

static void do_move(const int *ids, int n, float x, float y, int attackMove) {
  if (n <= 0) return;
  g_order_move(push_ids(ids, n), x, y, attackMove);
}

/* split a unit-id list into workers and everyone else */
static void split_workers(const int *ids, int n, int *wk, int *nw, int *ot, int *no) {
  *nw = *no = 0;
  for (int i = 0; i < n; i++) { const FUnit *u = get_unit(ids[i]); if (u && is_worker_u(u)) wk[(*nw)++] = ids[i]; else ot[(*no)++] = ids[i]; }
}

static void left_click(float sx, float sy, int shift) {
  if (cmdAttackMove) {
    float wx, wy; s2w(sx, sy, &wx, &wy);
    int us[MAXSEL], wk[MAXSEL], ot[MAXSEL], nw, no, n = mine_ids(us);
    split_workers(us, n, wk, &nw, ot, &no);
    do_move(ot, no, wx, wy, 1);                                 /* soldiers attack-move, builders just walk */
    do_move(wk, nw, wx, wy, 0);
    add_fx(wx, wy, 0xff8e7e); sfx(SFX_ORDER); set_mode(0);
    model_sync();
    return;
  }
  int id = pick_at(sx, sy);
  Uint32 now = SDL_GetTicks();
  if (!id) { if (!shift) sel_clear(); return; }
  const FUnit *u = get_unit(id);
  if (u && u->team == 0) {
    if (now - mouse.lastClick < 380 && mouse.lastType == u->type) {      /* double click: every unit of that type on screen */
      int same[MAXSEL], m = 0;
      for (int i = 0; i < nUnits && m < MAXSEL; i++) {
        const FUnit *v = &mUnits[i];
        if (v->team != 0 || v->type != u->type) continue;
        float px, py; w2s(v->x, v->y, &px, &py);
        if (px >= 0 && px <= W && py >= TOP_H && py <= H - BOTTOM_H) same[m++] = v->id;
      }
      set_sel(same, m);
    } else if (shift) {
      int keep[MAXSEL], m = 0, had = sel_has(id);
      for (int i = 0; i < nSel && m < MAXSEL - 1; i++) {
        const FUnit *v = get_unit(sel[i]);
        if (v && v->team == 0 && sel[i] != id) keep[m++] = sel[i];
      }
      if (!had) keep[m++] = id;
      set_sel(keep, m);
    } else sel_one(id);
  } else sel_one(id);
  mouse.lastClick = now; mouse.lastType = u ? u->type : -1;
  sfx(SFX_SELECT);
}

static void select_box(float x0, float y0, float x1, float y1, int add) {
  float xa = fminf(x0, x1), xb = fmaxf(x0, x1), ya = fminf(y0, y1), yb = fmaxf(y0, y1);
  int found[MAXSEL], n = 0;
  for (int i = 0; i < nUnits && n < MAXSEL; i++) {
    const FUnit *u = &mUnits[i];
    if (u->team != 0) continue;
    float px, py; w2s(u->x, u->y, &px, &py);
    if (px >= xa && px <= xb && py - 10 >= ya - 8 && py - 10 <= yb + 8) found[n++] = u->id;
  }
  if (!n) { if (!add) sel_clear(); return; }
  if (add) {
    int all[MAXSEL], m = 0;
    for (int i = 0; i < nSel && m < MAXSEL; i++) { const FUnit *u = get_unit(sel[i]); if (u && u->team == 0) all[m++] = sel[i]; }
    for (int i = 0; i < n && m < MAXSEL; i++) { int dup = 0; for (int j = 0; j < m; j++) if (all[j] == found[i]) { dup = 1; break; } if (!dup) all[m++] = found[i]; }
    set_sel(all, m);
  } else set_sel(found, n);
  sfx(SFX_SELECT);
}

static void right_click(float sx, float sy) {
  if (cmdAttackMove) { set_mode(0); return; }
  if (placingOn) { placingOn = 0; set_mode(0); return; }
  float wx, wy; s2w(sx, sy, &wx, &wy);
  int tgt = pick_at(sx, sy), us[MAXSEL], wk[MAXSEL], ot[MAXSEL], nw, no, n = mine_ids(us);
  split_workers(us, n, wk, &nw, ot, &no);
  const FBld *tb = get_bld(tgt);
  if (n) {
    if (team_of(tgt) == 1) { g_order_attack(push_ids(us, n), tgt); add_fx(wx, wy, 0xff6b5a); }
    else if (get_node(tgt)) {
      if (nw) g_order_gather(push_ids(wk, nw), tgt);
      do_move(ot, no, wx, wy, 0);
      add_fx(wx, wy, 0xfff3a0);
    } else if (tb && tb->team == 0 && (tb->type == B_FARM || !tb->done || tb->hp < tb->maxHp) && nw) {
      if (tb->type == B_FARM && tb->done) g_order_gather(push_ids(wk, nw), tgt); else g_order_build(push_ids(wk, nw), tgt);
      do_move(ot, no, wx, wy, 0);
      add_fx(wx, wy, 0x9af0a6);
    } else { do_move(us, n, wx, wy, 0); add_fx(wx, wy, 0x9af0a6); }
    sfx(SFX_ORDER);
    model_sync();
    return;
  }
  const FBld *b = nSel == 1 ? get_bld(sel[0]) : NULL;
  if (b && b->team == 0 && b->done && BDEF[b->type].trains[0] >= 0) {
    const FNode *nd = get_node(tgt);
    g_set_rally(b->id, wx, wy, (nd || (tb && tb->type == B_FARM)) ? tgt : 0);
    add_fx(wx, wy, 0x9af0a6); toast(0, "Rally point set"); sfx(SFX_ORDER);
    model_sync();
  }
}

/* ------------------------------ building placement ------------------------------ */

static void update_placing(void) {
  if (!placingOn) return;
  float wx, wy; s2w(mouse.x, mouse.y, &wx, &wy);
  placeX = (int)floorf(wx) - BDEF[placeType].w / 2;
  placeY = (int)floorf(wy) - BDEF[placeType].h / 2;
  placeValid = g_can_place(placeType, placeX, placeY, 0);
}

static void start_placing(int type) {
  int miss = missing_res(mTeam[0].res, BDEF[type].cost);
  if (miss >= 0) { toast(2, "Not enough %s", RES_NAME[miss]); sfx(SFX_ERR); return; }
  cmdAttackMove = 0; placingOn = 1; placeType = type;
  update_placing(); set_mode(0);
}

static void try_place(int shift) {
  if (!placingOn) return;
  int us[MAXSEL], wk[MAXSEL], ot[MAXSEL], nw, no, n = mine_ids(us);
  split_workers(us, n, wk, &nw, ot, &no);
  if (!nw) { placingOn = 0; set_mode(0); toast(2, "Select a builder first"); return; }
  int r = g_place(0, placeType, placeX, placeY, push_ids(wk, nw));
  if (r < 0) { toast(2, "%s", error_text(-r)); sfx(SFX_ERR); return; }
  sfx(SFX_PLACE);
  model_sync();
  if (shift && missing_res(mTeam[0].res, BDEF[placeType].cost) < 0) update_placing();
  else { placingOn = 0; set_mode(0); }
}

static void stop_cmd(void) {
  int us[MAXSEL], n = mine_ids(us);
  if (n) { g_order_stop(push_ids(us, n)); model_sync(); }
  set_mode(0);
}

/* ------------------------------ command card ------------------------------ */

enum { CK_NONE = 0, CK_TRAIN, CK_TECH, CK_BUILD, CK_ATTACK, CK_STOP };
typedef struct {
  int kind, arg, enabled, active, hasCost, cost[4];
  char label[20], title[48], stats[200], desc[200], warn[40], extra[24];
  char glyph;
} Cmd;
static Cmd curCmds[12];
static const char ROMAN[3][4] = { "I", "II", "III" };

static void cost_text(const int *c, char *out, size_t n) {
  out[0] = 0;
  for (int k = 0; k < 4; k++) if (c[k] > 0) { size_t l = strlen(out); snprintf(out + l, n - l, "%s%c%d", l ? " " : "", RES_LETTER[k], c[k]); }
}

static void bonus_text(const double *bonus, char *out, size_t n) {
  out[0] = 0;
  for (int c = 0; c < NUM_CLS; c++) if (bonus[c] > 0) { size_t l = strlen(out); snprintf(out + l, n - l, "%s%s", l ? ", " : " - Bonus vs ", CLS_NAME[c]); }
}

static void range_text(double range, char *out, size_t n) {
  if (range > 2) snprintf(out, n, "Range %g", range); else snprintf(out, n, "Melee");
}

static void get_commands(void) {
  memset(curCmds, 0, sizeof curCmds);
  if (!inGame || mWinner >= 0) return;
  int mineSel[MAXSEL], nm = 0;                                  /* selected entities of team 0, in selection order */
  for (int i = 0; i < nSel; i++) if (team_of(sel[i]) == 0) mineSel[nm++] = sel[i];
  if (!nm) return;
  const float *res = mTeam[0].res;
  const FBld *b = get_bld(mineSel[0]);
  if (b) {
    if (!b->done) return;
    int i = 0;
    for (int k = 0; k < 2; k++) {
      int t = BDEF[b->type].trains[k];
      if (t < 0) continue;
      const UnitDef *d = &UDEF[t];
      Cmd *c = &curCmds[i++];
      int popOk = mTeam[0].popUsed + d->pop <= mTeam[0].popCap;
      char rng[24], bon[80];
      range_text(d->range, rng, sizeof rng); bonus_text(d->bonus, bon, sizeof bon);
      c->kind = CK_TRAIN; c->arg = t; c->hasCost = 1; memcpy(c->cost, d->cost, sizeof c->cost);
      c->enabled = missing_res(res, d->cost) < 0 && popOk; c->glyph = UNIT_NAME[t][0];
      snprintf(c->label, sizeof c->label, "%s", UNIT_SHORT[t]);
      snprintf(c->title, sizeof c->title, "%s", UNIT_NAME[t]);
      snprintf(c->stats, sizeof c->stats, "HP %g - ATK %g - Armor %d/%d - %s - Pop %d%s", d->hp, d->atk, d->armor[0], d->armor[1], rng, d->pop, bon);
      snprintf(c->desc, sizeof c->desc, "%s", UNIT_DESC[t]);
      if (!popOk) snprintf(c->warn, sizeof c->warn, "Need more houses");
      snprintf(c->extra, sizeof c->extra, "Shift: queue 5");
    }
    for (int k = 0; k < 2; k++) {
      int t = BDEF[b->type].research[k];
      if (t < 0) continue;
      const TechDef *d = &TDEF[t];
      Cmd *c = &curCmds[i++];
      int have = t == TECH_WEAPONS ? mTeam[0].upWeapons : mTeam[0].upArmor;
      int lvl = have + g_tech_pending(0, t), maxed = lvl >= d->max, lc = lvl < d->max - 1 ? lvl : d->max - 1;
      int cost[4] = { d->base[0] + d->step[0] * lc, 0, d->base[1] + d->step[1] * lc, 0 };
      c->kind = CK_TECH; c->arg = t; c->hasCost = !maxed;
      if (!maxed) memcpy(c->cost, cost, sizeof c->cost);
      c->enabled = !maxed && missing_res(res, cost) < 0; c->glyph = TECH_NAME[t][0];
      snprintf(c->label, sizeof c->label, "%s %s", TECH_SHORT[t], maxed ? "MAX" : ROMAN[lvl > 2 ? 2 : lvl]);
      snprintf(c->title, sizeof c->title, "%s - level %d", TECH_NAME[t], lvl + 1);
      snprintf(c->desc, sizeof c->desc, "%s", TECH_DESC[t]);
      if (maxed) snprintf(c->warn, sizeof c->warn, "Fully researched");
    }
    return;
  }
  int nu = 0, workers = 0, soldiers = 0;
  for (int i = 0; i < nm; i++) { const FUnit *u = get_unit(mineSel[i]); if (!u) continue; nu++; if (is_worker_u(u)) workers++; else soldiers++; }
  if (!nu) return;
  if (workers) {
    for (int i = 0; i < 10; i++) {
      int t = BUILD_SLOTS[i];
      const BldDef *d = &BDEF[t];
      Cmd *c = &curCmds[i];
      c->kind = CK_BUILD; c->arg = t; c->hasCost = 1; memcpy(c->cost, d->cost, sizeof c->cost);
      c->enabled = missing_res(res, d->cost) < 0; c->active = placingOn && placeType == t; c->glyph = BLD_NAME[t][0];
      snprintf(c->label, sizeof c->label, "%s", BLD_SHORT[t]);
      snprintf(c->title, sizeof c->title, "%s", BLD_NAME[t]);
      char pop[24] = ""; if (d->pop) snprintf(pop, sizeof pop, " - +%d pop", d->pop);
      snprintf(c->stats, sizeof c->stats, "HP %g - Build time %gs%s", d->hp, d->time, pop);
      snprintf(c->desc, sizeof c->desc, "%s", BLD_DESC[t]);
    }
  }
  if (soldiers) {
    Cmd *c = &curCmds[workers ? 10 : 4];
    c->kind = CK_ATTACK; c->enabled = 1; c->active = cmdAttackMove; snprintf(c->label, sizeof c->label, "Attack");
    snprintf(c->title, sizeof c->title, "Attack-move"); snprintf(c->desc, sizeof c->desc, "Move to a point, attacking any enemy met on the way.");
  }
  Cmd *c = &curCmds[workers ? 11 : 5];
  c->kind = CK_STOP; c->enabled = 1; snprintf(c->label, sizeof c->label, "Stop");
  snprintf(c->title, sizeof c->title, "Stop"); snprintf(c->desc, sizeof c->desc, "Cancel current orders.");
}

static void run_cmd(int i, int shift) {
  const Cmd *c = &curCmds[i];
  if (c->kind == CK_NONE) return;
  if (c->kind == CK_TRAIN || c->kind == CK_TECH) {
    int bid = 0;
    for (int k = 0; k < nSel; k++) if (team_of(sel[k]) == 0) { bid = sel[k]; break; }
    if (c->kind == CK_TRAIN) {
      for (int n = 0; n < (shift ? 5 : 1); n++) {
        int err = g_queue_unit(bid, c->arg);
        if (err) { toast(2, "%s", error_text(err)); sfx(SFX_ERR); break; }
        if (n == 0) sfx(SFX_SELECT);
      }
    } else {
      int err = g_queue_tech(bid, c->arg);
      if (err) { toast(2, "%s", error_text(err)); sfx(SFX_ERR); } else sfx(SFX_SELECT);
    }
    model_sync();
  } else if (c->kind == CK_BUILD) start_placing(c->arg);
  else if (c->kind == CK_ATTACK) set_mode(!cmdAttackMove);
  else if (c->kind == CK_STOP) stop_cmd();
}

/* ------------------------------ camera, groups, misc commands ------------------------------ */

static int idle_builders(int *out) {
  int n = 0;
  for (int i = 0; i < nUnits; i++) { const FUnit *u = &mUnits[i]; if (u->team == 0 && u->type == U_BUILDER && u->state == ST_IDLE) out[n++] = u->id; }
  return n;
}

static void cycle_idle(void) {
  int list[SIM_MAXU], n = idle_builders(list);
  if (!n) { toast(0, "No idle builders"); return; }
  idleIdx = (idleIdx + 1) % n;
  const FUnit *u = get_unit(list[idleIdx]);
  sel_one(u->id); centerCam(u->x, u->y); sfx(SFX_SELECT);
}

static void center_selection(void) {
  float cx, cy;
  if (nSel) { entity_center(sel_kind(0), sel[0], &cx, &cy); centerCam(cx, cy); return; }
  for (int i = 0; i < nBlds; i++) if (mBlds[i].team == 0 && mBlds[i].type == B_TOWNCENTER) {
    entity_center(K_BLD, mBlds[i].id, &cx, &cy); centerCam(cx, cy); return;
  }
}

static void toggle_pause(void) {
  if (!inGame || mWinner >= 0) return;
  paused = !paused;
  if (paused) toast(0, "Paused (P to resume)");
}

static void cycle_speed(void) { speedMul = speedMul == 1 ? 2 : speedMul == 2 ? 3 : 1; }

static void group_key(int n, int assign) {
  if (assign) {
    int m = 0;
    for (int i = 0; i < nSel && m < (int)(sizeof groups[0] / sizeof(int)); i++) if (team_of(sel[i]) == 0) groups[n][m++] = sel[i];
    groupN[n] = m;
    toast(0, "Group %d set (%d)", n, m);
    return;
  }
  int found[MAXSEL / 4], m = 0;
  for (int i = 0; i < groupN[n]; i++) if (idKind[groups[n][i]] == K_UNIT || idKind[groups[n][i]] == K_BLD) found[m++] = groups[n][i];
  if (!m) return;
  Uint32 now = SDL_GetTicks();
  if (now - lastGroupTick < 400 && lastGroupN == n) { float cx, cy; entity_center(idKind[found[0]], found[0], &cx, &cy); centerCam(cx, cy); }
  set_sel(found, m);
  lastGroupTick = now; lastGroupN = n; sfx(SFX_SELECT);
}

static void camera_tick(float dt) {
  if (!inGame || overlay_open()) return;
  const Uint8 *k = SDL_GetKeyboardState(NULL);
  float dx = 0, dy = 0;
  if (k[SDL_SCANCODE_LEFT]) dx -= 1;
  if (k[SDL_SCANCODE_RIGHT]) dx += 1;
  if (k[SDL_SCANCODE_UP]) dy -= 1;
  if (k[SDL_SCANCODE_DOWN]) dy += 1;
  if (mouse.inside && mouse.seen && !mouse.down) {
    if (mouse.x < 6) dx -= 1;
    if (mouse.x > W - 6) dx += 1;
    if (mouse.y < TOP_H + 6) dy -= 1;
  }
  if (dx != 0 || dy != 0) { float s = 900 / zoom * dt; camX += dx * s; camY += dy * s * 0.6f; clampCam(); }
}

/* ------------------------------ events from the simulation ------------------------------ */

static void process_events(void) {
  for (int i = 0; i < nEvQueue; i++) {
    const FEvent *e = &evQueue[i];
    if (e->type == EVT_MARCH) { toast(1, "The Yellow army is on the march!"); sfx(SFX_ALERT); continue; }
    if (e->team != 0) continue;
    if (e->type == EVT_ALERT) {
      toast(1, "We are under attack!"); sfx(SFX_ALERT);
      if (nPings == 8) { memmove(pings, pings + 1, 7 * sizeof(Fx)); nPings = 7; }
      pings[nPings++] = (Fx){ e->x, e->y, 0, 0 };
    }
    else if (e->type == EVT_BUILT && e->a >= 0 && e->a < NUM_BLD) { toast(0, "%s complete", BLD_NAME[e->a]); sfx(SFX_BUILD); }
    else if (e->type == EVT_TECH && e->a >= 0 && e->a < NUM_TECH) toast(0, "%s researched", TECH_NAME[e->a]);
    else if (e->type == EVT_DESTROYED && e->a >= 0 && e->a < NUM_BLD) { toast(1, "Our %s was destroyed!", BLD_NAME[e->a]); sfx(SFX_ALERT); }
  }
  nEvQueue = 0;
}

/* ------------------------------ starting a game ------------------------------ */

static void ui_start_game(int diff, unsigned seed) {
  g_init(seed, diff, 1 << 1);
  model_bind_terrain();
  nSel = 0; nEvQueue = 0; placingOn = 0; cmdAttackMove = 0; paused = 0; speedMul = 1; zoom = 1;
  nPings = 0; nClickFx = 0; nToasts = 0; idleIdx = 0; endShown = 0; hoverId = 0;
  memset(groupN, 0, sizeof groupN);
  mouse.down = mouse.mid = mouse.drag = mouse.onMini = 0;
  model_sync();
  build_mini_terrain();
  for (int i = 0; i < nBlds; i++) if (mBlds[i].team == 0 && mBlds[i].type == B_TOWNCENTER) {
    centerCam(mBlds[i].x + 1.5f, mBlds[i].y + 1.5f); sel_one(mBlds[i].id); break;
  }
  inGame = 1; screen = SCR_PLAY; overlayPanel = PANEL_NONE;
  diffChoice = diff;
  set_mode(0);
  toast(0, "Train builders at your Town Center, then put them to work!");
  diffToastTimer = 1.2f;
}

/* ------------------------------ per-frame update ------------------------------ */

static void ui_layout(void) {
  int ww, wh, pw, ph;
  SDL_GetWindowSize(gWin, &ww, &wh);
  SDL_GetRendererOutputSize(gR, &pw, &ph);
  W = (float)ww; H = (float)wh; dpr = ww > 0 ? (float)pw / (float)ww : 1.0f;
  layout_view();
}

/* the end-of-game panel is shown once per game, after which the player can still look at the map or go to the menu */
static void check_end(void) {
  if (mWinner >= 0 && !endShown) { endShown = 1; overlayPanel = PANEL_END; }
}

static void ui_update(float dt) {
  ui_layout();
  gNow += dt;
  mouse.inside = SDL_GetMouseFocus() == gWin;
  for (int i = nToasts - 1; i >= 0; i--) { toasts[i].age += dt; if (toasts[i].age > 3.6f) { memmove(toasts + i, toasts + i + 1, (size_t)(nToasts - i - 1) * sizeof(Toast)); nToasts--; } }
  if (!inGame) return;
  if ((!paused && !overlay_open()) || mWinner >= 0) {
    float rem = dt * speedMul;
    while (rem > 0) { float s = rem < 0.05f ? rem : 0.05f; g_update(s); rem -= s; }
  }
  model_sync();
  sel_prune();
  camera_tick(dt);
  process_events();
  for (int i = nClickFx - 1; i >= 0; i--) { clickFx[i].t += dt; if (clickFx[i].t > 0.7f) { memmove(clickFx + i, clickFx + i + 1, (size_t)(nClickFx - i - 1) * sizeof(Fx)); nClickFx--; } }
  for (int i = nPings - 1; i >= 0; i--) { pings[i].t += dt; if (pings[i].t > 1.5f) { memmove(pings + i, pings + i + 1, (size_t)(nPings - i - 1) * sizeof(Fx)); nPings--; } }
  if (placingOn) update_placing();
  hoverId = (mouse.seen && !overlay_open() && !placingOn && !mouse.drag && !mouse.mid && in_world(mouse.y)) ? pick_at(mouse.x, mouse.y) : 0;
  if (diffToastTimer > 0 && (diffToastTimer -= dt) <= 0) toast(0, "Enemy difficulty: %s", DIFF_NAME[diffChoice]);
  check_end();
  get_commands();
}

/* ------------------------------ drawing helpers ------------------------------ */

static void ui_button(Rc r, const char *label, float scale, int selected, int hover) {
  unsigned bg = selected ? 0x7a5a2b : hover ? 0x75562f : 0x5a4228, border = selected ? 0xffe08a : 0xa98650;
  panel(r.x, r.y, r.w, r.h, 4, bg, border, 1);
  text_center(r.x + r.w / 2, r.y + (r.h - 8 * scale) / 2, scale, 0xf6e9c6, 1, label);
}

static void big_button(Rc r, const char *label, int hover) {
  fill_rrect(r.x, r.y, r.w, r.h, 6, 0xffe9a8, 1);
  fill_vgrad(r.x + 2, r.y + 2, r.w - 4, r.h - 4, hover ? 0xffe08a : 0xf3d27a, hover ? 0xdcaa40 : 0xc99a34, 1);
  text_center(r.x + r.w / 2, r.y + (r.h - 16) / 2, 2, 0x2a1a05, 1, label);
}

static void draw_costs(float x, float y, const int *cost, int center) {
  char tok[4][16]; float w = 0; int n = 0, k4[4];
  for (int k = 0; k < 4; k++) if (cost[k] > 0) { snprintf(tok[n], sizeof tok[n], "%c%d", RES_LETTER[k], cost[k]); k4[n] = k; w += text_width(tok[n], 1) + 6; n++; }
  if (!n) return;
  float cx = center ? x - (w - 6) / 2 : x;
  for (int i = 0; i < n; i++) {
    int k = k4[i];
    text(cx, y, 1, mTeam[0].res[k] >= cost[k] ? RES_COLOR[k] : 0xff8e7e, 1, tok[i]);
    cx += text_width(tok[i], 1) + 6;
  }
}

static void bar(float x, float y, float w, float h, float frac, unsigned col) {
  fill_rect(x, y, w, h, 0x000000, 1);
  fill_rect(x + 1, y + 1, w - 2, h - 2, 0x2a1d10, 1);
  fill_rect(x + 1, y + 1, (w - 2) * clampf(frac, 0, 1), h - 2, col, 1);
}
static unsigned hp_color(float f) { return f > 0.6f ? 0x5fcf6b : f > 0.3f ? 0xe2c23a : 0xd8493b; }

static const char *unit_status(const FUnit *u) {
  static char buf[64];
  switch (u->state) {
    case ST_IDLE: return "Idle";
    case ST_MOVE: return "Moving";
    case ST_ATTACKMOVE: return "Attack-moving";
    case ST_ATTACK: return "Attacking";
    case ST_BUILD: return u->targetDone ? "Repairing" : "Building";
    case ST_GATHER: {
      const char *w = u->gatherKind == RES_WOOD ? "Chopping wood" : u->gatherKind == RES_FOOD ? "Farming" : u->gatherKind == RES_GOLD ? "Mining gold" : u->gatherKind == RES_STONE ? "Quarrying stone" : "Gathering";
      snprintf(buf, sizeof buf, "%s%s", w, u->sub == SUBST_TODROP ? " (returning)" : "");
      return buf;
    }
  }
  return "";
}

static void tag_chip(float x, float y, int team) {
  const char *s = team == 0 || team == 1 ? TEAM_NAME[team] : "Neutral";
  unsigned col = team == 0 ? 0x5fcf6b : team == 1 ? 0xf1d44d : 0xbbbbbb;
  float w = text_width(s, 1) + 14;
  fill_rrect(x, y, w, 14, 7, col, 1);
  text(x + 7, y + 3, 1, 0x111111, 1, s);
}

/* ------------------------------ info panel ------------------------------ */

#define MAX_TILES 48
static Rc qRc[5]; static int nQRc;
static Rc tileRc[MAX_TILES]; static int tileId[MAX_TILES], nTiles;

static void draw_info(void) {
  Rc r = info_rc();
  panel(r.x, r.y, r.w, r.h, 4, 0x1a1209, 0x6b5230, 1);
  gfx_clip(r.x + 1, r.y + 1, r.w - 2, r.h - 2);
  float x = r.x + 12, y = r.y + 8, iw = r.w - 24;
  nQRc = 0; nTiles = 0;
  if (!nSel) {
    text(x, y, 2, 0xffe9a8, 1, "Nothing selected");
    text_wrap(x, y + 26, iw, 2, 0xb8a87e, 1, "Left-click or drag to select. Right-click to give orders. Select your Town Center to train builders, then send them to chop trees, build farms and mine gold. Press F1 for all controls.", 1);
    gfx_unclip(); return;
  }
  if (nSel == 1) {
    int id = sel[0];
    const FNode *n = get_node(id);
    const FUnit *u = get_unit(id);
    const FBld *b = get_bld(id);
    if (n) {
      const char *res[3] = { "wood", "gold", "stone" };
      text(x, y, 2, 0xffe9a8, 1, NODE_NAME[n->type]);
      bar(x, y + 24, iw, 9, n->amt / n->max, hp_color(n->amt / n->max));
      textf(x, y + 40, 2, 0xd9c9a0, 1, "Remaining: %d %s", (int)ceilf(n->amt), res[n->type]);
      text_wrap(x, y + 66, iw, 2, 0xb8a87e, 1, "Right-click with builders selected to gather. Build a Storehouse nearby to shorten the walk.", 1);
    } else if (u) {
      const UnitDef *d = &UDEF[u->type];
      float f = u->hp / u->maxHp;
      int soldier = d->soldier;
      int atk = (int)d->atk + (soldier && d->atkType != 2 ? 2 * mTeam[u->team].upWeapons : 0);
      int am = d->armor[0] + (soldier ? mTeam[u->team].upArmor : 0), ap = d->armor[1] + (soldier ? mTeam[u->team].upArmor : 0);
      text(x, y, 2, 0xffe9a8, 1, UNIT_NAME[u->type]);
      tag_chip(x + text_width(UNIT_NAME[u->type], 2) + 12, y + 1, u->team);
      bar(x, y + 24, iw, 9, f, hp_color(f));
      char rng[24]; range_text(d->range, rng, sizeof rng);
      textf(x, y + 40, 2, 0xd9c9a0, 1, "HP %d/%d  ATK %d  Armor %d/%d  %s", (int)ceilf(u->hp), (int)u->maxHp, atk, am, ap, rng);
      float ty = y + 60;
      if (u->team == 0) {
        if (u->carry >= 1) textf(x, ty, 2, 0xd9c9a0, 1, "%s - carrying %d %s", unit_status(u), (int)u->carry, u->carryType >= 0 ? RES_NAME[u->carryType] : "");
        else text(x, ty, 2, 0xd9c9a0, 1, unit_status(u));
        ty += 20;
      }
      text_wrap(x, ty, iw, 2, 0xb8a87e, 1, UNIT_DESC[u->type], 1);
    } else if (b) {
      const BldDef *d = &BDEF[b->type];
      float f = b->hp / b->maxHp;
      text(x, y, 2, 0xffe9a8, 1, BLD_NAME[b->type]);
      tag_chip(x + text_width(BLD_NAME[b->type], 2) + 12, y + 1, b->team);
      bar(x, y + 24, iw, 9, f, hp_color(f));
      char ex[64] = "";
      if (d->atk > 0) snprintf(ex, sizeof ex, "  ATK %g  Range %g", d->atk, d->range);
      if (d->pop && b->done) { size_t l = strlen(ex); snprintf(ex + l, sizeof ex - l, "  +%d pop", d->pop); }
      textf(x, y + 40, 2, 0xd9c9a0, 1, "HP %d/%d%s", (int)ceilf(b->hp), (int)b->maxHp, ex);
      float ty = y + 60;
      if (!b->done) {
        textf(x, ty, 2, 0xd9c9a0, 1, "Under construction - %d%%", (int)floorf(b->progress * 100));
        bar(x, ty + 20, iw, 9, b->progress, 0x4aa0e0);
      } else if (b->team == 0) {
        if (b->type == B_FARM) { textf(x, ty, 2, 0xd9c9a0, 1, "Farmers: %d/%d", b->nusers, FARM_SLOTS); ty += 20; }
        if (b->qlen) {
          for (int i = 0; i < b->qlen; i++) {
            Rc q = { x + i * 42, ty, 38, 38 };
            qRc[nQRc++] = q;
            fill_rrect(q.x, q.y, q.w, q.h, 4, 0xa98650, 1);
            fill_rrect(q.x + 1, q.y + 1, q.w - 2, q.h - 2, 3, 0x3a2a18, 1);
            const char *nm = b->q[i].kind == 0 ? UNIT_NAME[b->q[i].idx] : TECH_NAME[b->q[i].idx];
            char g[2] = { nm[0], 0 };
            text_center(q.x + q.w / 2, q.y + 8, 2, b->q[i].kind == 0 ? 0xffe9a8 : 0x9ad0ff, 1, g);
            if (i == 0) {
              float total = b->q[0].time > 0.001f ? b->q[0].time : 0.001f;
              fill_rect(q.x + 1, q.y + q.h - 5, (q.w - 2) * clampf(b->qt / total, 0, 1), 4, 0x4aa0e0, 1);
            }
          }
          text(x + b->qlen * 42 + 8, ty + 14, 1, 0x8f7f5a, 1, "Click to cancel");
        } else text_wrap(x, ty, iw, 2, 0xb8a87e, 1, (d->trains[0] >= 0 || d->research[0] >= 0) ? "Right-click the ground to set a rally point." : BLD_DESC[b->type], 1);
      } else text_wrap(x, ty, iw, 2, 0xb8a87e, 1, BLD_DESC[b->type], 1);
    }
    gfx_unclip(); return;
  }
  int cu[NUM_UNIT] = { 0 }, cb[NUM_BLD] = { 0 };
  for (int i = 0; i < nSel; i++) { const FUnit *u = get_unit(sel[i]); const FBld *b = get_bld(sel[i]); if (u) cu[u->type]++; else if (b) cb[b->type]++; }
  char head[200]; size_t l = 0;
  l += (size_t)snprintf(head + l, sizeof head - l, "%d selected", nSel);
  for (int t = 0; t < NUM_UNIT && l < sizeof head - 24; t++) if (cu[t]) l += (size_t)snprintf(head + l, sizeof head - l, "  %s x%d", UNIT_SHORT[t], cu[t]);
  for (int t = 0; t < NUM_BLD && l < sizeof head - 24; t++) if (cb[t]) l += (size_t)snprintf(head + l, sizeof head - l, "  %s x%d", BLD_SHORT[t], cb[t]);
  text(x, y, 2, 0xffe9a8, 1, head);
  int cols = (int)(iw / 37); if (cols < 1) cols = 1;
  int rows = (int)((r.h - 8 - 28 - 16) / 40); if (rows < 1) rows = 1;
  int shown = nSel < MAX_TILES ? nSel : MAX_TILES; if (shown > cols * rows) shown = cols * rows;
  for (int i = 0; i < shown; i++) {
    Rc t = { x + (i % cols) * 37, y + 26 + (i / cols) * 40, 34, 36 };
    tileRc[nTiles] = t; tileId[nTiles++] = sel[i];
    int hover = in_rc(t, mouse.x, mouse.y);
    fill_rrect(t.x, t.y, t.w, t.h, 3, hover ? 0xffe08a : 0x8a6a3a, 1);
    fill_rrect(t.x + 1, t.y + 1, t.w - 2, t.h - 2, 3, 0x3a2a18, 1);
    const FUnit *u = get_unit(sel[i]); const FBld *b = get_bld(sel[i]);
    const char *nm = u ? UNIT_NAME[u->type] : b ? BLD_NAME[b->type] : "?";
    char g[2] = { nm[0], 0 };
    text_center(t.x + t.w / 2, t.y + 6, 2, 0xffe9a8, 1, g);
    float f = u ? u->hp / u->maxHp : b ? b->hp / b->maxHp : 1;
    fill_rect(t.x + 2, t.y + t.h - 5, 30 * clampf(f, 0, 1), 3, hp_color(f), 1);
    if (u && u->carry >= 1) fill_ellipse(t.x + t.w - 4, t.y + 4, 3, 3, 0xffd24a, 1);
  }
  if (nSel > shown) textf(x, y + 26 + rows * 40, 1, 0xd9c9a0, 1, "+%d more", nSel - shown);
  gfx_unclip();
}

/* ------------------------------ command card + tooltip ------------------------------ */

static void draw_glyph(const Cmd *c, float cx, float cy, float a) {
  if (c->kind == CK_STOP) { fill_rect(cx - 8, cy - 8, 16, 16, 0xe86a5a, a); stroke_rect(cx - 8, cy - 8, 16, 16, 1.5f, 0x000000, a * 0.6f); return; }
  if (c->kind == CK_ATTACK) {
    line(cx - 10, cy + 9, cx + 10, cy - 9, 3, 0xe8ecf4, a); line(cx - 10, cy - 9, cx + 10, cy + 9, 3, 0xe8ecf4, a);
    line(cx - 12, cy + 11, cx - 6, cy + 5, 3, 0xb07a3e, a); line(cx - 12, cy - 11, cx - 6, cy - 5, 3, 0xb07a3e, a); return;
  }
  char g[2] = { c->glyph, 0 };
  text_center(cx, cy - 10, 3, c->kind == CK_TECH ? 0x9ad0ff : 0xfff4d0, a, g);
}

static void draw_cmds(void) {
  Rc r = cmds_rc();
  panel(r.x, r.y, r.w, r.h, 4, 0x1a1209, 0x6b5230, 1);
  int hov = -1;
  for (int i = 0; i < 12; i++) {
    const Cmd *c = &curCmds[i];
    if (c->kind == CK_NONE) continue;
    Rc cr = cell_rc(i);
    int hover = in_rc(cr, mouse.x, mouse.y); if (hover) hov = i;
    float a = c->enabled ? 1.0f : 0.5f;
    fill_rrect(cr.x, cr.y, cr.w, cr.h, 4, c->active ? 0xffe08a : hover ? 0xffe08a : 0xa98650, 1);
    fill_rrect(cr.x + 1, cr.y + 1, cr.w - 2, cr.h - 2, 3, c->enabled ? (hover ? 0x85622f : 0x5d4424) : 0x2e2417, 1);
    if (c->active) stroke_rect(cr.x + 1, cr.y + 1, cr.w - 2, cr.h - 2, 2, 0xffe08a, 1);
    text(cr.x + 4, cr.y + 3, 1, 0xffe9a8, a, (char[2]){ HK_CH[i], 0 });
    draw_glyph(c, cr.x + cr.w / 2, cr.y + 17, a);
    if (c->hasCost) draw_costs(cr.x + cr.w / 2, cr.y + cr.h - 10, c->cost, 1);
    else text_center(cr.x + cr.w / 2, cr.y + cr.h - 10, 1, 0xd9c9a0, a, c->label);
  }
  if (hov >= 0) {
    const Cmd *c = &curCmds[hov];
    Rc cr = cell_rc(hov);
    float tw = 300, lw = tw - 20, h = 12;
    char title[64]; snprintf(title, sizeof title, "%s [%c]", c->title, HK_CH[hov]);
    h += 20;
    if (c->hasCost) h += 14;
    if (c->stats[0]) h += text_wrap(0, 0, lw, 1, 0, 1, c->stats, 0) * 10 + 4;
    h += text_wrap(0, 0, lw, 1, 0, 1, c->desc, 0) * 10 + 4;
    if (c->warn[0]) h += 14;
    if (c->extra[0]) h += 14;
    float x = clampf(cr.x - 100, 8, W - tw - 8), y = fmaxf(8, cr.y - h - 10);
    panel(x, y, tw, h, 4, 0x140e08, 0xc9a566, 1);
    float ty = y + 8;
    text(x + 10, ty, 2, 0xffe9a8, 1, title); ty += 20;
    if (c->hasCost) { draw_costs(x + 10, ty, c->cost, 0); ty += 14; }
    if (c->stats[0]) { int n = text_wrap(x + 10, ty, lw, 1, 0xe6dcc0, 1, c->stats, 1); ty += n * 10 + 4; }
    int n = text_wrap(x + 10, ty, lw, 1, 0xcdbd93, 1, c->desc, 1); ty += n * 10 + 4;
    if (c->warn[0]) { text(x + 10, ty, 1, 0xff8e7e, 1, c->warn); ty += 14; }
    if (c->extra[0]) text(x + 10, ty, 1, 0xb8a87e, 1, c->extra);
  }
}

/* ------------------------------ top bar, toasts, hints ------------------------------ */

static void res_box(float x, char letter, unsigned col, int low, const char *value) {
  panel(x, 6, 104, 28, 4, 0x1f160c, 0x6b5230, 1);
  fill_ellipse(x + 16, 20, 9, 9, col, 1);
  text_center(x + 16, 16, 1, 0x2a1a05, 1, (char[2]){ letter, 0 });
  text(x + 32, 13, 2, low ? 0xff7b6b : 0xfff4d0, 1, value);
}

static void draw_topbar(void) {
  fill_vgrad(0, 0, W, TOP_H, 0x3b2a18, 0x281b0f, 1);
  fill_rect(0, TOP_H - 2, W, 2, 0x8a6a3a, 1);
  char buf[32]; float x = 10;
  for (int k = 0; k < 4; k++) { snprintf(buf, sizeof buf, "%d", (int)floorf(mTeam[0].res[k])); res_box(x, RES_LETTER[k], RES_COLOR[k], mTeam[0].res[k] < 30, buf); x += 110; }
  snprintf(buf, sizeof buf, "%d/%d", mTeam[0].popUsed, mTeam[0].popCap);
  res_box(x, 'P', 0xcfd6e0, mTeam[0].popUsed >= mTeam[0].popCap, buf);
  Rc idle, speed, pause, help; top_rcs(&idle, &speed, &pause, &help);
  int ib[SIM_MAXU];
  snprintf(buf, sizeof buf, "Idle %d", idle_builders(ib)); ui_button(idle, buf, 2, 0, in_rc(idle, mouse.x, mouse.y));
  snprintf(buf, sizeof buf, "%dx", speedMul); ui_button(speed, buf, 2, 0, in_rc(speed, mouse.x, mouse.y));
  ui_button(pause, "", 2, 0, in_rc(pause, mouse.x, mouse.y));
  if (paused) fill_tri(pause.x + 12, pause.y + 8, pause.x + 12, pause.y + 20, pause.x + 24, pause.y + 14, 0xf6e9c6, 1);
  else { fill_rect(pause.x + 11, pause.y + 8, 4, 12, 0xf6e9c6, 1); fill_rect(pause.x + 19, pause.y + 8, 4, 12, 0xf6e9c6, 1); }
  ui_button(help, "?", 2, 0, in_rc(help, mouse.x, mouse.y));
  int s = (int)mTime;
  snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
  text_right(W - 12, 13, 2, 0xe3cf9c, 1, buf);
}

static void draw_toasts(void) {
  float y = TOP_H + 12;
  for (int i = 0; i < nToasts; i++) {
    const Toast *t = &toasts[i];
    float a = t->age < 0.3f ? t->age / 0.3f : t->age > 2.9f ? (3.6f - t->age) / 0.7f : 1;
    float w = text_width(t->text, 2) + 32, x = (W - w) / 2;
    gAlpha = clampf(a, 0, 1);
    unsigned bg = t->alert == 1 ? 0x6e140e : 0x1e140a, border = t->alert == 1 ? 0xff7b6b : t->alert == 2 ? 0xe0584a : 0xc9a566;
    panel(x, y, w, 28, 14, bg, border, 1);
    text(x + 16, y + 7, 2, t->alert == 2 ? 0xffb3a8 : 0xf1e6c8, 1, t->text);
    gAlpha = 1;
    y += 34;
  }
}

static void draw_mode_hint(void) {
  const char *s = cmdAttackMove ? "Attack-move: click a destination (right-click or Esc to cancel)"
                : placingOn ? "Click to build (Shift = keep building, right-click / Esc = cancel)" : NULL;
  if (!s) return;
  float w = text_width(s, 1) * 2 + 28;
  panel((W - w) / 2, bottom_y() - 36, w, 26, 13, 0x000000, 0xffe08a, 1);
  text_center(W / 2, bottom_y() - 36 + 5, 2, 0xf1e6c8, 1, s);
}

static void draw_bottom_bar(void) {              /* background only: the minimap is drawn on top of it, the rest of the HUD after */
  screen_begin();
  fill_vgrad(0, bottom_y(), W, BOTTOM_H, 0x2c1f12, 0x1c140b, 1);
  fill_rect(0, bottom_y(), W, 3, 0x8a6a3a, 1);
}

/* small tooltip next to the cursor naming what is under it: unit / building / resource, owner and health */
static void draw_hover_tip(void) {
  if (!hoverId) return;
  char head[64], detail[96];
  const FUnit *u = get_unit(hoverId); const FBld *b = get_bld(hoverId); const FNode *n = get_node(hoverId);
  unsigned hc = 0xffe9a8;
  if (u) {
    snprintf(head, sizeof head, "%s (%s)", UNIT_NAME[u->type], TEAM_NAME[u->team]);
    snprintf(detail, sizeof detail, "HP %d/%d", (int)ceilf(u->hp), (int)u->maxHp);
    if (u->team == 1) hc = 0xff8e7e;
  } else if (b) {
    snprintf(head, sizeof head, "%s (%s)", BLD_NAME[b->type], TEAM_NAME[b->team]);
    if (b->done) snprintf(detail, sizeof detail, "HP %d/%d", (int)ceilf(b->hp), (int)b->maxHp);
    else snprintf(detail, sizeof detail, "Under construction %d%%", (int)floorf(b->progress * 100));
    if (b->team == 1) hc = 0xff8e7e;
  } else if (n) {
    const char *res[3] = { "wood", "gold", "stone" };
    snprintf(head, sizeof head, "%s", NODE_NAME[n->type]);
    snprintf(detail, sizeof detail, "%d %s left", (int)ceilf(n->amt), res[n->type]);
  } else return;
  float w = fmaxf(text_width(head, 2), text_width(detail, 1)) + 20, h = 44;
  float x = clampf(mouse.x + 16, 4, W - w - 4), y = clampf(mouse.y + 20, TOP_H + 4, H - BOTTOM_H - h - 4);
  panel(x, y, w, h, 4, 0x140e08, 0xc9a566, 1);
  text(x + 10, y + 7, 2, hc, 1, head);
  text(x + 10, y + 28, 1, 0xd9c9a0, 1, detail);
}

static void draw_hud(void) {
  screen_begin();
  draw_info();
  draw_cmds();
  draw_topbar();
  draw_mode_hint();
  draw_hover_tip();
  draw_toasts();
  gfx_flush();
}

/* ------------------------------ overlays: menu, help, end screen ------------------------------ */

static Rc menu_rc(void) { float w = fminf(640, W - 32), h = 590; Rc r = { (W - w) / 2, (H - h) / 2, w, h }; return r; }
static Rc menu_diff_rc(int i) { Rc p = menu_rc(); float x0 = p.x + (p.w - (3 * 104 - 8)) / 2; Rc r = { x0 + i * 104, p.y + 150, 96, 34 }; return r; }
static Rc menu_start_rc(void) { Rc p = menu_rc(); Rc r = { p.x + (p.w - 240) / 2, p.y + 204, 240, 50 }; return r; }

static const char *HELP_ROWS[][2] = {
  { "Left click / drag", "Select units or buildings (double click: all of a type on screen)" },
  { "Right click", "Move, attack, gather, build / repair, or set a rally point" },
  { "Arrows / screen edge", "Scroll the map (middle mouse drag also pans)" },
  { "Mouse wheel", "Zoom" },
  { "Q W E R A S D F Z X C V", "Command card hotkeys (the key shown on each button)" },
  { "Shift + click / train", "Keep placing buildings / train 5 units at once" },
  { "A, then click", "Attack-move (soldiers selected)" },
  { "S", "Stop (soldiers selected)" },
  { "1-9, Ctrl/Shift + 1-9", "Recall / assign a control group" },
  { ". (period)", "Select next idle builder" },
  { "Space", "Jump to selection (or Town Center)" },
  { "P, Esc, M", "Pause, cancel placement / deselect, sound on / off" },
  { "F1, F11", "This help, fullscreen on / off" },
};
#define HELP_N ((int)(sizeof HELP_ROWS / sizeof HELP_ROWS[0]))

static Rc help_rc(float *tableY, float *rowH, int *lines) {
  float w = fminf(900, W - 32), dw = w - 60 - 330, h = 150;
  for (int i = 0; i < HELP_N; i++) { lines[i] = text_wrap(0, 0, dw, 2, 0, 1, HELP_ROWS[i][1], 0); h += lines[i] * 20 + 8; }
  *rowH = 0; *tableY = 70;
  Rc r = { (W - w) / 2, fmaxf(8, (H - h) / 2), w, h };
  return r;
}
static Rc help_close_rc(void) {
  float ty, rh; int lines[HELP_N];
  Rc p = help_rc(&ty, &rh, lines);
  Rc r = { p.x + (p.w - 200) / 2, p.y + p.h - 70, 200, 46 };
  return r;
}

static Rc end_rc(void) { float w = fminf(560, W - 32), h = 270; Rc r = { (W - w) / 2, (H - h) / 2, w, h }; return r; }
static Rc end_again_rc(void) { Rc p = end_rc(); Rc r = { p.x + (p.w - 240) / 2, p.y + p.h - 76, 240, 50 }; return r; }

static void draw_panel_frame(Rc p) {
  fill_rrect(p.x, p.y, p.w, p.h, 8, 0xb18d52, 1);
  fill_vgrad(p.x + 3, p.y + 8, p.w - 6, p.h - 16, 0x3d2c19, 0x251a0e, 1);
  fill_vgrad(p.x + 6, p.y + 3, p.w - 12, 6, 0x3d2c19, 0x3d2c19, 1);
  fill_rect(p.x + 6, p.y + p.h - 9, p.w - 12, 6, 0x251a0e, 1);
}

static void draw_title(float cx, float y, float scale, unsigned col, const char *s) {
  text_center(cx + scale, y + scale, scale, 0x5a3d12, 1, s);
  text_center(cx, y, scale, col, 1, s);
}

static void draw_menu(void) {
  Rc p = menu_rc();
  draw_panel_frame(p);
  draw_title(p.x + p.w / 2, p.y + 26, 4, 0xffe39a, "Banners of Peakkk");
  text_wrap(p.x + 30, p.y + 78, p.w - 60, 2, 0xdfcfa3, 1, "A medieval real-time strategy game. Lead the Green kingdom against the Yellow realm.", 1);
  text(menu_diff_rc(0).x - 0, p.y + 124, 2, 0xcdbd93, 1, "Difficulty");
  for (int i = 0; i < 3; i++) { Rc r = menu_diff_rc(i); ui_button(r, DIFF_NAME[i], 2, diffChoice == i, in_rc(r, mouse.x, mouse.y)); }
  Rc s = menu_start_rc(); big_button(s, "Start Game", in_rc(s, mouse.x, mouse.y));
  static const char *tips[4] = {
    "Train builders at the Town Center and put them to work: chop trees, build farms, mine gold and stone.",
    "Build houses before you hit the population cap, and storehouses next to forests and mines.",
    "Raise an army from the Barracks, Archery Range and Stable. Swordsmen beat spearmen, spearmen beat cavalry, cavalry beat archers, archers beat swordsmen.",
    "Destroy every Yellow building to win. Press F1 in game for all controls.",
  };
  float y = p.y + 276;
  for (int i = 0; i < 4; i++) {
    fill_ellipse(p.x + 36, y + 7, 3, 3, 0xcdbd93, 1);
    int n = text_wrap(p.x + 50, y, p.w - 80, 2, 0xcdbd93, 1, tips[i], 1);
    y += n * 20 + 8;
  }
  text_center(p.x + p.w / 2, p.y + p.h - 22, 1, 0x8f7f5a, 1, "Enter: start   F1: controls (in game)   F11: fullscreen");
}

static void draw_help(void) {
  float ty, rh; int lines[HELP_N];
  Rc p = help_rc(&ty, &rh, lines);
  draw_panel_frame(p);
  draw_title(p.x + p.w / 2, p.y + 22, 3, 0xffe39a, "Controls");
  float y = p.y + ty, dx = p.x + 30 + 330;
  for (int i = 0; i < HELP_N; i++) {
    text(p.x + 30, y, 2, 0xffe39a, 1, HELP_ROWS[i][0]);
    text_wrap(dx, y, p.w - 60 - 330, 2, 0xe6dcc0, 1, HELP_ROWS[i][1], 1);
    y += lines[i] * 20 + 8;
    fill_rect(p.x + 30, y - 5, p.w - 60, 1, 0xffffff, 0.1f);
  }
  Rc c = help_close_rc(); big_button(c, "Close", in_rc(c, mouse.x, mouse.y));
}

static void draw_end(void) {
  Rc p = end_rc();
  int won = mWinner == 0, s = (int)mTime;
  draw_panel_frame(p);
  draw_title(p.x + p.w / 2, p.y + 28, 5, won ? 0x9af0a6 : 0xff8e7e, won ? "Victory!" : "Defeat");
  char buf[200];
  if (won) snprintf(buf, sizeof buf, "The Yellow realm has fallen. Your Green banners fly victorious after %dm %ds.", s / 60, s % 60);
  else snprintf(buf, sizeof buf, "Your last building has fallen after %dm %ds. The Yellow realm prevails.", s / 60, s % 60);
  text_wrap(p.x + 30, p.y + 96, p.w - 60, 2, 0xdfcfa3, 1, buf, 1);
  Rc a = end_again_rc(); big_button(a, "Play Again", in_rc(a, mouse.x, mouse.y));
}

static void draw_overlay(void) {
  if (!overlay_open()) return;
  screen_begin();
  fill_rect(0, 0, W, H, 0x050804, screen == SCR_MENU ? 0.86f : 0.55f);
  if (screen == SCR_MENU) draw_menu();
  else if (overlayPanel == PANEL_HELP) draw_help();
  else if (overlayPanel == PANEL_END) draw_end();
  gfx_flush();
}

/* ------------------------------ frame ------------------------------ */

static void ui_draw(void) {
  ui_layout();
  gfx_flush();
  SDL_SetRenderDrawColor(gR, 11, 16, 12, 255);
  SDL_RenderClear(gR);
  if (inGame) {
    render_world((int)mouse.x, (int)mouse.y, mouse.down && mouse.drag && !placingOn, mouse.sx, mouse.sy);
    draw_bottom_bar();
    draw_minimap(MM_X, mm_y());
    draw_hud();
  }
  draw_overlay();
  gfx_flush();
  SDL_RenderPresent(gR);
}

/* ------------------------------ input ------------------------------ */

static void overlay_click(float x, float y) {
  if (screen == SCR_MENU) {
    for (int i = 0; i < 3; i++) if (in_rc(menu_diff_rc(i), x, y)) { diffChoice = i; return; }
    if (in_rc(menu_start_rc(), x, y)) ui_start_game(diffChoice, (unsigned)time(NULL) ^ (SDL_GetTicks() * 2654435761u));
  } else if (overlayPanel == PANEL_HELP) {
    if (in_rc(help_close_rc(), x, y)) overlayPanel = PANEL_NONE;
  } else if (overlayPanel == PANEL_END) {
    if (in_rc(end_again_rc(), x, y)) { screen = SCR_MENU; overlayPanel = PANEL_NONE; }
  }
}

static void minimap_click(float x, float y, int button, int fresh) {
  float wx, wy; mini_to_world(x - MM_X, y - mm_y(), &wx, &wy);
  if (button == SDL_BUTTON_LEFT) {
    if (fresh && cmdAttackMove) {
      int us[MAXSEL], wk[MAXSEL], ot[MAXSEL], nw, no, n = mine_ids(us);
      split_workers(us, n, wk, &nw, ot, &no);
      do_move(ot, no, wx, wy, 1);
      set_mode(0); model_sync();
      return;
    }
    mouse.onMini = 1; centerCam(wx, wy);
  } else if (button == SDL_BUTTON_RIGHT && fresh) {
    int us[MAXSEL], n = mine_ids(us);
    if (n) { do_move(us, n, wx, wy, 0); sfx(SFX_ORDER); model_sync(); }
  }
}

static void on_mouse_down(int button, int shift) {
  float x = mouse.x, y = mouse.y;
  if (overlay_open()) { if (button == SDL_BUTTON_LEFT) overlay_click(x, y); return; }
  if (!inGame) return;
  if (y < TOP_H) {
    if (button != SDL_BUTTON_LEFT) return;
    Rc idle, speed, pause, help; top_rcs(&idle, &speed, &pause, &help);
    if (in_rc(idle, x, y)) cycle_idle();
    else if (in_rc(speed, x, y)) cycle_speed();
    else if (in_rc(pause, x, y)) toggle_pause();
    else if (in_rc(help, x, y)) overlayPanel = PANEL_HELP;
    return;
  }
  if (y >= H - BOTTOM_H) {
    if (in_rc(minimap_rc(), x, y)) { minimap_click(x, y, button, 1); return; }
    if (button != SDL_BUTTON_LEFT) return;
    for (int i = 0; i < 12; i++) if (in_rc(cell_rc(i), x, y)) { run_cmd(i, shift); return; }
    for (int i = 0; i < nQRc; i++) if (in_rc(qRc[i], x, y) && nSel == 1 && get_bld(sel[0])) { g_cancel_queue(sel[0], i); sfx(SFX_ERR); model_sync(); return; }
    for (int i = 0; i < nTiles; i++) if (in_rc(tileRc[i], x, y)) { sel_one(tileId[i]); return; }
    return;
  }
  if (button == SDL_BUTTON_LEFT) {
    if (placingOn) { try_place(shift); return; }
    mouse.down = 1; mouse.drag = 0; mouse.sx = x; mouse.sy = y;
  } else if (button == SDL_BUTTON_MIDDLE) mouse.mid = 1;
  else if (button == SDL_BUTTON_RIGHT) right_click(x, y);
}

static void on_mouse_up(int button, int shift) {
  if (!inGame) return;
  if (button == SDL_BUTTON_LEFT) {
    mouse.onMini = 0;
    if (mouse.down) {
      if (mouse.drag && !placingOn) select_box(mouse.sx, mouse.sy, mouse.x, mouse.y, shift);
      else if (!placingOn && in_world(mouse.y)) left_click(mouse.x, mouse.y, shift);
    }
    mouse.down = mouse.drag = 0;
  } else if (button == SDL_BUTTON_MIDDLE) mouse.mid = 0;
}

static void on_key(SDL_Scancode sc, int mod) {
  int shift = (mod & KMOD_SHIFT) != 0, ctrl = (mod & (KMOD_CTRL | KMOD_GUI)) != 0;
  if (sc == SDL_SCANCODE_F11) {                                  /* works on every screen, including the menu */
    int fs = (SDL_GetWindowFlags(gWin) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(gWin, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    return;
  }
  if (sc == SDL_SCANCODE_F1) {
    if (screen != SCR_PLAY) return;
    overlayPanel = overlayPanel == PANEL_HELP ? PANEL_NONE : overlayPanel == PANEL_NONE ? PANEL_HELP : overlayPanel;
    return;
  }
  if (screen == SCR_MENU) { if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) ui_start_game(diffChoice, (unsigned)time(NULL) ^ (SDL_GetTicks() * 2654435761u)); return; }
  if (!inGame || overlay_open()) { if (sc == SDL_SCANCODE_ESCAPE && overlayPanel == PANEL_HELP) overlayPanel = PANEL_NONE; return; }
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) { group_key(sc - SDL_SCANCODE_1 + 1, ctrl || shift); return; }
  switch (sc) {
    case SDL_SCANCODE_ESCAPE: if (placingOn) { placingOn = 0; set_mode(0); } else if (cmdAttackMove) set_mode(0); else sel_clear(); return;
    case SDL_SCANCODE_P: toggle_pause(); return;
    case SDL_SCANCODE_SPACE: center_selection(); return;
    case SDL_SCANCODE_PERIOD: cycle_idle(); return;
    case SDL_SCANCODE_M: muted = !muted; toast(0, muted ? "Sound off" : "Sound on"); return;
    default: break;
  }
  if (ctrl || (mod & KMOD_ALT)) return;
  for (int i = 0; i < 12; i++) if (sc == HK_SC[i] && curCmds[i].kind != CK_NONE) { run_cmd(i, shift); return; }
}

static void ui_event(const SDL_Event *e) {
  if (inGame) get_commands();                       /* hotkeys and clicks always act on the current selection */
  switch (e->type) {
    case SDL_QUIT: uiQuit = 1; break;
    case SDL_MOUSEBUTTONDOWN:
      mouse.x = (float)e->button.x; mouse.y = (float)e->button.y;
      on_mouse_down(e->button.button, (SDL_GetModState() & KMOD_SHIFT) != 0);
      break;
    case SDL_MOUSEBUTTONUP:
      mouse.x = (float)e->button.x; mouse.y = (float)e->button.y;
      on_mouse_up(e->button.button, (SDL_GetModState() & KMOD_SHIFT) != 0);
      break;
    case SDL_MOUSEMOTION:
      mouse.x = (float)e->motion.x; mouse.y = (float)e->motion.y; mouse.seen = 1;
      if (!inGame) break;
      if (mouse.down && !mouse.drag && hypotf(mouse.x - mouse.sx, mouse.y - mouse.sy) > 6) mouse.drag = 1;
      if (mouse.mid) { camX -= e->motion.xrel / zoom; camY -= e->motion.yrel / zoom; clampCam(); }
      if (mouse.onMini && (e->motion.state & SDL_BUTTON_LMASK)) { float wx, wy; mini_to_world(mouse.x - MM_X, mouse.y - mm_y(), &wx, &wy); centerCam(wx, wy); }
      if (placingOn) update_placing();
      break;
    case SDL_MOUSEWHEEL:
      if (inGame && in_world(mouse.y) && e->wheel.y != 0) {
        float bx, by, ax, ay; s2w(mouse.x, mouse.y, &bx, &by);
        zoom = clampf(zoom * (e->wheel.y > 0 ? 1.1f : 1 / 1.1f), 0.55f, 1.6f);
        s2w(mouse.x, mouse.y, &ax, &ay);
        camX += isoX(bx, by) - isoX(ax, ay); camY += isoY(bx, by) - isoY(ax, ay);
        clampCam();
      }
      break;
    case SDL_KEYDOWN:
      if (!e->key.repeat) on_key(e->key.keysym.scancode, e->key.keysym.mod);
      break;
    case SDL_WINDOWEVENT:
      if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) mouse.down = mouse.mid = mouse.drag = mouse.onMini = 0;
      break;
    default: break;
  }
}

/* ------------------------------ setup / teardown ------------------------------ */

static int ui_init(const char *title, int w, int h, int hidden) {
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError()); return 0; }
  Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
  gWin = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
  if (!gWin) { fprintf(stderr, "Cannot create window: %s\n", SDL_GetError()); return 0; }
  SDL_SetWindowMinimumSize(gWin, 1000, 680);
  gR = SDL_CreateRenderer(gWin, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
  if (!gR) gR = SDL_CreateRenderer(gWin, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
  if (!gR) { fprintf(stderr, "Cannot create renderer: %s\n", SDL_GetError()); return 0; }
  SDL_SetRenderDrawBlendMode(gR, SDL_BLENDMODE_BLEND);
  font_init();
  audio_init();
  curArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
  curCross = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
  if (!model_init()) { fprintf(stderr, "Snapshot layout of the simulation core does not match sdl/sim.h\n"); return 0; }
  ui_layout();
  return 1;
}

static void ui_shutdown(void) {
  if (audioDev) SDL_CloseAudioDevice(audioDev);
  if (miniTex) SDL_DestroyTexture(miniTex);
  if (gFont) SDL_DestroyTexture(gFont);
  if (curArrow) SDL_FreeCursor(curArrow);
  if (curCross) SDL_FreeCursor(curCross);
  if (gR) SDL_DestroyRenderer(gR);
  if (gWin) SDL_DestroyWindow(gWin);
  SDL_Quit();
}

/* save what is currently in the render target as a BMP (for the --shot option and the smoke test) */
static int ui_screenshot(const char *path) {
  int pw, ph;
  SDL_GetRendererOutputSize(gR, &pw, &ph);
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return 0;
  int ok = SDL_RenderReadPixels(gR, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0 && SDL_SaveBMP(s, path) == 0;
  SDL_FreeSurface(s);
  return ok;
}

#endif
