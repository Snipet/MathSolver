import assert from "node:assert/strict";
import { test } from "node:test";
import { formatOutcome } from "../src/core/format.js";
import type { Outcome } from "../src/core/outcome.js";

/** Collect the text of formatted lines. */
function texts(outcome: Outcome, latex = false): string[] {
  return formatOutcome(outcome, { latex }).map((l) => l.text);
}

test("transform renders plain or latex plus notes", () => {
  const o: Outcome = {
    kind: "render",
    render: "transform",
    result: { ok: true, plain: "5*x", latex: "5x", notes: ["convergents: 3, 22/7"] },
  };
  assert.deepEqual(texts(o), ["5*x", "convergents: 3, 22/7"]);
  assert.deepEqual(texts(o, true), ["5x", "convergents: 3, 22/7"]);
});

test("parse error renders a caret when the source is known", () => {
  const o: Outcome = {
    kind: "render",
    render: "transform",
    result: { ok: false, error: "unexpected character '?'", begin: 1, end: 2 },
    source: "a?b",
  };
  const out = formatOutcome(o);
  assert.equal(out.length, 1);
  assert.equal(out[0]!.tone, "error");
  assert.match(out[0]!.text, /^error: unexpected character/);
  assert.match(out[0]!.text, /\^/);
});

test("engine error without a span renders a single line", () => {
  const o: Outcome = {
    kind: "render",
    render: "transform",
    result: { ok: false, error: "no Laplace transform rule" },
  };
  assert.deepEqual(texts(o), ["error: no Laplace transform rule"]);
});

test("solve prints one line per solution with the variable", () => {
  const o: Outcome = {
    kind: "render",
    render: "solve",
    variable: "x",
    result: {
      ok: true,
      status: "solved",
      method: "quadratic formula",
      warnings: [],
      solutions: [
        { plain: "2", latex: "2", exact: true, note: "", approx: 2 },
        { plain: "-2", latex: "-2", exact: true, note: "", approx: -2 },
      ],
    },
  };
  assert.deepEqual(texts(o), ["x = 2", "x = -2", "method: quadratic formula"]);
});

test("solve complex and numeric branches", () => {
  const complex: Outcome = {
    kind: "render",
    render: "solve",
    variable: "x",
    result: {
      ok: true,
      status: "complex",
      method: "",
      warnings: [],
      solutions: [{ plain: "i", latex: "i", exact: true, note: "", approx: null }],
    },
  };
  assert.deepEqual(texts(complex), ["no real solutions; complex roots:", "x = i"]);

  const numeric: Outcome = {
    kind: "render",
    render: "solve",
    variable: "x",
    result: {
      ok: true,
      status: "numeric",
      method: "bisection",
      warnings: ["numeric root"],
      solutions: [{ plain: "0.739085", latex: "0.739085", exact: false, note: "", approx: 0.7390851332 }],
    },
  };
  assert.deepEqual(texts(numeric), ["x ≈ 0.7390851332", "method: bisection", "warning: numeric root"]);
});

test("system, integrate, definite, limit, sum", () => {
  assert.deepEqual(
    texts({
      kind: "render",
      render: "system",
      result: {
        ok: true,
        status: "solved",
        values: [
          { symbol: "x", plain: "2", latex: "2" },
          { symbol: "y", plain: "1", latex: "1" },
        ],
        free: [],
        method: "Gaussian elimination",
        warnings: [],
      },
    }),
    ["x = 2", "y = 1", "method: Gaussian elimination"],
  );

  assert.deepEqual(
    texts({
      kind: "render",
      render: "integrate",
      result: { ok: true, solved: true, plain: "-x*cos(x) + sin(x)", latex: "x", method: "parts", warnings: [] },
    }),
    ["-x*cos(x) + sin(x) + C", "method: parts"],
  );

  assert.deepEqual(
    texts({
      kind: "render",
      render: "definite",
      result: { ok: true, status: "exact", plain: "2", latex: "2", method: "FTC", warnings: [] },
    }),
    ["value = 2", "method: FTC"],
  );

  assert.deepEqual(
    texts({
      kind: "render",
      render: "limit",
      result: { ok: true, status: "diverges", sign: 1, method: "", warnings: [] },
    }),
    ["limit = +inf"],
  );

  assert.deepEqual(
    texts({
      kind: "render",
      render: "sum",
      noun: "product",
      result: { ok: true, status: "exact", plain: "120", latex: "120", method: "", warnings: [] },
    }),
    ["product = 120"],
  );
});

test("fit, stats, seq, evaluate, dsolve, rsolve", () => {
  assert.deepEqual(
    texts({
      kind: "render",
      render: "fit",
      result: { ok: true, plain: "2*x + 1", latex: "2x + 1", model: "linear", exact: true, r2: 1, n: 3 },
    }),
    ["2*x + 1", "model: linear (exact)", "R^2: 1"],
  );

  assert.deepEqual(
    texts({
      kind: "render",
      render: "stats",
      result: { ok: true, exact: true, n: 3, items: [{ label: "mean", plain: "7/3", latex: "7/3" }] },
    }),
    ["mean = 7/3"],
  );

  assert.deepEqual(
    texts({
      kind: "render",
      render: "seq",
      result: {
        ok: true,
        kind: "polynomial",
        description: "perfect squares",
        plain: "n^2",
        latex: "n^2",
        next: ["36", "49"],
        warnings: [],
      },
    }),
    ["pattern: perfect squares", "a(n) = n^2   (n = 0, 1, 2, ...)", "next: 36, 49"],
  );

  assert.deepEqual(texts({ kind: "render", render: "evaluate", result: { ok: true, value: 9.5 } }), ["9.5"]);

  assert.deepEqual(
    texts({
      kind: "render",
      render: "dsolve",
      result: {
        ok: true,
        plain: "e^(-t)",
        latex: "e^{-t}",
        transformPlain: "",
        transformLatex: "",
        implicit: false,
        method: "Laplace",
        warnings: [],
      },
    }),
    ["y(t) = e^(-t)", "method: Laplace"],
  );

  assert.deepEqual(
    texts({
      kind: "render",
      render: "rsolve",
      result: { ok: true, order: 1, plain: "2^n - 1", latex: "2^n - 1", method: "characteristic", warnings: [] },
    }),
    ["a(n) = 2^n - 1", "method: characteristic"],
  );
});

test("lines outcome passes through", () => {
  const o: Outcome = { kind: "lines", lines: [{ text: "note: hi", tone: "note" }] };
  assert.deepEqual(texts(o), ["note: hi"]);
});
