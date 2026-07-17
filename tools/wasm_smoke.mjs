// Node smoke test for the WASM module: exercises every bound function and
// checks real values. Run: node tools/wasm_smoke.mjs [path/to/mathsolver.js]
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const modPath = process.argv[2] ?? resolve(here, "../build-wasm/mathsolver.js");
const createModule = (await import(modPath)).default;
const ms = await createModule();

let failures = 0;
function check(name, actual, predicate, expectation) {
  const r = JSON.parse(actual);
  if (!predicate(r)) {
    failures++;
    console.error(`FAIL ${name}: expected ${expectation}\n  got ${actual}`);
  } else {
    console.log(`ok   ${name}`);
  }
}

check("version", ms.version(), (r) => r.ok && /^\d+\.\d+\.\d+$/.test(r.version), "semver");
check("simplify", ms.simplify("2x + 3x"), (r) => r.ok && r.plain === "5*x", "5*x");
check("simplify latex", ms.simplify("2x + 3x"), (r) => r.ok && r.latex === "5x", "5x");
check("expand", ms.expand("(x+1)^2"), (r) => r.ok && r.plain === "x^2 + 2*x + 1", "x^2 + 2*x + 1");
check("factor", ms.factor("x^2 - 5x + 6"), (r) => r.ok && r.plain.includes("(x - 3)"), "(x-3)(x-2)");
check("latex frac", ms.latex("sqrt(x)/2"), (r) => r.ok && r.latex.includes("\\frac"), "\\frac");
check("analyze expr", ms.analyze("a*x^2 + b"), (r) => r.ok && r.kind === "expression" && r.symbols.join(",") === "a,b,x", "symbols a,b,x");
check("analyze equation", ms.analyze("x^2 = 4"), (r) => r.ok && r.kind === "equation", "equation");
check("analyze system", ms.analyze("x + y = 3; x - y = 1"), (r) => r.ok && r.kind === "system", "system");
check("parse error span", ms.simplify("2 + \\fraq{1}{2}"), (r) => !r.ok && r.begin === 4 && r.end > r.begin, "span at \\fraq");
check("derivative", ms.derivative("sin(x^2)", "x"), (r) => r.ok && r.plain === "2*x*cos(x^2)", "2*x*cos(x^2)");
check("integrate", ms.integrate("x*sin(x)", "x"), (r) => r.ok && r.solved && r.plain.includes("-x*cos(x)"), "-x*cos(x) + sin(x)");
check("integrate honesty", ms.integrate("e^(x^2)", "x"), (r) => r.ok && !r.solved, "unsolved");
check("definite exact", ms.integrateDefinite("sin(x)", "x", "0", "pi"), (r) => r.ok && r.status === "exact" && r.plain === "2", "value 2");
check("definite numeric", ms.integrateDefinite("e^(-x^2)", "x", "0", "1"), (r) => r.ok && r.status === "numeric" && Math.abs(r.approx - 0.7468241328) < 1e-6, "~0.746824");
check("definite divergent", ms.integrateDefinite("1/x", "x", "-1", "2"), (r) => r.ok && r.status === "unsolved", "unsolved (divergent)");
check("solve exact", ms.solve("x^2 = 4", "x", 0, 0, false), (r) => r.ok && r.status === "solved" && r.solutions.length === 2 && r.solutions.map((s) => s.plain).sort().join(",") === "-2,2", "roots -2, 2");
check("solve bare expr", ms.solve("x^2 - 9", "x", 0, 0, false), (r) => r.ok && r.solutions.length === 2, "expr treated as = 0");
check("solve numeric+range", ms.solve("cos(x) = x", "x", -1, 1, true), (r) => r.ok && r.status === "numeric" && Math.abs(r.solutions[0].approx - 0.7390851332) < 1e-6, "~0.739085");
check("solve trig note", ms.solve("sin(x) = 1/2", "x", 0, 0, false), (r) => r.ok && r.solutions[0].plain === "pi/6" && r.solutions[0].note.includes("2*pi*n"), "pi/6 with periodicity note");
check("system solved", ms.solveSystem("x + y = 3; x - y = 1", "x,y"), (r) => r.ok && r.status === "solved" && r.values.find((v) => v.symbol === "x").plain === "2", "x=2");
check("system inferred vars", ms.solveSystem("x + y = 3; x - y = 1", ""), (r) => r.ok && r.status === "solved", "vars inferred");
check("system underdetermined", ms.solveSystem("x + y = 3", "x,y"), (r) => r.ok && r.status === "underdetermined" && r.free.includes("y"), "free y");
check("evaluate", ms.evaluate("x^2 + y", "x=3, y=0.5"), (r) => r.ok && r.value === 9.5, "9.5");
check("evaluate error", ms.evaluate("ln(x)", "x=-1"), (r) => !r.ok && r.error.length > 0, "domain error");
check("sample", ms.sample("1/x", "x", -1, 1, 5), (r) => r.ok && r.ys.length === 5 && r.ys[2] === null && typeof r.ys[0] === "number", "null at x=0");
check("latex json escaping", ms.latex("\\frac{1}{2} + \\sqrt{x}"), (r) => r.ok && r.latex.includes("\\sqrt"), "backslashes survive JSON");
// subs: expression + equation arms and the simplifyResult flag
// (variable-assignment spec §8: false returns the substituted form
// un-simplified so "computed from" shows the resolved input as resolved).
check("subs expression", ms.subs("a*x + 3", "a=2", true), (r) => r.ok && r.plain === "2*x + 3", "2*x + 3");
check("subs sequential parents-first", ms.subs("f + y", "f=g + 1,g=x^2", true), (r) => r.ok && r.plain === "x^2 + y + 1", "x^2 + y + 1");
check("subs equation both sides", ms.subs("a*x = a + 3", "a=2", true), (r) => r.ok && r.plain === "2*x = 5", "2*x = 5");
check("subs unsimplified", ms.subs("x + x + w", "w=7", false), (r) => r.ok && r.plain === "x + x + 7", "x + x + 7 (no simplify)");
check("subs unsimplified latex", ms.subs("x + x + w", "w=7", false), (r) => r.ok && r.latex === "x + x + 7", "x + x + 7");
check("subs subscripted name", ms.subs("v_{max} + 1", "v_max=8", true), (r) => r.ok && r.plain === "9", "9 (canonical symbol names accepted)");
check("subs malformed", ms.subs("x", "x", true), (r) => !r.ok && r.error.includes("malformed substitution"), "malformed error");

console.log(failures === 0 ? `\nALL PASS` : `\n${failures} FAILURES`);
process.exit(failures === 0 ? 0 : 1);
