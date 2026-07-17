# Web App UX Findings — Fresh-Eyes Review

Reviewed 2026-07-16 against the live build (`cd web && npm run build && npm run preview`,
engine chip **v0.3.0**, staged WASM in `web/src/lib/wasm/`). Every finding below was
observed by actually driving the site in Chrome (puppeteer-core) as three personas:

- **Student** — iPhone-size viewport (390x844, touch): quadratics, derivatives, definite integrals.
- **Teacher** — laptop (1440x900): systems, plots, copying LaTeX into slides.
- **Engineer** — 1280x800: evaluate with bindings, scientific notation, grammar probes.

Findings are ordered most severe first. "Stale artifact" marks issues caused by the
staged WASM predating the current parser — they will disappear on rebuild, but they are
what a user of *this* build experiences today, and they point at a missing guardrail.

---

## 1. CRITICAL — Shipped example chips fail or silently return wrong answers (stale artifact + missing guardrail)

**Persona:** all three (chips are the first thing every persona reached for).

**Issue:** `web/src/lib/tabs.ts` was updated for the new grammar (°, π, `|…|`,
scientific notation) but the bundled WASM predates it. Three of the app's own example
chips are broken in the live build — two error, one silently computes a **wrong number**:

| Chip (tab) | Web build result | Current CLI result |
|---|---|---|
| `2e-3 + 1e2` (Evaluate) | parses as `2e + 1e - 3` → **7.87313** (e = Euler) | `50001/500` = 100.002 |
| `sin(30°)` (Simplify) | `unexpected character '\xC2\xB0'` | `1/2` |
| `\|3 - π\|` (Simplify) | `unexpected character '\xCF\x80'` | `abs(-pi + 3)` |

The Evaluate case is the worst kind of failure: a student clicking the app's own
suggestion gets a plausible-looking wrong answer with no warning. The engineer's `2e3`
likewise simplifies to `6e` instead of `2000`.

**Evidence:** clicked the `2e-3 + 1e2` chip on Evaluate → parse preview showed
"as parsed: 2e + 2e − 3", Compute → `7.87313`. Typed `2e3` on Simplify → result card `6e`.
Typed the two Simplify chips → red byte-escape errors (screenshots `e02-scinot-chip`,
`e03-2e3`, `e04-abs-pi`).

**Suggested fix:** rebuild `web/src/lib/wasm/` from current sources before the next
deploy; add an automated check (unit test or CI step) that runs **every example string
in `tabs.ts` through the bundled WASM's `analyze`** so chips and engine can never drift
apart again.

---

## 2. HIGH — Word guard missing in web build: `solve x^2 = 4` returns algebra garbage (stale artifact)

**Persona:** student (this is the single most likely first thing a student types).

**Issue:** the current parser rejects `solve x^2=4` with a helpful message
(`unknown name 'solve': variables are single letters (x, y), greek names (alpha), or
subscripted (x_1)…`). The staged WASM predates the guard, so the web app parses it as
the product `e·l·o·s·v·x^2 = 4` and Solve proudly answers
`x = ±2e⁻¹o⁻¹s⁻¹v⁻¹√(elosv)/l — method: quadratic formula`, plus warnings about
`16*e*l*o*s*v >= 0`. To a homework-doing student this is meaningless noise with no hint
that the *input* was the problem.

**Evidence:** typed `solve x^2 = 4` on the Solve tab; parse preview read
"as parsed: elosvx² = 4" and Compute produced the garbage solutions
(screenshot `m06-word-guard`).

**Suggested fix:** the WASM rebuild restores the guard. Additionally worth doing in the
web layer regardless: when input starts with a keyword-like token (`solve`, `simplify`,
`factor`, …), show a UI-level hint — "just type the equation; the tabs choose the
operation" — since the engine's guard message talks about greek letters and products,
which is parser-truthful but not what a web user needs to hear.

---

## 3. HIGH — Solve / System / Evaluate results have no copy affordance; manual selection of KaTeX copies garbage

**Persona:** teacher (blocking for the stated slide-preparation workflow).

**Issue:** only Transform (simplify/expand/factor) and Integral/Definite results render
`Plain`/`LaTeX` copy fields. `ResultSolve.svelte`, `ResultSystem.svelte`, and
`ResultEvaluate.svelte` render none. A teacher who solves `x + y = 3; x - y = 1` and
wants `x=2,\; y=1` in LaTeX for slides has nothing to click. The natural fallback —
selecting the rendered math with the mouse — yields KaTeX's doubled internal text
(`𝑥 = − 2 x=−2`), which is unusable when pasted.

**Evidence:** solved the system via example chip → result card contained zero buttons
(`SYSTEM-COPY-BUTTONS: []`). Solved `x^2 = 4` → zero `.copy-field` elements.
Programmatically selected the result KaTeX and read the selection:
`"𝑥\n=\n−\n2\nx=−2"`. By contrast, Expand's LaTeX Copy button worked perfectly and the
clipboard received `x^{3} - 4x^{2} - 3x + 18` (screenshots `t02-system-result`,
`t03-expand-result`).

**Suggested fix:** add the same `CopyField` pair to solve/system/evaluate results — the
engine already returns `latex` and `plain` per solution; a combined "all solutions"
LaTeX string (e.g. `x = 2,\; y = 1`) would match the slide use-case best.

---

## 4. HIGH — Restoring a history entry duplicates it (history fills with clones)

**Persona:** all; hits teacher hardest (history is the workflow for re-showing examples).

**Issue:** `restore()` re-runs `compute()`, and every successful compute unconditionally
`history.add(...)`s. Each click on a history entry therefore prepends an identical copy.
The list (capped at 50) silently fills with duplicates, pushing real work out.

**Evidence:** ran `2x + 3x` once (1 entry), clicked the entry twice → **3 identical
entries** (`ENTRIES-AFTER-2-RESTORES: 3`; visible as a stack of identical "SIMPLIFY
2x + 3x / 5*x" cards in screenshot `e05-long-expr`).

**Suggested fix:** on add, drop an existing entry with identical `(tab, input, params)`
and re-insert at top (move-to-top semantics), or skip the add when restoring.

---

## 5. MEDIUM — Parse errors are shown twice, identically, stacked

**Persona:** student (typos are their most common event).

**Issue:** with a typo like `x^2 - 5x + = 0`, the live parse preview shows
`unexpected '='` with the span-highlighted source; pressing Enter then renders the
*same message and same highlight* a second time in the result card, ~120px lower. Two
identical red blocks for one mistake reads as two errors and doubles the visual noise on
a phone.

**Evidence:** screenshot `m05-typo-after-enter` shows both blocks simultaneously.

**Suggested fix:** when the outcome error equals the current analysis error, suppress
the result card (or suppress the preview while an identical error card is shown). The
span highlight itself is excellent — keep one instance of it.

---

## 6. MEDIUM — Enter never computes from any secondary field (definite bounds, bindings, ranges)

**Persona:** student (definite integrals), engineer (bindings), teacher (plot range).

**Issue:** Enter-to-compute only works inside the main textarea. The natural flow
"type expression → tab/tap into *From*/*To* (or a binding) → type value → **Enter**"
does nothing, everywhere: definite integral bounds, Evaluate binding fields, Solve
numeric range, Plot range. Users must mouse back to Compute or refocus the textarea.

**Evidence:** on Integral with Definite checked, focused the *To* field and pressed
Enter → no result card appeared (`DEFINITE-ENTER-FROM-BOUNDS: null`); same for an
Evaluate binding (`EVAL-ENTER-IN-BINDING: null`); Enter in the Plot *From* field added
no history entry (`ENTER-IN-NUMFIELD historyBefore/after: 0 0`).

**Suggested fix:** wrap the workbench in a `<form onsubmit={compute}>` (inputs then get
Enter-to-submit for free), or add the same `onkeydown` handler to the ctl inputs.

---

## 7. MEDIUM — Example chips don't hand focus (or the result) back to the flow

**Persona:** teacher/keyboard users, but affects everyone's first run.

**Issue:** clicking an example chip fills the input but focus stays on the chip and
nothing computes. Pressing Enter at that point re-clicks the *chip* (re-filling the
input), not Compute. The user must make a second, different gesture (click Compute or
refocus the textarea) to see anything.

**Evidence:** clicked the `x + y = 3; x - y = 1` chip → `document.activeElement` was the
chip button; Enter produced no result (`SYSTEM-AFTER-ENTER-ON-CHIP: null`); only a
subsequent Compute click ran it.

**Suggested fix:** chips should set the input **and focus the textarea** (cursor at
end); consider computing immediately — a sample is exactly the case where the user wants
to see the answer.

---

## 8. MEDIUM — Plot: no zoom/pan/readout, and y-autofit can hide the interesting region

**Persona:** teacher.

**Issue (a):** the range can only be changed by typing numbers into two small fields.
Mouse wheel and drag on the canvas do nothing; there is no cursor coordinate readout and
no export button (Chrome's right-click "Save image as" works on the canvas but nothing
advertises it).
**Issue (b):** the y-range fits all sampled values, so on `x^3 - 3x` over `[-4, 10]` the
axis spans ±1000 and the local max/min at x=±1 — the entire pedagogical point of that
example — flattens into a line hugging y=0. The IQR outlier clipping doesn't trigger for
smoothly-growing cubics.

**Evidence:** plotted `x^3 - 3x` (its own example chip) with From −4 / To 10 → curve
visually indistinguishable from y=0 for x∈[−2,2] (screenshot `p03-overlays-cubic`);
`mouse.wheel` over the canvas changed nothing; canvas has no `title`, no buttons
(`PLOT-BUTTONS: []`).

**Suggested fix (incremental):** wheel-to-zoom x-range about the cursor + drag-to-pan
writing back into From/To; a hover crosshair with `(x, f(x))`; an optional y-range pair
of fields; a small "Save PNG" button. Any one of these materially improves the tab.

**Positive:** the overlay checkboxes are discoverable, the "Antiderivative" box honestly
disables itself with a "no closed form" hint for `sin(x)/x`, the legend appears when
overlays are on, and the inverted-range message ("Enter a valid range (from < to)")
works (screenshot `p01-inverted-range`).

---

## 9. MEDIUM — Plot range fields accept junk / go empty while the plot silently falls back

**Persona:** teacher.

**Issue:** if a range field ends up empty or with unparseable content, the plot keeps
rendering using the numeric fallback (−10/10) with no message, so what's drawn no longer
matches what the fields say. (Number inputs also happily *display* strings like
`-10-530` while reporting an empty value.)

**Evidence:** cleared the *From* field → fields read `["", "10"]`, no overlay message,
plot still drawn on the previous domain (screenshot `p02-cleared-from`); earlier
appended-text state visible in screenshot `t06-plot-invalid-range` with the plot
unchanged.

**Suggested fix:** treat an empty/NaN field like the inverted-range case — show the
"Enter a valid range" overlay instead of silently plotting a fallback domain.

---

## 10. MEDIUM — Mobile: 3 of 8 tabs are off-screen with only a lucky truncation as a hint

**Persona:** student on phone.

**Issue:** at 390px the tab strip is 608px wide; **Integral, Evaluate and Plot start
off-screen** (Derivative is clipped mid-word to "Derivat"). The clipped label is the
only scroll affordance — no fade, no gradient, and the OS scrollbar is hidden until you
already scroll. A student needing the Integral tab may not realize more tabs exist; at
other widths a tab could end exactly at the edge leaving no hint at all.

**Evidence:** measured `tabsScrollW: 608` vs `tabsClientW: 358`; screenshot
`m01-first-run` shows the strip ending at "Derivat".

**Suggested fix:** add an edge fade (mask-image gradient) or chevron when
`scrollWidth > clientWidth`; alternatively let tabs wrap to two rows on narrow screens —
eight short labels wrap cleanly.

---

## 11. MEDIUM — Mobile tap targets: 13px checkboxes

**Persona:** student on phone.

**Issue:** the Definite / Numeric-search-range / f′-overlay / Antiderivative checkboxes
render at 13x13 px. The clickable label helps, but the visible target is far below the
~44px guideline and the label text is muted, so it doesn't read as tappable.

**Evidence:** measured the Definite checkbox bounding box `{"w":13,"h":13}`
(screenshot `m08-definite-controls`).

**Suggested fix:** scale checkboxes up on coarse pointers
(`@media (pointer: coarse) { .ctl.checkbox input { width/height: 20px } }`) and add
padding to the label hit area.

---

## 12. LOW — Raw UTF-8 byte escapes in user-facing errors (engine-wide, not just stale WASM)

**Persona:** student/teacher (anyone typing `π`, `°`, or pasting from a doc).

**Issue:** unsupported characters produce `unexpected character '\xC2\xA7'`-style
messages. The current CLI does the same for genuinely unsupported chars (verified with
`§`), so a rebuilt WASM will still show byte escapes to web users who paste, e.g., smart
quotes or `×`.

**Evidence:** web build: `unexpected character '\xCF\x80'` for `π`; CLI today:
`unexpected character '\xC2\xA7'` for `§`.

**Suggested fix:** print the offending character itself (it is by definition printable
enough to have been typed) with the escape as a parenthetical, e.g.
`unexpected character '§' (U+00A7)`.

---

## 13. LOW — Plot runs only reach History via the Compute button

**Persona:** teacher.

**Issue:** the plot renders live as you type (nice), but nothing is recorded in History
unless you *also* press Compute/Enter — unlike every other tab where seeing a result and
having it in history coincide. Teachers who type a function, admire the plot, and move
on will find no trace of it later.

**Evidence:** typed `x^3 - 3x` on Plot, watched it render, never pressed Compute →
sidebar still showed "Computations you run will appear here" (screenshot
`p03-overlays-cubic`).

**Suggested fix:** debounce-add (or update-in-place) a history entry when a plot
successfully samples, or label the button "Save to history" on the Plot tab so the model
is explicit.

---

## 14. LOW — Redundant "≈" chip when the exact solution is already an integer

**Persona:** student.

**Issue:** solving `x^2 - 5x + 6 = 0` renders `x = 2  [≈ 2]` and `x = 3  [≈ 3]` — the
approximation chip repeats the exact value and adds noise exactly where the answer
should be cleanest.

**Evidence:** screenshot `m03-quadratic-result`.

**Suggested fix:** suppress the approx chip when `fmt(approx)` equals the exact string
(or when the solution is rational with a short decimal form).

---

## 15. LOW — Escape does nothing; "Lo/Hi" vs "From/To" wording; Clear has no undo

**Persona:** all (minor polish).

- **Escape** in the expression input neither clears the input nor dismisses a stale
  result (verified: value unchanged after Esc). A stale result card sitting under
  newly-typed input is mildly misleading; Esc-to-dismiss would be cheap.
- Solve's numeric range fields are labelled **Lo / Hi** while Integral and Plot use
  **From / To** — same concept, two vocabularies.
- History **Clear** wipes up to 50 entries in one click with no confirmation or undo.

---

## What already works well (worth preserving)

- **First-run comprehension is good**: per-tab placeholders, example chips, and the live
  "as parsed:" KaTeX preview make it obvious what to type and what the engine understood
  (screenshots `m01-first-run`, `t01-first-run`).
- **Error span highlighting** (wavy underline on the offending token, correct even for
  multi-byte input thanks to the UTF-8→UTF-16 conversion) is better than most CAS UIs.
- **Enter-to-compute from the textarea** works; tab strip supports Arrow/Home/End keys;
  the mobile layout stacks the full-width Compute button directly under the input, well
  within thumb reach.
- **Result readability**: KaTeX at ~21px, display results scroll horizontally instead of
  breaking the page, copy buttons (where present) give clear "✓ Copied" feedback and put
  clean LaTeX on the clipboard.
- **Theme**: Auto → Light → Dark cycle is labelled, the canvas plot re-renders with
  theme colors, and both themes look consistent (screenshots `t04-plot-default`,
  `p05-overlays-light`).
- **History persists** across reloads (localStorage), restores tab + parameters
  correctly, and the ≥1100px sidebar keeps it visible without stealing focus.
