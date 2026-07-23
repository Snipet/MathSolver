import assert from "node:assert/strict";
import { test } from "node:test";
import { executeIntent } from "../src/core/execute.js";
import { parseLine, type Intent, type Job } from "../src/core/intent.js";
import type { AnalyzeResult } from "../src/engine/types.js";
import { createStub, type Stub } from "./stub.js";

function jobOf(line: string): Job {
  const intent = parseLine(line);
  assert.equal(intent.kind, "job", `expected job for ${line}`);
  return (intent as Extract<Intent, { kind: "job" }>).job;
}

/** Parse a line and run it against a fresh stub; return the stub + outcome. */
async function run(line: string, tweak?: (s: Stub) => void) {
  const stub = createStub();
  tweak?.(stub);
  const outcome = await executeIntent(stub.engine, jobOf(line));
  return { stub, outcome };
}

test("direct transforms call the matching engine method with the raw input", async () => {
  const { stub } = await run("simplify 2x + 3x");
  assert.deepEqual(stub.calls, [{ fn: "simplify", args: ["2x + 3x"] }]);
});

test("the latex verb forces LaTeX output", async () => {
  const { stub, outcome } = await run("latex sqrt(x)/2", (s) => {
    s.responses.latex = () => ({ ok: true, plain: "sqrt(x)/2", latex: "\\frac{\\sqrt{x}}{2}" });
  });
  assert.deepEqual(stub.calls, [{ fn: "latex", args: ["sqrt(x)/2"] }]);
  assert.equal(outcome.kind, "render");
  if (outcome.kind === "render") assert.equal(outcome.forceLatex, true);
});

test("rsolve splits the recurrence from its conditions", async () => {
  const { stub } = await run("rsolve a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1");
  assert.deepEqual(stub.calls, [
    { fn: "rsolve", args: ["a(n+2) = a(n+1) + a(n)", "a(0)=0,a(1)=1"] },
  ]);
});

test("vector op passes field and variable list separately", async () => {
  const { stub } = await run("grad x^2 + y^2, x, y");
  assert.deepEqual(stub.calls, [{ fn: "vectorOp", args: ["grad", "x^2 + y^2", "x,y"] }]);
});

test("number theory verb routes to its function", async () => {
  const { stub } = await run("gcd 48, 36");
  assert.deepEqual(stub.calls, [{ fn: "gcd", args: ["48, 36"] }]);
});

test("sum vs product select distinct engine methods", async () => {
  const s1 = await run("sum k^2, k, 1, n");
  assert.deepEqual(s1.stub.calls, [{ fn: "sum", args: ["k^2", "k", "1", "n"] }]);
  const s2 = await run("product k, k, 1, 5");
  assert.deepEqual(s2.stub.calls, [{ fn: "product", args: ["k", "k", "1", "5"] }]);
});

test("definite integral routes to integrateDefinite (after inference)", async () => {
  const single: AnalyzeResult = { ok: true, kind: "expression", symbols: ["x"], plain: "", latex: "" };
  const { stub } = await run("integrate sin(x), x, 0, pi", (s) => (s.analyzeImpl = () => single));
  // Variable was explicit, so no analyze call is needed here.
  assert.deepEqual(stub.calls, [{ fn: "integrateDefinite", args: ["sin(x)", "x", "0", "pi"] }]);
});

test("inequality solve routes to solveIneq", async () => {
  const { stub } = await run("solve x^2 < 4");
  assert.deepEqual(stub.calls, [{ fn: "solveIneq", args: ["x^2", "4", "<", ""] }]);
});

test("variable inference: diff with one free symbol", async () => {
  const single: AnalyzeResult = { ok: true, kind: "expression", symbols: ["x"], plain: "", latex: "" };
  const { stub, outcome } = await run("diff sin(x^2)", (s) => (s.analyzeImpl = () => single));
  assert.deepEqual(stub.calls, [
    { fn: "analyze", args: ["sin(x^2)"] },
    { fn: "derivative", args: ["sin(x^2)", "x"] },
  ]);
  assert.equal(outcome.kind, "render");
});

test("variable inference fails with multiple free symbols", async () => {
  const many: AnalyzeResult = { ok: true, kind: "expression", symbols: ["x", "y"], plain: "", latex: "" };
  const { stub, outcome } = await run("diff x*y", (s) => (s.analyzeImpl = () => many));
  assert.deepEqual(stub.calls, [{ fn: "analyze", args: ["x*y"] }]); // no derivative call
  assert.equal(outcome.kind, "lines");
  if (outcome.kind === "lines") {
    assert.equal(outcome.lines[0]!.tone, "error");
    assert.match(outcome.lines[0]!.text, /free symbols \(x, y\)/);
  }
});

test("solve infers the variable and passes it to solve()", async () => {
  const single: AnalyzeResult = { ok: true, kind: "equation", symbols: ["x"], plain: "", latex: "" };
  const { stub } = await run("solve x^2 = 4", (s) => (s.analyzeImpl = () => single));
  assert.deepEqual(stub.calls, [
    { fn: "analyze", args: ["x^2 = 4"] },
    { fn: "solve", args: ["x^2 = 4", "x", 0, 0, false] },
  ]);
});

test("bare equation with one symbol solves for it", async () => {
  const single: AnalyzeResult = { ok: true, kind: "equation", symbols: ["x"], plain: "", latex: "" };
  const { stub, outcome } = await run("x^2 = 4", (s) => (s.analyzeImpl = () => single));
  assert.deepEqual(stub.calls, [
    { fn: "analyze", args: ["x^2 = 4"] },
    { fn: "solve", args: ["x^2 = 4", "x", 0, 0, false] },
  ]);
  assert.equal(outcome.kind, "render");
});

test("closed equation reports identity via simplify of the difference", async () => {
  const none: AnalyzeResult = { ok: true, kind: "equation", symbols: [], plain: "", latex: "" };
  const { outcome } = await run("2 + 2 = 4", (s) => {
    s.analyzeImpl = () => none;
    s.responses.simplify = () => ({ ok: true, plain: "0", latex: "0" });
  });
  assert.equal(outcome.kind, "lines");
  if (outcome.kind === "lines") assert.match(outcome.lines[0]!.text, /identity/);
});

test("cancel rejects a variable that is not free in the input", async () => {
  const only: AnalyzeResult = { ok: true, kind: "expression", symbols: ["x"], plain: "", latex: "" };
  const { outcome } = await run("cancel (x^2-1)/(x-1), y", (s) => (s.analyzeImpl = () => only));
  assert.equal(outcome.kind, "lines");
  if (outcome.kind === "lines") {
    assert.match(outcome.lines[0]!.text, /not a free variable/);
  }
});
