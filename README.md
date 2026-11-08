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
