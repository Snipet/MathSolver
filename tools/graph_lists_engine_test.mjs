// End-to-end test of list-of-points plotting: define a list, then evaluate a
// point row's list-valued coordinates against the real WASM engine, mirroring
// GraphCalculator's evalCoordSeq + broadcast (zip lists, repeat scalars).
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const wasm = process.argv[2] ?? resolve(process.cwd(), "web/src/lib/wasm/mathsolver.js");
const M = await (await import(wasm)).default();

const dir = mkdtempSync(join(tmpdir(), "gle-"));
const entry = join(dir, "entry.ts");
writeFileSync(
  entry,
  `export { classifyRow } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/classify.ts")};
   export { isBracketList, listInside, parseListBody, rangeValues, referencedLists, substIdent } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/lists.ts")};`,
);
const out = join(dir, "bundle.mjs");
execFileSync("npx", ["esbuild", entry, "--bundle", "--format=esm", `--outfile=${out}`], {
  cwd: process.cwd(),
  stdio: ["ignore", "ignore", "inherit"],
});
const G = await import(out);
rmSync(dir, { recursive: true, force: true });

const evalNum = (expr) => {
  const r = JSON.parse(M.evaluate(expr, ""));
  return r.ok && r.value !== null && Number.isFinite(r.value) ? r.value : null;
};

const CAP = 400;
function evalListLiteral(inside) {
  const spec = G.parseListBody(inside);
  if (spec.kind === "range") {
    const from = evalNum(spec.from), to = evalNum(spec.to);
    const step = spec.step ? evalNum(spec.step) : 1;
    if (from === null || to === null || step === null) return null;
    return G.rangeValues(from, step, to, CAP);
  }
  const out = [];
  for (const it of spec.items.slice(0, CAP)) { const v = evalNum(it); if (v === null) return null; out.push(v); }
  return out;
}
function evalCoordSeq(expr, lists) {
  const e = expr.trim();
  if (G.isBracketList(e)) { const v = evalListLiteral(G.listInside(e)); return v ? { values: v, isList: true } : null; }
  const refs = G.referencedLists(e, Object.keys(lists));
  if (refs.length === 0) return { values: [evalNum(e)], isList: false };
  const arrs = refs.map((n) => lists[n] ?? []);
  const len = Math.min(CAP, ...arrs.map((a) => a.length));
  const values = [];
  for (let i = 0; i < len; i++) {
    let sub = e;
    for (let k = 0; k < refs.length; k++) sub = G.substIdent(sub, refs[k], String(arrs[k][i]));
    values.push(evalNum(sub));
  }
  return { values, isList: true };
}
// Broadcast a point row's coords the way buildDrawable does.
function pointsOf(row, lists) {
  const spec = G.classifyRow(row);
  const pts = [];
  for (const [xe, ye] of spec.coords) {
    const xs = evalCoordSeq(xe, lists), ys = evalCoordSeq(ye, lists);
    if (!xs || !ys) continue;
    const n = !xs.isList ? ys.values.length : !ys.isList ? xs.values.length : Math.min(xs.values.length, ys.values.length);
    for (let i = 0; i < n; i++) {
      const x = xs.isList ? xs.values[i] : xs.values[0];
      const y = ys.isList ? ys.values[i] : ys.values[0];
      if (x !== null && y !== null) pts.push([x, y]);
    }
  }
  return pts;
}

let pass = 0, fail = 0;
const check = (n, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) { pass++; console.log("PASS  " + n); }
  else { fail++; console.log(`FAIL  ${n}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); }
};

// Build a list registry the way recompute does.
const lists = {};
for (const row of ["L = [1, 2, 3, 4]", "A = [0...3]"]) {
  const s = G.classifyRow(row);
  lists[s.name] = evalListLiteral(s.inside);
}
check("L literal materializes", lists.L, [1, 2, 3, 4]);
check("A range materializes", lists.A, [0, 1, 2, 3]);

check("(L, L^2) → parabola samples", pointsOf("(L, L^2)", lists), [[1, 1], [2, 4], [3, 9], [4, 16]]);
check("(L, 2L+1) broadcast", pointsOf("(L, 2*L + 1)", lists), [[1, 3], [2, 5], [3, 7], [4, 9]]);
check("(L, 0) scalar y repeats", pointsOf("(L, 0)", lists), [[1, 0], [2, 0], [3, 0], [4, 0]]);
check("(2, L) scalar x repeats", pointsOf("(2, L)", lists), [[2, 1], [2, 2], [2, 3], [2, 4]]);
check("(A, L) zips to the shorter list", pointsOf("(A, L)", lists), [[0, 1], [1, 2], [2, 3], [3, 4]]);
check("inline literal coords", pointsOf("([1, 2, 3], [4, 5, 6])", lists), [[1, 4], [2, 5], [3, 6]]);
check("inline range coords", pointsOf("([1...3], [1...3])", lists), [[1, 1], [2, 2], [3, 3]]);
check("plain scalar point still works", pointsOf("(1, 2)", lists), [[1, 2]]);
check("mixed row: scatter group + plain point", pointsOf("(L, L), (7, 8)", lists), [[1, 1], [2, 2], [3, 3], [4, 4], [7, 8]]);

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
