// A recording, programmable stand-in for the WASM engine. It logs every call
// (so tests can assert the dispatch mapping) and returns canned envelopes (so
// the formatters have something realistic to render) without a WASM build.

import type { Engine } from "../src/engine/engine.js";
import type { AnalyzeResult } from "../src/engine/types.js";
import { hasTopLevelEquals } from "../src/core/text.js";

export interface Call {
  fn: string;
  args: unknown[];
}

export interface Stub {
  engine: Engine;
  calls: Call[];
  /** Per-function response override: responses[fn] = (args) => envelope. */
  responses: Record<string, (args: unknown[]) => unknown>;
  /** Override the analyze result (used for variable inference / preview). */
  analyzeImpl: (input: string) => AnalyzeResult;
}

function defaultAnalyze(input: string): AnalyzeResult {
  const set = new Set<string>();
  for (const ch of input) if (/[a-z]/.test(ch)) set.add(ch);
  const symbols = [...set].sort();
  return hasTopLevelEquals(input)
    ? { ok: true, kind: "equation", symbols, plain: input, latex: input }
    : { ok: true, kind: "expression", symbols, plain: input, latex: input };
}

const TRANSFORM = { ok: true, plain: "P", latex: "L" };

export function createStub(): Stub {
  const calls: Call[] = [];
  const responses: Record<string, (args: unknown[]) => unknown> = {};
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const s: any = { calls, responses, analyzeImpl: defaultAnalyze };

  const make =
    (fn: string, def: (args: unknown[]) => unknown) =>
    (...args: unknown[]) => {
      calls.push({ fn, args });
      return Promise.resolve(responses[fn] ? responses[fn]!(args) : def(args));
    };

  s.version = make("version", () => ({ ok: true, version: "0.6.0" }));
  s.analyze = (input: string) => {
    calls.push({ fn: "analyze", args: [input] });
    return Promise.resolve(responses["analyze"] ? responses["analyze"]!([input]) : s.analyzeImpl(input));
  };

  for (const fn of [
    "simplify", "expand", "factor", "cancel", "together", "latex", "collect",
    "apart", "derivative", "subs", "laplace", "ilaplace", "vectorOp", "stirling",
    "gcd", "lcm", "isprime", "nextprime", "divisors", "totient", "cfrac", "mod",
    "powmod", "modinv", "crt", "solveIneq", "series",
  ]) {
    s[fn] = make(fn, () => ({ ...TRANSFORM }));
  }

  s.fit = make("fit", () => ({ ok: true, plain: "2*x", latex: "2x", model: "linear", exact: true, r2: 1, n: 3 }));
  s.stats = make("stats", () => ({ ok: true, exact: true, n: 3, items: [{ label: "mean", plain: "2", latex: "2" }] }));
  s.seq = make("seq", () => ({ ok: true, kind: "polynomial", description: "squares", plain: "n^2", latex: "n^2", next: ["36"], warnings: [] }));
  s.limit = make("limit", () => ({ ok: true, status: "exact", sign: 0, plain: "1", latex: "1", method: "m", warnings: [] }));
  s.mlimit = make("mlimit", () => ({ ok: true, status: "exact", sign: 0, plain: "5", latex: "5", method: "m", warnings: [] }));
  s.sum = make("sum", () => ({ ok: true, status: "exact", plain: "S", latex: "S", method: "m", warnings: [] }));
  s.product = make("product", () => ({ ok: true, status: "exact", plain: "Pr", latex: "Pr", method: "m", warnings: [] }));
  s.rsolve = make("rsolve", () => ({ ok: true, order: 2, plain: "R", latex: "R", method: "m", warnings: [] }));
  s.dsolve = make("dsolve", () => ({ ok: true, plain: "y", latex: "y", transformPlain: "", transformLatex: "", implicit: false, method: "m", warnings: [] }));
  s.integrate = make("integrate", () => ({ ok: true, solved: true, plain: "F", latex: "F", method: "m", warnings: [] }));
  s.integrateDefinite = make("integrateDefinite", () => ({ ok: true, status: "exact", plain: "2", latex: "2", method: "m", warnings: [] }));
  s.solve = make("solve", () => ({ ok: true, status: "solved", method: "m", warnings: [], solutions: [{ plain: "2", latex: "2", exact: true, note: "", approx: 2 }] }));
  s.solveSystem = make("solveSystem", () => ({ ok: true, status: "solved", values: [{ symbol: "x", plain: "2", latex: "2" }], free: [], method: "m", warnings: [] }));
  s.evaluate = make("evaluate", () => ({ ok: true, value: 9.5 }));

  // The handle IS `s`, so tests that mutate stub.analyzeImpl / stub.responses
  // affect the very references the engine methods close over.
  s.engine = s;
  return s as Stub;
}
