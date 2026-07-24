// End-to-end test of piecewise evaluation: classify a `{cond: val, …}` row
// (bundled classify.ts) and evaluate it per point against the real WASM engine
// with the same first-true-wins semantics GraphCalculator's buildDrawable uses.
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const wasm = process.argv[2] ?? resolve(process.cwd(), "web/src/lib/wasm/mathsolver.js");
const M = await (await import(wasm)).default();

const dir = mkdtempSync(join(tmpdir(), "gpw-"));
const entry = join(dir, "entry.ts");
writeFileSync(
  entry,
  `export { classifyRow, parseRestriction } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/classify.ts")};`,
);
const out = join(dir, "bundle.mjs");
execFileSync("npx", ["esbuild", entry, "--bundle", "--format=esm", `--outfile=${out}`], {
  cwd: process.cwd(),
  stdio: ["ignore", "ignore", "inherit"],
});
const { classifyRow, parseRestriction } = await import(out);
rmSync(dir, { recursive: true, force: true });

const evalNum = (expr, x) => {
  const r = JSON.parse(M.evaluate(expr, `x=${x}`));
  return r.ok && r.value !== null && Number.isFinite(r.value) ? r.value : null;
};
const cmpTrue = (op, d) =>
  op === "<" ? d < 0 : op === "<=" ? d <= 0 : op === ">" ? d > 0 : op === ">=" ? d >= 0 : Math.abs(d) < 1e-9;

function piecewiseAt(spec, x) {
  for (const br of spec.branches) {
    const cmps = parseRestriction([br.cond]);
    let on = true;
    for (const c of cmps) {
      const d = evalNum(`(${c.lhs}) - (${c.rhs})`, x);
      if (d === null || !cmpTrue(c.op, d)) { on = false; break; }
    }
    if (on) return evalNum(br.value, x); // this branch owns the point
  }
  return spec.otherwise !== undefined ? evalNum(spec.otherwise, x) : null;
}

let pass = 0, fail = 0;
const check = (n, got, want) => {
  const ok = got === want || (got !== null && want !== null && Math.abs(got - want) < 1e-9);
  if (ok) { pass++; console.log("PASS  " + n); }
  else { fail++; console.log(`FAIL  ${n}: got ${got}, want ${want}`); }
};

const absSpec = classifyRow("{x < 0: -x, x}"); // |x|
check("abs at -2", piecewiseAt(absSpec, -2), 2);
check("abs at 2", piecewiseAt(absSpec, 2), 2);
check("abs at 0 (else branch)", piecewiseAt(absSpec, 0), 0);

const signSpec = classifyRow("{x > 0: 1, x < 0: -1, 0}"); // sign(x)
check("sign at 3", piecewiseAt(signSpec, 3), 1);
check("sign at -3", piecewiseAt(signSpec, -3), -1);
check("sign at 0 (else)", piecewiseAt(signSpec, 0), 0);

const windowSpec = classifyRow("{0 < x < 5: x^2}"); // x^2 on (0,5), else undefined
check("window inside → x^2", piecewiseAt(windowSpec, 3), 9);
check("window below → undefined", piecewiseAt(windowSpec, -1), null);
check("window above → undefined", piecewiseAt(windowSpec, 6), null);

const firstWins = classifyRow("{x > 0: 10, x > -5: 20, 30}");
check("first matching branch wins at x=1", piecewiseAt(firstWins, 1), 10);
check("second branch at x=-1", piecewiseAt(firstWins, -1), 20);
check("else at x=-10", piecewiseAt(firstWins, -10), 30);

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
