# Docker dev environment (web + WASM)

MathSolver's web app embeds the C++23 engine compiled to WebAssembly, which
normally means installing Emscripten, CMake, and Node.js on your machine. This
containerized environment bundles that whole toolchain so you can develop the
**website** and **WASM** parts with nothing on the host but Docker.

The image builds on the official Emscripten SDK image, pinned to the same
version CI uses (`emscripten/emsdk:6.0.3`). That base ships everything the
web/WASM loop needs — `emcc`/`emcmake`, CMake, Python, git, and Node.js/npm
(Node 22.x) — so a build here matches the toolchain on GitHub Actions.

> **Scope.** This environment covers the web/WASM workflow. The native C++ test
> suite (GCC 14 + Catch2 via `ctest`) is not included — see the root README for
> running that directly on a host.
>
> **Node version.** The image uses the Node bundled with emsdk (22.x); CI's web
> job uses Node 24. Both build the app identically (Vite 6 / Svelte 5 support
> 20/22/24), so the produced `web/dist` is the same.

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/) with the Compose plugin
  (`docker compose version` ≥ v2). Nothing else.

## Quick start

```sh
docker compose up dev
```

That's it. On the first run this:

1. builds the image (Emscripten 6.0.3 + Node/CMake/Python from the base);
2. compiles the engine to WASM (`tools/build_wasm.sh`) and stages
   `mathsolver.js`/`mathsolver.wasm` into `web/src/lib/wasm/`;
3. installs the web dependencies (`npm ci`);
4. starts the Vite dev server.

Open **http://localhost:5173**. Edits to files under `web/` hot-reload as usual
— the repository is bind-mounted into the container, so you edit with your
normal tools on the host.

Stop with `Ctrl-C`, or `docker compose down` to also remove the container.

## Interactive shell / one-off commands

For anything other than the dev server, open a shell in the same environment:

```sh
docker compose run --rm dev bash
```

Inside, the full toolchain is on `PATH`:

```sh
tools/build_wasm.sh                 # recompile the engine to WASM
node tools/wasm_smoke.mjs           # 27-check Node smoke battery
node tools/wasm_acceptance.mjs      # WASM acceptance battery

cd web
npm run dev -- --host 0.0.0.0       # dev server (use --host to reach it)
npm run check                       # svelte-check type-check
npm run build                       # production build -> web/dist/
npm run preview -- --host 0.0.0.0   # serve the production build
```

You can also run a single command without an interactive shell:

```sh
docker compose run --rm dev bash -lc "cd web && npm run build"
```

## Rebuilding after engine (C++) changes

The staged WASM module is only rebuilt automatically when it's missing. After
changing the C++ engine (`src/`, `include/`, `wasm/bindings.cpp`), rebuild it:

```sh
docker compose run --rm dev tools/build_wasm.sh
# or force the entrypoint to rebuild on next start:
FORCE_WASM=1 docker compose up dev
```

The dev server picks up the new `web/src/lib/wasm/*` automatically.

## Browser end-to-end tests (optional)

`tools/web_session_test.mjs` and `tools/web_vars_test.mjs` drive the built app
in a real browser via `puppeteer-core`, and so need a Chrome/Chromium binary
pointed to by the `CHROME` environment variable. A browser is **not** bundled
in the default image — a headless Chromium that works across CPU architectures
is awkward to ship on the Ubuntu base (snap-only `chromium`, no arm64 Chrome).

To run these tests, provide a browser and point `CHROME` at it, e.g. install
Google Chrome into a throwaway container (amd64) after building the web app:

```sh
docker compose run --rm dev bash -lc '
  apt-get update && apt-get install -y wget &&
  wget -q https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb &&
  apt-get install -y ./google-chrome-stable_current_amd64.deb &&
  cd web && npm run build && cd .. &&
  CHROME=/usr/bin/google-chrome-stable node tools/web_session_test.mjs
'
```

The core develop-the-website loop (dev server, `build`, `check`) and the Node
WASM batteries (`wasm_smoke`, `wasm_acceptance`) need no browser.

## How it fits together

| Piece | Role |
|-------|------|
| `docker/Dockerfile.dev` | Thin toolchain image over `emscripten/emsdk:6.0.3` (Emscripten + Node + CMake/Python/git). |
| `docker/dev-entrypoint.sh` | Activates emsdk, then bootstraps the WASM build + `npm ci` (idempotent). |
| `compose.yaml` | `dev` service: bind-mounts the repo, forwards port 5173, and keeps `node_modules`/`build-wasm` in named volumes. |

The image contains only the toolchain — your source lives in the bind mount, so
code changes never require an image rebuild. `web/node_modules` and
`build-wasm/` are kept in Docker-managed named volumes so container-built,
platform-specific artifacts don't collide with the host filesystem.

### Environment knobs

| Variable | Effect |
|----------|--------|
| `FORCE_WASM=1` | Rebuild the WASM module on entrypoint start even if staged. |
| `FORCE_NPM=1` | Re-run `npm ci` on entrypoint start even if `node_modules` exists. |
| `SKIP_BOOTSTRAP=1` | Skip both bootstrap steps (straight to the command). |

Example: `docker compose run --rm -e SKIP_BOOTSTRAP=1 dev bash`.

## Keeping the Emscripten version in sync

`docker/Dockerfile.dev` pins `emscripten/emsdk:6.0.3`. If you bump
`EMSDK_VERSION` in `.github/workflows/ci.yml` / `deploy.yml`, update the
`FROM` line here to match and rebuild (`docker compose build dev`).
