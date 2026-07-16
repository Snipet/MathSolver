#!/bin/sh
# Compile and run the MathSolver round-trip fuzz harness.
#
# Builds into $TMPDIR (not the pipeline-owned build/ directory) and passes
# every argument through to the harness:
#
#   tools/run_fuzz.sh                    # seed=42 count=5000, full round-trip
#   tools/run_fuzz.sh 7 20000            # seed=7  count=20000
#   tools/run_fuzz.sh --gen-only         # generator + parser only
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTDIR="${TMPDIR:-/tmp}"
BIN="$OUTDIR/mathsolver_fuzz_roundtrip"

clang++ -std=c++23 -O1 -I "$ROOT/include" \
    "$ROOT/src/rational.cpp" \
    "$ROOT/src/expr.cpp" \
    "$ROOT/src/parser.cpp" \
    "$ROOT/src/printer.cpp" \
    "$ROOT/src/evaluator.cpp" \
    "$ROOT/tools/fuzz_roundtrip.cpp" \
    -o "$BIN"

exec "$BIN" "$@"
