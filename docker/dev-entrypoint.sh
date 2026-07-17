#!/usr/bin/env bash
#
# Container entrypoint for the MathSolver web/WASM dev environment.
#
# Ensures the emsdk toolchain env is active, then bootstraps the project so a
# fresh checkout is ready to develop: the WASM engine is compiled and staged
# into web/src/lib/wasm/ (that directory is gitignored, so it is empty on a
# clean clone) and the web dependencies are installed. Both steps are
# idempotent — they are skipped when their outputs already exist.
#
# Bootstrapping is skipped entirely when SKIP_BOOTSTRAP=1, and either step can
# be forced to re-run with FORCE_WASM=1 / FORCE_NPM=1.
set -euo pipefail

# Make sure emcc/emcmake/node are on PATH. The base image sets these via ENV
# already; sourcing emsdk_env.sh is belt-and-suspenders if the entrypoint is
# overridden in a way that skips that.
if [ -f /emsdk/emsdk_env.sh ]; then
  # shellcheck disable=SC1091
  . /emsdk/emsdk_env.sh >/dev/null 2>&1 || true
fi

cd /workspace

if [ "${SKIP_BOOTSTRAP:-0}" != "1" ]; then
  if [ ! -f web/src/lib/wasm/mathsolver.js ] || [ "${FORCE_WASM:-0}" = "1" ]; then
    echo ">> Building the WASM engine (tools/build_wasm.sh)..."
    bash tools/build_wasm.sh
  else
    echo ">> WASM module already staged (set FORCE_WASM=1 to rebuild)."
  fi

  if [ ! -x web/node_modules/.bin/vite ] || [ "${FORCE_NPM:-0}" = "1" ]; then
    echo ">> Installing web dependencies (npm ci)..."
    ( cd web && npm ci )
  else
    echo ">> Web dependencies already installed (set FORCE_NPM=1 to reinstall)."
  fi
fi

# If no command was given, drop into a shell.
if [ "$#" -eq 0 ]; then
  set -- bash
fi

exec "$@"
