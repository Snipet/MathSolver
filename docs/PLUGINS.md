# Plugins

MathSolver plugins package domain-specific computations — typically
numeric-heavy code that benefits from running as native C++/WASM rather than
being expressed through the CAS — behind a uniform command surface. The
first built-in plugin is **dsp** (IIR filter design); the framework it sits
on is generic.

```text
console:  dsp.butter lowpass, 4, 1000, 48000
          dsp.freqz 48000, 0.2,0.4,0.2,-0.5,0.3
          plugins                     ← catalog of everything compiled in
```

## Design at a glance

- **Compiled in, not dynamically loaded.** Plugins are C++ translation units
  linked into the engine (native and WASM builds alike) and registered
  explicitly in `plugins/register_builtin.cpp`. No dlopen, no separate
  `.wasm` files, no loader security surface; adding a plugin is adding
  sources to one CMake target.
- **Strings in, JSON out.** A plugin exposes named *commands*. The host
  splits the user's comma-separated arguments and passes them as strings;
  the command returns one JSON object.
- **Declarative UI blocks.** A successful result is a title plus a list of
  *blocks* — key/value pairs, tables, data series, text. The website renders
  blocks generically (`PluginResult.svelte`), so a new plugin gets tables
  and charts **without any frontend code**.

```
┌────────────┐   dsp.butter …      ┌──────────────┐  invoke("butter",args) ┌────────────┐
│ Console    │ ──────────────────▶ │ WASM binding │ ─────────────────────▶ │ DSP plugin │
│ (run.ts)   │ ◀────────────────── │ pluginCall() │ ◀───────────────────── │ (C++)      │
└────────────┘   blocks JSON       └──────────────┘   blocks JSON          └────────────┘
       │ renders generically
       ▼
  PluginResult.svelte → kv / table / SeriesChart / text
```

## The contract (C++ side)

Defined in [`include/mathsolver/plugin.hpp`](../include/mathsolver/plugin.hpp):

```cpp
class Plugin {
  std::string_view name();      // namespace, e.g. "dsp"
  std::string_view version();
  std::string_view summary();
  std::vector<CommandInfo> commands();   // name + summary + usage line
  std::string invoke(std::string_view command,
                     const std::vector<std::string>& args);  // → JSON
};
```

`invoke` must never throw; it returns either

```json
{"ok":true,"title":"…","blocks":[ … ]}
{"ok":false,"error":"…"}
```

with blocks drawn from:

| Block | Shape | Rendered as |
|---|---|---|
| `kv` | `{"type":"kv","items":[["label","value"],…]}` | inline label/value pairs |
| `table` | `{"type":"table","title","columns":[…],"rows":[[…],…]}` | scrollable table |
| `series` | `{"type":"series","title","xlabel","ylabel","logx",` `"x":[…],"series":[{"label","ys":[…]}],` `"vlines":[{"x":…,"label":"fc"}]}` | line chart (`null` y = gap; `logx` for decades; `vlines` draws dashed markers, e.g. cutoffs) |
| `text` | `{"type":"text","lines":[…]}` | muted paragraphs |

JSON helpers (`jstr`, `jnum`, `error_json`) are exported from the same
header; `jnum` maps non-finite doubles to `null`.

## Adding a plugin (walkthrough)

Using dsp as the template:

1. **Write the computation** in its own files so it is unit-testable without
   JSON: `plugins/dsp/dsp_design.{hpp,cpp}` hold the Butterworth/bilinear
   math; `tests/test_plugin_dsp.cpp` checks it numerically (DC gain, the
   −3.01 dB cutoff point, monotonic rolloff, biquad stability triangle).
2. **Write the command layer**: `plugins/dsp/dsp.cpp` parses argument
   strings, validates (returning `error_json(...)` with a usage line), and
   composes blocks.
3. **Register it**: add a `make_<name>_plugin()` factory and one line in
   `plugins/register_builtin.cpp`; add the sources to `mathsolver_plugins`
   in the root `CMakeLists.txt`.
4. **Done.** The WASM layer (`plugins()` / `pluginCall()` in
   `wasm/bindings.cpp`), the console dispatch (`<plugin>.<command> args…`
   in `web/src/lib/notebook/run.ts`), the `plugins` catalog command, and the
   block renderer all pick it up with no further changes.

Conventions:

- Command grammar is the console's: `plugin.command arg, arg, …` — args are
  top-level-comma-separated and therefore cannot contain commas.
- Validate every argument and return actionable errors (`usage: …`); the
  host guards against exceptions, but a thrown exception reads as an
  internal error, not a user mistake.
- Emit `null` (via `jnum`) for non-finite samples — the chart renders a gap.

## The dsp plugin

IIR design runs the classic zpk pipeline (`plugins/dsp/dsp_design.cpp`):
normalized analog low-pass prototype → analog frequency transform (edges
prewarped) → bilinear transform → conjugate-pair grouping into biquads.
`<type>` is `lowpass|highpass|bandpass|bandstop` (`lp/hp/bp/bs/notch`); band
types take two edges and double the effective order.

| Command | Does |
|---|---|
| `dsp.butter <type>, <order 1-12>, <fc>[, <f2>], <fs>` | Butterworth (maximally flat). |
| `dsp.cheby1 <type>, <order 1-12>, <ripple dB>, <fc>[, <f2>], <fs>` | Chebyshev I (equiripple passband; cutoff = ripple edge). |
| `dsp.cheby2 <type>, <order 1-12>, <atten dB>, <fc>[, <f2>], <fs>` | Chebyshev II (equiripple stopband; cutoff = stop edge). |
| `dsp.ellip <type>, <order 1-12>, <ripple dB>, <atten dB>, <fc>[, <f2>], <fs>` | Elliptic/Cauer (equiripple both bands — the sharpest transition per order). Jacobi elliptic functions by Landen recursion. |
| `dsp.fir <type>, <taps 5-255>, <fc>[, <f2>], <fs>[, <window>[, <beta>]]` | Linear-phase windowed-sinc FIR (`rect`, `hann`, `hamming` default, `blackman`, `kaiser` with optional beta), response normalized to exactly unity at the band's reference frequency; odd taps required for high/band-stop. |
| `dsp.remez <type>, <taps 5-255 odd>, <edges…>, <fs>[, <stop weight>]` | Optimal equiripple FIR by the Parks–McClellan (Remez exchange) algorithm. Edges are the transition-band corners — `fpass, fstop` for low-pass, `fstop, fpass` for high-pass, `fstop1, fpass1, fpass2, fstop2` for band-pass, `fpass1, fstop1, fstop2, fpass2` for band-stop. The optional stop weight (default 1) trades passband ripple for a deeper stopband. |
| `dsp.freqz <fs Hz>, <b0>,<b1>,<b2>,<a1>,<a2> [, …groups of 5]` | Magnitude, phase, group delay, and time response of an arbitrary biquad cascade. |

IIR design results carry: a summary (with the **measured** gain at each
specified edge), the biquad coefficient table (Copy exports full-precision
TSV), magnitude + phase responses with the edges marked, and the time
response (impulse + step, length adapted to the slowest pole's decay). FIR
results show the taps table, magnitude response, and time response, with the
constant group delay reported in the summary. Everything is verified in
`tests/test_plugin_dsp.cpp` by property (ripple corridors, exact edge gains,
elliptic equiripple in both bands + transition-ratio bounds from the degree
equation, FIR symmetry/linear phase/window-dependent stopbands, notch depth,
the biquad stability triangle across every family × type × order, and time
responses of known systems).

Extension points (same pattern): Parks–McClellan equiripple FIR,
matched-z/impulse invariance, fixed-point coefficient quantization analysis.

## The sys plugin

Continuous-time LTI systems in the Laplace domain. Polynomial arguments go
through the **CAS**: any polynomial spelling works (`(s+1)(s+2)` or
`s^2 + 3s + 2`) because coefficients are extracted via repeated symbolic
differentiation; roots come from a Durand–Kerner iteration, and time
responses from an RK4 state-space simulation (controllable canonical form,
substeps chosen from the fastest pole).

| Command | Does |
|---|---|
| `sys.tf <num poly in s>, <den poly in s>` | Full analysis of a proper H(s): poles/zeros table (with ωₙ and damping), stability verdict, DC gain, pole-zero map (scatter, jω axis marked), Bode magnitude + phase, step + impulse response. |
| `sys.ode <LTI ODE in y and u>` | Convert `y'' + 3y' + 2y = u' + u` (zero initial conditions; decimal coefficients, primes for derivatives, input on either side) to H(s), then the same analysis. |
| `sys.feedback <num>, <den>[, <K>]` | Closed loop K·G/(1 + K·G) under unity feedback, run through the full analysis. |
| `sys.rlocus <num>, <den>[, <K max>]` | Root locus: closed-loop pole sweep (160 gains over four decades) as a scatter, with open-loop poles/zeros marked and the smallest destabilizing K reported. |
| `sys.tfz <num>, <den>, <fs Hz>` | Analyze a **discrete** transfer function H(z) (positive powers of z): poles/zeros with \|z\|/angle, stability by \|pole\| < 1, pole-zero map with the **unit circle** (equal-aspect), magnitude/phase response, and step + impulse from the difference equation. |
| `sys.c2d <num>, <den>, <fs Hz>` | Discretize via the bilinear transform into digital biquads — reusing the dsp plugin's public `Zpk`/`bilinear_zpk`/`zpk_to_biquads` machinery — with the digital-vs-analog magnitude overlay. |
| `sys.dde <f(t, x, x_d)>, <tau>, <phi(t)>, <T>` | **Delay differential equation** x′(t) = f(t, x, x(t−τ)) by the method of steps: RK4 with the delayed value interpolated from the stored solution (history φ before t = 0). The chart shows the history segment and the response. `x_d` is the delayed value (a subscripted symbol). |

The **pde** plugin solves the classic 1-D boundary-value problems on
`[0, L]` with homogeneous Dirichlet conditions by separation of variables —
initial data expands in the sine eigenbasis with coefficients from the CAS
integrator (exact where its rules reach, numeric otherwise):

| Command | What it does |
| --- | --- |
| `pde.heat <L>, <alpha>, <f(x)>[, <T>]` | u_t = α u_xx with u(x,0) = f(x): b_n table, mode-1 time constant, and temperature profiles at t = 0, T/16, T/4, T. |
| `pde.wave <L>, <c>, <f(x)>[, <g(x)>[, <T>]]` | u_tt = c² u_xx with displacement f and optional velocity g: displacement profiles across the fundamental period 2L/c. |

The **ie** plugin solves second-kind integral equations with symbolic
kernels K(x, t) and forcing f(x), evaluated pointwise by the CAS. Every
solve repeats at **half resolution** and reports the largest disagreement as
an error estimate, so the curve carries its own accuracy check:

| Command | What it does |
| --- | --- |
| `ie.fredholm <K(x,t)>, <f(x)>, <lambda>, <a>, <b>` | u(x) = f(x) + λ∫ₐᵇ K(x,t) u(t) dt by the **Nyström method**: 31-node composite-Simpson quadrature turns the equation into (I − λKW)u = f, solved by the linalg LU; off-node chart values use the Nyström interpolation formula. λ at a characteristic value fails with a specific message. |
| `ie.volterra <K(x,t)>, <f(x)>, <lambda>, <a>, <b>` | u(x) = f(x) + λ∫ₐˣ K(x,t) u(t) dt by **trapezoidal marching** (200 steps), each step solving its scalar implicit equation. `ie.volterra x - t, x, -1, 0, 3` reproduces u = sin x. |

The **hyb** plugin simulates hybrid systems — a two-state RK4 flow
x′ = f_x(t,x,v), v′ = f_v(t,x,v) with an event surface. When the guard
crosses from positive to non-positive inside a step, the event time is
refined by bisection, the reset map is applied at the pre-event state, and
integration restarts; resets that land *on* the guard surface (a bouncing
ball resets to x = 0) don't re-fire because crossings require a strictly
positive step start. Inter-event gaps below the step resolution stop the run
with a **Zeno** note that extrapolates the accumulation time geometrically,
and solution blow-ups stop with a note instead of nonsense:

| Command | What it does |
| --- | --- |
| `hyb.sim <x'>; <v'>, <guard>, <reset x>; <reset v>, <x0>, <v0>, <T>` | Event-driven simulation: events table (times, impact/rebound states), trajectory chart with event markers. Bouncing ball: `hyb.sim v; -9.81, x, x; -0.8*v, 1, 0, 3`. On a horizon past the accumulation (`T = 5`), the Zeno note reports the estimated accumulation time ≈ 9√(2/g) ≈ 4.06. |

The **linalg** plugin does dense linear algebra. Matrices are written
`[1 2; 3 4]` (rows by `;`, entries by spaces or commas); entries are full
expressions, and a matrix with symbolic entries routes to exact machinery
where supported:

| Command | What it does |
| --- | --- |
| `linalg.solve [A], [b]` | LU with partial pivoting; reports the residual max\|Ax − b\|. |
| `linalg.det [A]` | Numeric LU determinant, or **exact Bareiss** for symbolic entries (≤ 5×5): `linalg.det [a b; c d]` → `a*d - b*c`. |
| `linalg.inv [A]` | Inverse with the 2-norm condition number. |
| `linalg.eig [A]` | **Exact eigendecomposition** for n ≤ 4: characteristic polynomial by Bareiss over Expr, roots through the core solver (exact surds — `[1 1; 1 0]` → golden ratio — complex pairs, and symbolic matrices: `[a 1; 1 a]` → a ± 1), eigenvectors from an exact rational null space (multi-dimensional eigenspaces included) or the 2×2 closed form. Anything the exact machinery can't factor, and everything larger, falls back to Hessenberg + shifted QR (≤ 16×16) with spectral radius and a trace cross-check. |
| `linalg.svd [A]` | One-sided Jacobi SVD: singular values, rank, cond, U and V tables. |
| `linalg.rank [A]` | Numeric rank via the SVD tolerance. |
| `linalg.lstsq [A], [b]` | Least squares via the SVD pseudoinverse, with the residual norm. |
| `linalg.trisolve [sub], [diag], [super], [b]` | **Structured**: tridiagonal solve by the O(n) Thomas algorithm (n up to 4096), zero pivots reported instead of divided by. |
| `linalg.toeplitz [first column], [b]` | **Structured**: symmetric Toeplitz solve by the O(n²) Levinson recursion; singular leading principal minors are reported (the recursion needs strong nonsingularity). |
| `linalg.circulant [first column], [b]` | **Structured**: circulant solve by DFT diagonalization; a vanishing eigenvalue (DFT coefficient of the first column) is reported as singular. Every structured command prints the residual against a structured matvec. |

Every analysis additionally reports **classical stability margins** (gain
margin at the −180° crossing, phase margin at the 0 dB crossing, refined by
bisection on Im H(jω)), with the crossover frequencies marked as ωpc/ωgc
vlines on both Bode charts.

Console note: a plugin argument that is a **pure identifier bound by `:=`**
resolves through the session environment when its value is a closed numeric
expression (`f_c := 2000` → `dsp.butter lowpass, 4, f_c, 48000`); keywords
and polynomial/ODE arguments pass through verbatim. (Names follow the CAS
grammar: single letters, optionally subscripted.)

The pole-zero map uses the block contract's scatter extension: a series may
set `"points": true` with `"shape": "x" | "o"`, and the chart draws markers
instead of a connected line.

Extension points: state-space input/output (`sys.ss`), feedback
interconnection (`sys.feedback G, K`), root locus, Nyquist plots, delay
margins.

## Scope and future work

- Plugins currently surface in the **web console** (and are exercised by the
  native test suite and `tools/wasm_smoke.mjs`). The CLI/REPL does not yet
  dispatch `plugin.command` lines; the registry is host-agnostic, so wiring
  the REPL through the same `invoke` surface is a straightforward follow-up.
- Session variables (`:=`) are not resolved into plugin arguments — plugin
  args are raw strings, not CAS expressions. If a plugin wants expression
  arguments, the natural route is resolving through `applyEnv` in the
  console before dispatch; deferred until a plugin needs it.
