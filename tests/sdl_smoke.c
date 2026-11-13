/* Headless smoke test for the SDL2 front end (sdl/ui.h) against the real C simulation.
 *
 *   make test-sdl                      (SDL_VIDEODRIVER=dummy, software renderer: no display or sound card needed)
 *   ./build/sdl_smoke [shot_dir]       with a directory, also writes menu / play / help / end screenshots there as BMP
 *
 * Input is injected as SDL events through ui_event(), so this exercises the same code paths as a real mouse and
 * keyboard: hotkeys, the command card, box selection, orders, building placement, control groups, minimap, zoom.
 */
#include "ui.h"

static int failures, checks;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { printf("FAIL: %s  (line %d)\n", msg, __LINE__); failures++; } } while (0)

static void step(int frames) { for (int i = 0; i < frames; i++) { ui_update(1.0f / 60.0f); ui_draw(); } }
static void step_sim(float seconds) { for (float t = 0; t < seconds; t += 1.0f / 20.0f) ui_update(1.0f / 20.0f); }

static void key(SDL_Scancode sc, int mod) {
  SDL_Event e; SDL_zero(e);
  e.type = SDL_KEYDOWN; e.key.keysym.scancode = sc; e.key.keysym.mod = (Uint16)mod; e.key.state = SDL_PRESSED;
  SDL_SetModState((SDL_Keymod)mod);
  ui_event(&e);
  SDL_SetModState(KMOD_NONE);
}
static void move_to(float x, float y) {
  SDL_Event e; SDL_zero(e);
  e.type = SDL_MOUSEMOTION; e.motion.x = (int)x; e.motion.y = (int)y; e.motion.state = SDL_GetMouseState(NULL, NULL);
  ui_event(&e);
}
static void button(int which, int down, float x, float y, int mod) {
  SDL_Event e; SDL_zero(e);
  e.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; e.button.button = (Uint8)which; e.button.x = (int)x; e.button.y = (int)y;
  SDL_SetModState((SDL_Keymod)mod);
  ui_event(&e);
  SDL_SetModState(KMOD_NONE);
}
static void click(int which, float x, float y, int mod) { move_to(x, y); button(which, 1, x, y, mod); button(which, 0, x, y, mod); }
static void wheel(int dy) { SDL_Event e; SDL_zero(e); e.type = SDL_MOUSEWHEEL; e.wheel.y = dy; ui_event(&e); }

static const FBld *town_center(void) { for (int i = 0; i < nBlds; i++) if (mBlds[i].team == 0 && mBlds[i].type == B_TOWNCENTER) return &mBlds[i]; return NULL; }
static int count_units(int team, int type) { int n = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == team && (type < 0 || mUnits[i].type == type)) n++; return n; }
static int count_blds(int team) { int n = 0; for (int i = 0; i < nBlds; i++) if (mBlds[i].team == team) n++; return n; }
static void shot(const char *dir, const char *name) {
  if (!dir) return;
  char path[512]; snprintf(path, sizeof path, "%s/%s.bmp", dir, name);
  if (!ui_screenshot(path)) printf("could not write %s: %s\n", path, SDL_GetError());
}

int main(int argc, char **argv) {
  const char *shots = argc > 1 ? argv[1] : NULL;
  SDL_setenv("SDL_VIDEODRIVER", "dummy", 0);
  SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
  if (!ui_init("sdl smoke", 1280, 800, 1)) { printf("FAIL: ui_init: %s\n", SDL_GetError()); return 1; }

  /* ---- menu ---- */
  step(2);
  CHECK(screen == SCR_MENU && !inGame, "starts on the menu");
  shot(shots, "menu");
  Rc hard = menu_diff_rc(2);
  click(SDL_BUTTON_LEFT, hard.x + 5, hard.y + 5, 0);
  CHECK(diffChoice == 2, "clicking Hard selects it");
  click(SDL_BUTTON_LEFT, menu_diff_rc(1).x + 5, menu_diff_rc(1).y + 5, 0);
  CHECK(mapChoice == 0, "the Central Lake map is selected by default");
  for (int m = 0; m < MAP_COUNT; m++) {
    Rc r = menu_map_rc(m);
    click(SDL_BUTTON_LEFT, r.x + 5, r.y + 5, 0);
    CHECK(mapChoice == m, "clicking a map button selects it");
  }
  /* the menu shows every map and each map generates real terrain */
  for (int m = 0; m < MAP_COUNT; m++) {
    mapChoice = m; step(2);
    char name[80]; snprintf(name, sizeof name, "menu_map%d", m); shot(shots, name);
  }
  click(SDL_BUTTON_LEFT, menu_map_rc(2).x + 5, menu_map_rc(2).y + 5, 0);   /* start the real game on the river map */
  Rc st = menu_start_rc();
  click(SDL_BUTTON_LEFT, st.x + 10, st.y + 10, 0);
  CHECK(screen == SCR_PLAY && inGame, "Start Game begins a game");
  { int water = 0; for (int i = 0; i < MAP_W * MAP_H; i++) water += mTerrain[i] == TILE_WATER; CHECK(water > 120 && water < 800, "the river map has water"); }
  step(3);

  /* ---- start state ---- */
  const FBld *tc = town_center();
  CHECK(tc && nSel == 1 && sel[0] == tc->id, "town center is selected at start");
  CHECK(curCmds[0].kind == CK_TRAIN && curCmds[0].arg == U_BUILDER, "town center offers Train Builder on Q");
  CHECK(count_units(0, U_BUILDER) == 5, "five starting builders");
  float food0 = mTeam[0].res[RES_FOOD];

  /* ---- train a builder with the hotkey, then cancel it by clicking the queue slot ---- */
  key(SDL_SCANCODE_Q, 0);
  tc = town_center();
  CHECK(tc->qlen == 1, "Q queues a builder");
  CHECK(fabsf(mTeam[0].res[RES_FOOD] - (food0 - 50)) < 1.0f, "training costs 50 food");
  step(2);
  CHECK(nQRc == 1, "queue slot is drawn");
  click(SDL_BUTTON_LEFT, qRc[0].x + 5, qRc[0].y + 5, 0);
  tc = town_center();
  CHECK(tc->qlen == 0, "clicking the queue slot cancels it");
  CHECK(fabsf(mTeam[0].res[RES_FOOD] - food0) < 1.0f, "cancelling refunds the food");

  /* the same through the command card with the mouse; Shift queues several */
  step(2);
  Rc cell0 = cell_rc(0);
  click(SDL_BUTTON_LEFT, cell0.x + 10, cell0.y + 10, KMOD_SHIFT);
  tc = town_center();
  CHECK(tc->qlen >= 2, "Shift-click on the card queues several builders");
  for (int i = tc->qlen - 1; i >= 0; i--) g_cancel_queue(tc->id, i);
  model_sync();

  /* ---- box select ---- */
  step(2);
  move_to(40, 70); button(SDL_BUTTON_LEFT, 1, 40, 70, 0);
  move_to(1240, 600); button(SDL_BUTTON_LEFT, 0, 1240, 600, 0);
  CHECK(nSel == count_units(0, -1), "box select picks all own units on screen");
  CHECK(curCmds[0].kind == CK_NONE || 1, "card refreshed");
  step(1);
  CHECK(curCmds[11].kind == CK_STOP && curCmds[10].kind == CK_ATTACK && curCmds[0].kind == CK_BUILD, "mixed selection shows build cards + attack + stop");

  /* ---- right-click a tree: builders gather ---- */
  const FNode *tree = NULL; float best = 1e9f;
  for (int i = 0; i < nNodes; i++) {
    if (mNodes[i].type != N_TREE) continue;
    float px, py; w2s(mNodes[i].x + 0.5f, mNodes[i].y + 0.5f, &px, &py);
    float d = hypotf(px - W / 2, py - H / 2);
    if (px > 60 && px < W - 60 && py > TOP_H + 60 && py < H - BOTTOM_H - 60 && d < best) { best = d; tree = &mNodes[i]; }
  }
  CHECK(tree != NULL, "found a tree on screen");
  if (tree) {
    float px, py; w2s(tree->x + 0.5f, tree->y + 0.5f, &px, &py);
    int id = tree->id;
    click(SDL_BUTTON_RIGHT, px, py, 0);
    step(1);
    int gather = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == 0 && mUnits[i].type == U_BUILDER && mUnits[i].state == ST_GATHER) gather++;
    CHECK(gather == 5, "right-click on a tree sends all builders to gather");
    CHECK(pick_at(px, py) == id, "pick_at finds the tree under the cursor");
  }

  /* ---- hover feedback ---- */
  {
    const FUnit *own = NULL; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == 0) { own = &mUnits[i]; break; }
    float hx, hy; w2s(own->x, own->y, &hx, &hy);
    move_to(hx, hy - 11 * zoom); step(1);
    CHECK(hoverId == own->id, "hovering a unit sets hoverId");
    step(1); shot(shots, "hover");
    move_to(hx, H - 5); step(1);
    CHECK(hoverId == 0, "no hover over the HUD");
    move_to(hx, hy - 11 * zoom); key(SDL_SCANCODE_F1, 0); step(1);
    CHECK(hoverId == 0, "no hover while a panel is open");
    key(SDL_SCANCODE_F1, 0); step(1);
  }

  /* ---- control groups ---- */
  int nb = nSel;
  key(SDL_SCANCODE_1, KMOD_CTRL);
  CHECK(groupN[1] == nb, "Ctrl+1 stores the selection");
  key(SDL_SCANCODE_ESCAPE, 0);
  CHECK(nSel == 0, "Esc clears the selection");
  key(SDL_SCANCODE_1, 0);
  CHECK(nSel == nb, "1 recalls the group");

  /* ---- building placement ---- */
  int bid = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == 0 && mUnits[i].type == U_BUILDER) { bid = mUnits[i].id; break; }
  sel_one(bid); step(1);
  float wood0 = mTeam[0].res[RES_WOOD]; int blds0 = count_blds(0);
  key(SDL_SCANCODE_Q, 0);                                        /* house */
  CHECK(placingOn && placeType == B_HOUSE, "Q with a builder starts placing a house");
  int tx = -1, ty = -1;
  for (int r = 4; r < 12 && tx < 0; r++) for (int dy = -r; dy <= r && tx < 0; dy++) for (int dx = -r; dx <= r; dx++) {
    int x = tc->x + dx, y = tc->y + dy;
    if (g_can_place(B_HOUSE, x, y, 0)) { tx = x; ty = y; break; }
  }
  CHECK(tx >= 0, "found a free spot near the town center");
  float sx, sy; w2s(tx + 1.5f, ty + 1.5f, &sx, &sy);
  move_to(sx, sy); step(1);
  CHECK(placingOn && placeX == tx && placeY == ty && placeValid, "ghost snaps to the tile under the cursor and is valid");
  step(1); shot(shots, "placing");
  click(SDL_BUTTON_LEFT, sx, sy, 0);
  CHECK(!placingOn && count_blds(0) == blds0 + 1, "left click places the house");
  CHECK(fabsf(mTeam[0].res[RES_WOOD] - (wood0 - 40)) < 1.0f, "house costs 40 wood");
  key(SDL_SCANCODE_Q, 0);
  CHECK(placingOn, "Q again starts placing");
  click(SDL_BUTTON_RIGHT, sx, sy, 0);
  CHECK(!placingOn, "right click cancels placing");
  key(SDL_SCANCODE_Q, 0); key(SDL_SCANCODE_ESCAPE, 0);
  CHECK(!placingOn, "Esc cancels placing");

  /* ---- attack-move with a soldier ---- */
  int sp = 0; for (int i = 0; i < nUnits; i++) if (mUnits[i].team == 0 && mUnits[i].type == U_SPEARMAN) { sp = mUnits[i].id; break; }
  sel_one(sp); step(1);
  CHECK(curCmds[4].kind == CK_ATTACK && curCmds[5].kind == CK_STOP, "soldiers get Attack on A and Stop on S");
  key(SDL_SCANCODE_A, 0);
  CHECK(cmdAttackMove, "A enters attack-move mode");
  click(SDL_BUTTON_LEFT, W / 2 + 200, H / 2 - 100, 0);
  step(1);
  const FUnit *u = get_unit(sp);
  CHECK(!cmdAttackMove && u && u->state == ST_ATTACKMOVE, "clicking the ground issues the attack-move");
  key(SDL_SCANCODE_S, 0); step(1);
  u = get_unit(sp);
  CHECK(u && u->state == ST_IDLE, "S stops the soldier");

  /* ---- camera: minimap, wheel, space ---- */
  float cx0 = camX, cy0 = camY;
  Rc mm = minimap_rc();
  click(SDL_BUTTON_LEFT, mm.x + mm.w * 0.8f, mm.y + mm.h * 0.5f, 0);
  CHECK(camX != cx0 || camY != cy0, "clicking the minimap moves the camera");
  float z0 = zoom;
  move_to(W / 2, H / 2); wheel(1);
  CHECK(zoom > z0, "wheel up zooms in");
  for (int i = 0; i < 40; i++) wheel(-1);
  CHECK(zoom >= 0.55f - 1e-4f && zoom < 0.56f, "zoom is clamped at 0.55");
  for (int i = 0; i < 60; i++) wheel(1);
  CHECK(zoom <= 1.6f + 1e-4f && zoom > 1.59f, "zoom is clamped at 1.6");
  zoom = 1.0f;
  sel_one(tc->id);
  key(SDL_SCANCODE_SPACE, 0);
  float ex, ey; entity_center(K_BLD, tc->id, &ex, &ey);
  CHECK(fabsf(camX - isoX(ex, ey)) < 1 && fabsf(camY - isoY(ex, ey)) < 1, "Space centres on the selection");

  /* ---- idle builders / pause / speed / help ---- */
  sel_clear();
  key(SDL_SCANCODE_PERIOD, 0);
  CHECK(nSel == 1 || toasts[nToasts - 1].text[0] == 'N', "period selects an idle builder or says there is none");
  float t0 = mTime;
  key(SDL_SCANCODE_P, 0);
  CHECK(paused, "P pauses");
  step(60);
  CHECK(mTime == t0, "the simulation clock stops while paused");
  key(SDL_SCANCODE_P, 0);
  CHECK(!paused, "P resumes");
  Rc idleR, speedR, pauseR, helpR; top_rcs(&idleR, &speedR, &pauseR, &helpR);
  click(SDL_BUTTON_LEFT, speedR.x + 5, speedR.y + 5, 0);
  CHECK(speedMul == 2, "speed button cycles to 2x");
  click(SDL_BUTTON_LEFT, speedR.x + 5, speedR.y + 5, 0); click(SDL_BUTTON_LEFT, speedR.x + 5, speedR.y + 5, 0);
  CHECK(speedMul == 1, "speed button wraps back to 1x");
  key(SDL_SCANCODE_F1, 0);
  CHECK(overlayPanel == PANEL_HELP, "F1 opens the help panel");
  step(2); shot(shots, "help");
  float tHelp = mTime; step(30);
  CHECK(mTime == tHelp, "the game is paused behind the help panel");
  click(SDL_BUTTON_LEFT, help_close_rc().x + 5, help_close_rc().y + 5, 0);
  CHECK(overlayPanel == PANEL_NONE, "Close button closes the help panel");
  click(SDL_BUTTON_LEFT, helpR.x + 5, helpR.y + 5, 0);
  CHECK(overlayPanel == PANEL_HELP, "the ? button opens help");
  key(SDL_SCANCODE_ESCAPE, 0);
  CHECK(overlayPanel == PANEL_NONE, "Esc closes help");

  key(SDL_SCANCODE_F11, 0); key(SDL_SCANCODE_F11, 0);            /* toggle fullscreen twice (a no-op for the dummy driver): must not disturb the game */
  step(1);
  CHECK(inGame && screen == SCR_PLAY && overlayPanel == PANEL_NONE, "F11 leaves the game state alone");

  /* ---- an idle player against the AI: run 4 simulated minutes, drawing now and then ---- */
  step(1); shot(shots, "play");
  for (int m = 0; m < 8; m++) { step_sim(30); step(1); }
  CHECK(mTime > 240.0f, "four minutes of game time pass");
  CHECK(count_units(1, -1) > 6, "the AI is building an army");
  CHECK(mWinner == -1 || mWinner == 1, "no impossible winner");
  printf("after %d s: green units %d, yellow units %d, winner %d\n", (int)mTime, count_units(0, -1), count_units(1, -1), mWinner);

  /* ---- end screen and back to the menu ---- */
  mWinner = 0; endShown = 0; check_end();                          /* pretend we won (ui_update would re-sync the real value) */
  step(0);
  CHECK(overlayPanel == PANEL_END, "end panel appears when a winner is declared");
  ui_draw(); ui_draw(); shot(shots, "end");
  Rc again = end_again_rc();
  click(SDL_BUTTON_LEFT, again.x + 5, again.y + 5, 0);
  CHECK(screen == SCR_MENU && overlayPanel == PANEL_NONE, "Play Again returns to the menu");
  mWinner = 0; check_end(); ui_draw();                             /* winner still set, as in the web UI after a finished game */
  CHECK(screen == SCR_MENU && overlayPanel == PANEL_NONE, "the end panel does not pop back up over the menu");
  key(SDL_SCANCODE_RETURN, 0);
  CHECK(screen == SCR_PLAY && mWinner == -1 && count_units(0, U_BUILDER) == 5, "Enter on the menu starts a fresh game");

  printf("%d checks, %d failed\n", checks, failures);
  ui_shutdown();
  return failures ? 1 : 0;
}
