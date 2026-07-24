// Unit tests for the user-function scanner (web/src/lib/graph/functions.ts) and
// classify.ts parameter capture. Bundled through esbuild so the modules'
// extensionless `./classify` / `./calculus` imports resolve under node.
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const dir = mkdtempSync(join(tmpdir(), "gfns-"));
const entry = join(dir, "entry.ts");
writeFileSync(
  entry,
  `export { classifyRow } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/classify.ts")};
   export { findInnermostAny, findInnermostAppl, findScalarPrimeCall, stripScalarPrimes, stripCalls, freshPlaceholders, hasCalcCall } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/functions.ts")};`,
);
const out = join(dir, "bundle.mjs");
execFileSync(
  "npx",
  ["esbuild", entry, "--bundle", "--format=esm", `--outfile=${out}`],
  { cwd: process.cwd(), stdio: ["ignore", "ignore", "inherit"] },
);
const { classifyRow, findInnermostAny, findInnermostAppl, findScalarPrimeCall, stripScalarPrimes, stripCalls, freshPlaceholders, hasCalcCall } = await import(out);
rmSync(dir, { recursive: true, force: true });

let pass = 0, fail = 0;
const check = (n, c, e = "") => { if (c) { pass++; console.log("PASS  " + n); } else { fail++; console.log(`FAIL  ${n}${e ? "  [" + e + "]" : ""}`); } };

// --- classify.ts parameter capture ----------------------------------------
check("f(x) = x^2 → define with params [x]", (() => {
  const r = classifyRow("f(x) = x^2");
  return r.t === "define" && r.name === "f" && r.expr === "x^2" && JSON.stringify(r.params) === '["x"]';
})());
check("f(x, y) = x + y → define with params [x, y]", (() => {
  const r = classifyRow("f(x, y) = x + y");
  return r.t === "define" && JSON.stringify(r.params) === '["x","y"]';
})());
check("plain g = x^2 → define without params", (() => {
  const r = classifyRow("g = x^2");
  return r.t === "define" && r.params === undefined;
})());
check("f() = 5 (empty params) is not a function define", classifyRow("f() = 5").t !== "define" || classifyRow("f() = 5").params === undefined);
check("y = x^2 is still a function, not a define", classifyRow("y = x^2").t === "function");
check("f = x → scalar define (not functionY x=f)", (() => {
  const r = classifyRow("f = x");
  return r.t === "define" && r.name === "f" && r.expr === "x" && r.params === undefined;
})());
check("x = y^2 stays functionY (axis on the left)", classifyRow("x = y^2").t === "functionY");
check("sin(y) = x stays functionY (lhs is a compound expr)", classifyRow("sin(y) = x").t === "functionY");
check("x = f stays functionY (vertical line at x=f)", classifyRow("x = f").t === "functionY");
check("y = f stays a function of x", classifyRow("y = f").t === "function");
check("f = y → define (bare name), not a function", classifyRow("f = y").t === "define");

// --- findInnermostAny ------------------------------------------------------
check("f(x) with f registered → appl", (() => {
  const c = findInnermostAny("f(x)", ["f"]);
  return c && c.kind === "appl" && c.name === "f" && c.inner === "x" && c.primes === 0;
})());
check("f is not detected inside foo", findInnermostAny("foo(x)", ["f"]) === null);
check("g(f(x)) expands inner f first", (() => {
  const c = findInnermostAny("g(f(x))", ["f", "g"]);
  return c && c.name === "f" && c.inner === "x";
})());
check("diff(f(x)) expands inner f (appl) first", (() => {
  const c = findInnermostAny("diff(f(x))", ["f"]);
  return c && c.kind === "appl" && c.name === "f";
})());
check("f(diff(x)) expands inner diff (calc) first", (() => {
  const c = findInnermostAny("f(diff(x))", ["f"]);
  return c && c.kind === "calc" && c.name === "diff";
})());
check("f'(x) → appl with primes 1", (() => {
  const c = findInnermostAny("f'(x)", ["f"]);
  return c && c.kind === "appl" && c.primes === 1;
})());
check("f''(x) → primes 2", (() => {
  const c = findInnermostAny("f''(x)", ["f"]);
  return c && c.primes === 2;
})());
check("2(x+1) with no registered fns → no call", findInnermostAny("2(x+1)", []) === null);
check("a(x+1) with a not registered → no call (stays multiplication)", findInnermostAny("a(x+1)", ["f"]) === null);
check("sin(x) is not a call (sin is a builtin, not a registered fn)", findInnermostAny("sin(x)", ["f"]) === null);
check("multi-arg f(a, b) inner keeps the comma", (() => {
  const c = findInnermostAny("f(a, b)", ["f"]);
  return c && c.inner === "a, b";
})());
check("nested arg f(g(x,y)) picks inner g", (() => {
  const c = findInnermostAny("f(g(x,y))", ["f", "g"]);
  return c && c.name === "g" && c.inner === "x,y";
})());

// --- stripCalls ------------------------------------------------------------
check("stripCalls f(x)+1 → (x)+1, called [f]", (() => {
  const r = stripCalls("f(x)+1", ["f"]);
  return r.text === "(x)+1" && JSON.stringify(r.calledFns) === '["f"]';
})());
check("stripCalls g(f(x)) → ((x)), called f and g", (() => {
  const r = stripCalls("g(f(x))", ["f", "g"]);
  return r.text === "((x))" && r.calledFns.includes("f") && r.calledFns.includes("g");
})());
check("stripCalls a*f(x+1) keeps the arg symbols", (() => {
  const r = stripCalls("a*f(x+1)", ["f"]);
  return r.text === "a*(x+1)" && JSON.stringify(r.calledFns) === '["f"]';
})());
check("stripCalls of a prime call drops the prime", (() => {
  const r = stripCalls("f'(x)", ["f"]);
  return r.text === "(x)";
})());
check("stripCalls sum: bound index neutralized, x kept", (() => {
  const r = stripCalls("sum(x^k, k, 0, 5)", []);
  // index k → 1 in the term; lo/hi surfaced; no phantom `k` symbol
  return r.text === "((x^1)+(0)+(5))" && !r.text.includes("k");
})());
check("stripCalls sum: upper-bound slider survives", (() => {
  const r = stripCalls("sum(a*x^k, k, 0, n)", []);
  return r.text === "((a*x^1)+(0)+(n))";
})());
check("stripCalls product: bound index neutralized too", (() => {
  const r = stripCalls("product(x/j, j, 1, m)", []);
  return r.text === "((x/1)+(1)+(m))";
})());

// --- findScalarPrimeCall / stripScalarPrimes (scalar-define prime notation) --
// A scalar define like `f = x` isn't a registered function, so `f'(x)` is not
// an `appl`; it is recognized separately and rewritten to f's derivative.
check("scalar prime: f'(x) with f a scalar define → matched, primes 1", (() => {
  const c = findScalarPrimeCall("f'(x)", ["f"]);
  return c && c.name === "f" && c.primes === 1 && c.inner === "x";
})());
check("scalar prime: f''(x/2) → primes 2, inner kept", (() => {
  const c = findScalarPrimeCall("f''(x/2)", ["f"]);
  return c && c.primes === 2 && c.inner === "x/2";
})());
check("scalar prime: bare f(x) (no prime) is NOT matched", findScalarPrimeCall("f(x)", ["f"]) === null);
check("scalar prime: only names in the set match", findScalarPrimeCall("g'(x)", ["f"]) === null);
check("scalar prime: word boundary — no match inside foo'(x)", findScalarPrimeCall("foo'(x)", ["f"]) === null);
check("scalar prime: innermost first for g'(f'(x))", (() => {
  const c = findScalarPrimeCall("g'(f'(x))", ["f", "g"]);
  return c && c.name === "f";
})());
check("stripScalarPrimes: f'(x) → (x)", stripScalarPrimes("f'(x)", ["f"]) === "(x)");
check("stripScalarPrimes: 2*f''(x+1)+3 → 2*(x+1)+3", stripScalarPrimes("2*f''(x+1)+3", ["f"]) === "2*(x+1)+3");
check("stripScalarPrimes: leaves non-prime f(x) alone", stripScalarPrimes("f(x)", ["f"]) === "f(x)");
check("stripScalarPrimes: no-op when set empty", stripScalarPrimes("f'(x)", []) === "f'(x)");

// --- findInnermostAppl (console: user-function calls only) ------------------
check("appl-only: f(x) found", (() => { const c = findInnermostAppl("f(x)", ["f"]); return c && c.name === "f"; })());
check("appl-only: diff(f(x)) picks f, not diff", (() => { const c = findInnermostAppl("diff(f(x))", ["f"]); return c && c.name === "f"; })());
check("appl-only: f(diff(x)) treats diff as opaque, picks f", (() => { const c = findInnermostAppl("f(diff(x))", ["f"]); return c && c.name === "f" && c.inner === "diff(x)"; })());
check("appl-only: no user call → null even with calc present", findInnermostAppl("diff(x)", ["f"]) === null);
check("appl-only: composition picks inner", (() => { const c = findInnermostAppl("g(f(x))", ["f", "g"]); return c && c.name === "f"; })());
check("appl-only: prime carried", (() => { const c = findInnermostAppl("f'(x)", ["f"]); return c && c.primes === 1; })());

// --- hasCalcCall (memoization gate: pure-appl rows are cacheable) ----------
check("hasCalcCall: diff(...) is a calc row", hasCalcCall("diff(f(x))") === true);
check("hasCalcCall: integral(...) is a calc row", hasCalcCall("y = integral(x^2)") === true);
check("hasCalcCall: pure function application is not", hasCalcCall("f(x) + g(x)") === false);
check("hasCalcCall: plain expression is not", hasCalcCall("a*x^2 + 1") === false);

// --- freshPlaceholders -----------------------------------------------------
check("freshPlaceholders avoids letters in the body/args", (() => {
  const p = freshPlaceholders(2, "a/b");
  return p.length === 2 && !p.includes("a") && !p.includes("b") && p[0] !== p[1];
})());
check("freshPlaceholders skips an already-used capital", (() => {
  const p = freshPlaceholders(1, "Z*x");
  return p.length === 1 && p[0] !== "Z";
})());
check("freshPlaceholders never mints E or I", (() => {
  const p = freshPlaceholders(3, "");
  return !p.includes("E") && !p.includes("I");
})());

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
