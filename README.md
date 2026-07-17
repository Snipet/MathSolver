# MathSolver

A from-scratch computer-algebra system (CAS) in C++23: parse LaTeX-style math,
simplify and transform expressions, differentiate and integrate symbolically,
and solve equations — exactly where possible, numerically otherwise. No dependencies
beyond the standard library (Catch2 is fetched automatically for tests only).

## Build

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build          # run the test suite
```

Requires CMake ≥ 3.24 and a C++23 compiler (tested with Apple clang 21).

## Usage

One-shot commands:

```console
$ mathsolver simplify "2x + 3x"
5*x
$ mathsolver expand "(x+1)^3"
x^3 + 3*x^2 + 3*x + 1
$ mathsolver factor "x^2 - 5x + 6"
(x - 3)*(x - 2)
$ mathsolver diff "\sin{x^2}" x
2*x*cos(x^2)
$ mathsolver eval "x^2 + y" x=3 y=0.5
9.5
$ mathsolver latex "sqrt(x)/2"
\frac{\sqrt{x}}{2}
$ mathsolver solve "x^2 = 4" x
x = -2
x = 2
method: quadratic formula
$ mathsolver solve "\cos{x} = x" x
x ≈ 0.739085133215161
method: numeric (Newton/bisection)
warning: numeric search covered [-100, 100]; roots outside this interval are not reported
```

Integration — rule-based symbolic antiderivatives (`+ C` is printed for
you), and definite integrals that use the fundamental theorem of calculus
when an antiderivative is found and verified, falling back to adaptive
Simpson quadrature otherwise:

```console
$ mathsolver integrate "x*sin(x)"
-x*cos(x) + sin(x) + C
method: integration by parts + linearity + table
$ mathsolver integrate "1/(x^2-1)"
-ln(abs(x + 1))/2 + ln(abs(x - 1))/2 + C
method: partial fractions
$ mathsolver integrate "sin(x)" --from 0 --to pi
value = 2
method: FTC
$ mathsolver integrate "e^(-x^2)" --from 0 --to 1
value ≈ 0.746824132812499
method: numeric (adaptive Simpson)
$ mathsolver integrate "e^(x^2)"
unable to integrate
warning: no applicable integration rule
```

An integral the rules cannot handle is reported honestly (`unable to
integrate`) rather than guessed: every candidate antiderivative is verified
by differentiating it back before it is printed.

Systems of equations — separate the equations with `;` inside one argument
(the variables after it are optional when they can be inferred):

```console
$ mathsolver solve "x + y = 3; x - y = 1" x y
x = 2
y = 1
method: gaussian elimination
$ mathsolver solve "x + y = 3; 2x + 2y = 6" x y
x = -y + 3
free: y
method: gaussian elimination
$ mathsolver solve "x + y = 1; x + y = 2" x y
no solution (inconsistent system)
method: gaussian elimination
```

The `solve`/`diff`/`integrate` variable is optional when the input has
exactly one free symbol. `--latex` switches any command's output to LaTeX,
`solve ... --range LO HI` sets the numeric search interval, and
`mathsolver --help` shows the full synopsis. The `latex` command converts
without simplifying. Exit codes: 0 success, 1 parse/math error, 2 usage
error; error diagnostics (with a caret pointing into the input) go to stderr.

Interactive REPL (like `python` with no arguments):

```console
$ mathsolver
MathSolver 0.5.0 — type "help" for commands, "quit" to exit
>>> 2x + 3x
5*x
>>> solve x^2 - 4 = 0, x
x = -2
x = 2
method: quadratic formula
>>> diff \frac{\sin{x}}{x}, x
cos(x)/x - sin(x)/x^2
>>> quit
```

A bare expression is simplified; a bare equation is solved for its single
free symbol; `help` lists the commands (`solve`, `diff`, `integrate`,
`eval`, `subs`, `collect`, `expand`, `factor`, `latex`, `debug` — e.g.
`integrate sin(x), x, 0, pi`); errors keep the session alive.

The REPL (and the web app) also keeps a session environment of **variable
assignments**, entered as `name := value` and applied lazily to every
computing command (`latex`/`debug` show input as typed; a `solve`/`diff`
variable is excluded with a warning):

```console
>>> a := 2
a := 2
>>> f := g + 1
f := g + 1
>>> g := x^2
g := x^2
>>> f + a
x^2 + 3
>>> vars
a := 2
f := g + 1
g := x^2
>>> unset a
>>> clear
cleared 2 assignment(s)
```

Values may be expressions or equations (`E_1 := x + y = 3`, then
`solve E_1; x - y = 1, x, y`); cycles are rejected at definition time. The
full contract is docs/proposals/variable-assignment.md (condensed in
DESIGN.md §10). One-shot subcommands stay stateless — the composable
equivalent is `mathsolver subs "a*x + 3" a=2`.

Input accepts both LaTeX (`\frac{1}{2}`, `\sin{x}`, `\sqrt[3]{x}`, `\pi`,
`\alpha`) and plain style (`1/2`, `sin(x)`, `pi`) with implicit
multiplication (`2x`, `(x+1)(x-2)`).

## Web app

The same engine, compiled to WebAssembly, powers a static single-page app in
[`web/`](web) (Svelte + Vite). It offers two views, switched from the header:

- **Workbench** — a guided, tabbed UI: simplify/expand/factor, solve equations
  and linear systems, differentiate, integrate (indefinite and definite),
  evaluate numerically, and plot functions (with optional derivative and
  antiderivative overlays).
- **Console** — a Mathematica-style, line-by-line surface where you call the
  engine programmatically instead of switching tabs. Enter a bare expression to
  simplify it or an equation to solve it, or use a verb:

  ```text
  2x + 3x
  solve x^2 = 4, x
  diff sin(x^2), x
  integrate x*sin(x), x        integrate sin(x), x, 0, pi
  eval x^2 + y, x=3, y=0.5      subs a*x + 3, a=2
  factor x^2 - 5x + 6           collect a*x + b*x + c, x
  a := 2                        (bindings apply to later lines; vars/unset/clear)
  ```

  Each entry becomes an In[n]/Out[n] cell; `:=` assignments feed the same
  Variables environment the Workbench uses, and the session persists locally.
  The console fills the viewport with a clickable **Commands** reference
  panel (built-ins plus the live plugin catalog), Tab autocompletion with
  inline usage hints, ↑/↓ history recall, per-cell rerun/edit actions, and
  Ctrl+L to clear.

  The console also dispatches **plugin commands** — compiled-in C++ modules
  for numeric domains the CAS doesn't cover (see
  [docs/PLUGINS.md](docs/PLUGINS.md)). The built-in `dsp` plugin does IIR
  filter design:

  ```text
  dsp.butter lowpass, 4, 1000, 48000     → biquads + magnitude/phase/time
  dsp.cheby1 bandpass, 3, 1, 500, 2000, 48000
  dsp.ellip lowpass, 5, 1, 60, 1000, 48000
  dsp.fir lowpass, 101, 1000, 48000, kaiser, 10
  dsp.freqz 48000, 0.2,0.4,0.2,-0.5,0.3  → response of your own biquads
  sys.tf s+1, s^2+3s+2                   → poles/zeros, margins, Bode, step
  sys.ode y'' + 3y' + 2y = u' + u        → ODE to transfer function
  sys.feedback 1, s(s+1)(s+2), 2         → closed loop under gain-K feedback
  sys.rlocus 1, s^3 + 3s^2 + 2s          → root locus + critical gain
  sys.c2d 1, s+1, 100                    → discretize H(s) to digital biquads
  plot sin(x)/x, -20, 20                 → chart any expression inline
  plugins                                → catalog of compiled-in plugins
  ```

Results render as KaTeX with copyable plain text, a live "as parsed" preview
underlines errors in place, and computation history persists locally. All
computation runs in your browser via WebAssembly — nothing is sent to a
server, and the built site is plain static files with no backend.

Prerequisites: [Emscripten](https://emscripten.org) (`brew install
emscripten`) and Node.js.

```sh
tools/build_wasm.sh        # compile the engine to WASM, stage into web/src/lib/wasm
cd web
npm install
npm run build              # static site in web/dist/
npm run preview            # serve the build locally
```

For development, `npm run dev` starts a hot-reloading dev server (re-run
`tools/build_wasm.sh` after engine changes); `npm run check` type-checks the
Svelte/TypeScript sources, and `node ../tools/wasm_smoke.mjs` exercises every
WASM binding directly.

### Develop in Docker (no local toolchain)

To develop the website and WASM parts without installing Emscripten, CMake, or
Node.js on your machine, use the containerized environment — Docker is the only
prerequisite:

```sh
docker compose up dev      # builds WASM, installs deps, serves http://localhost:5173
```

The repo is bind-mounted, so edits hot-reload as usual. Open a shell in the same
toolchain with `docker compose run --rm dev bash`. See [docs/DOCKER.md](docs/DOCKER.md)
for the full workflow.

## Features

- **Parser** — LaTeX-style grammar with caret-underlined error diagnostics.
  As of v0.4 the input is forgiving: unicode math symbols (`×`, `÷`, `√`,
  `π`, `x²`, `sin(30°)`), scientific notation (`2e-3`), `|x|`
  absolute-value bars, and helpful errors for word-like variables (`speed`
  suggests writing `s*p*e*e*d` instead of silently multiplying letters).
- **Simplifier** — exact rational arithmetic, like-term/factor collection,
  power and exp/ln rules, trig special values and identities; plus `expand`,
  `collect`, and best-effort `factor`.
- **Derivatives** — full symbolic differentiation (chain/product/general
  power rule) over sin/cos/tan, inverse trig, hyperbolics, ln, abs.
- **Integrals** — rule-based symbolic integration (table forms, linearity,
  u-substitution, integration by parts, partial fractions via exact linear
  systems, trig-power identities), every result self-verified by
  differentiating it back; definite integrals by FTC with a
  quadrature cross-check, or adaptive Simpson when no antiderivative is
  found.
- **Solver** — linear and quadratic (exact, symbolic coefficients OK),
  rational-root peeling for higher degrees, isolation through invertible
  layers (`ln(x+1)=2` → `x = e^2 - 1`), and a Newton/bisection numeric
  fallback for the rest (`cos(x) = x`).
- **Linear systems** — Gaussian elimination over exact expression
  arithmetic (`solve "x + y = 3; x - y = 1"`): symbolic parameters as
  coefficients, underdetermined systems with free variables, and
  inconsistency detection.

## Limitations (by design, v0.1)

- Mostly real domain. As of v0.6 the imaginary unit `i` is part of the
  grammar (`i^2` simplifies to `-1`) and polynomial solving returns complex
  conjugate pairs (`x^2 + 2x + 5 = 0` → `x = -1 ± 2i`), but general
  symbolic complex algebra (complex function evaluation, `abs`/`arg` of
  complex values, numeric evaluation containing `i`) is not supported.
- Variables are single letters (optionally subscripted: `x_1`) or greek
  names; `e` always means Euler's number and `i` the imaginary unit.
- Exact arithmetic is 64-bit rational, overflow-checked (throws rather than
  silently wrapping).
- Formal cancellations such as `x/x → 1` assume nonzero denominators.
- Numeric root search only covers its interval (default `[-100, 100]`,
  override with `--range`).
- Scientific-notation literals are exact and must fit 64-bit rationals
  (`1e300` is a clean parse error); `2e` with no digits after it is still
  `2·e` with Euler's number.

## Architecture

See [DESIGN.md](DESIGN.md) for the module map, the canonical expression form,
the full grammar, the simplification rule inventory, and the solver strategy.
