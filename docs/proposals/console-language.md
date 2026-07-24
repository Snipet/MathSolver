# Proposal: A Real Language for the Console

Status: **proposed** (Phase 0 — this document; no code yet). Size: **XL**
(multi-phase, touches every surface). Grounded in `apps/main.cpp`,
`web/src/lib/notebook/run.ts`, `apps/ink/src/core/intent.ts`, and
`web/src/lib/graph/lists.ts`.

Goal: grow what users already type — `x := 3`, `f(x) = x^2 - 3`, `diff …` —
into a small scripting language with the features people expect (lists,
conditionals, loops, multi-statement functions), **without** losing the
math-first feel and **without** forking the three surfaces further apart.

Prerequisite reading: `docs/proposals/variable-assignment.md` is the normative
spec for the environment layer (every `§N` reference in `web/src/lib/vars/*.ts`
and `apps/main.cpp` points at it); `docs/GRAMMAR.md` is the user-facing grammar
contract; `docs/proposals/next-features.md` T4 and
`docs/proposals/inequalities.md` both stake claims on syntax this proposal
needs.

---

## 1. Current state: four half-languages, none shared

There is no "the language". There are four independent text layers that each
implement part of one. Every layer is line-oriented, textual, and
**pre-parser** — the core AST never sees any of it.

**A. The C++ REPL** (`apps/main.cpp`). `repl_line` (:3359) splits an input into
assignment / bare expression / bare equation / verb-with-comma-arguments;
`repl_command` (:2844) dispatches the verbs; `is_repl_command` (:2355) is the
table. The environment is `std::vector<Binding>` (:2392) holding **parsed
ASTs**, applied by composing `substitute()` (DESIGN §10).

**B. The web console** (`web/src/lib/notebook/run.ts`, 1270 lines). `runLine`
(:1197) re-implements the same grammar in TypeScript and then extends it well
past it: ~90 verbs, plugin commands (`dsp.butter …`), `plot`, `wave`, user
functions, cell slider overrides, and a per-notebook `ScopeEnv`. Its bindings
store **strings**, not ASTs — the same environment, modelled twice, two
different ways.

**C. The Ink terminal app** (`apps/ink/src/core/intent.ts`, 364 lines).
`parseLine` is a third implementation — pure and exhaustively unit-tested, the
cleanest of the three (parse → execute → format is properly separated). Its
header already records the drift: session assignments are *not modelled at
all*, so `x := 3` returns a friendly note.

**D. The grapher's list layer** (`web/src/lib/graph/lists.ts`). The surprise: a
genuine list language already exists, but only inside the grapher. Literals
`[1, 2, 3]`, ranges `[a...b]` / `[a, b...c]`, element-wise broadcasting,
aggregates (`mean`, `total`, `max`, …), slices, and list transforms (`sort`,
`unique`, `reverse`, `join`). It is a pure *text* layer that rewrites strings
and hands scalars to the engine. The console cannot see any of it.

**How far apart they already are.** The CLI has **no user functions** (`f(x) =
…` appears nowhere in `apps/main.cpp`) and **no plugins at all**. It also lacks
`plot`, `wave`, `steps`, `funcs`, notebooks, inequality solving, and the whole
interpolation family. Ink has no environment. The grapher has lists nobody else
has. Parity is upheld by comments — `run.ts:3-8` and `intent.ts:3` both say
"mirrors `apps/main.cpp`" — and by hand-copied code. It is a convention, not a
mechanism, and it has already lost.

**The common root: everything is a string.** `f(x) = x^2` is beta-reduction by
textual substitution through the engine's `subs` verb
(`web/src/lib/graph/expand.ts:19-30` — two-phase via fresh placeholders,
because `subs` applies its CSV sequentially). `x := 3` is `substitute()`
composition. Verb arguments are split on top-level commas and re-parsed.
Results are `{plain, latex}` render envelopes, never values a later line can
consume except by pasting text back in.

The strain shows in the escape hatches: `fit <data> | <model>` needs a pipe
because the data contains both commas and semicolons; `grad`/`curl`/`vecfield`
take `;`-separated components; `interp`/`stats`/`gcd` take the entire remainder
un-split; `solve x^2 < 4` splits the relation textually because the parser
rejects `<`. Six bespoke sub-grammars, all working around the same missing
thing.

**This works remarkably well for math, and it is exactly why the language
cannot grow: a list, a boolean, a loop variable, or a string has nowhere to
live.**

## 2. The constraint that decides the design

Adding conditionals, loops, and local scope means writing a real evaluator.
Writing it three times is not a plan. So the first decision is not "which
features" but **where the language lives**.

### Option 1 — TypeScript only, in `run.ts` (rejected)

Fastest to ship and web-first, which matches the stated priority. But it
permanently forks the CLI, and the §10 contract would have to be deleted rather
than honored. Ink — which already loads the same WASM module — would be
stranded. Rejected not because it is wrong for the web, but because "web-first
*with CLI parity*" is the standing requirement and this delivers only the first
half.

### Option 2 — a `script` module in the C++ core, shared via WASM (recommended)

A new module, above the CAS and outside the pure-expression engine:

| Module | Files | Depends on |
|--------|-------|------------|
| script | `script.hpp/.cpp` | expr, parser, printer, evaluator, simplify, solver |

It owns a tokenizer, a statement parser, a typed `Value`, an environment, and a
builtin registry. The CAS becomes its arithmetic. Every surface becomes a thin
front end:

- `apps/main.cpp` — the REPL feeds lines to `script::eval` and prints the
  resulting `Value`. The verb switch stays, reached as builtins rather than as
  a parallel grammar.
- The web console — one new WASM entry point; `run.ts` keeps its
  console-specific extras (plugins, `plot`, `wave`, sliders) as **builtins
  registered from the host**, not as a second grammar.
- Ink — gets the language *and* the environment it never had, for free. This
  matters disproportionately: Ink is otherwise the **largest** port cost of the
  three (a grammar change today means editing `intent.ts`, `execute.ts`,
  `format.ts`, and `help.ts`, plus building an environment from scratch —
  roughly the ~780 lines of `vars/` and `graph/registry.ts`), and therefore the
  surface most likely to be quietly left behind.

**Engine purity is preserved, not violated.** DESIGN §10 says no header under
`include/mathsolver/` and no file under `src/` knows about environments. The
script module *is* where environments are allowed to live, so the rule becomes
"nothing below `script` knows about environments" — `expr`, `simplify`,
`solver` stay pure. This needs a new DESIGN §13 stating the boundary, §10
amended to say the REPL grammar is `script`'s, and `docs/GRAMMAR.md` updated
(its `:360` currently states flatly that `:=` is not part of the expression
grammar — which stays true, since `script` sits *above* the expression
grammar).

Cost, honestly: a C++ evaluator is more work than a TS one; the WASM surface
grows; and the console's plugin/plot verbs need a host-callback shape that does
not exist yet. Phase 1 is designed to de-risk exactly that before anything
depends on it.

## 3. Value model

The crux. `Expr` stays the default so every existing verb keeps working
untouched:

```cpp
struct Value; using ValuePtr = std::shared_ptr<const Value>;
struct Value {
    std::variant<Expr,                    // the default — every existing verb
                 Equation,                // already a first-class REPL value
                 std::vector<ValuePtr>,   // list
                 bool,                    // condition results
                 std::string,             // labels, formatting
                 Closure>                 // user function, incl. multi-statement
        v;
};
```

Rules that keep the math feel:

- A bare `Expr` value prints and behaves exactly as today. Nothing regresses.
- Arithmetic on lists **broadcasts element-wise**, matching the grapher's
  existing semantics (`2*L`, `L^2`) rather than inventing new ones.
- Equations keep their existing placement rule — legal only where a whole
  equation is expected (DESIGN §10, unchanged).
- **A list is a script value, not an `Expr`.** The 7-node AST
  (`include/mathsolver/expr.hpp:21`) is not extended. This is deliberate and it
  is where this proposal and `next-features.md` T4 diverge: T4 wants a `Matrix`
  node *inside* the AST so `det [[1,2],[3,4]]` is one expression. Both can be
  true eventually, but they must not be built twice — see Open Question 3.

## 4. Syntax decisions forced by what already exists

These are not free choices. The repo has a standing doctrine for adding
keywords, stated at DESIGN.md:922 — **a new keyword must previously have been a
parse error**, so claiming it breaks no working input. That test decides most
of the following.

- **`;` cannot separate statements.** `solve` already uses top-level `;` for
  systems, and so do `grad`/`curl`/`vecfield`. Statements are separated by
  **newlines**; the console already accepts Shift+Enter newlines in a cell, and
  the CLI gains a script-file path (today the only script mechanism is piping
  stdin at the REPL, which DESIGN.md:930 mandates as an e2e test).
- **`[1, 2, 3]` is safe to claim.** `src/parser.cpp:756` treats `[` as a
  grouping synonym for `(`, but a comma inside a group is a parse error
  (GRAMMAR.md:178) — so a bracket group containing a top-level comma or `...`
  is *currently* an error and is free. `[x + 1]` keeps meaning `(x + 1)`, and
  `\sqrt[n]{x}` (`src/parser.cpp:927`) is untouched.
- **`=` stays "equation", `:=` stays "bind".** So there is no `==`. An `if`
  condition can simply *be* an equation or inequality — `repl_bare_equation`
  (`apps/main.cpp:2801`) already folds a closed equation to `equation holds` /
  `equation is false`, including the honest numeric-caveat case. That is the
  truth test; conditionals reuse it rather than duplicating it. Getting `<` and
  `>` past the lexer at all is `docs/proposals/inequalities.md`'s job — this
  proposal **depends on** it rather than re-deriving it.
- **Half the obvious keywords are already taken.** The word guard
  (DESIGN.md:190) rejects a run containing **3 or more single-letter
  *symbols*** — and `e` and `i` are *constants*, not symbols, so they do not
  count toward the three. That makes the free/taken split unintuitive, and it
  has to be measured rather than guessed. Tested against the built binary
  (`mathsolver simplify <word>`):

  | Free (parse error today) | Taken (a valid product today) |
  |---|---|
  | `for`, `while`, `then`, `return`, `repeat`, `map`, `filter`, `otherwise` | `if` → `i*f`, `else` → `l*s*e^2`, `in` → `i*n`, `let` → `e*l*t`, `end` → `e*d*n`, `do` → `d*o`, `elif` → `e*i*f*l` |

  So **`if` and `else` — the two most obvious keywords — are both unavailable**
  under the doctrine, while `then`, `while`, and `for` are free. The
  conditional needs either a different shape or an accepted, explicitly
  argued breaking change. This must be settled before Phase 2 (Open Question 1).
- **Two-argument function *calls* are frozen.** GRAMMAR.md:408 — "there are no
  two-argument functions like `\max(a, b)`" — and a comma in parens is a parse
  error. So `diff(f, x)` as callable syntax is a real grammar change, not a
  convenience. See Open Question 2.
- **Reserved names.** `RESERVED_NAMES` (`web/src/lib/graph/functions.ts:19`)
  already guards builtins and constants; keywords join it.

## 5. Phasing

Each phase is independently shippable and independently verifiable, the way
step-by-step was.

- **Phase 0 — this document.**
- **Phase 1 — the evaluator, with no new user-visible syntax.** Build
  `script.hpp/.cpp`: tokenizer, statement parser, `Value`, environment, builtin
  registry. Port `apps/main.cpp`'s REPL onto it. **The gate is that nothing
  changes**: the existing CTest end-to-end suite (including the piped-stdin
  REPL session) and the Ink unit tests must pass unmodified. This is where the
  architecture is proven or abandoned.
- **Phase 2 — lists, comparisons, conditionals.** List literals, ranges,
  indexing, slicing; element-wise broadcasting; the grapher's aggregates and
  transforms promoted into core builtins so `lists.ts` becomes a thin caller
  instead of a private implementation. Conditionals built on the existing
  equation-truth path, after the keyword question above is settled. Sequenced
  after (or alongside) `inequalities.md`, which owns the comparison lexing.
- **Phase 3 — iteration and real functions.** `for` over a list or range;
  comprehensions (`[f(k) for k in 1...10]`) generalizing the grapher's existing
  broadcasting; multi-statement function bodies with **local scope** and a
  return value. Two known decisions land here:
  - Local scope has a precedent to follow — `ScopeEnv`
    (`web/src/lib/vars/session.ts:129`) already establishes "notebook scope
    does not leak into the session". But user *functions* are **not** scoped
    today: `sessionFunctions` is a module-global singleton
    (`web/src/lib/graph/registry.ts:262`) and `runFunctionDef` ignores scope
    entirely, so a notebook run's `f(x) = …` leaks. `grapher-functions.md:315`
    flags this as an open decision; Phase 3 must close it.
  - **Recursion is currently rejected outright** (`registry.ts:168` — "`'f'` is
    defined recursively (not supported)") because inlining cannot terminate
    without a base case. Conditionals plus a real evaluator remove that
    obstacle. Whether to allow recursion — and with what depth bound — is a
    deliberate Phase 3 choice, not an accident.
- **Phase 4 — ergonomics.** Strings and `print`; error semantics (proposed:
  stop at the first failing statement, report its index, keep prior bindings —
  matching the REPL's existing "errors keep the session alive" behavior);
  multi-line cells as a first-class notebook concept.
- **Phase 5 — surfaces.** WASM binding; console rendering of the new value
  kinds (a list result card, a script transcript with per-statement output);
  Ink; a CLI script-file mode. Reference-panel entries and runnable examples
  for every new form, per the existing house rule. `docs/GRAMMAR.md` updated in
  the same PR as each user-visible change, not at the end.

## 6. Non-goals

- Not a general-purpose programming language. No modules, classes, mutable
  aliasing, I/O, or an FFI. If a feature does not make a math session shorter
  or clearer, it does not belong.
- Not a replacement for the plugin system. Plugins stay the extension point for
  heavy numerics; the language calls them, it does not subsume them.
- Not the matrix feature. `linalg` keeps its own private matrix syntax
  (`plugins/linalg/linalg.cpp:55-120`) until Open Question 3 is answered.
- No breaking changes to existing input. Every line that works today keeps
  working and keeps printing the same thing — Phase 1's explicit gate, and a
  gate for every later phase.
- Not a performance story. The evaluator is for small scripts; anything hot
  belongs in a plugin.

## 7. Open questions

1. **What shape does the conditional take,** given both `if` and `else` are
   already valid products (§4)? A different keyword pair (`when`/`otherwise`),
   a `then`-led form, or an accepted breaking change argued on its merits.
   This blocks Phase 2.
2. **How much of the verb table becomes callable syntax?** `diff(f, x)` inside
   a loop is far more useful than `diff f, x` as a line — but multi-argument
   calls are frozen by GRAMMAR.md:408, so this is a grammar change with its own
   blast radius. The line form must stay regardless.
3. **Script lists vs. an AST `Matrix` node** (`next-features.md` T4). Are they
   the same feature or two layers? Answering this late means building list
   semantics twice.
4. **Do plugin commands become builtins?** They are host-provided on the web
   and compiled in natively; one registry shape has to serve both.
5. **Does the grapher adopt the shared list implementation in Phase 2**, or
   keep its text layer until Phase 5? Adopting early is more work but retires a
   duplicate language sooner.
