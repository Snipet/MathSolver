# Proposal: Variable Assignment (v0.5 headline)

Status: **proposed** — targets v0.5. User-requested feature: assign literals,
expressions, and equations to variables and have them apply across the REPL
and the web app.

This document is written in DESIGN.md's contract style: it is the spec an
implementer follows. Section references (§N) without a document name refer to
DESIGN.md.

```
>>> a := 2
a := 2
>>> a*x + 3
2*x + 3
```

---

## 1. Summary and doctrine

MathSolver gains a **session environment**: an ordered set of bindings
`name := value` where `value` is an `Expr` or an `Equation`. Assignments are
entered with `:=`, listed with `vars`, removed with `unset <name>` / `clear`,
and are applied automatically — by plain `substitute()` composition — to the
input of every *computing* operation in the REPL and the web app.

**Doctrine (the one-sentence version): the engine stays pure.** No file under
`include/mathsolver/` or `src/` changes for this feature. The environment is
application state — a data structure in the REPL loop (`apps/main.cpp`) and a
Svelte store in the web app — and "applying" it means composing the existing
`substitute()` primitive (§2) / the `subs` verb (§10) over the user's input
before the requested operation runs. This is the "REPL-level `let` sitting
above the parser" that docs/ROADMAP.md §5 explicitly reserved room for; the
ROADMAP's "the REPL is stateless by design" sentence is amended by this
proposal (statefulness is confined to the session layer; parser, printer, and
every library module are untouched).

**Hard dependency.** The `subs` and `collect` verbs (ROADMAP P0-7, DESIGN §10)
must be shipped in CLI, REPL, and wasm before this feature lands. At the time
of writing they exist in `apps/main.cpp` (`run_subs`, `run_collect`) and
`wasm/bindings.cpp` (`ms_subs`, `ms_collect`) on the in-flight v0.4 branch but
are **not** in the last released binary (v0.3.0 answers
`usage error: unknown command 'subs'`). Stage 0 of the plan (§12) verifies
them before any v0.5 work starts.

---

## 2. Assignment syntax: `:=`

### 2.1 The token

Assignment is written `name := value`. Probed against the current binary:
`:` is a clean lex error today (`error: unexpected character ':'`), so `:=`
occupies unclaimed syntax space — **no existing input changes meaning**.

The parser does **not** learn about `:=`. Assignment is recognized by the
input layer (REPL command dispatch / web variables panel) before the parser
ever sees the text:

- An input line whose first `:=` occurrence splits it into a non-empty left
  part and a non-empty right part is an assignment. (`:` not followed by `=`
  falls through to the parser and keeps its existing error.)
- The **left side** must lex as exactly one Symbol token under the §4
  grammar: a single letter (`x`, `A`), a greek name (`alpha`), or a
  subscripted form (`x_1`, `E_1`, `x_{max}`). Anything else is an error
  (§2.3).
- The **right side** is handed to `parse_input` and may be an expression or
  an equation (§4). It is parsed **at definition time** so the user gets
  caret diagnostics immediately; the parsed `Expr`/`Equation` AST is what the
  binding stores.

### 2.2 Why not `=`

`=` was considered and rejected:

1. **It already means something.** `x = 3` is an equation (§4: single
   top-level `=` → `Equation`), and a bare equation in the REPL *solves* it.
   Overloading `=` would silently change the behavior of every existing
   equation input — the exact class of silent meaning-change the v0.4
   word-guard was built to kill (ROADMAP §2, "trust-destroying").
2. **Named equations become unparseable.** `E_1 = x + y = 3` has two `=` at
   top level, a hard ParseError under the §4 grammar ("at most one"). With
   `:=` there is no ambiguity: everything right of `:=` is one ordinary
   §4 input, so `E_1 := x + y = 3` contains exactly one `=`.
3. **Heuristics don't save it.** "Treat `lhs = rhs` as an assignment when
   `lhs` is a bare symbol" makes `x = 3` (solve: trivially `x = 3`) and
   `y = x^2` (solve for the single free symbol… which one?) assignments,
   while `x - 3 = 0` stays a solve — two spellings of the same math with
   different effects. Rejected.
4. `:=` is standard definition notation in mathematics and in prior CAS art
   (Maple, Mathematica's `SetDelayed` spirit), so it carries the right
   *lazy* connotation (§5).

### 2.3 Assignable names — and the ones that are not

The target must be a name the existing grammar can produce as a single
Symbol. This keeps the §4 grammar, the word guard, and the §5 round-trip
completely unchanged: a bound name is just a symbol that happens to have a
value in the session.

| Input | Result |
|---|---|
| `a := 2` | binds symbol `a` |
| `alpha := pi/6` | binds symbol `alpha` (greek names are symbols, §4) |
| `x_1 := 3`, `x_{max} := 10` | binds `x_1`, `x_max` (subscript forms, §4) |
| `E_1 := x + y = 3` | binds `E_1` to an **Equation** value (§4 named equations) |
| `E1 := ...` | **error**: `assignment target must be a single variable name (e.g. x, alpha, E_1) — 'E1' reads as E*1; did you mean E_1?` |
| `speed := 5` | **error**: same message class; `speed` is not a symbol (v0.4 word guard). See §6. |
| `pi := 3`, `e := 2.7` | **error**: `cannot assign to the constant 'pi'` (constants are `Kind::Constant`, not symbols — same doctrine as §10's eval binding rejection) |
| `sin := x` | **error**: `cannot assign to the function name 'sin'` |
| `x :=` (empty right side) | **error**: `assignment needs a value (e.g. x := 2)` |

Redefinition is allowed and replaces the previous value (subject to the cycle
check, §5.2). Defining a binding never prints a computed result; the REPL
echoes the binding in canonical §5 plain form (`a := 2`), which doubles as
confirmation that the value parsed as intended.

---

## 3. Where the environment lives

**Not in the engine.** The environment is:

- **REPL**: a member of the REPL session in `apps/main.cpp` — an
  insertion-ordered `std::vector<Binding>` with
  `struct Binding { std::string name; std::variant<Expr, Equation> value; }`
  (vector, not map: `vars` lists in definition order; lookups are linear over
  a handful of entries). It lives and dies with the process. **One-shot CLI
  subcommands remain stateless** — there is no persisted environment file and
  no `--let` flag in v1; the stateless equivalent is the existing `subs`
  verb (`mathsolver subs "a*x + 3" a=2`). Non-goal, see §11.
- **Web**: a Svelte store (`web/src/lib/vars.svelte.ts`, modeled on
  `history.svelte.ts`) persisted to `localStorage` (§9), living beside the
  history store.
- **WASM**: nothing. The wasm module stays a pure function table (§8); the
  web client applies the environment through the existing `subs` binding
  before each call.

**Invariant (engine purity):** no header under `include/mathsolver/`
mentions environments, bindings, or sessions. `grep -ri "environment\|binding"
include/ src/` after implementation must show no new hits beyond today's
(the evaluator's numeric `bindings` parameter, §6, is unrelated and
unchanged).

Why not engine-side? (a) Every library function's contract (§6–§9b) is a pure
input→output mapping; the fuzzers and the idempotence/round-trip property
tests (§11, REVIEW-NOTES) all assume it — ambient state would invalidate the
entire standing verification estate. (b) The wasm worker can be restarted at
any time (and a second browser tab gets a second worker); engine-held state
would silently diverge from the panel. (c) `substitute()` already *is* the
application mechanism, shipped and tested via the `subs` verb — the feature
needs orchestration, not new math.

---

## 4. Values: literals, expressions, equations

A binding's value is whatever `parse_input` returned:

- **Expr** — a literal (`2`, `3/4`, `2e-3`) or any expression (`x^2 + 1`,
  `sin(alpha)`). Stored as the parsed AST, i.e. with §2 factory
  canonicalization already applied (`a := 2/4` stores `1/2` and `vars` echoes
  `a := 1/2` — same doctrine as the `latex` verb, §10: parse-time
  canonicalization only, no simplify).
- **Equation** — `E_1 := x + y = 3` stores `Equation{x + y, 3}`. Equation
  values are *not* expressions and can never appear inside one:

**Equation-name placement rule.** A name bound to an Equation may appear only
where a whole equation is expected: as the entire input line, or as an entire
`;`-separated segment of a `solve` command (§9b CLI split). There it denotes
the stored equation (whose *sides* then resolve expression-valued names
lazily as usual, §5). Anywhere else — inside an expression, inside another
binding's value — resolution fails with
`error: 'E_1' names an equation and cannot be used inside an expression`.

```
>>> E_1 := x + y = 3
E_1 := x + y = 3
>>> E_2 := x - y = 1
E_2 := x - y = 1
>>> solve E_1; E_2, x, y
x = 2
y = 1
method: gaussian elimination
>>> E_1 + 1
error: 'E_1' names an equation and cannot be used inside an expression
```

---

## 5. Resolution semantics: lazy (use-time), recursive, cycle-free

### 5.1 Use-time binding

Bindings are **lazy**: a value is stored as its AST and its symbols are
resolved against the environment *at each use*, not frozen at definition.

```
>>> f := g + 1        # g is not even defined yet — fine
f := g + 1
>>> g := x^2
g := x^2
>>> f
x^2 + 1
>>> g := x^3          # redefine g; f follows
g := x^3
>>> f
x^3 + 1
```

Why lazy, not eager (definition-time snapshot)?

- **It matches the web panel.** Panel rows are edited independently and in
  any order; eager binding would make row *order of edit* semantically
  significant and invisible — editing `g` would silently *not* update `f`.
- **It matches the REPL workflow** — define a formula once, re-point its
  parameters repeatedly. Eager semantics turn that into re-typing the
  formula.
- **It is the cheaper contract to state**: "the result is what you would get
  by running `subs` with the current bindings" — one sentence, one existing
  primitive. Eager semantics need a second notion of value (the snapshot)
  that `vars` would then have to display alongside the source text.
- Definition-time *parsing* (with immediate caret diagnostics) is kept — the
  laziness is only in symbol resolution.

### 5.2 Recursive resolution and cycle detection

**resolve(input, env, excluded)** — the single algorithm both frontends
implement (REPL in C++, web in TypeScript; §8 explains why twice, §10 how the
two are kept honest):

1. Collect `free = free_symbols(input)` (both sides for an Equation).
2. `active = { b ∈ env : b.name ∈ closure(free) and b.name ∉ excluded }`,
   where `closure` chases names appearing in bound values transitively.
3. Order `active` **parents-first**: binding A precedes binding B whenever
   A's value references B's name. (Any topological order of the dependency
   graph with edges A → B for "A's value mentions B". The graph is acyclic by
   the invariant below, so this order exists.)
4. Apply sequentially, left to right, exactly as the `subs` verb does:
   `e = substitute(e, name_i, value_i)` (both sides for an Equation).
5. The result feeds the requested operation.

Parents-first ordering makes one sequential pass fully resolve chains: for
`f := g + 1`, `g := x^2`, the order is `f` then `g`; substituting `f` into
`f + y` yields `g + 1 + y`, then substituting `g` yields `x^2 + 1 + y`.
Since a binding applied at step *i* can only *introduce* names applied at
steps > *i* (parents-first) and `excluded` names are never introduced by
active bindings (they were excluded from the closure), the sequential result
equals the simultaneous fixpoint — **resolution is deterministic and
independent of definition order**. This is a tested property (§10).

**Environment invariant (cycle-freedom):** at all times the dependency graph
over *defined* names is acyclic. Enforced at definition: before accepting
`name := value`, walk the graph of the environment-as-it-would-become; if
`name` is reachable from `value`'s free symbols, reject:

```
>>> a := a + 1
error: 'a' cannot be defined in terms of itself
>>> a := b + 1
a := b + 1
>>> b := a^2
error: assignment would create a cycle: b -> a -> b
```

Note `a := b + 1` with `b` undefined is legal (lazy!); the cycle check runs
against defined names only, and *every* definition re-runs it, so a cycle can
never form through later definitions. Belt-and-braces: `resolve()` still
carries a seen-set and a depth bound of 64 and raises
`internal error: assignment cycle detected` if the invariant is ever violated
— defensive, never expected to fire.

Substitution reconstructs through the §2 factories, so resolution can throw
`OverflowError`/`DivisionByZeroError` like any expression construction; these
surface as ordinary math errors (REPL: print and keep the session alive; web:
error card), exit code doctrine unchanged.

---

## 6. Shadowing, the word guard, and multi-letter names

**v1 keeps the grammar frozen: assignable names are exactly the names the §4
grammar already produces** (single letters, greek names, subscripted forms).
No parser change, no printer change, no round-trip risk, no word-guard
change.

The tension, stated honestly: the user who wants named values often wants
*words* — `speed := 5`, `rate := 3/100`. The v0.4 word guard (§4) makes
`speed` a ParseError precisely so it can't silently become `s·p·e·e·d`, and
that is the right behavior for v1 assignment too: `speed := 5` errors with
the standard hint plus assignment-specific guidance — reuse the word-guard
text (`variables are single letters (a-z), Greek names, or subscripted
(v_1)`) and append
`assignment targets follow the same rule — try a subscripted name like s_max := 5`.
Subscripts are the pressure valve: `v_max := 10`, `r_1 := 3/100` are legal
today and read well.

**The v2 path** is ROADMAP P2-1 (quoted multi-letter symbols,
`\text{speed}` / `"speed"` mapping to a Symbol with a multi-char name). The
printer already round-trips arbitrary symbol names through the subscript
machinery, and *assignment is the feature that finally makes P2-1 worth its
cost* — a stored `"speed" := 5` binding is far more useful than an inline
multi-letter symbol. When P2-1 lands, this proposal's name rule widens
automatically ("must lex as exactly one Symbol token") with **zero changes to
the environment machinery**. Do not widen it any other way; in particular do
not special-case multi-letter names on the left of `:=` only — a name you can
assign but cannot subsequently *type in an expression* is useless, and one
you can type would need the full P2-1 grammar work anyway.

Shadowing summary:

- Constants (`pi`, `e`) and function names: never assignable (§2.3).
- A bound name does not change how anything *parses* — `a := 2` then `2a`
  still parses as `Mul(2, a)`; resolution happens after parsing.
- Explicit per-command bindings shadow the environment for that command
  (§7: `eval`, `subs`).

---

## 7. Interaction with each verb

One rule generates the table: **the environment applies, via §5 resolve(), to
the input of every computing verb, with the verb's own designated symbols
excluded and a warning where the exclusion is surprising. Display verbs never
apply it.**

| Verb (REPL) | Environment behavior |
|---|---|
| bare expression | resolve all, then simplify. `x := 3` → `x^2 + 1` prints `10`. |
| bare equation | resolve all bindings first; if exactly one free symbol remains, solve for it (this makes `m := 2` then `m*x = 6` → `x = 3` — the environment *disambiguates* the solve variable); if none remain, evaluate the truth: print `equation holds (identity)` / `equation is false (contradiction)` (internally §9 step 1); if several remain, keep today's "use solve ..., var" prompt. |
| `solve eq, x` | **excluded: `x`.** Resolve every assigned name *except* the solve variable, then solve. If `x` is assigned, WARN (spec below) — do not refuse, do not substitute. Multi-variable systems (`solve E_1; E_2, x, y`): exclude every requested variable, warn per assigned one. Equation-valued names allowed as whole segments (§4). |
| `diff expr, x` | **excluded: `x`** (else `d/dx` of an x-free expression is a silent 0). Same warning as solve when `x` is assigned. |
| `integrate expr, x[, lo, hi]` | **excluded: `x`**, same warning. Bounds `lo`, `hi` are ordinary expressions: resolve them fully (`a := 2` → `integrate x^2, x, 0, a` integrates to `8/3`). |
| `collect expr, x` | **excluded: `x`**, same warning. |
| `eval expr, x=1, ...` | **excluded: the explicitly bound names.** Resolve the rest, then evaluate with the numeric bindings. An explicit binding overriding an assignment adds the note `note: binding x=1 overrides the assignment x := 3 for this command`. |
| `subs expr, x=v, ...` | **excluded: the explicit substitution names** (explicit wins, with the same note). Resolution runs once, before the verb; expressions *introduced by* explicit substitution values are not re-resolved in v1 (documented; keeps the verb a single deterministic pass). |
| `expand`, `factor` | resolve all, then operate. |
| `latex`, `debug` | **never resolve.** These are display verbs — `latex` is pinned (§10) as "print what was parsed, un-simplified", and `debug` is the s-expression dump; both must show the input as typed. `latex f` prints `f`, not `x^2`. To render a resolved form: `latex` the output of a computing verb, or use `subs`. |
| `vars`, `unset x`, `clear` | environment management, §7.1. |

**The solve-for-an-assigned-variable warning, precisely.** When the requested
variable `x` has a binding, `solve` (and `diff`/`integrate`/`collect`)
proceeds with `x` as a free symbol and appends to the §9/§10 warnings block:

```
warning: 'x' has an assigned value (x := 3), which is ignored while solving for it; 'unset x' removes the assignment
```

Why warn-and-proceed rather than refuse or substitute: substituting yields
`9 = 9` → `AllReals` — a bewildering answer to "solve x² = 9"; refusing
breaks the natural flow of assigning a trial value and then solving properly
(forced `unset`/re-assign churn). Warn-and-proceed matches the solver's
standing doctrine of answering with explicit caveats (§9 symbolic-pivot and
domain warnings). The warning prints in the existing warnings position (after
the solution lines, with `method:`).

`vars`, `unset`, and `clear` cannot collide with expressions: as bare inputs
today all three are word-guard ParseErrors (≥3 single-letter segments), so
claiming them as REPL commands changes no working input.

### 7.1 Environment management commands (REPL)

```
vars            list bindings, insertion order, canonical §5 plain print;
                equation values print as `lhs = rhs`; empty env prints
                `no variables defined`
unset x         remove the binding for x; unknown name → `note: 'x' is not
                defined` (session stays alive; not an error)
clear           remove all bindings; prints `cleared 3 assignment(s)`
                (`clear` with an empty environment prints `cleared 0
                assignment(s)`)
help            gains an "Assignments" section documenting :=, vars, unset,
                clear
```

`unset` cannot break the cycle invariant (removing edges never creates a
cycle), so it needs no check.

---

## 8. WASM surface: client-side application

**Recommendation: no environment-aware binding; no engine/wasm state. The web
client resolves via the existing `subs` binding.** Two alternatives were
considered:

- *Env-aware bindings* (`ms_set_var` / `ms_solve_with_env(...)` or a
  stateful module): rejected. It moves session state into the wasm module,
  which the worker protocol treats as restartable and the §3 purity doctrine
  forbids; every one of the ~14 bindings would need an env-carrying variant
  or a hidden global; and the wasm suites (`tools/wasm_smoke.mjs`,
  `tools/wasm_acceptance.mjs`) would face stateful test ordering for the
  first time.
- *Client-side textual resolution* (the TS layer string-splices values into
  the input): rejected — string splicing re-invents the parser badly
  (precedence: splicing `g+1` into `2f` textually gives `2g+1`).

**Chosen mechanism.** The client keeps the environment (Svelte store), does
the §5 closure/ordering/cycle work in TypeScript on parse metadata it already
gets from `analyze` (free `symbols` per input — cached per binding at edit
time), and then makes **one** call to the `subs` binding with the
parents-first list serialized as `"f=g+1,g=x^2"` (values cannot contain
commas — §4 tokens: a comma inside an expression is a ParseError, so the
existing CSV split in `ms_subs` is safe). The returned `plain` string — which
re-parses to the same AST by the §5 round-trip invariant — feeds the actual
operation call (`solve`, `derivative`, `sample`, …) unchanged.

**One binding change is required:** `ms_subs` currently parses its input with
`parse_expression` (expressions only), while the CLI `subs` verb accepts
equations and substitutes into both sides (§10). v0.5 aligns the binding with
the CLI: `ms_subs` switches to `parse_input` and, for an Equation, returns
the rendered `lhs = rhs` (both styles) exactly like `ms_analyze`'s equation
arm. This is additive (expression behavior unchanged) and is the only
`wasm/bindings.cpp` edit in the whole feature. Additionally `ms_subs`'s
trailing `simplify()` is harmless here (simplify is value-preserving, §7) but
muddies "show me the resolved input" UX; add an optional third parameter
`simplifyResult` (default true, preserving current callers) and pass `false`
from the resolver path so chips/previews show the un-simplified resolved
form. `types.ts` (`EngineApi.subs`) updates in lockstep — it is the typed
mirror of the bindings by contract (its header comment).

Exclusion rules (§7) are enforced client-side: the tab's designated variable
(derivative/integral/solve/collect/plot variable, evaluate's explicit
bindings) is dropped from the active set and the §7 warning is rendered as a
UI notice.

---

## 9. Web UX and persistence

### 9.1 Variables panel

A "Variables" section in the existing sidebar, above History
(`web/src/App.svelte` already hosts the `<aside class="sidebar">`):

- **Rows** of `name := value`. Add row (empty name/value inputs), edit in
  place, delete per row, "clear all" with confirm.
- **Live validation + KaTeX preview** per row, debounced through the worker:
  name validated by the same single-symbol rule (§2.3; client-side check
  against `analyze(name)` returning one symbol); value validated by
  `analyze(value)` — parse errors show the existing caret treatment
  (`SpanHighlight`), success renders the value's `latex` via the `Katex`
  component. Cycle violations (§5.2) flag the offending row with the
  `a -> b -> a` message and mark it **inactive** (stored, but excluded from
  resolution) until edited — the panel, unlike the REPL, must tolerate
  transient invalid states mid-edit.
- **Equation values** are accepted (`analyze` kind `"equation"`) and badged
  `eq`; they participate only in the Solve tab per §4's placement rule, and
  other tabs show a muted chip `E_1 is an equation — not applied here` when
  referenced.
- **Indicator chips on the input**: whenever the current input's free symbols
  (from the live `analyze` the `ParsePreview` already runs) intersect the
  active environment closure, chips render under the input —
  `a := 2` `f := x^2 + 1` — so the user always knows a substitution will
  happen. The §7 exclusion warning renders as an amber chip:
  `x := 3 ignored (solving for x)`. Clicking a chip focuses its panel row.
- Result cards show the resolved input (the `subs` output, KaTeX) in the
  existing `MethodMeta`/notes area so "what did it actually compute" is
  always answerable.

### 9.2 Persistence format and migration safety

`localStorage` key `mathsolver.vars`, beside `mathsolver.history`. Schema is
**versioned from day one** (the history store shipped as a bare array and its
loader now carries filter/normalize scar tissue for legacy shapes — line
comments "Drop unknown/legacy tab ids", "Older/corrupt entries may lack
params"; do not repeat that):

```json
{
  "v": 1,
  "vars": [
    { "name": "a",   "value": "2",         "ts": 1752600000000 },
    { "name": "E_1", "value": "x + y = 3", "ts": 1752600001000 }
  ]
}
```

Values persist as **plain-printed §5 strings** (round-trip-guaranteed), never
serialized ASTs — the AST layout is not a stable format; strings survive
engine upgrades.

**Load rules (never throw, never block app start — same hardening doctrine as
`history.svelte.ts`, with one deliberate difference):**

- Whole-store failures (missing key, JSON parse failure, non-object, `vars`
  not an array) → empty environment. `try`/`catch` around everything.
- `v` missing or greater than known → still attempt the row parse below
  (forward-compatible best effort); unknown extra fields ignored.
- Per row: `name` and `value` must be non-empty strings (`name` ≤ 64 chars,
  `value` ≤ 1024); structurally malformed rows are dropped; duplicate names
  keep the **last** occurrence; cap 32 rows.
- **Grammar-invalid rows are kept, not dropped** — flagged inactive in the
  panel with their parse error. This is the deliberate difference from the
  history loader: history entries are disposable records, but a variable's
  value is *user data* — silently deleting `v_max := 2e3` because the user's
  cached app predates the scientific-notation change would destroy work.
  Inactive rows are excluded from resolution until the user edits them.
- The loader never writes back a normalized store at load time; persistence
  happens only on user edits (add/edit/delete/clear), mirroring the history
  store — a buggy loader must not be able to destructively rewrite storage.

Cross-tab: v1 loads once at startup, last-writer-wins on edit (same as
history). Live `storage`-event sync is a non-goal (§11).

---

## 10. Testing and verification

Gating assets, by name (REVIEW-NOTES "Verification assets"):

- **Round-trip + differential-eval fuzzer** (`tools/run_fuzz.sh`): the
  parser, printer, and factories are untouched, so this must stay clean
  *unchanged* — run it as the standing no-drift gate before and after each
  stage.
- **Simplify/expand and derivative differential fuzzers**: untouched engine →
  standing gate, no new configuration.
- **Acceptance battery** (`tools/run_acceptance.py` +
  `tests/acceptance/cases.tsv`): new rows pinning that one-shot CLI is
  *unchanged* — `simplify "x := 3"` remains exit 1 with
  `unexpected character ':'` (assignment is REPL/web-only), `subs`/`collect`
  rows from P0-7 still pass. NOTE: `cases.tsv` and `tests/test_cli.cpp` are
  under concurrent v0.4 edits — v0.5 rows land after v0.4 stabilizes
  (Stage 0).
- **Piped-REPL end-to-end tests** (`tests/test_cli.cpp`, §10's piped-stdin
  mechanism): the bulk of new coverage. Minimum matrix: define/echo; lazy
  chain `f := g + 1` / `g := x^2` / `f` → `x^2 + 1`; redefinition updates
  dependents; self-cycle and indirect-cycle errors (exact messages, session
  stays alive); `vars`/`unset`/`clear` including `unset` of an unknown name;
  constant/function/word/`E1` target errors; solve-excluded warning text;
  bare-equation disambiguation (`m := 2` → `m*x = 6` → `x = 3`); identity/
  contradiction prints; `eval`/`subs` explicit-binding shadowing notes;
  `latex f` NOT resolving; equation-name placement errors; named-equation
  system solve; definite-integral bounds resolving; overflow during
  resolution keeps the session alive.
- **Resolution parity vectors**: a shared table of (env, input, excluded) →
  expected plain output, consumed twice — by a `test_cli.cpp` REPL run and by
  a node script `tools/web_vars_test.mjs` driving the TS resolver module
  against the built wasm (patterned on `wasm_acceptance.mjs`). This is what
  keeps the C++ and TS implementations of §5 honest against each other, and
  it must include the order-independence property (same env defined in
  shuffled orders → identical output).
- **WASM/browser suite** (`tools/wasm_smoke.mjs`, `tools/wasm_acceptance.mjs`):
  rows for the `ms_subs` equation extension and `simplifyResult=false` mode;
  existing expression rows must pass unchanged. `npm run check`
  (svelte-check) gates the store/panel types.
- **Property called out explicitly** (asserted in both parity harnesses):
  for any acyclic env, `resolve(input, env)` is `structurally_equal` to the
  result of applying the same bindings via one `subs` verb invocation in
  parents-first order — resolution *is* subs, by construction.

Everything runs under the standing rule: `ctest --test-dir build` green +
`tools/run_fuzz.sh` + `tools/run_acceptance.py` at the end of every stage
(ROADMAP §3 preamble).

---

## 11. Non-goals (v1)

- **Multi-letter names** (`speed := 5`) — §6; unlocked later by ROADMAP
  P2-1, not by this feature.
- **Function definitions** (`f(x) := x^2`) — a lambda/arity concept, not a
  symbol binding; `f(x)` doesn't even parse as an application (§4,
  ROADMAP §5). `f := x^2` + `subs`/`eval` covers the practical need.
- **Engine- or wasm-side environments** — §3, §8.
- **Stateful one-shot CLI** (env files, `--let`) — the composable stateless
  equivalent is `subs`; persistent hidden state in a CLI invites
  irreproducible bug reports.
- **Eager/frozen bindings or a `freeze` command** — §5.1; revisit only with
  evidence.
- **Assumptions** (`x > 0`) — different feature (ROADMAP P1-6); an
  assignment binds a *value*, not a *property*.
- **Cross-tab live sync** of the variables store — §9.2.
- **Equation aliases** (`E_2 := E_1`) and equations inside expressions — §4.
- **Re-resolving expressions introduced by explicit `subs` arguments** — §7,
  single-pass doctrine.

---

## 12. Worked session transcripts

### 12.1 REPL

```
$ mathsolver
MathSolver 0.5.0 — type 'help' for commands
>>> a := 2
a := 2
>>> a^3 + a
10
>>> f := g + 1
f := g + 1
>>> g := x^2
g := x^2
>>> f
x^2 + 1
>>> diff f, x
2*x
>>> g := sin(x)
g := sin(x)
>>> diff f, x
cos(x)
>>> a := a + 1
error: 'a' cannot be defined in terms of itself
>>> b := c + 1
b := c + 1
>>> c := b^2
error: assignment would create a cycle: c -> b -> c
>>> vars
a := 2
f := g + 1
g := sin(x)
b := c + 1
>>> x := 3
x := 3
>>> solve x^2 = 9, x
x = -3
x = 3
method: rational roots + quadratic
warning: 'x' has an assigned value (x := 3), which is ignored while solving for it; 'unset x' removes the assignment
>>> unset x
>>> m := 2
m := 2
>>> m*x = 6
x = 3
method: linear
>>> E_1 := x + y = 3
E_1 := x + y = 3
>>> E_2 := x - y = 1
E_2 := x - y = 1
>>> solve E_1; E_2, x, y
x = 2
y = 1
method: gaussian elimination
>>> latex f
f
>>> subs f, g=t
t + 1
>>> integrate x^2, x, 0, a
value = 8/3
method: FTC
>>> clear
cleared 6 assignment(s)
>>> f
f
>>> quit
```

(Note the last `f`: with the environment cleared, `f` is an ordinary free
symbol again — simplify of a bare symbol prints itself.)

### 12.2 Web

1. User opens the app; the Variables panel (sidebar, above History) restores
   `a := 2` from `localStorage` and shows it with a KaTeX-rendered value.
2. On the **Simplify** tab they type `a x^2 + a` — the live parse preview
   renders it, and a chip `a := 2` appears under the input. Running it shows
   the result `2*x^2 + 2`, with a "computed from: 2·x² + 2" resolved-input
   line in the meta area, and the run lands in History.
3. They click **+ add** in the panel, type name `speed`, and get the inline
   error `variables are single letters (a-z), Greek names, or subscripted
   (v_1)…`; they rename it `v_max`, set the value `a^3`, and the row previews
   `v_max := a^3` (KaTeX: `v_{max} := a^{3}`), chips updating live.
4. They edit the value to `v_max + 1`; the row flags
   `'v_max' cannot be defined in terms of itself` and grays out (inactive)
   until fixed back to `a^3`.
5. On the **Solve** tab they enter `x^2 = v_max + 1`, chips show `v_max := a^3`
   and `a := 2`; solving yields `x = -3`, `x = 3` (resolution: `v_max → a^3 →
   8`). They then add a row `x := 1`; the Solve tab now shows the amber chip
   `x := 1 ignored (solving for x)` and the same roots.
6. They add `E_1 := x + y = 3` (badged `eq`); on the Solve tab, input
   `E_1; x - y = 1` with variables `x, y` returns `x = 2`, `y = 1`. Switching
   to the Derivative tab with `E_1` in the input shows the muted chip
   `E_1 is an equation — not applied here` and the parse preview's normal
   guidance.
7. They reload the browser: all rows return; a row hand-edited in devtools to
   `{"name":"q","value":"2^"}` comes back flagged inactive with its parse
   error instead of vanishing.

---

## 13. Staged implementation plan

Effort scale per ROADMAP: S < 1 day, M = days, L = week+. Total: **M–L
(≈ 5 days)**. Stages are independently landable; each ends with the §10
standing gate green.

- **Stage 0 — preflight (S, hours).** Confirm v0.4 P0-7 has landed: CLI
  `subs`/`collect` pass their acceptance rows, `ms_subs`/`ms_collect` exist
  in the wasm build, `types.ts` has their entries. Confirm `simplify "x := 3"`
  is still the `':'` lex error and add the pinning acceptance row. **Do not
  start Stage 1 while `apps/main.cpp` / `wasm/bindings.cpp` /
  `tests/test_cli.cpp` / `tests/acceptance/cases.tsv` are under concurrent
  v0.4 edits.**
- **Stage 1 — REPL environment (M, ~2 days).** `apps/main.cpp` only:
  `Binding`/environment struct, `:=` dispatch + target validation (§2.3),
  definition-time parse + cycle check (§5.2), `resolve()` (§5), verb
  integration table (§7), `vars`/`unset`/`clear`, warning/note strings
  verbatim from §7. Piped-REPL tests in `tests/test_cli.cpp` (§10 matrix) +
  the shared parity-vector table. Gate: ctest, acceptance, run_fuzz (no
  drift).
- **Stage 2 — wasm alignment (S, ~half day).** `wasm/bindings.cpp`:
  `ms_subs` → `parse_input` + equation rendering + optional `simplifyResult`
  flag; `types.ts` mirror. Extend `wasm_smoke.mjs`/`wasm_acceptance.mjs`.
  Gate: wasm suites + existing rows unchanged.
- **Stage 3 — web store, resolver, panel (M, ~2 days).**
  `web/src/lib/vars.svelte.ts` (store + §9.2 persistence),
  `web/src/lib/vars/resolve.ts` (pure TS §5 algorithm — closure, topo order,
  cycle check; no Svelte imports so the node harness can drive it),
  `VariablesPanel.svelte`, input chips + exclusion warnings in `App.svelte`
  tabs, resolved-input display in result meta. New
  `tools/web_vars_test.mjs` running the parity vectors + persistence-loader
  cases (malformed stores from §9.2). Gate: `npm run check`,
  `web_vars_test.mjs`, wasm suites.
- **Stage 4 — docs (S, ~half day).** DESIGN §10 gains the environment
  contract (condensed from §§2–7 here); docs/GRAMMAR.md gains an
  "Assignments (REPL and web)" section stating `:=` is *not* part of the
  expression grammar; README + ROADMAP §5 amendment ("REPL stateless"
  sentence → points here); CHANGELOG entry. Same-commit rule for doc/behavior
  pairing per ROADMAP precedent.
