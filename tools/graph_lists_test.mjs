// Unit tests for the grapher list text layer (web/src/lib/graph/lists.ts).
// Bundled with esbuild so the extensionless `./classify` import resolves.
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const dir = mkdtempSync(join(tmpdir(), "gls-"));
const entry = join(dir, "entry.ts");
writeFileSync(
  entry,
  `export { classifyRow } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/classify.ts")};
   export { isBracketList, listInside, parseListBody, matchListDef, rangeValues, referencedLists, substIdent, AGGREGATES, AGG_NAMES, findAggCall, findIndex, isListArg, listSurrogate, hasListOps, LIST_FNS, LIST_XFORM, findListFnCall, parseWholeListFn, parseWholeSlice } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/lists.ts")};`,
);
const out = join(dir, "bundle.mjs");
execFileSync("npx", ["esbuild", entry, "--bundle", "--format=esm", `--outfile=${out}`], {
  cwd: process.cwd(),
  stdio: ["ignore", "ignore", "inherit"],
});
const L = await import(out);
rmSync(dir, { recursive: true, force: true });

let pass = 0, fail = 0;
const check = (n, c) => { if (c) { pass++; console.log("PASS  " + n); } else { fail++; console.log("FAIL  " + n); } };
const eqArr = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// --- isBracketList ---------------------------------------------------------
check("bracket list recognized", L.isBracketList("[1, 2, 3]"));
check("range bracket recognized", L.isBracketList("[1...5]"));
check("nested brackets ok", L.isBracketList("[[1,2],[3,4]]"));
check("not a list: plain expr", !L.isBracketList("1 + 2"));
check("not a list: split brackets", !L.isBracketList("[1][2]"));
check("not a list: trailing op", !L.isBracketList("[1,2] + 3"));

// --- classifyRow: list definitions -----------------------------------------
check("L = [1,2,3] → listdef (full RHS in expr)", (() => { const r = L.classifyRow("L = [1, 2, 3]"); return r.t === "listdef" && r.name === "L" && r.expr === "[1, 2, 3]"; })());
check("range → listdef", (() => { const r = L.classifyRow("A = [1...5]"); return r.t === "listdef" && r.expr === "[1...5]"; })());
check("M = sort(L) → listdef", (() => { const r = L.classifyRow("M = sort(L)"); return r.t === "listdef" && r.expr === "sort(L)"; })());
check("M = L[2...4] slice → listdef", (() => { const r = L.classifyRow("M = L[2...4]"); return r.t === "listdef" && r.expr === "L[2...4]"; })());
check("k = L[3] scalar element is NOT a listdef", L.classifyRow("k = L[3]").t !== "listdef");
check("j = join(A, B) → listdef", (() => { const r = L.classifyRow("j = join(A, B)"); return r.t === "listdef" && r.expr === "join(A, B)"; })());
check("f(x) = x^2 is still a function, not a list", L.classifyRow("f(x) = x^2").t === "define");
check("scalar define unaffected", L.classifyRow("k = 3").t === "define");
check("y = [1,2,3] is NOT a listdef (reserved name)", L.classifyRow("y = [1,2,3]").t !== "listdef");

// --- parseListBody ---------------------------------------------------------
check("literal body", (() => { const s = L.parseListBody("1, 2, 3"); return s.kind === "literal" && eqArr(s.items, ["1", "2", "3"]); })());
check("range body a...b", (() => { const s = L.parseListBody("1...5"); return s.kind === "range" && s.from === "1" && s.to === "5" && s.step === null; })());
check("stepped range a, b ... c", (() => { const s = L.parseListBody("1, 3 ... 9"); return s.kind === "range" && s.from === "1" && s.to === "9" && s.step === "(3) - (1)"; })());
check("commas protect nested calls in literals", (() => { const s = L.parseListBody("max(1,2), 3"); return s.kind === "literal" && eqArr(s.items, ["max(1,2)", "3"]); })());

// --- rangeValues -----------------------------------------------------------
check("range 1..5 step 1", eqArr(L.rangeValues(1, 1, 5), [1, 2, 3, 4, 5]));
check("range 0..10 step 2", eqArr(L.rangeValues(0, 2, 10), [0, 2, 4, 6, 8, 10]));
check("descending range", eqArr(L.rangeValues(5, -1, 1), [5, 4, 3, 2, 1]));
check("step 0 defaults to unit step", eqArr(L.rangeValues(1, 0, 3), [1, 2, 3]));
check("step pointing away from target → empty", eqArr(L.rangeValues(1, -1, 5), []));
check("range honors the cap", L.rangeValues(0, 1, 1e9, 5).length === 5);

// --- referencedLists / substIdent ------------------------------------------
check("referencedLists finds whole-word names", eqArr(L.referencedLists("L^2 + m", ["L", "m", "k"]), ["L", "m"]));
check("referencedLists ignores substrings", eqArr(L.referencedLists("Lab + xL", ["L"]), []));
check("substIdent wraps replacement in parens", L.substIdent("L^2", "L", "-3") === "(-3)^2");
check("substIdent replaces every occurrence, whole-word only", L.substIdent("L + Lab + L", "L", "2") === "(2) + Lab + (2)");
check("substIdent inside a call", L.substIdent("f(L)", "L", "4") === "f((4))");

// --- comprehensions (Phase 2) ----------------------------------------------
check("comprehension body/var/source", (() => { const s = L.parseListBody("k^2 for k = [1...5]"); return s.kind === "comprehension" && s.body === "k^2" && s.varName === "k" && s.source === "[1...5]"; })());
check("comprehension over a named list", (() => { const s = L.parseListBody("2*n for n = L"); return s.kind === "comprehension" && s.source === "L"; })());
check("'for' inside a call is not a comprehension keyword", (() => { const s = L.parseListBody("format(x)"); return s.kind === "literal"; })());
check("comprehension classifies as listdef", (() => { const r = L.classifyRow("B = [k^2 for k=[1...4]]"); return r.t === "listdef" && r.expr === "[k^2 for k=[1...4]]"; })());

// --- aggregates + indexing scanners ----------------------------------------
check("AGG_NAMES include the core reducers", ["total", "mean", "min", "max", "length", "median", "stdev"].every((n) => L.AGG_NAMES.includes(n)));
check("aggregate arithmetic", L.AGGREGATES.total([1, 2, 3, 4]) === 10 && L.AGGREGATES.mean([1, 2, 3, 4]) === 2.5 && L.AGGREGATES.max([3, 9, 1]) === 9 && L.AGGREGATES.length([1, 2, 3]) === 3);
check("median even/odd", L.AGGREGATES.median([1, 2, 3]) === 2 && L.AGGREGATES.median([1, 2, 3, 4]) === 2.5);
check("isListArg: name and bracket yes, scalar no", L.isListArg("L", ["L"]) && L.isListArg("[1,2]", ["L"]) && !L.isListArg("x", ["L"]) && !L.isListArg("x, 2", ["L"]));
check("findAggCall matches mean(L)", (() => { const c = L.findAggCall("mean(L) + 1", ["mean"], ["L"]); return c && c.name === "mean" && c.inner === "L"; })());
check("findAggCall skips scalar max(x,2), still finds mean(L)", (() => { const c = L.findAggCall("max(x, 2) + mean(L)", ["max", "mean"], ["L"]); return c && c.name === "mean"; })());
check("findAggCall ignores max(x,2) entirely", L.findAggCall("max(x, 2)", ["max", "mean"], ["L"]) === null);
check("findIndex matches L[3]", (() => { const c = L.findIndex("L[3] + 1", ["L"]); return c && c.name === "L" && c.idx === "3"; })());
check("findIndex ignores non-list names", L.findIndex("a[3]", ["L"]) === null);

// --- listSurrogate (free-variable exposure) --------------------------------
check("surrogate drops a bare list name", L.listSurrogate("L", ["L"]) === "(0)");
check("surrogate exposes a range bound slider", (() => { const s = L.listSurrogate("[1...n]", ["L"]); return /n/.test(s) && !/\[/.test(s); })());
check("surrogate of mean(L) has no symbols", (() => { const s = L.listSurrogate("mean(L)", ["L"]); return !/[A-Za-z]/.test(s.replace(/[eE]/g, "")); })());
check("surrogate of mean([1...n]) exposes n only", (() => { const s = L.listSurrogate("mean([1...n])", ["L"]); return /n/.test(s) && !/mean|\[/.test(s); })());
check("surrogate keeps real sliders, drops bound var", (() => { const s = L.listSurrogate("[a*k for k=[1...4]]", ["L"]); return /a/.test(s) && !/\bk\b/.test(s) && !/for|\[/.test(s); })());
check("surrogate leaves a plain expression untouched", L.listSurrogate("x^2 + a", []) === "x^2 + a");
check("hasListOps detects brackets/aggregates/refs", L.hasListOps("[1,2]", []) && L.hasListOps("mean(L)", ["L"]) && L.hasListOps("L^2", ["L"]) && !L.hasListOps("x^2 + a", ["L"]));

// --- list-returning ops (Phase 4) ------------------------------------------
check("sort transform", JSON.stringify(L.LIST_XFORM.sort([3, 1, 2])) === "[1,2,3]");
check("unique keeps first-seen order", JSON.stringify(L.LIST_XFORM.unique([2, 1, 2, 3, 1])) === "[2,1,3]");
check("reverse transform", JSON.stringify(L.LIST_XFORM.reverse([1, 2, 3])) === "[3,2,1]");
check("findListFnCall matches sort(L), skips scalar", (() => { const c = L.findListFnCall("sort(L) + 1", ["L"]); return c && c.name === "sort" && c.inner === "L"; })());
check("findListFnCall skips a scalar-arg call", L.findListFnCall("sort(x)", ["L"]) === null);
check("parseWholeListFn: join(A, B)", (() => { const c = L.parseWholeListFn("join(A, B)"); return c && c.name === "join" && JSON.stringify(c.args) === '["A","B"]'; })());
check("parseWholeListFn: not whole expr → null", L.parseWholeListFn("sort(L) + 1") === null);
check("parseWholeSlice: L[2...4]", (() => { const c = L.parseWholeSlice("L[2...4]"); return c && c.name === "L" && c.from === "2" && c.to === "4"; })());
check("parseWholeSlice: scalar index is not a slice", L.parseWholeSlice("L[3]") === null);
check("findIndex skips a slice", L.findIndex("L[2...4]", ["L"]) === null);
check("findIndex still matches a scalar index", (() => { const c = L.findIndex("L[3]", ["L"]); return c && c.idx === "3"; })());
check("surrogate drops sort() and its list", (() => { const s = L.listSurrogate("sort(L)", ["L"]); return !/sort/.test(s) && !/L/.test(s.replace(/[a-z]/g, (c) => c)); })());
check("surrogate of a slice exposes bound n, drops L", (() => { const s = L.listSurrogate("L[2...n]", ["L"]); return /n/.test(s) && !/sort/.test(s); })());

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
