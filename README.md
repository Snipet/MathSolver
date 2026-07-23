# MathSolver

A from-scratch computer-algebra system (CAS) in C++23: parse LaTeX-style math,
simplify and transform expressions, differentiate and integrate symbolically,
take limits, Laplace transforms and their inverses, solve equations, ODE
initial-value problems, and recurrences, expand partial fractions and Taylor
series, sum series in closed form, and do multivariate vector calculus —
exactly where possible, numerically otherwise. No dependencies beyond the
standard library (Catch2 is fetched automatically for tests only).

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

The function library covers the elementary functions (trig and their
inverses, hyperbolics and their inverses `asinh`/`acosh`/`atanh`,
`exp`/`ln`/`log` with bases, `sec`/`csc`/`cot`), the special functions
`gamma`, `digamma` (`psi`), `erf`, `erfc` — with exact values where closed
forms exist and high-precision numerics elsewhere — and the integer
functions `binomial`, `factorial`, `fib`, `harmonic`:

```console
$ mathsolver simplify "gamma(7/2)"
15*sqrt(pi)/8
$ mathsolver diff "erf(x)" x
2*e^(-x^2)/sqrt(pi)
$ mathsolver integrate "e^(-x^2)" x
sqrt(pi)*erf(x)/2 + C
$ mathsolver simplify "binomial(10, 5) + fib(10) + harmonic(4)"
3709/12
$ mathsolver sum "1/k" k 1 n
sum = harmonic(n)
$ mathsolver seq 0 1 1 2 3 5 8
pattern: linear recurrence of order 2 (Fibonacci)
a(n) = -sqrt(5)*((-sqrt(5) + 1)/2)^n/5 + sqrt(5)*((sqrt(5) + 1)/2)^n/5   (n = 0, 1, 2, ...)
recurrence: a(n+2) = a(n+1) + a(n)
next: 13, 21, 34
```

`seq` recognizes the pattern behind a list of terms — geometric ratios,
vanishing finite differences (arithmetic and polynomial sequences get
closed forms by Newton's forward formula), and linear recurrences of order
2–3 found by an exact solve and verified against every remaining term,
with the closed form recovered through `rsolve`.

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
$ mathsolver integrate "sin(x)/x" --from 1 --to 2
value ≈ 0.659329906435827
method: numeric (adaptive Simpson)
$ mathsolver integrate "e^(x^2)"
unable to integrate
warning: no applicable integration rule
```

An integral the rules cannot handle is reported honestly (`unable to
integrate`) rather than guessed: every candidate antiderivative is verified
by differentiating it back before it is printed.

Laplace transforms — `laplace` maps a time function `f(t)` to `F(s)`, and
`ilaplace` inverts `F(s)` back to `f(t)`. The forward transform composes a
base table (1, `sin`/`cos`/`sinh`/`cosh`) with the s-shift theorem
(`L{e^{at} g} = G(s - a)`) and frequency differentiation (`L{t^n g} =
(-1)^n dⁿ/dsⁿ G`); the inverse matches each partial-fraction term against
linear factors `c/(s - a)^n` and irreducible quadratics (completing the
square into damped `sin`/`cos`):

```console
$ mathsolver laplace "e^(-t) sin(2t)"
2/((s + 1)^2 + 4)
$ mathsolver laplace "t^2 e^(-3t)"
2/(s + 3)^3
$ mathsolver ilaplace "1/(s^2 + 2s + 5)"
e^(-t)*sin(2*t)/2
$ mathsolver ilaplace "(2s + 3)/(s^2 + 4)"
2*cos(2*t) + 3*sin(2*t)/2
```

The time variable defaults to `t` (any name but `s`) and the frequency
variable to `s`; both accept an explicit name as a second argument. Inputs
with no transform rule error rather than returning a wrong answer.

Differential equations — `dsolve` solves linear constant-coefficient
initial-value problems exactly by the Laplace method: transform the
equation (folding the initial conditions in), decompose Y(s) into partial
fractions, and invert. Resonance is handled exactly (the secular `t`-term
appears), omitted initial conditions default to zero with a warning, and
the partial-fraction Y(s) is shown alongside the answer:

```console
$ mathsolver dsolve "y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0"
y(t) = t*e^(-t) + e^(-t)
Y(s) = 1/(s + 1) + 1/(s + 1)^2
method: laplace transform + partial fractions
$ mathsolver dsolve "y'' + y = sin(t), y(0)=0, y'(0)=0"
y(t) = (-t*cos(t) + sin(t))/2
Y(s) = 1/(s^2 + 1)^2
method: laplace transform + partial fractions
```

Equations spelled `y' = f(t, y)` route to first-order methods instead —
variable-coefficient linear (integrating factor), Bernoulli, and separable
equations, with explicit inversion where `solve` can manage it and an
honest implicit relation otherwise. Without an initial condition the
general solution keeps a symbolic `C`:

```console
$ mathsolver dsolve "y' = -2t*y, y(0)=1"
y(t) = e^(-t^2)
method: integrating factor
$ mathsolver dsolve "y' = t/y, y(0)=2"
y(t) = sqrt(-2*(-t^2/2 - 2))
method: separation of variables
```

First-order linear **systems** solve through a single exact linear solve
over rational functions of s — `;`-separated equations share the initial
conditions:

```console
$ mathsolver dsolve "x' = -2x + y; y' = x - 2y, x(0)=1, y(0)=0"
x(t) = e^(-3*t)/2 + e^(-t)/2
y(t) = -e^(-3*t)/2 + e^(-t)/2
method: laplace transform + linear solve + partial fractions
```

Two supporting verbs round out the calculus toolkit. `apart` expands a
rational function into partial fractions over the rationals (linear and
irreducible-quadratic factors, repeated factors, improper inputs divided
out first), and `series` builds Taylor polynomials with exact
coefficients:

```console
$ mathsolver apart "(3x+2)/((x+1)(x+2))"
-1/(x + 1) + 4/(x + 2)
$ mathsolver apart "x^2/(x^2-1)"
1 - 1/(2*(x + 1)) + 1/(2*(x - 1))
$ mathsolver series "sin(x)" x 0 5
x^5/120 - x^3/6 + x
$ mathsolver series "ln(x)" x 1 3
(x - 1)^3/3 - (x - 1)^2/2 + x - 1
```

Limits — exact where the structure allows (substitution guarded by
defined-at-the-point checks, L'Hôpital on 0/0 quotients, rational degree
analysis at infinity), with an honest numeric-extrapolation fallback that
distinguishes signed divergence, two-sided disagreement, and "unable to
determine":

```console
$ mathsolver limit "sin(x)/x" x 0
limit = 1
method: l'hopital
$ mathsolver limit "(3x^2+1)/(x^2-5)" x inf
limit = 3
method: rational degree analysis
$ mathsolver limit "1/x" x 0
the limit does not exist
warning: left limit: -inf
warning: right limit: +inf
```

Two-variable limits sample eight rays plus the parabolic paths that catch
the classic counterexamples; disagreement proves nonexistence with both
witnesses shown, agreement returns the value with an honest caveat.
`series` also expands at infinity:

```console
$ mathsolver mlimit "x*y^2/(x^2+y^4)" x 0 y 0
the limit does not exist
warning: along x-axis (+): 0
warning: along parabola x=y^2: 0.5
$ mathsolver series "(x+1)/(x-1)" x inf 3
1 + 2/x + 2/x^2 + 2/x^3
```

Asymptotics of the gamma function — `stirling` builds the Stirling series
for ln Γ(x) with exact Bernoulli-number coefficients (computed by the
defining recurrence over exact rationals) and reports its own accuracy
against lgamma, including the reminder that an asymptotic series is a
truncation, not a convergent expansion:

```console
$ mathsolver stirling x 3
ln Gamma(x) ~ ln(x)*(x - 1/2) - x + ln(2*pi)/2 + 1/(12*x) - 1/(360*x^3) + 1/(1260*x^5)
note: ln Gamma(10): Stirling 12.8018274801 vs exact 12.8018274801 (|error| = 5.87e-11)
```

Least-squares regression — `fit` fits `x,y` data. Polynomial fits are
solved **exactly** over the rationals (the normal equations reduce to
exact power sums, then Gaussian elimination over `Q`), so a best fit comes
back as exact fractions where other tools show only a rounded decimal; it
falls back to double precision on overflow or non-rational data. `exp`,
`power`, and `log` fit their linearized numeric models. Each fit reports
its model, whether it is exact, and R²:

```console
$ mathsolver fit "0,1; 1,2; 2,2; 3,4"
9*x/10 + 9/10
model: linear (exact)
R^2: 0.852632
$ mathsolver fit "0,0; 1,1; 2,4; 3,9" quadratic
x^2
model: quadratic (exact)
R^2: 1
$ mathsolver fit "0,1; 1,2.72; 2,7.39" exp
e^x
model: exponential
R^2: 1
```

Summary statistics — `stats` reports the mean, median, quartiles (Moore &
McCabe), spread, and both population and sample standard deviation of a
data list. On rational data every statistic is **exact** — the mean stays
a fraction and the standard deviation a simplified radical, where a
calculator can only show a decimal:

```console
$ mathsolver stats "1, 2, 3, 4, 5"
n = 5
mean = 3
median = 3
variance (pop) = 2
stdev (pop) = sqrt(2)
stdev (sample) = sqrt(10)/2
$ mathsolver stats "1, 2, 4"
mean = 7/3
stdev (pop) = sqrt(14)/3
```

Multivariate and vector calculus — `grad`, `div`, `curl` (3-D vector and
2-D scalar), `laplacian`, `jacobian`, and `hessian` operate on
`;`-separated fields over an explicit variable list, and the web console's
`vecfield Fx; Fy` renders a magnitude-colored quiver plot:

```console
$ mathsolver grad "x^2 + y^2" x y
(2*x, 2*y)
$ mathsolver curl "x*y; y*z; z*x" x y z
(-y, -z, -x)
$ mathsolver hessian "x^3 + x*y^2" x y
[6*x, 2*y; 2*y, 2*x]
```

Discrete calculus — `sum` and `product` find exact closed forms
(polynomial Faulhaber fits, geometric forms with numeric or symbolic
ratios, infinite geometric series, and telescoping through partial
fractions), and `rsolve` solves linear recurrences by characteristic
roots — Fibonacci comes out as Binet's formula with `sqrt(5)` exact:

```console
$ mathsolver sum "k^2" k 1 n
sum = n^3/3 + n^2/2 + n/6
$ mathsolver sum "k*(1/2)^k" k 1 inf
sum = 2
$ mathsolver rsolve "a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1"
a(n) = -sqrt(5)*((-sqrt(5) + 1)/2)^n/5 + sqrt(5)*((sqrt(5) + 1)/2)^n/5
method: characteristic roots
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
`laplace`, `ilaplace`, `dsolve`, `apart`, `series`, `eval`, `subs`,
`collect`, `expand`, `factor`, `latex`, `debug` — e.g.
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
  Ctrl+L to clear. As you type, a **live typeset preview** renders the math
  the line will compute (`diff sin(x^2), x` shows d/dx(sin x²); definite
  integrals render with their bounds; parse errors are caret-underlined),
  and a **symbol palette** (π, √, |x|, ², ×, ÷, °, :=) inserts at the
  cursor.

  The console also dispatches **plugin commands** — compiled-in C++ modules
  for numeric domains the CAS doesn't cover (see
  [docs/PLUGINS.md](docs/PLUGINS.md)). The built-in `dsp` plugin does IIR
  and FIR filter design:

  ```text
  dsp.butter lowpass, 4, 1000, 48000     → biquads + magnitude/phase/time
  dsp.cheby1 bandpass, 3, 1, 500, 2000, 48000
  dsp.ellip lowpass, 5, 1, 60, 1000, 48000
  dsp.fir lowpass, 101, 1000, 48000, kaiser, 10
  dsp.remez lowpass, 31, 1000, 1500, 8000 → optimal equiripple (Parks–McClellan)
  dsp.freqz 48000, 0.2,0.4,0.2,-0.5,0.3  → response of your own biquads
  linalg.solve [2 1; 1 3], [3 5]         → LU solve with residual
  linalg.eig [0 -1; 1 0]                 → exact eigendecomposition (chi poly,
                                           surds, complex pairs, eigenvectors;
                                           numeric QR past the exact reach)
  linalg.eig [a 1; 1 a]                  → symbolic eigenvalues a ± 1
  linalg.svd [1 2; 3 4; 5 6]             → singular values, rank, cond
  linalg.det [a b; c d]                  → symbolic determinants (Bareiss)
  linalg.trisolve [-1], [2 2], [-1], [1 1] → structured: Thomas O(n)
  linalg.toeplitz [2 1 0], [3 4 3]       → structured: Levinson O(n^2)
  linalg.circulant [2 1 1], [4 4 4]      → structured: DFT diagonalization
  pde.heat 1, 1, x*(1-x)                 → heat equation: Fourier series + profiles
  pde.wave 1, 2, sin(pi*x)               → wave equation: standing-wave evolution
  pde.simulate 10, 1, u*(1-u), 0.5*sin(pi*x/10), 8 → nonlinear reaction–diffusion
                                           (method of lines, exact-Jacobian Newton)
  fem.bvp 1, 0, pi^2*sin(pi*x), 0, 1, u=0, u=0 → 1-D finite elements with the
                                           observed convergence order reported
  fem.modes 1, 0, 1, 0, pi               → Sturm–Liouville eigenmodes (λ = n²)
  ie.fredholm x*t, x, 1, 0, 1            → integral equation by Nyström quadrature
  ie.volterra x - t, x, -1, 0, 3         → Volterra equation by marching (u = sin x)
  hyb.sim v; -9.81, x, x; -0.8*v, 1, 0, 3 → bouncing ball: events, resets, Zeno
  sys.dde -x_d, 1, 1, 20                 → delay equation x' = -x(t-1) by steps
  sys.tf s+1, s^2+3s+2                   → poles/zeros, margins, Bode, step
  sys.ode y'' + 3y' + 2y = u' + u        → ODE to transfer function
  sys.feedback 1, s(s+1)(s+2), 2         → closed loop under gain-K feedback
  sys.rlocus 1, s^3 + 3s^2 + 2s          → root locus + critical gain
  sys.tfz z, z^2 - 0.5z + 0.06, 8000     → discrete H(z): unit circle, |p|<1
  sys.c2d 1, s+1, 100                    → discretize H(s) to digital biquads
  prob.normalcdf 1.96                    → P(X<=1.96) = 0.975, with the bell curve
  prob.invnorm 0.975                     → the quantile x = 1.95996
  prob.binompdf 10, 0.5, 5              → Binomial P(X=5) + the PMF stems
  prob.tcdf 2.228, 10                    → Student's t: P(X<=2.228) = 0.975
  prob.chi2cdf 7.815, 3                  → chi-squared: P(X<=7.815) = 0.95
  prob.expcdf 2, 0.5                     → Exponential: 1 - e^-1 = 0.6321
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
  `collect`, best-effort `factor`, `polydiv` (polynomial long division →
  quotient and remainder, e.g. `polydiv x^3 - 1, x - 1` → `x² + x + 1`), and
  `polygcd`/`polylcm` (monic polynomial GCD/LCM), and `resultant` (zero iff two
  polynomials share a root). **`trigexpand`** expands trig of sums
  and multiples into single angles (`sin(a+b)` → `sin(a)cos(b) + cos(a)sin(b)`,
  `cos(2x)` → `cos(x)² - sin(x)²`); **`trigreduce`** inverts it, turning
  products and powers back into multiple angles (`sin(x)²` → `1/2 - cos(2x)/2`,
  `2 sin(x) cos(x)` → `sin(2x)`). **`logexpand`/`logcombine`** do the same for
  logarithms (`ln(x·y)` → `ln x + ln y` and back).
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
- **Discriminant** — `discriminant a*x^2 + b*x + c, x` → `b^2 - 4*a*c`,
  exact closed forms for degree 2–4 with symbolic coefficients kept
  symbolic. With numeric coefficients it also reports the nature of the
  roots (`x^2 - 5x + 6` → `1`, "two distinct real roots"; `x^2 + 1` → `-4`,
  "two complex-conjugate roots").
- **Inequalities** — `solve x^2 < 4` → `x ∈ (-2, 2)`. The solver combines
  the two sides over a common denominator, takes the real roots of the
  numerator (zeros) and denominator (poles) as breakpoints, sign-tests each
  interval, and reports the solution set with exact endpoints (radicals and
  all) and correct open/closed brackets — poles excluded, `≤`/`≥` roots
  included:

  ```console
  $ mathsolver solve "x^2 >= 4"
  x ∈ (-∞, -2] ∪ [2, ∞)
  $ mathsolver solve "(x-2)/(x+1) <= 0"
  x ∈ (-1, 2]
  $ mathsolver solve "x^2 < 2"
  x ∈ (-sqrt(2), sqrt(2))
  ```
- **Linear systems** — Gaussian elimination over exact expression
  arithmetic (`solve "x + y = 3; x - y = 1"`): symbolic parameters as
  coefficients, underdetermined systems with free variables, and
  inconsistency detection.
- **Number theory** — integer `factor` (prime factorization), `gcd`/`lcm`
  of a list, deterministic `isprime` (Miller–Rabin over the whole 64-bit
  range), `nextprime`, `divisors`, and Euler's `totient` — all exact,
  factoring via trial division + Pollard's rho:

  ```console
  $ mathsolver factor 360
  2^3 * 3^2 * 5
  $ mathsolver gcd "1071, 462"
  21
  $ mathsolver isprime 2147483647
  2147483647 is prime
  $ mathsolver divisors 28
  1, 2, 4, 7, 14, 28
  $ mathsolver totient 36
  12
  ```
- **Modular arithmetic** — `mod`, `powmod` (modular exponentiation that
  handles huge exponents no plain evaluation could), `modinv` (modular
  inverse via extended Euclid), and `crt` (Chinese remainder theorem, even
  with non-coprime moduli):

  ```console
  $ mathsolver powmod "7, 1000000, 13"
  9
  $ mathsolver modinv "3, 11"
  4
  $ mathsolver crt "2, 3, 2; 3, 5, 7"
  23 (mod 105)
  ```
- **Continued fractions** — `cfrac` expands a rational (finite), `sqrt(n)`
  (exact periodic expansion), or any real (numeric) into `[a0; a1, a2, …]`
  and lists the **convergents** — the successive best rational
  approximations:

  ```console
  $ mathsolver cfrac 355/113
  [3; 7, 16]
  convergents: 3, 22/7, 355/113
  $ mathsolver cfrac "sqrt(2)"
  [1; (2)]
  convergents: 1, 3/2, 7/5, 17/12, 41/29, 99/70, …
  $ mathsolver cfrac pi
  [3; 7, 15, 1, 292, …]
  convergents: 3, 22/7, 333/106, 355/113, …
  ```

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
