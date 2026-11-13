/* Randomised input fuzz for the SDL2 front end: thousands of random mouse / wheel / key events (and window-size
 * changes) against the real simulation, with frames in between. Meant to run under the sanitizers:
 *
 *   make fuzz-sdl                    ASan + UBSan build, default 30000 events, seed 1
 *   ./build/sdl_fuzz [events] [seed]
 *
 * It only asserts "does not crash / trip a sanitizer / corrupt UI state", so any failure is a real bug.
 */
#include "ui.h"

static unsigned long long rs;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }
static float rf(float lo, float hi) { return lo + (hi - lo) * (rnd() % 10001) / 10000.0f; }

static const SDL_Scancode KEYS[] = {
  SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F,
  SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_P, SDL_SCANCODE_M, SDL_SCANCODE_SPACE,
  SDL_SCANCODE_PERIOD, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F1, SDL_SCANCODE_F11, SDL_SCANCODE_RETURN, SDL_SCANCODE_LEFT,
  SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_9, SDL_SCANCODE_TAB };

int main(int argc, char **argv) {
  int events = argc > 1 ? atoi(argv[1]) : 30000;
  rs = 88172645463325252ull ^ ((unsigned long long)(argc > 2 ? atoi(argv[2]) : 1) * 0x9E3779B97F4A7C15ull);
  SDL_setenv("SDL_VIDEODRIVER", "dummy", 0); SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
  if (!ui_init("sdl fuzz", 1280, 800, 1)) { printf("ui_init failed: %s\n", SDL_GetError()); return 1; }
  mapChoice = (int)(rnd() % MAP_COUNT);
  ui_start_game((int)(rnd() % 3), 1234);
  int games = 1;
  for (int n = 0; n < events; n++) {
    SDL_Event e; SDL_zero(e);
    float x = rf(-20, W + 20), y = rf(-20, H + 20);
    unsigned k = rnd() % 100, mod = (rnd() % 4 == 0) ? KMOD_SHIFT : (rnd() % 6 == 0) ? KMOD_CTRL : 0;
    SDL_SetModState((SDL_Keymod)mod);
    if (k < 30) { e.type = SDL_MOUSEMOTION; e.motion.x = (int)x; e.motion.y = (int)y; e.motion.xrel = (int)rf(-30, 30); e.motion.yrel = (int)rf(-30, 30); e.motion.state = (rnd() & 1) ? SDL_BUTTON_LMASK : 0; }
    else if (k < 55) { e.type = (rnd() & 1) ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP; e.button.button = (Uint8)(1 + rnd() % 3); e.button.x = (int)x; e.button.y = (int)y; }
    else if (k < 62) { e.type = SDL_MOUSEWHEEL; e.wheel.y = (int)(rnd() % 3) - 1; }
    else if (k < 97) { e.type = SDL_KEYDOWN; e.key.keysym.scancode = KEYS[rnd() % (sizeof KEYS / sizeof KEYS[0])]; e.key.keysym.mod = (Uint16)mod; }
    else if (k < 98) { e.type = SDL_WINDOWEVENT; e.window.event = SDL_WINDOWEVENT_FOCUS_LOST; }
    else { SDL_SetWindowSize(gWin, 1000 + (int)(rnd() % 700), 680 + (int)(rnd() % 400)); }
    ui_event(&e);
    SDL_SetModState(KMOD_NONE);
    ui_update(rf(0.001f, 0.12f) * (rnd() % 5 == 0 ? 8 : 1));
    if (n % 25 == 0) ui_draw();
    if (screen == SCR_MENU && rnd() % 20 == 0) { mapChoice = (int)(rnd() % MAP_COUNT); ui_start_game((int)(rnd() % 3), (unsigned)rnd()); games++; }
    if (overlayPanel == PANEL_END) { overlayPanel = PANEL_NONE; if (rnd() & 1) screen = SCR_MENU; }   /* like clicking Play Again, or dismissing the panel */
    if (n % 3000 == 2999) screen = SCR_MENU;                                       /* regularly return to the menu so restarts are fuzzed too */
    /* invariants of the UI state */
    if (nSel < 0 || nSel > MAXSEL || nToasts < 0 || nToasts > 4 || nPings < 0 || nPings > 8 || nClickFx < 0 || nClickFx > 16 || zoom < 0.55f - 1e-3f || zoom > 1.6f + 1e-3f) {
      printf("FAIL: UI invariant broken at event %d (nSel=%d toasts=%d pings=%d fx=%d zoom=%g)\n", n, nSel, nToasts, nPings, nClickFx, zoom);
      return 1;
    }
  }
  ui_draw();
  printf("fuzz ok: %d events, %d games, t=%d s, winner %d\n", events, games, (int)mTime, mWinner);
  ui_shutdown();
  return 0;
}
