/* Shared front-end state: window metrics, camera, selection, toasts, sound.
 * Counterpart of the top-level `let` declarations, coordinate helpers and sfx() in js/ui.js. */
#ifndef APP_H
#define APP_H

#include "gfx.h"
#include "model.h"

#define TOP_H 40.0f
#define BOTTOM_H 168.0f
#define MM_W 260.0f
#define MM_H 130.0f
#define MM_K (MM_W / ((MAP_W + MAP_H) * 32.0f))

static float W, H, dpr = 1.0f, VX, VY;            /* logical window size (points), pixel ratio, world-view centre */
static float camX, camY, zoom = 1.0f;
static float gNow;                                  /* seconds since start, for animations */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float isoX(float x, float y) { return (x - y) * 32.0f; }
static float isoY(float x, float y) { return (x + y) * 16.0f; }
static void w2s(float x, float y, float *sx, float *sy) { *sx = (isoX(x, y) - camX) * zoom + VX; *sy = (isoY(x, y) - camY) * zoom + VY; }
static void s2w(float sx, float sy, float *wx, float *wy) {
  float a = ((sx - VX) / zoom + camX) / 32.0f, b = ((sy - VY) / zoom + camY) / 16.0f;
  *wx = (a + b) / 2; *wy = (b - a) / 2;
}
static void clampCam(void) {
  camX = clampf(camX, -MAP_H * 32.0f, MAP_W * 32.0f);
  camY = clampf(camY, 0, (MAP_W + MAP_H) * 16.0f);
}
static void centerCam(float x, float y) { camX = isoX(x, y); camY = isoY(x, y); clampCam(); }
static void layout_view(void) { VX = W / 2; VY = TOP_H + (H - TOP_H - BOTTOM_H) / 2; }

/* ------------------------------ game / UI state ------------------------------ */

enum { SCR_MENU = 0, SCR_PLAY };
enum { PANEL_NONE = 0, PANEL_HELP, PANEL_END };

static int screen = SCR_MENU, overlayPanel = PANEL_NONE;   /* overlayPanel: help / end-of-game panel over the play screen */
static int inGame;                                  /* a game has been started */
static int diffChoice = 1;
static const char *DIFF_NAME[3] = { "Easy", "Normal", "Hard" };

#define MAXSEL 700
static int sel[MAXSEL], nSel;
static int hoverId;                      /* entity under the mouse (0 = none), set by ui_update, drawn by render_world */
static int placingOn, placeType, placeX, placeY, placeValid;
static int cmdAttackMove;
static int paused, speedMul = 1;
static int idleIdx;
static int groups[10][MAXSEL / 4], groupN[10];
static unsigned lastGroupTick; static int lastGroupN;

typedef struct { float x, y, t; unsigned color; } Fx;
static Fx clickFx[16]; static int nClickFx;
static Fx pings[8];    static int nPings;

typedef struct { char text[96]; int alert; float age; } Toast;
static Toast toasts[4]; static int nToasts;

static void toast(int alert, const char *fmt, ...) {
  if (nToasts == 4) { memmove(toasts, toasts + 1, 3 * sizeof(Toast)); nToasts = 3; }
  Toast *t = &toasts[nToasts++];
  va_list ap; va_start(ap, fmt); vsnprintf(t->text, sizeof t->text, fmt, ap); va_end(ap);
  t->alert = alert; t->age = 0;
}
static void add_fx(float x, float y, unsigned color) {
  if (nClickFx == 16) { memmove(clickFx, clickFx + 1, 15 * sizeof(Fx)); nClickFx = 15; }
  clickFx[nClickFx++] = (Fx){ x, y, 0, color };
}

/* ---------------------------------- sound ----------------------------------- */
/* Tiny synth: a few decaying oscillators, the same tones js/ui.js plays through WebAudio. Optional: if no audio
 * device can be opened the game simply runs silent. */

typedef struct { float f, vol, gain, k, phase, delay, left; int wave, on; } Voice;
static Voice voices[24];
static SDL_AudioDeviceID audioDev;
static int audioRate = 44100, muted;
enum { W_SQUARE, W_TRI, W_SAW };

static void audio_cb(void *ud, Uint8 *stream, int len) {
  (void)ud;
  float *out = (float *)stream;
  int n = len / (int)sizeof(float);
  memset(out, 0, (size_t)len);
  for (int v = 0; v < 24; v++) {
    Voice *p = &voices[v];
    if (!p->on) continue;
    for (int i = 0; i < n; i++) {
      if (p->delay > 0) { p->delay -= 1.0f / audioRate; continue; }
      if (p->left <= 0) { p->on = 0; break; }
      p->phase += p->f / audioRate; if (p->phase >= 1) p->phase -= 1;
      float s = p->wave == W_SQUARE ? (p->phase < 0.5f ? 1.f : -1.f) : p->wave == W_TRI ? 4 * fabsf(p->phase - 0.5f) - 1 : 2 * p->phase - 1;
      out[i] += s * p->gain;
      p->gain *= p->k;
      p->left -= 1.0f / audioRate;
    }
  }
}
static void audio_init(void) {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024; want.callback = audio_cb;
  audioDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!audioDev) return;
  audioRate = have.freq;
  SDL_PauseAudioDevice(audioDev, 0);
}
static void tone(float f, float dur, int wave, float vol, float delay) {
  if (!audioDev) return;
  SDL_LockAudioDevice(audioDev);
  for (int i = 0; i < 24; i++) if (!voices[i].on) {
    Voice *p = &voices[i];
    p->f = f; p->vol = vol; p->gain = vol; p->wave = wave; p->phase = 0; p->delay = delay; p->left = dur;
    p->k = expf(logf(0.0001f / vol) / (dur * audioRate));
    p->on = 1;
    break;
  }
  SDL_UnlockAudioDevice(audioDev);
}
enum { SFX_SELECT, SFX_ORDER, SFX_ERR, SFX_BUILD, SFX_ALERT, SFX_PLACE };
static void sfx(int kind) {
  if (muted) return;
  switch (kind) {
    case SFX_SELECT: tone(620, 0.06f, W_TRI, 0.05f, 0); break;
    case SFX_ORDER: tone(420, 0.07f, W_TRI, 0.05f, 0); tone(560, 0.07f, W_TRI, 0.04f, 0.05f); break;
    case SFX_ERR: tone(150, 0.18f, W_SAW, 0.05f, 0); break;
    case SFX_BUILD: tone(520, 0.12f, W_TRI, 0.06f, 0); tone(660, 0.12f, W_TRI, 0.06f, 0.1f); tone(780, 0.2f, W_TRI, 0.06f, 0.2f); break;
    case SFX_ALERT: tone(330, 0.2f, W_SAW, 0.06f, 0); tone(250, 0.3f, W_SAW, 0.06f, 0.2f); break;
    case SFX_PLACE: tone(240, 0.1f, W_SQUARE, 0.05f, 0); break;
  }
}

/* ------------------------------- small helpers ------------------------------- */

static const char *ERROR_TEXT[13] = {
  "", "Not ready", "Cannot train that here", "Queue is full", "Need more houses", "Population limit reached",
  "Not enough food", "Not enough wood", "Not enough gold", "Not enough stone",
  "Already fully researched", "Cannot build here", "Ports must be built on the lake shore",
};
static const char *error_text(int code) { return code > 0 && code <= 12 ? ERROR_TEXT[code] : "Cannot do that"; }
static const char *RES_NAME[4] = { "food", "wood", "gold", "stone" };
static const char RES_LETTER[4] = { 'F', 'W', 'G', 'S' };
static const unsigned RES_COLOR[4] = { 0xe3cf5a, 0xb07a3e, 0xffd23a, 0xaeaeb8 };

/* index of the first resource the team cannot pay for, or -1 */
static int missing_res(const float *res, const int *cost) {
  for (int k = 0; k < 4; k++) if (cost[k] > 0 && res[k] < cost[k]) return k;
  return -1;
}

static int sel_has(int id) { for (int i = 0; i < nSel; i++) if (sel[i] == id) return 1; return 0; }
static int sel_kind(int i) { return idKind[sel[i]]; }

/* selection entities may disappear; keep only live ids */
static void sel_prune(void) {
  int n = 0;
  for (int i = 0; i < nSel; i++) if (sel[i] > 0 && sel[i] < SIM_MAXID && idKind[sel[i]]) sel[n++] = sel[i];
  nSel = n;
}

static int is_worker_u(const FUnit *u) { return UDEF[u->type].cls == CLS_WORKER; }

/* ids of my (team 0) units in the selection */
static int mine_ids(int *out) {
  int n = 0;
  for (int i = 0; i < nSel; i++) { const FUnit *u = get_unit(sel[i]); if (u && u->team == 0) out[n++] = u->id; }
  return n;
}

/* send a list of ids to the core through its shared id buffer; returns how many fit */
static int push_ids(const int *ids, int n) {
  int *buf = g_idbuf(), cap = g_idbuf_len(), m = 0;
  for (int i = 0; i < n && m < cap; i++) buf[m++] = ids[i];
  return m;
}

static int point_in_poly(float px, float py, const float *p, int n) {
  int inside = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float xi = p[2 * i], yi = p[2 * i + 1], xj = p[2 * j], yj = p[2 * j + 1];
    if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}

#endif
