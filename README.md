# Banners of Peakkk

A 2D isometric medieval RTS for one player. You lead the **Green** kingdom against an AI-controlled **Yellow** realm. Gather resources, build a base, raise an army and destroy every Yellow building to win.

## Play

Open `index.html` in a browser. There is no server or build step; the compiled C core is already embedded in `js/wasm_blob.js`, so it works straight from `file://`.

Press **F1** in game for the full controls.

## What's in the game

- **Economy:** builders chop trees, work farms (3 per farm), and mine gold and stone. Resources are carried back to a Town Center or Storehouse, so placing Storehouses near forests and mines matters.
- **Population:** every unit costs pop. Build Houses (+5) to raise the cap, up to 120.
- **Buildings:** Town Center, House, Farm, Storehouse, Barracks, Archery Range, Stable, Blacksmith, Port (lake shore only), Watch Tower.
- **Units:** Builder, Swordsman, Spearman, Archer, Cavalry and Warship (built at the Port, fights on the central lake).
- **Counters:** swordsmen beat spearmen, spearmen beat cavalry, cavalry beat archers, archers beat swordsmen.
- **Upgrades:** the Blacksmith researches weapon and armor levels.
- **Enemy AI:** three difficulties (Easy, Normal, Hard). It builds an economy, expands its base, researches upgrades and sends growing attack waves.

## How it's built

The simulation (map generation, A* pathfinding, units, buildings, economy, combat and the enemy AI) is written in **C** (`c/game.c`) and compiled to **WebAssembly**. The browser side (`js/ui.js`) only draws the isometric view, the HUD and handles input.

```
c/game.c          the whole simulation, freestanding C99 (no libc)
c/defs.h          generated from js/defs.js by tools/gen_defs.js
js/defs.js        unit / building / tech data tables (single source of truth)
js/game_wasm.js   JS bridge: loads the wasm and exposes the game state to the UI
js/wasm_blob.js   the compiled game.wasm, embedded as base64 (generated)
js/ui.js          isometric renderer, minimap, HUD, input
tests/sim_test.c  native C tests
sdl/              native SDL2 front end (see below): ui.h input + HUD, render.h world drawing, gfx.h 2D batch layer, main.c
tests/sdl_smoke.c headless smoke test of the SDL2 front end
```

## Building and testing

You only need this if you change the C code or the data tables.

Requirements: `node`, `clang` with the wasm32 target and `wasm-ld`, and `gcc` (or clang) for the native tests.

```
make          # regenerate c/defs.h, build build/game.wasm, embed it in js/wasm_blob.js
make test     # build and run the native C tests
make clean
```

The native tests include the real `c/game.c`, so they test exactly the code that ships. They check map generation, pathfinding between the bases, gathering and farming, combat counters, and full AI-vs-AI games on Normal and Hard, plus an idle player against the AI on every difficulty.

To change a unit's stats or a building's cost, edit `js/defs.js` and run `make`.

## Balance testing

`tests/bot_player.c` is a scripted player (economy, production, upgrades, defence, attack waves) that plays team 0 against the AI headless, using only the public `g_*` API. It is there to measure difficulty instead of guessing at it:

```
make bot BOT_ARGS="30 45 both all"     # seeds, max minutes, strategy (boom|rush|both), difficulty (0|1|2|all)
BOT_TRACE=1 ./build/bot_player 1 15 boom 1   # print the bot's economy every 20 s of game time
```

Win rates of the bot's sensible `boom` strategy over 30 maps (`rush` is a deliberate all-in at 12 soldiers):

| AI difficulty | boom | rush |
|---|---|---|
| Easy | 87% | 87% |
| Normal | 47% | 27% |
| Hard | 17% | 27% |

The bot is a mediocre player, so read these as a curve, not as what a person would score. Hard used to win against the bot 0 times in 30 (its +300 starting resources, x1.25 gathering and 33% faster build order were too much); it is now beatable but still clearly harder than Normal. If you retune `DIFFS` in `js/defs.js`, re-run the sweep: a first guess of "delay the AI's first attack" made Normal *harder* (the AI just built a bigger economy), which is why the numbers are worth checking.

## Native SDL2 front end

The same C simulation also runs as a desktop game, with the browser UI replaced by an SDL2 one (`sdl/`). It links `c/game.c` as a separate object and talks to it through the same `g_*` functions the WebAssembly build exports (declared in `sdl/sim.h`), so both front ends play identically.

Requirements: SDL2 >= 2.0.18 with development files (`libsdl2-dev` on Debian/Ubuntu) and a C compiler.

```
make sdl            # build build/peakkk_sdl
make run-sdl        # build and start it
make test-sdl       # headless smoke test: injects mouse / keyboard events into the real UI (no display needed)
make fuzz-sdl       # random-input fuzz of the UI under ASan + UBSan (./build/sdl_fuzz [events] [seed])
./build/peakkk_sdl --start --difficulty hard --seed 7
```

Controls are the same as in the browser (press **F1**), except that the HUD uses a built-in ASCII bitmap font, so units, buildings and commands are labelled with letters and short names instead of emoji. Sound uses the same tiny synth as the web version; if no audio device can be opened the game just runs silent. `--frames N --shot out.bmp` runs N frames at a fixed timestep and saves a screenshot, which is handy for CI.

`sdl/gamedata.h` (names, descriptions, team colours) is generated from `js/defs.js` by `tools/gen_sdl_data.js`; `make` keeps it up to date.
