#!/bin/sh
# Build the MathSolver WASM module and stage it into the frontends that consume
# it: the Svelte web app and the Ink terminal app (apps/ink).
# Requires Emscripten (emcmake/emcc on PATH; e.g. `brew install emscripten`).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-wasm"

emcmake cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "$ROOT"
cmake --build "$BUILD" -j

# The same node/web-compatible ES module drives both frontends.
for DEST in "$ROOT/web/src/lib/wasm" "$ROOT/apps/ink/wasm"; do
  mkdir -p "$DEST"
  cp "$BUILD/mathsolver.js" "$BUILD/mathsolver.wasm" "$DEST/"
  echo "Staged: $DEST/mathsolver.js + mathsolver.wasm"
done
