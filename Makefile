# Banners of Peakkk — build helpers.
#   make          regenerate c/defs.h, build game.wasm and embed it in js/wasm_blob.js
#   make test     build + run the native C tests (gcc)
#   make clean
#
# Needs: node (code generation), clang with the wasm32 target + wasm-ld (WebAssembly build), gcc or clang (tests).
# Open index.html directly in a browser to play; the committed js/wasm_blob.js already contains the compiled core.

CC_NATIVE ?= gcc
CLANG     ?= clang
CFLAGS    := -std=c99 -O2 -fno-math-errno -Wall -Wextra -Wno-unused-function

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

clean:
	rm -rf build

.PHONY: all wasm test clean
