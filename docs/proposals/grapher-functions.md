# Proposal: Loosely-defined functions in the grapher (Desmos parity)

Status: **proposed**. User-requested: define functions loosely — `f(x) = x^2`
— then use them everywhere, including derivatives via `f'(x)`, evaluation
`f(3)`, composition `g(f(x))`, and inside any row `y = f(x) + 1`. This document
is the spec an implementer follows; it also lays out a prioritized roadmap for
bringing the grapher closer to Desmos overall.

```
f(x) = x^2 - 3
g(x) = sin(x)
y = f(x) + 1          # plots x^2 - 2
y = f'(x)             # plots 2x  (derivative of the body)
y = g(f(x))           # composition
(2, f(2))             # a point at (2, 1)
f(3)                  # in the console: evaluates to 6
h(x, y) = x^2 + y^2   # multi-argument
```

---

## 1. Summary and the one decision that matters

The headline feature is **user-defined functions**. Everything else in the
Desmos-parity roadmap (§9) is secondary and sequenced after it.

There are two ways to add functions, and the choice governs the whole plan:

- **Approach A — web-layer text expander (RECOMMENDED).** No C++ changes.
  A function is a session-layer record `f ↦ {params, body}`; a pre-engine
  **beta-reduction pass** rewrites `f(args…)` into the body with the arguments
  substituted for the parameters, *before* anything is parsed, mirroring the
  existing `resolveCalculus` / `calculus.ts` expander. It leans on the engine's
  existing `subs` (AST substitution) and `derivative` bindings.
- **Approach B — core AST `Apply` node.** Add a first-class symbolic
  application `Kind::Apply` to the C++ AST plus a session `Lambda` table, so `f`,
  `f'`, and `g(f(x))` are ordinary sub-expressions usable in *every* CAS verb.

**Recommendation: ship Approach A** for the function feature (Phases 1–3
below), and keep Approach B documented as a future option (§8).

Why A: it delivers the entire user ask — `f(x)=…`, `f'(x)`, `f(3)`, `g(f(x))`,
`y=f(x)+1`, `f(x,y)` — with **zero risk to the CAS core**, ships in a focused
multi-file web change, and reuses a pattern already proven in
`resolveCalculus`. The user always *defines* `f`, so `f'(x)` is just "the
derivative of the body", which A handles.

Why not B (yet): B is the "right" long-term representation and the only way to
get **abstract** functions (an undefined `f` usable symbolically across
`diff`/`solve`/ODEs). But it is L-effort with a large blast radius: the
`a(x+1)`-vs-`f(x+1)` ambiguity is unsolvable without threading function names
into the parser (so the parser stops being context-free), ~9 core files and
~22 `Kind` switches change, `Apply` breaks the "Function has exactly one arg /
Add·Mul args are sorted" invariant for its own kind, and ~6 embind signatures
grow. The current ask does not need abstract functions, so B's cost is not
justified now. (See §8 for when it becomes worth it.)

**Doctrine (A):** the engine stays pure — no file under `include/mathsolver/`
or `src/` or `wasm/` changes. Functions are application state (a Svelte store),
and "applying" one means composing the existing `subs`/`derivative` primitives
over the user's text before the requested operation runs. This is exactly the
doctrine of the variable-assignment feature (docs/proposals/variable-assignment.md),
extended from `name := value` to `name(params) = body`.

---

## 2. Current state (what exists, what blocks us)

Verified against the working tree.

- **Unknown `f(x)` silently becomes `f*x`.** The lexer makes an unknown letter a
  single-symbol token and the parser applies implicit multiplication
  (`src/parser.cpp:699`). So `f(x)` never errors — it parses to `Mul(f, x)`.
  **Consequence: function-call detection MUST be textual and happen *before* the
  engine ever sees the row.**
- **No function abstraction in the AST.** `enum Kind { Number, Symbol, Constant,
  Add, Mul, Pow, Function }` (`expr.hpp:21`); `Function` is a fixed single-arg
  `FunctionId`. No apply/lambda node.
- **`substitute` is symbol→expression only** (`expr.cpp:641`), exposed as the
  `subs(input, "a=expr,b=expr", simplify)` binding (`bindings.cpp:579`). It does
  AST-correct substitution (parenthesization/precedence for free). This is the
  beta-reduction engine we lean on.
- **`derivative(input, var)`** (`bindings.cpp:645`) gives `d/dvar` — the engine
  for `f'`.
- **The parser rejects `'`** (apostrophe → ParseError). `y'`/`dy/dx` are
  recognized *only* by the web classifier (`classify.ts:241`). So prime notation
  must be consumed entirely at the text layer for registered functions.
- **`classify.ts` already half-recognizes `f(x)=…`** but the LHS regex
  `/^([A-Za-z][A-Za-z0-9_]*)\s*(?:\([^()]*\))?$/` (`classify.ts:249`) uses a
  **non-capturing** group — it drops the parameter list and makes `f` a plain
  session variable `f := x^2`. So `f(3)` today resolves to `x^2*3`, not `9`.
- **`resolveCalculus` (`GraphCalculator.svelte:167`) + `calculus.ts` is the
  template.** `findInnermostCall` detects a call site (`nameAt`: word boundary +
  `(`), depth-matches parens, extracts inner args, expands innermost-first via
  engine verbs, and splices the result back. `stripCalc` replaces innermost
  calls with `(firstArg)` so free-symbol `analyze` doesn't choke. A function
  expander mirrors this exactly.
- **Session vars** live in `vars.svelte.ts` (localStorage `mathsolver.vars`,
  cap 32); `applyEnv` (`vars/session.ts:172`) resolves via the engine `subs`
  over a dependency closure. Functions will be a **sibling store** so they never
  leak into that value-substitution CSV.

**Already at Desmos parity** (do not rebuild): `y=f(x)`/bare expr, `x=f(y)`,
polar, parametric, draggable points, implicit relations + inequality shading,
slope fields + solution curves, contour of `z=f(x,y)`, tables + regression,
auto-sliders from free variables, `{…}` restrictions, calc operators
(diff/integral/series/tangent/normal), definite-integral area shading, exact
POIs (roots/extrema/asymptotes), curve–curve intersections.

---

## 3. Architecture (Approach A)

Four new/changed pieces, all web-layer:

1. **`classify.ts`** — capture the parameter list (stop dropping it).
2. **`functions.svelte.ts`** (NEW) — the function registry store.
3. **`graph/functions.ts`** (NEW) — the pure-text scanner (mirror of
   `calculus.ts`).
4. **`GraphCalculator.svelte` / `run.ts` / `App.svelte`** — route every compute
   path through one expander, `resolveRow` (a superset of `resolveCalculus`).

### 3.1 Definition capture — `classify.ts`

Widen the define LHS regex to **capture** the params:

```
/^([A-Za-z][A-Za-z0-9_]*)\s*(?:\(\s*([^()]*)\s*\))?$/
```

When group 2 is present, `params = splitTopLevelCommas(group2).map(trim).filter(Boolean)`
and emit `{ t: "define", name, expr: rhs, params }` — `params` is an **optional
field** on the existing `define` RowKind (minimizes switch churn). Empty parens
`f()=5` are rejected with a clear message (a function needs ≥1 parameter) rather
than silently degrading to a constant that `f(3)` would then read as `f*3`.
Each param must lex as a single symbol (validated at commit via the existing
`nameVerdict`). `x`/`y`/`r` are legal params — they shadow the axes only inside
the body and are gone after reduction.

### 3.2 Function registry — `functions.svelte.ts` (mirror of `vars.svelte.ts`)

A `FunctionsStore` keyed by name, each entry:

```ts
FnBinding {
  name: string;
  params: string[];
  body: string;          // plain-printed body
  bodySymbols: string[]; // see the corrected formula below
  freeParams: string[];  // body free symbols that become sliders
  deps: string[];        // registered function names the body calls
  error?: string;        // last parse/validation error (keep-last-valid)
}
```

- Persist to `localStorage` `mathsolver.functions` (cap 32, **non-destructive
  loader** contract like vars).
- **A name is a var XOR a function.** Committing `f(x)=…` evicts any plain var
  `f`; committing a var `f` is rejected if `f` is a registered function.
- **Whole-registry fixpoint on every commit/remove.** `bodySymbols`,
  `freeParams`, and `deps` for **every** entry are recomputed on any change
  (mirroring the vars store's `#recomputeEnv`), so forward references
  (`f(x)=g(x)+1` defined above `g`) don't leave stale metadata. Cycle detection
  runs over the whole dep graph including the name being defined (mirror
  `resolve.ts findCycle`), rejecting direct self-recursion and mutual recursion
  at commit — not only via a runtime cap.
- **Keep-last-valid.** A transient parse error during editing flags the def
  row's own error but keeps the previous valid binding active, so consuming rows
  don't flicker to implicit multiplication mid-keystroke.
- **Name-collision guard at commit** (reject with a message): any core builtin
  function (`sin cos tan asin…atan sinh…atanh ln log exp sqrt abs sec csc cot
  erf erfc gamma digamma conj re im arg fib harmonic binomial factorial`),
  constant (`e i pi` and greek names), any `CALC_FNS` name (`diff derivative
  integral antiderivative series taylor tangent normal`), and the notebook verb
  set (`MATH_VERBS ∪ {plot, wave}`). This is the airtight complement to
  registry-membership disambiguation.

**Corrected `bodySymbols` / `freeParams` formula** (adversarial fix — the naive
`bodySymbols = analyze(rhs)` leaks callee names as phantom sliders and blinds
the cycle check because `g(x)+1` collapses to `g*x+1`):

```
bodySymbols = analyze( stripCalls(rhs, registryNames ∪ {selfName}) ).symbols
freeParams  = bodySymbols  \  ( params ∪ deps ∪ registryNames ∪ {selfName} )
```

So `f(x)=a*x^2` → `freeParams = {a}` (a becomes a slider), never `x`; and
`f(x)=g(x)+1` neither shows a spurious `g` slider nor hides the dependency.

### 3.3 Text scanner — `graph/functions.ts` (mirror of `calculus.ts`)

- **`functionCallAt(text, i, names)`** — reuse `nameAt`'s word-boundary check,
  but also consume a **trailing prime run** (`k` apostrophes) between the
  identifier and `(`. Returns `{ name, primes: k, … }` when `name ∈ names`.
- **`findInnermostAny(text, fnNames)`** — left-to-right; a call is innermost iff
  its paren-matched inner text contains **no further calc call AND no registered
  function call** (block on *both* kinds). This makes `diff(f(x))` expand `f`
  first (it is inner) and `f(diff(x))` expand `diff` first — correct
  interleaving with the existing calc expander. Returns
  `{ kind: "calc"|"appl", name, inner, start, end, primes }`.
- **`stripCalls(text, fnNames)`** — the `stripCalc` analog for cheap free-symbol
  detection: replaces each innermost call (calc OR appl) with `(firstArg)`. It
  **must be prime-aware** (strip the apostrophes too, or `analyze("f'(x)")`
  hits the core's apostrophe ParseError) and must **block on both call kinds**
  (or `diff(f(x))` mis-picks `diff` as innermost).
- **Placeholder minting** for capture-avoidance (§3.4): **canonical, brace-free**
  identifiers `Q_0, Q_1, …` (adversarial blocker fix — `Q_{0}` parses to the
  canonical symbol `Q_0`, but `substitute` matches by exact string, so the
  braced spelling never substitutes). Assert the round-trip
  `analyze(name).symbols[0] === name` before use, and bump the base letter if
  any placeholder collides with a token in the body or an argument.

### 3.4 The expander — `resolveRow` (supersedes `resolveCalculus`)

One innermost-first loop over both kinds:

```
resolveRow(text, scope?):
  for guard in 0..63:
    c = findInnermostAny(text, registryNames(scope))
    if !c: break
    if c.kind == "calc":  <existing resolveCalculus per-operator logic>
    else:                  text = spliceApplication(text, c, scope)
  return text
```

`spliceApplication` for a call to `F = {params p1..pn, body B}` with raw
argument strings `A1..An = splitTopLevelCommas(c.inner)`:

1. **Arity check** — `n` args or throw `"f expects n argument(s), got m"`.
2. **Prime** — if `c.primes = k > 0`: require `n == 1` (else
   `"prime notation needs a single-argument function"`); compute `B_k` by
   calling `derivative(B, p1)` `k` times; use `B_k` as the body.
3. **Capture-avoiding two-phase `subs`** (because `subs` applies its CSV
   *sequentially*, so `f(a,b)=a/b` at `f(b,a)` via a naive `a=b,b=a` would give
   `a/a`): mint fresh canonical placeholders `Q_0..Q_{n-1}`;
   - phase 1: `subs(B_k, "p1=Q_0,…,pn=Q_{n-1}", false)` → `B'`;
   - phase 2: `subs(B', "Q_0=(A1),…,Q_{n-1}=(An)", false)` → `R`.
   Pass **raw** arg text (not `applyEnv`'d) so session vars and the axis `x`
   stay symbolic and resolve later — deliberately unlike the calc branch, which
   `applyEnv`s its arg because differentiation needs a concrete form.
4. **Parenthesized splice** — `text[0..c.start] + "(" + R + ")" + text[c.end..]`.
   `subs` returns a correctly-parenthesized print, so `f(x)=x^2` at `f(x+1)` →
   `(x + 1)^2`, and the outer wrap gives `y=f(x)+1` → `((x+1)^2)+1`.
5. **Composition** falls out of innermost-first: `g(f(x))` reduces `f(x)` →
   `(x^2)` then `g((x^2))`.

Guard: registry-commit cycle check (§3.2) is the primary recursion defense; the
`guard in 0..63` cap is a backstop.

### 3.5 Pipeline wiring — every compute path

Detection and expansion must be swapped in at **every** `analyze`/`applyEnv`/
`sample`/`evaluate` site, or an unexpanded `f(x)` leaks as `f*x` (phantom
slider + a bogus `f` in the `subs` closure). The complete site list (grep-audit
`stripCalc` and `resolveCalculus`, plus every `applyEnv`/`sample`):

- **`recompute` detection** (`GraphCalculator.svelte:~906`): `analyze(stripCalc)`
  → `analyze(stripCalls)`, and **commit the function registry from the
  classified specs BEFORE the detection `Promise.all`** (gather funcdefs first,
  the way `defs` is gathered) so one pass is self-consistent (no
  detection-vs-build registry skew, no per-keystroke slider flicker).
- **`buildDrawable`** — swap `resolveCalculus`→`resolveRow` in the function,
  functionY, parametric, relation, polar, and area branches.
- **Slope field** (`GraphCalculator.svelte:~984`) — expand before its
  `applyEnv` (this path is in `recompute`, not `buildDrawable`).
- **`restrictMask`** (`~374`) and `paramBounds` — expand each clause before
  `applyEnv`/`sample`.
- **`evalCoord`** (points) — prepend `resolveRow`, so `(2, f(2))` and bare
  `f(3)` evaluate.
- **Intersections** and **POI/asymptote** paths — already resolve via
  `resolveCalculus`; swap to `resolveRow`.
- **`gatherSharedVars`** (share-link, `~1250`) — align to `stripCalls` +
  `freeParams` so the share path can't drift from `recompute` detection.
- **Point-drag detection loop** — same `stripCalls` swap.

**Split the define handling** (adversarial fix — otherwise a funcdef is
committed as *both* a var and a function and the namespace guard oscillates):
in `recompute`, route params-present specs to a `funcDefs` map →
`reconcileFunctions`, and params-absent specs to the existing `defs` map →
`reconcileDefines`. Update `plotVars(funcdef) = Set(params)`,
`specExprs(funcdef) = [expr]`, exclude funcdef names from the `defs` map,
and make `isSettableVar`/`axisSource` treat function names as non-settable/
non-draggable.

**Standalone funcdef rows plot as `y = f(x)`** (Desmos parity): a `define` row
with exactly one param gets a `buildDrawable` case that samples `y = f(x)` over
the view (reusing the function branch after expanding `f(x)`). Multi-param
funcdefs draw nothing (documented).

**Memoize `resolveRow`** keyed by `(rowText, registryVersion)` for pure-`appl`
rows (exclude viewport/overrides from the key, since application expansion is
viewport- and slider-independent — args stay raw). Rows that also contain calc
ops include `overrides` in the key. This is the main perf lever: `buildDrawable`
reruns on every pan/zoom/slider frame, and without memoization
`y=g(f(x))` re-runs 4 `subs` round-trips every 90 ms.

### 3.6 Error semantics (the "define loosely, fail clearly" part)

- **Bare use of a function name** — `f` where `f` is a registered function but
  not called: explicit error `"f is a function; write f(x)"` (never a silent
  slider=1). This is also the migration-safety net (§7).
- **`identifier(args)` that is neither a registered function, a known var, nor
  a builtin** — a soft warning on that row ("undefined function `g`") instead of
  silently multiplying, so a typo is distinguishable from intentional implicit
  multiplication (`2(x+1)` stays multiplication; `a(x+1)` with `a` a var stays
  multiplication; `g(x+1)` with `g` unknown warns).
- **Dependency errors propagate**: a row using `f` where `f`'s body has a parse
  error shows `"references f, which has an error"`, not implicit multiplication.
- **Prime on an unregistered name** — clear error, not an apostrophe ParseError.

### 3.7 Console + workbench

- **`run.ts`**: recognize `name(params) = body` def lines → registry (or the
  run's `ScopeEnv` for `run <notebook>`); route each line and each comma-arg
  through `resolveRow` before `analyze`/`applyEnv`, so bare `f(3) → 6`, points,
  and composition work. Add a `funcs` listing beside `vars`. **Thread a
  function-scope parameter through `resolveRow`** (mirroring `applyEnv`'s scope)
  so scoped notebook functions don't leak globally and vice-versa. **Decide
  intentionally** whether the console gains inline calc-op expansion (it has
  none today): if not, gate the console's `resolveRow` to the `appl` branch
  only, and regression-test either way.
- **`App.svelte`** (workbench): recognize function-def input beside
  `splitAssignment`; prepend `resolveRow` before `analyze`/`applyEnv` in the
  compute paths and the debounced live-preview effect.

---

## 4. Edge cases (design + adversarial review)

| Input | Correct result | Mechanism |
|---|---|---|
| `f(x)=x^2` then `f(x+1)` | `(x+1)^2` | `subs` AST substitution + outer wrap |
| `f(a,b)=a/b` at `f(b,a)` | `b/a` (not `a/a`) | two-phase canonical placeholders |
| `g(f(x))` | inner `f` then `g` | innermost-first |
| `diff(f(x))` vs `f(diff(x))` | textual-inner expands first | `findInnermostAny` blocks on both kinds |
| `f(x)=a*x^2` | `a` is a slider, `x` is not | `freeParams` formula |
| `f(x)=g(x)+1`, `g` defined | no `g` slider; `g` in `deps` | `stripCalls`-based `bodySymbols` |
| `f(x)=f(x)+1` / mutual | rejected at commit | cycle check incl. self, whole-registry rescan |
| `f'(x)`, `f''(x)` | body differentiated k× then applied | prime run + `derivative` |
| `f'(x,y)` | error | prime requires arity 1 |
| `sin(x)=x^2` | rejected at commit | builtin-name guard |
| `f(1,2)` for 1-arg `f` | `"f expects 1 argument, got 2"` | arity check |
| bare `f` (registered fn) | `"f is a function; write f(x)"` | error semantics |
| `g(x)` with `g` unknown | soft "undefined function g" | error semantics |
| `2(x+1)`, `a(x+1)` (a a var) | stays multiplication | not in registry → untouched |
| `f()=5` | rejected (needs ≥1 param) | classify guard |

---

## 5. Testing

- **`tools/graph_classify_test.mjs`** — `f(x)=x^2` → `define` with
  `params:["x"]`; multi-arg; `f()` rejected; a plain `f=x^2` still a param-less
  define.
- **`tools/graph_functions_test.mjs`** (NEW, pure-text, no engine) — the whole
  `functions.ts` surface: `findInnermostAny` innermost selection across calc+appl;
  `stripCalls` prime-awareness and dual-kind blocking; top-level comma splitting
  with nested `g(a,b)`; **placeholder minting canonicalization round-trip**
  (`Q_0` re-lexes to `Q_0`); prime-run parsing.
- **WASM smoke** — `subs`/`derivative` already exist and are unit-tested; add a
  node script driving the two-phase reduction end-to-end, asserting
  `f(a,b)=a/b` @ `f(b,a)` yields `b/a` with **no `Q_` token**, and `f'(x)` for
  `f(x)=x^2` yields `2*x`.
- **`svelte-check` + `npm run build`** clean.
- **Manual grapher scenarios** (canvas can't be headless-verified): the full
  edge-case table, live editing/redefinition flicker, forward references, share
  link round-trip, migration of a saved `f(x)=…` graph.

---

## 6. Phased rollout (function feature)

- **Phase 1 — definitions, application, evaluation.** classify param capture;
  `functions.svelte.ts` registry (fixpoint metadata, cycle check, name-collision
  guard, keep-last-valid); `functions.ts` scanner; `resolveRow` with two-phase
  capture-avoiding `subs`; wire **all** compute sites; split define handling;
  standalone funcdef → `y=f(x)`; multi-arg; memoization. Covers `f(x)=…`,
  `f(3)`, `y=f(x)+1`, `g(f(x))`, `f(x,y)`. Tests + docs.
- **Phase 2 — prime notation.** `f'(x)`/`f''(x)` via the prime run +
  `derivative`; arity-1 guard; slope-field-`y'` shorthand stays intercepted.
- **Phase 3 — polish + parity of errors/UX.** bare-name and undefined-call
  errors; dependency-error propagation; row affordance distinguishing funcdef
  rows from `y=` rows / sliders (and the `y=` lead); a functions panel/listing;
  console function parity + scope threading; localStorage migration (move
  param-carrying defines from `mathsolver.vars` → `mathsolver.functions`
  atomically; namespace eviction ordering).

Each phase is independently shippable and PR'd to `dev`, with the standing
verification gate: native tests, WASM smoke, `svelte-check`, web build.

---

## 7. Migration & back-compat (must not silently change results)

Existing saved graphs / share links contain `f(x)=expr` rows that today
classify as a plain define → session var `f := expr` (param dropped). Under the
new classify they become **functions**, so bare `f` is no longer in
`vars.active` and a prior row `y = f + 1` would resolve `f` as an undefined
symbol → slider = 1 → silently plots a constant. Mitigations:

- **Bare use of a registered function name is an explicit error** (§3.6), so the
  degradation is loud, never silent.
- **One-time migration**: on load, move param-carrying `f(x)=…` entries out of
  `mathsolver.vars` into `mathsolver.functions`; perform the cross-store
  eviction atomically under `untrack` with a fixed order (functions committed
  before vars reconcile), reusing the existing reconcile-debounce discipline.
- **Versioned, non-destructive loaders** for both localStorage keys.

---

## 8. Approach B (core `Apply` node) — future option, not now

B adds `Kind::Apply` (head name + derivative order + positional args, reusing
`ExprNode`'s existing `symbol_`/`number_`/`args_` fields so the struct doesn't
grow) plus a session `Lambda{params, body}` table and a stateless
`apply_user_functions()` primitive (the beta-reduction counterpart of `subs`),
a function-aware parser call rule + prime lexing, and one arm each in
`substitute`/`differentiate`/`simplify`/`evaluate`/`printer` (+ integrate/trig/
solver/logexpand). Empty-table parsing is byte-identical to today, so the golden
corpus is a regression net.

**What B buys that A cannot:** *abstract* functions — an undefined `f` usable
symbolically in every verb, and true symbolic `f^{(k)}(u)=f^{(k+1)}(u)·u'` with
no definition. **Costs:** the parser stops being context-free (function names
threaded in; a stale name set turns a call into a silent product); ~9 files /
~22 `Kind` switches; `Apply` violates the sorted/1-arg canonical invariant for
its kind (generic structure-matching code must be audited, not just switches);
`-Werror=switch` should be promoted for safe rollout; ~6 embind signatures grow.

**Adopt B when** a future feature needs an *undefined* function to flow through
the CAS — e.g. solving with an unknown `f` in an ODE, or `diff(f(x))`
symbolically. Until then, A covers the ask at a fraction of the risk. A and B
are compatible: A's registry is the same session-layer `{params, body}` record
B would expose to the core, so A is not throwaway work.

---

## 9. Desmos-parity roadmap (after functions)

Prioritized from a full inventory. `S/M/L` = effort.

**Must-have for parity (do next, after functions):**

- **Piecewise / conditionals `{cond: val, cond: val, …}`** — *high, M.* Desmos'
  core control-flow (`y = {x<0: -x, x}`). Parse the brace form in `classify.ts`
  (distinct from the existing `{…}` domain restriction — a colon marks a
  piecewise), sample each branch under its predicate mask (the `restrictMask`
  machinery already exists). Pairs naturally with function bodies.
- **Inline summation / product in a plotted row** — *med, M.* `sum`/`product`
  exist as verbs but not inline in a sampled expression; add a `resolveRow`
  expander branch (like the calc ops) that rewrites `sum(k, 1, n, …)` to a
  closed form or numeric fold before sampling.

**Nice-to-have (breadth, lower priority):**

- **Lists `[1,2,3]` + comprehensions + list ops** — *med, L.* Foundational for
  Desmos-style data/statistics but a large new value type across parse/eval.
  *Phase 1 shipped:* `L = [1,2,3]` / ranges `[a...b]`, element-wise broadcast,
  and list-of-points plotting `(L, L^2)` (web layer, `graph/lists.ts`).
  *Phase 2 shipped:* comprehensions `[k^2 for k=L]`, scalar aggregates
  (`total`/`mean`/`min`/`max`/`median`/`stdev`/`length`), and 1-based indexing
  `L[i]`. *Phase 3 shipped:* list-as-lines — `y = [ … ]` / `y = L` horizontal
  lines and `x = [ … ]` vertical lines. *Phase 4 shipped:* list-returning ops —
  `sort`/`unique`/`reverse`/`join` and slices `L[a...b]`. Still open:
  distribution/statistics plots, indexing a computed list (`sort(L)[i]`), and
  scalar-from-list defines (`k = mean(L)` as a session variable).
- **Statistics on lists / distribution plots** — *low–med, M–L.* Builds on lists
  and the existing `stats`/`prob` verbs.
- **Point & curve labels (`showLabel`)** — *low, M.* Per-row label text rendered
  at the point/curve.
- **Per-row line/point style** (dashed/thickness/point size) — *low, M.* Extend
  `DrawSeries` + the row model + `GraphCanvas` rendering.
- **Polygon / list-of-points plotting** — *low, M.* Depends on lists.
- **Animated sliders (play button)** — *med, M.* A rAF loop stepping a slider
  var between its bounds; the slider infra already exists.
- **Zoom-to-fit** — *low, S.* Frame the sampled data extent in the viewport.
- **Folders / notes / organization** — *low, M.* Row-list grouping UI.
- **LaTeX / live math input field** — *med, L.* A large input-UX change; the
  console already has a typeset preview to build on.
- **Actions & tickers** — *low, L.* Desmos' imperative layer; likely out of
  scope for a CAS-first grapher.

---

## 10. Risks (top)

1. **Two-phase capture-avoidance is the highest-severity correctness
   dependency.** Placeholders must be canonical (`Q_0`, not `Q_{0}`) and fresh;
   a miss silently produces wrong math on multi-arg reductions. Unit-test
   `f(a,b)=a/b` @ `f(b,a)` explicitly.
2. **Detection-path completeness.** Any unconverted `analyze(stripCalc)` /
   `applyEnv` site (slope field, restrictions, share, point-drag) leaks
   `f(x)`→`f*x`. Grep-audit and test that a function-shaped call never spawns a
   slider named after the function.
3. **Registry/vars coordination.** Namespace eviction, the params-present vs
   -absent split, and slider cleanup must stay in sync under the existing
   `untrack`/debounce discipline, or a slider/var row orphans or oscillates.
4. **Recompute latency.** Full async expansion in the per-keystroke path; keep
   detection on cheap `stripCalls`, memoize `resolveRow`, reduce only what is
   sampled.
5. **Silent semantic change on upgrade** for pre-existing `f(x)=…` graphs — fully
   mitigated only by the bare-name error + migration (§7).
