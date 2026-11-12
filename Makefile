# Banners of Peakkk — build helpers.
#   make          regenerate c/defs.h, build game.wasm and embed it in js/wasm_blob.js
#   make test     build + run the native C tests (gcc)
#   make sdl      build the native SDL2 front end: build/peakkk_sdl   (make run-sdl to build and start it)
#   make test-sdl build + run the headless SDL2 front-end smoke test
#   make clean
#
# Needs: node (code generation), clang with the wasm32 target + wasm-ld (WebAssembly build), gcc or clang (tests),
#        SDL2 >= 2.0.18 development files (SDL2 front end; found with sdl2-config / pkg-config).
# Open index.html directly in a browser to play; the committed js/wasm_blob.js already contains the compiled core.

CC_NATIVE ?= gcc
CLANG     ?= clang
CFLAGS    := -std=c99 -O2 -fno-math-errno -Wall -Wextra -Wno-unused-function

SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)
SDL_SRC    := $(wildcard sdl/*.h)

all: wasm

c/defs.h: js/defs.js tools/gen_defs.js
	node tools/gen_defs.js

build/game.wasm: c/game.c c/defs.h
	@mkdir -p build
	$(CLANG) --target=wasm32 -std=c99 -O2 -fno-math-errno -nostdlib -ffreestanding -mbulk-memory \
	  -Wall -Wextra -Wno-unused-function -fuse-ld=lld \
	  -Wl,--no-entry -Wl,--export-dynamic -Wl,-z,stack-size=262144 -Wl,--gc-sections -Wl,--strip-all \
	  -Ic c/game.c -o $@

js/wasm_blob.js: build/game.wasm tools/embed_wasm.js
	node tools/embed_wasm.js build/game.wasm $@

wasm: js/wasm_blob.js

build/sim_test: tests/sim_test.c c/game.c c/defs.h
	@mkdir -p build
	$(CC_NATIVE) $(CFLAGS) -Ic tests/sim_test.c -o $@

test: build/sim_test
	./build/sim_test

# ---- native SDL2 front end ----
# game.c is compiled as its own object: the front end only sees the g_* functions declared in sdl/sim.h,
# the same boundary the WebAssembly build exposes to js/game_wasm.js.
sdl/gamedata.h: js/defs.js tools/gen_sdl_data.js
	node tools/gen_sdl_data.js

build/game_native.o: c/game.c c/defs.h
	@mkdir -p build
	$(CC_NATIVE) $(CFLAGS) -Ic -c c/game.c -o $@

build/peakkk_sdl: sdl/main.c $(SDL_SRC) c/defs.h build/game_native.o
	$(CC_NATIVE) $(CFLAGS) -Isdl -Ic $(SDL_CFLAGS) sdl/main.c build/game_native.o -o $@ $(SDL_LIBS) -lm

sdl: build/peakkk_sdl

run-sdl: sdl
	./build/peakkk_sdl

build/sdl_smoke: tests/sdl_smoke.c $(SDL_SRC) c/defs.h build/game_native.o
	$(CC_NATIVE) $(CFLAGS) -Isdl -Ic $(SDL_CFLAGS) tests/sdl_smoke.c build/game_native.o -o $@ $(SDL_LIBS) -lm

test-sdl: build/sdl_smoke
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/sdl_smoke

# random-input fuzz of the front end under ASan + UBSan (game.c is rebuilt with the sanitizers too)
build/sdl_fuzz: tests/sdl_fuzz.c $(SDL_SRC) c/defs.h c/game.c
	@mkdir -p build
	$(CC_NATIVE) -std=c99 -O1 -g -fno-math-errno -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isdl -Ic $(SDL_CFLAGS) tests/sdl_fuzz.c c/game.c -o $@ $(SDL_LIBS) -lm

fuzz-sdl: build/sdl_fuzz
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ASAN_OPTIONS=detect_leaks=0 ./build/sdl_fuzz

clean:
	rm -rf build

.PHONY: all wasm test sdl run-sdl test-sdl fuzz-sdl clean
