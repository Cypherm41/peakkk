/* Banners of Peakkk - SDL2 front end for the C simulation (c/game.c).
 *
 *   make sdl && ./build/peakkk_sdl
 *
 * Options:
 *   --difficulty easy|normal|hard   difficulty of the Yellow AI (default normal)
 *   --map lake|plains|river|ponds|forest   terrain (default lake)
 *   --seed N                        map seed (default: random)
 *   --start                         skip the menu and start a game right away
 *   --size WxH                      initial window size (default 1280x800)
 *   --frames N                      run N frames at a fixed 60 Hz timestep and exit (no real-time pacing)
 *   --shot FILE.bmp                 save a screenshot of the last frame on exit (use with --frames)
 *   --help
 */
#include "ui.h"

static void usage(const char *argv0) {
  printf("usage: %s [--difficulty easy|normal|hard] [--map lake|plains|river|ponds|forest] [--seed N] [--start] [--size WxH] [--frames N] [--shot FILE.bmp]\n", argv0);
}

int main(int argc, char **argv) {
  int diff = 1, start = 0, winW = 1280, winH = 800, maxFrames = 0, haveSeed = 0;
  unsigned seed = 0;
  const char *shot = NULL;
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(argv[0]); return 0; }
    else if (!strcmp(a, "--start")) start = 1;
    else if (!strcmp(a, "--difficulty") && i + 1 < argc) {
      const char *d = argv[++i];
      diff = !strcmp(d, "easy") ? 0 : !strcmp(d, "hard") ? 2 : 1;
      if (strcmp(d, "easy") && strcmp(d, "normal") && strcmp(d, "hard")) { fprintf(stderr, "unknown difficulty '%s'\n", d); return 2; }
    }
    else if (!strcmp(a, "--map") && i + 1 < argc) {
      static const char *keys[MAP_COUNT] = { "lake", "plains", "river", "ponds", "forest" };
      const char *m = argv[++i]; int found = -1;
      for (int k = 0; k < MAP_COUNT; k++) if (!strcmp(m, keys[k])) found = k;
      if (found < 0) { fprintf(stderr, "unknown map '%s'\n", m); return 2; }
      mapChoice = found;
    }
    else if (!strcmp(a, "--seed") && i + 1 < argc) { seed = (unsigned)strtoul(argv[++i], NULL, 10); haveSeed = 1; }
    else if (!strcmp(a, "--size") && i + 1 < argc) { if (sscanf(argv[++i], "%dx%d", &winW, &winH) != 2) { usage(argv[0]); return 2; } }
    else if (!strcmp(a, "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    else if (!strcmp(a, "--shot") && i + 1 < argc) shot = argv[++i];
    else { usage(argv[0]); return 2; }
  }

  if (!ui_init("Banners of Peakkk", winW, winH, 0)) { ui_shutdown(); return 1; }
  diffChoice = diff;
  if (start || maxFrames) ui_start_game(diff, haveSeed ? seed : (unsigned)time(NULL));

  Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
  SDL_RendererInfo info;
  int vsync = SDL_GetRendererInfo(gR, &info) == 0 && (info.flags & SDL_RENDERER_PRESENTVSYNC);
  int frames = 0;
  while (!uiQuit) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) ui_event(&e);
    Uint64 now = SDL_GetPerformanceCounter();
    float dt = maxFrames ? 1.0f / 60.0f : (float)((double)(now - last) / (double)freq);
    last = now;
    if (dt > 0.1f) dt = 0.1f;                       /* never let a stall (window drag, breakpoint) explode the simulation */
    ui_update(dt);
    ui_draw();
    frames++;
    if (maxFrames && frames >= maxFrames) break;
    if (!maxFrames && !vsync) SDL_Delay(8);
  }
  if (shot && !ui_screenshot(shot)) fprintf(stderr, "could not save %s: %s\n", shot, SDL_GetError());
  ui_shutdown();
  return 0;
}
