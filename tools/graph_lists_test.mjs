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
   export { isBracketList, listInside, parseListBody, matchListDef, rangeValues, referencedLists, substIdent } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/lists.ts")};`,
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
check("L = [1,2,3] → listdef", (() => { const r = L.classifyRow("L = [1, 2, 3]"); return r.t === "listdef" && r.name === "L" && r.inside === "1, 2, 3"; })());
check("range → listdef", (() => { const r = L.classifyRow("A = [1...5]"); return r.t === "listdef" && r.inside === "1...5"; })());
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

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
