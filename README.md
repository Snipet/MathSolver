# MathSolver

A from-scratch computer-algebra system (CAS) in C++23: parse LaTeX-style math,
simplify and transform expressions, differentiate symbolically, and solve
equations — exactly where possible, numerically otherwise. No dependencies
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

The `solve`/`diff` variable is optional when the input has exactly one free
symbol. `--latex` switches any command's output to LaTeX,
`solve ... --range LO HI` sets the numeric search interval, and
`mathsolver --help` shows the full synopsis. The `latex` command converts
without simplifying. Exit codes: 0 success, 1 parse/math error, 2 usage
error; error diagnostics (with a caret pointing into the input) go to stderr.

Interactive REPL (like `python` with no arguments):

```console
$ mathsolver
MathSolver 0.1.0 — type "help" for commands, "quit" to exit
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
free symbol; `help` lists the commands (`solve`, `diff`, `eval`, `expand`,
`factor`, `latex`, `debug`); errors keep the session alive.

Input accepts both LaTeX (`\frac{1}{2}`, `\sin{x}`, `\sqrt[3]{x}`, `\pi`,
`\alpha`) and plain style (`1/2`, `sin(x)`, `pi`) with implicit
multiplication (`2x`, `(x+1)(x-2)`).

## Features

- **Parser** — LaTeX-style grammar with caret-underlined error diagnostics.
- **Simplifier** — exact rational arithmetic, like-term/factor collection,
  power and exp/ln rules, trig special values and identities; plus `expand`,
  `collect`, and best-effort `factor`.
- **Derivatives** — full symbolic differentiation (chain/product/general
  power rule) over sin/cos/tan, inverse trig, hyperbolics, ln, abs.
- **Solver** — linear and quadratic (exact, symbolic coefficients OK),
  rational-root peeling for higher degrees, isolation through invertible
  layers (`ln(x+1)=2` → `x = e^2 - 1`), and a Newton/bisection numeric
  fallback for the rest (`cos(x) = x`).
- **Linear systems** — Gaussian elimination over exact expression
  arithmetic (`solve "x + y = 3; x - y = 1"`): symbolic parameters as
  coefficients, underdetermined systems with free variables, and
  inconsistency detection.

## Limitations (by design, v0.1)

- Real domain only — no complex results (`x^2 = -1` reports "no real
  solutions").
- Variables are single letters (optionally subscripted: `x_1`) or greek
  names; `e` always means Euler's number.
- Exact arithmetic is 64-bit rational, overflow-checked (throws rather than
  silently wrapping).
- Formal cancellations such as `x/x → 1` assume nonzero denominators.
- Numeric root search only covers its interval (default `[-100, 100]`,
  override with `--range`).
- No scientific-notation literals (`2e3` parses as `2·e·3`).

## Architecture

See [DESIGN.md](DESIGN.md) for the module map, the canonical expression form,
the full grammar, the simplification rule inventory, and the solver strategy.
