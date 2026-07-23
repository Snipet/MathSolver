# MathSolver — Ink terminal app

An interactive terminal UI for the MathSolver engine, built with
[Ink](https://github.com/vadimdemedes/ink) (React for the command line).

This is a **second, independent frontend** alongside the classic C++
REPL/one-shot CLI (`apps/main.cpp`). Both talk to the exact same engine; the
classic CLI is unchanged and remains the reference. This app is an experiment
that may grow into a richer terminal experience over time.

```
» factor x^2 - 5x + 6
(x - 3)*(x - 2)

» solve x^2 = 4
x = -2
x = 2
method: quadratic formula

» integrate x*sin(x)
-x*cos(x) + sin(x) + C
method: integration by parts
```

## Architecture

The MathSolver core is C++. It already compiles to a Node-compatible
WebAssembly ES module (`wasm/bindings.cpp` → `mathsolver.js` + `mathsolver.wasm`)
that exposes a JSON API — the same module the Svelte web app in `web/` uses.
This app loads that module in-process and renders results with Ink:

```
   terminal (Ink/React)
        │  src/ui/App.tsx          interactive REPL, live "as parsed" preview
        │  src/cli.tsx             interactive / one-shot / piped-batch routing
        ▼
   src/core/                       pure, engine-agnostic logic
        │  intent.ts               parseLine() — REPL grammar (mirrors main.cpp)
        │  execute.ts              variable inference + dispatch to the engine
        │  format.ts               render results (mirrors main.cpp print_*)
        │  caret.ts                parse-error caret diagnostics
        ▼
   src/engine/                     the WASM bridge
        │  load.ts                 locate + instantiate the module
        │  engine.ts               typed async facade over the JSON API
        ▼
   mathsolver.js + mathsolver.wasm (built by tools/build_wasm.sh)
```

Keeping `src/core` free of any engine or I/O dependency is what makes the
grammar, formatting, and diagnostics unit-testable without a WASM build (the
tests inject a stub engine).

## Prerequisites

- Node 20+.
- The WASM engine, built once (needs [Emscripten](https://emscripten.org)):

  ```sh
  bash ../../tools/build_wasm.sh   # from the repo root: bash tools/build_wasm.sh
  ```

  This stages `mathsolver.js` + `mathsolver.wasm` into `apps/ink/wasm/`. At
  runtime the loader also checks `../../build-wasm` and `../../web/src/lib/wasm`,
  and honors `MATHSOLVER_WASM_DIR`.

## Usage

```sh
npm install
npm run build

# interactive REPL
node dist/cli.js

# one-shot (quote the whole line; use commas for extra arguments)
node dist/cli.js "factor x^2 - 5x + 6"
node dist/cli.js --latex "diff sin(x^2), x"

# piped batch (one line per input)
printf 'simplify 2x+3x\nsolve x^2 = 4\n' | node dist/cli.js
```

During development, `npm run dev` runs the TypeScript entry directly via `tsx`.

In the REPL, type `help` for the command list, `clear` to clear the screen, and
`quit` (or `exit`, or Ctrl-C) to leave. The grammar mirrors the classic REPL:
type a bare expression to simplify it, a bare equation to solve it, or a command
(`solve`, `diff`, `integrate`, `limit`, `dsolve`, `sum`, `fit`, `grad`, `gcd`, …)
with comma-separated arguments.

## Commands

- `build` — compile `src/` to `dist/`.
- `dev` — run the app from source with `tsx`.
- `typecheck` — `tsc --noEmit` over `src/`.
- `test` — grammar / formatting / caret / UI unit tests (stub engine, no WASM).
- `e2e` — smoke test the built app against the real WASM engine.

## Scope and known gaps

This first cut deliberately leaves out session variables — the classic REPL's
`name := value` assignments plus `vars` / `unset` — because that state lives in
`apps/main.cpp`, not in the engine. Those inputs print a friendly note pointing
back to the classic REPL. Everything else in the REPL grammar is supported.
