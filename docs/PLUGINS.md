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
| `sys.c2d <num>, <den>, <fs Hz>` | Discretize via the bilinear transform into digital biquads — reusing the dsp plugin's public `Zpk`/`bilinear_zpk`/`zpk_to_biquads` machinery — with the digital-vs-analog magnitude overlay. |

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
