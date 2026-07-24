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
   export { isBracketList, listInside, parseListBody, rangeValues, referencedLists, substIdent, AGGREGATES, AGG_NAMES, findAggCall, findIndex, LIST_XFORM, parseWholeListFn, parseWholeSlice } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/lists.ts")};`,
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
let LISTS = {};
function evalListLiteral(inside) {
  const spec = G.parseListBody(inside);
  if (spec.kind === "range") {
    const from = evalNum(spec.from), to = evalNum(spec.to);
    const step = spec.step ? evalNum(spec.step) : 1;
    if (from === null || to === null || step === null) return null;
    return G.rangeValues(from, step, to, CAP);
  }
  if (spec.kind === "comprehension") {
    const src = evalListArray(spec.source);
    if (!src) return null;
    const out = [];
    for (const val of src.slice(0, CAP)) { const v = evalNum(G.substIdent(spec.body, spec.varName, String(val))); if (v === null) return null; out.push(v); }
    return out;
  }
  const out = [];
  for (const it of spec.items.slice(0, CAP)) { const v = evalNum(it); if (v === null) return null; out.push(v); }
  return out;
}
function evalListArray(expr) {
  const e = expr.trim();
  if (G.isBracketList(e)) return evalListLiteral(G.listInside(e));
  const fn = G.parseWholeListFn(e);
  if (fn) {
    const arrs = [];
    for (const a of fn.args) { const arr = evalListArray(a); if (!arr) return null; arrs.push(arr); }
    if (fn.name === "join") return arrs.flat().slice(0, CAP);
    const x = arrs[0] ?? [];
    return G.LIST_XFORM[fn.name] ? G.LIST_XFORM[fn.name](x).slice(0, CAP) : x;
  }
  const sl = G.parseWholeSlice(e);
  if (sl && sl.name in LISTS) {
    const arr = LISTS[sl.name]; if (!arr) return null;
    const a = evalNum(sl.from), b = evalNum(sl.to);
    if (a === null || b === null) return null;
    const lo = Math.max(1, Math.round(a)), hi = Math.min(arr.length, Math.round(b));
    const o = []; for (let k = lo; k <= hi; k++) o.push(arr[k - 1]); return o;
  }
  if (e in LISTS) return LISTS[e];
  const refs = G.referencedLists(e, Object.keys(LISTS));
  if (refs.length === 0) return null;
  const arrs = refs.map((n) => LISTS[n] ?? []);
  const len = Math.min(CAP, ...arrs.map((a) => a.length));
  const out = [];
  for (let i = 0; i < len; i++) { let sub = e; for (let k = 0; k < refs.length; k++) sub = G.substIdent(sub, refs[k], String(arrs[k][i])); out.push(evalNum(sub) ?? NaN); }
  return out;
}
function expandListScalars(text) {
  const names = Object.keys(LISTS);
  let s = text;
  for (let g = 0; g < 64; g++) {
    const ix = G.findIndex(s, names);
    if (!ix) break;
    const arr = LISTS[ix.name]; const iv = evalNum(ix.idx);
    if (!arr || iv === null) throw new Error("bad index");
    const k = Math.round(iv);
    if (!(k >= 1 && k <= arr.length)) throw new Error("out of range");
    s = s.slice(0, ix.start) + `(${arr[k - 1]})` + s.slice(ix.end);
  }
  for (let g = 0; g < 64; g++) {
    const ag = G.findAggCall(s, G.AGG_NAMES, names);
    if (!ag) break;
    const arr = evalListArray(ag.inner);
    if (!arr) break;
    const val = G.AGGREGATES[ag.name](arr.filter(Number.isFinite));
    s = s.slice(0, ag.start) + `(${val})` + s.slice(ag.end);
  }
  return s;
}
function evalCoordSeq(expr, lists) {
  LISTS = lists;
  let e;
  try { e = expandListScalars(expr.trim()).trim(); } catch { return null; }
  if (G.isBracketList(e)) { const v = evalListLiteral(G.listInside(e)); return v ? { values: v, isList: true } : null; }
  const refs = G.referencedLists(e, Object.keys(lists));
  if (refs.length === 0) return { values: [evalNum(e)], isList: false };
  const arr = evalListArray(e);
  return arr ? { values: arr, isList: true } : null;
}
// Scalar evaluation of an expression that may contain list ops (aggregates/index).
function evalScalarOps(expr, lists) { LISTS = lists; return evalNum(expandListScalars(expr.trim())); }
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

// Build a list registry the way recompute does (order matters: C uses L).
const lists = {};
LISTS = lists;
for (const row of ["L = [1, 2, 3, 4]", "A = [0...3]", "C = [k^2 for k=[1...4]]", "D = [2*n for n=L]", "U = [3, 1, 4, 1, 5]"]) {
  const s = G.classifyRow(row);
  lists[s.name] = evalListArray(s.expr);
}
check("L literal materializes", lists.L, [1, 2, 3, 4]);
check("A range materializes", lists.A, [0, 1, 2, 3]);
check("comprehension [k^2 for k=[1...4]]", lists.C, [1, 4, 9, 16]);
check("comprehension over a named list [2n for n=L]", lists.D, [2, 4, 6, 8]);

check("(L, L^2) → parabola samples", pointsOf("(L, L^2)", lists), [[1, 1], [2, 4], [3, 9], [4, 16]]);
check("(L, 2L+1) broadcast", pointsOf("(L, 2*L + 1)", lists), [[1, 3], [2, 5], [3, 7], [4, 9]]);
check("(L, 0) scalar y repeats", pointsOf("(L, 0)", lists), [[1, 0], [2, 0], [3, 0], [4, 0]]);
check("(2, L) scalar x repeats", pointsOf("(2, L)", lists), [[2, 1], [2, 2], [2, 3], [2, 4]]);
check("(A, L) zips to the shorter list", pointsOf("(A, L)", lists), [[0, 1], [1, 2], [2, 3], [3, 4]]);
check("inline literal coords", pointsOf("([1, 2, 3], [4, 5, 6])", lists), [[1, 4], [2, 5], [3, 6]]);
check("inline range coords", pointsOf("([1...3], [1...3])", lists), [[1, 1], [2, 2], [3, 3]]);
check("plain scalar point still works", pointsOf("(1, 2)", lists), [[1, 2]]);
check("mixed row: scatter group + plain point", pointsOf("(L, L), (7, 8)", lists), [[1, 1], [2, 2], [3, 3], [4, 4], [7, 8]]);

// Aggregates reduce a list to a scalar (usable anywhere, e.g. y = mean(L)).
check("total(L)", evalScalarOps("total(L)", lists), 10);
check("mean(L)", evalScalarOps("mean(L)", lists), 2.5);
check("max(L) and min(L)", [evalScalarOps("max(L)", lists), evalScalarOps("min(L)", lists)], [4, 1]);
check("length(L)", evalScalarOps("length(L)", lists), 4);
check("aggregate over a broadcast: mean(L^2)", evalScalarOps("mean(L^2)", lists), 7.5);
check("aggregate over a comprehension", evalScalarOps("total([k for k=[1...5]])", lists), 15);
check("mean of a scatter y-coordinate: y = mean(L) is a constant", evalScalarOps("mean(L) + 0*x", lists), 2.5);

// Indexing (1-based, Desmos-style).
check("L[1] first element", evalScalarOps("L[1]", lists), 1);
check("L[4] last element", evalScalarOps("L[4]", lists), 4);
check("L[2] + L[3]", evalScalarOps("L[2] + L[3]", lists), 5);
check("index used in a point: (L[1], L[4])", pointsOf("(L[1], L[4])", lists), [[1, 4]]);

// Aggregates/index as list-of-point coordinates.
check("(A, mean(L)) → horizontal band of points", pointsOf("(A, mean(L))", lists), [[0, 2.5], [1, 2.5], [2, 2.5], [3, 2.5]]);

// List-valued function RHS → one line per value (y = [ … ] / y = L / y = L^2).
// The component draws a horizontal line at each of these; here we check the set
// of line positions evalCoordSeq yields.
const lineVals = (expr) => { const s = evalCoordSeq(expr, lists); return s && s.isList ? s.values : null; };
check("y = [1,2,3] → 3 line positions", lineVals("[1, 2, 3]"), [1, 2, 3]);
check("y = L^2 → line per squared element", lineVals("L^2"), [1, 4, 9, 16]);
check("y = L → line per element", lineVals("L"), [1, 2, 3, 4]);
check("y = mean(L) is scalar (single line, not a list)", (() => { const s = evalCoordSeq("mean(L)", lists); return s && !s.isList && s.values[0] === 2.5; })(), true);
check("y = [mean(L), max(L)] → aggregated line positions", lineVals("[mean(L), max(L)]"), [2.5, 4]);

// List-returning ops (Phase 4): sort / unique / reverse / join / slices.
const asList = (expr) => evalListArray(expr);
check("sort(U)", asList("sort(U)"), [1, 1, 3, 4, 5]);
check("unique(U) keeps first-seen order", asList("unique(U)"), [3, 1, 4, 5]);
check("reverse(L)", asList("reverse(L)"), [4, 3, 2, 1]);
check("join(L, A)", asList("join(L, A)"), [1, 2, 3, 4, 0, 1, 2, 3]);
check("slice L[2...4] (1-based inclusive)", asList("L[2...4]"), [2, 3, 4]);
check("slice clamps out-of-range bounds", asList("L[0...99]"), [1, 2, 3, 4]);
check("nested sort(unique(U))", asList("sort(unique(U))"), [1, 3, 4, 5]);
check("aggregate over a sorted list: median(sort(U))", evalScalarOps("median(sort(U))", lists), 3);
check("aggregate over a slice: total(L[2...4])", evalScalarOps("total(L[2...4])", lists), 9);
// A points row over a transform: (sort(U), [1...length(U)]).
check("(sort(U), [1...length(U)]) → ranked scatter", pointsOf("(sort(U), [1...length(U)])", lists), [[1, 1], [1, 2], [3, 3], [4, 4], [5, 5]]);

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
