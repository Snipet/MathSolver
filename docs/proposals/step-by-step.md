# Proposal: Step-by-Step Explanations

Status: **in progress** — Phase 1 (this PR) adds the derivative step recorder
(core + CLI + tests). Size: **L** (multi-phase). The flagship student-facing
feature and the main commercial-CAS parity gap (Symbolab/Wolfram "show steps").

Goal: for the operations students most want worked out — **derivatives**,
**integrals**, and **equation solving** — return not just the answer but the
ordered list of rules applied, each rendered in plain text and LaTeX, so the
console (and CLI) can show a worked solution. The engine already walks these
rule trees; this feature records the walk.

---

## 1. Approach

Each explainable operation gains a parallel "recording" pass that mirrors the
existing recursive solver but accumulates a list of steps instead of only the
result. The result is always identical to the non-explained verb (the recorder
is a superset), so the feature never changes existing answers.

```cpp
struct ExplainStep {
    std::string rule;   // short rule name, e.g. "power rule", "chain rule"
    std::string plain;  // "d/dx(x^3) = 3*x^2"
    std::string latex;  // LaTeX rendering of the same line
};
struct Explanation {
    std::vector<ExplainStep> steps; // innermost-first, ending with "simplify"
    std::string result_plain;       // == the plain verb's output
    std::string result_latex;
};
```

## 2. Phasing

- **Phase 1 (this PR) — derivatives.** `explain_derivative(e, symbol)` mirrors
  `differentiate`, recording one step per rule application (sum, product, power,
  exponential `e^u`, general `u^v`, chain rule through the function table), then
  a final "simplify" step. Wired to a CLI `steps` verb (`steps diff <expr>`);
  native Catch2 tests assert the rule sequence and that the last step equals the
  plain `diff` output. **Core + CLI + tests + docs; the web UI is Phase 1b.**
- **Phase 1b — web.** A WASM `explainDerivative` binding + a "Show steps" toggle
  on the console derivative result that renders the step list (LaTeX via the
  existing typeset path).
- **Phase 2 — integrals.** Record the rule chosen per term (linearity, power
  rule, u-substitution, by-parts, partial fractions) alongside the existing
  `method:` note.
- **Phase 3 — solving.** Record the algebra of `solve` (isolate, factor,
  quadratic formula, …).

## 3. Non-goals

- Not a proof assistant: steps mirror the engine's actual decisions, not an
  idealized textbook derivation.
- No natural-language paragraphs — terse rule names + the `d/dx(before)=after`
  line per step (a renderer can prettify later).
