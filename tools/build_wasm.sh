#!/bin/sh
# Build the MathSolver WASM module and stage it into the web app.
# Requires Emscripten (emcmake/emcc on PATH; e.g. `brew install emscripten`).
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-wasm"
DEST="$ROOT/web/src/lib/wasm"

emcmake cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "$ROOT"
cmake --build "$BUILD" -j

mkdir -p "$DEST"
cp "$BUILD/mathsolver.js" "$BUILD/mathsolver.wasm" "$DEST/"
echo "Staged: $DEST/mathsolver.js + mathsolver.wasm"
