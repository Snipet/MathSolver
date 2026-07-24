// End-to-end test of inline sum()/product() expansion in a plotted grapher row.
// Mirrors GraphCalculator.resolveRow: detect the calc call (bundled classify +
// functions scanners), call the real WASM sum/product binding on
// (term, index, lo, hi), splice the closed form back, and evaluate it at sample
// points — checking against the partial sum computed the naive way.
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const wasm = process.argv[2] ?? resolve(process.cwd(), "web/src/lib/wasm/mathsolver.js");
const M = await (await import(wasm)).default();

const dir = mkdtempSync(join(tmpdir(), "gis-"));
const entry = join(dir, "entry.ts");
writeFileSync(
  entry,
  `export { classifyRow, splitTopLevelCommas } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/classify.ts")};
   export { findInnermostAny } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/functions.ts")};`,
);
const out = join(dir, "bundle.mjs");
execFileSync("npx", ["esbuild", entry, "--bundle", "--format=esm", `--outfile=${out}`], {
  cwd: process.cwd(),
  stdio: ["ignore", "ignore", "inherit"],
});
const { classifyRow, splitTopLevelCommas, findInnermostAny } = await import(out);
rmSync(dir, { recursive: true, force: true });

// resolveRow's sum/product expansion, minus session-variable resolution.
function resolveInlineSum(text) {
  let s = text;
  for (let guard = 0; guard < 32; guard++) {
    const c = findInnermostAny(s, []);
    if (!c) break;
    if (c.name !== "sum" && c.name !== "product") {
      throw new Error(`unexpected calc op ${c.name} in test`);
    }
    const p = splitTopLevelCommas(c.inner).map((x) => x.trim());
    const r = JSON.parse(M[c.name](p[0] ?? "", p[1] ?? "k", p[2] ?? "0", p[3] ?? "0"));
    if (!r.ok || r.status !== "exact" || r.plain === undefined) {
      throw new Error(`no closed form: ${JSON.stringify(r)}`);
    }
    s = s.slice(0, c.start) + "(" + r.plain + ")" + s.slice(c.end);
  }
  return s;
}

const evalNum = (expr, x) => {
  const r = JSON.parse(M.evaluate(expr, `x=${x}`));
  return r.ok && r.value !== null && Number.isFinite(r.value) ? r.value : null;
};

let pass = 0, fail = 0;
const check = (n, got, want) => {
  const ok = got !== null && want !== null && Math.abs(got - want) < 1e-6;
  if (ok) { pass++; console.log("PASS  " + n); }
  else { fail++; console.log(`FAIL  ${n}: got ${got}, want ${want}`); }
};

// Geometric partial sum: sum_{k=0}^{5} x^k, evaluated at a few x.
{
  const spec = classifyRow("y = sum(x^k, k, 0, 5)");
  const expr = resolveInlineSum(spec.expr);
  const partial = (x) => { let a = 0; for (let k = 0; k <= 5; k++) a += x ** k; return a; };
  for (const x of [-1.5, -0.5, 0, 0.5, 2, 3]) {
    check(`sum x^k k=0..5 at x=${x}`, evalNum(expr, x), partial(x));
  }
}

// Polynomial-in-x sum with a symbolic coefficient collapsed at fixed bounds:
// sum_{k=1}^{3} k*x = (1+2+3)*x = 6x.
{
  const spec = classifyRow("y = sum(k*x, k, 1, 3)");
  const expr = resolveInlineSum(spec.expr);
  for (const x of [-2, 0, 1, 4]) check(`sum k*x k=1..3 at x=${x}`, evalNum(expr, x), 6 * x);
}

// Inline product: product_{j=1}^{4} x = x^4.
{
  const spec = classifyRow("y = product(x, j, 1, 4)");
  const expr = resolveInlineSum(spec.expr);
  for (const x of [-2, 0.5, 2]) check(`product x j=1..4 at x=${x}`, evalNum(expr, x), x ** 4);
}

// Nested: a sum inside a larger expression, plus the outer term.
{
  const spec = classifyRow("y = 1 + sum(x^k, k, 1, 3)");
  const expr = resolveInlineSum(spec.expr);
  const want = (x) => 1 + (x + x ** 2 + x ** 3);
  for (const x of [-1, 0.5, 2]) check(`1 + sum x^k k=1..3 at x=${x}`, evalNum(expr, x), want(x));
}

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
