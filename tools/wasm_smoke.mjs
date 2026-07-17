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
// plugins (mathsolver/plugin.hpp, docs/PLUGINS.md)
check("plugins catalog", ms.plugins(), (r) => r.ok && r.plugins.some((p) => p.name === "dsp" && p.commands.some((c) => c.name === "butter")), "dsp plugin listed");
check("plugin butter", ms.pluginCall("dsp", "butter", "lowpass,4,1000,48000"), (r) => r.ok && r.blocks.some((b) => b.type === "table") && b_series_ok(r), "block envelope");
check("plugin butter kv gain", ms.pluginCall("dsp", "butter", "lowpass,4,1000,48000"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Gain at cutoff" && v.startsWith("-3.0"))), "-3.01 dB at fc");
check("plugin freqz", ms.pluginCall("dsp", "freqz", "48000,0.2,0.4,0.2,-0.5,0.3"), (r) => r.ok && r.blocks.filter((b) => b.type === "series").length === 4, "mag+phase+group delay+time");
check("plugin bad args", ms.pluginCall("dsp", "butter", "lowpass,4,30000,48000"), (r) => !r.ok && r.error.includes("(0, fs/2)"), "cutoff range error");
check("plugin unknown", ms.pluginCall("nope", "x", ""), (r) => !r.ok && r.error.includes("no plugin named"), "unknown plugin error");
check("plugin cheby1", ms.pluginCall("dsp", "cheby1", "lowpass,5,1,1000,48000"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Gain at cutoff" && v.startsWith("-1.00"))), "-1 dB at ripple edge");
check("plugin cheby2", ms.pluginCall("dsp", "cheby2", "lowpass,4,40,2000,48000"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Gain at cutoff" && v.startsWith("-40.00"))), "-40 dB at stop edge");
check("plugin bandpass edges", ms.pluginCall("dsp", "butter", "bandpass,3,500,2000,48000"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Gain at f1" && v.startsWith("-3.01")) && b.items.some(([k, v]) => k === "Gain at f2" && v.startsWith("-3.01"))), "-3.01 dB at both edges");
check("plugin vlines", ms.pluginCall("dsp", "butter", "bandpass,3,500,2000,48000"), (r) => r.ok && r.blocks.some((b) => b.type === "series" && Array.isArray(b.vlines) && b.vlines.length === 2), "edges marked on chart");
check("plugin ellip", ms.pluginCall("dsp", "ellip", "lowpass,5,1,60,1000,48000"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Gain at cutoff" && v.startsWith("-1.00"))), "-1 dB at passband edge");
check("plugin fir", ms.pluginCall("dsp", "fir", "lowpass,101,1000,48000,kaiser,10"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Group delay" && v.includes("50 samples"))) && r.blocks.some((b) => b.type === "table" && b.rows.length === 101), "linear phase + 101 taps");
check("plugin time response", ms.pluginCall("dsp", "butter", "lowpass,4,1000,48000"), (r) => r.ok && r.blocks.some((b) => b.type === "series" && b.title === "Time response" && b.series.length === 2), "impulse + step series");
// complex numbers (v0.6): i in the core, complex quadratic roots
check("complex solve", ms.solve("x^2 = -1", "x", 0, 0, false), (r) => r.ok && r.status === "complex" && r.solutions.map((s) => s.plain).sort().join(",") === "-i,i", "roots -i, i");
check("complex conjugate pair", ms.solve("x^2 + 2*x + 5 = 0", "x", 0, 0, false), (r) => r.ok && r.status === "complex" && r.solutions.some((s) => s.plain === "2*i - 1"), "2*i - 1 (canonical order)");
check("i simplifies", ms.simplify("i^2"), (r) => r.ok && r.plain === "-1", "i^2 = -1");
// sys plugin: transfer functions, ODE -> H(s), discretization
check("sys tf", ms.pluginCall("sys", "tf", "s+1,s^2+3s+2"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Stability" && v === "stable")) && r.blocks.some((b) => b.type === "series" && b.series.some((s) => s.points === true)), "stable + pzmap points");
check("sys ode", ms.pluginCall("sys", "ode", "y'' + 3y' + 2y = u' + u"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "H(s)" && v.includes("s^2 + 3 s + 2"))), "ODE -> H(s)");
check("sys c2d", ms.pluginCall("sys", "c2d", "1,s+1,100"), (r) => r.ok && r.blocks.some((b) => b.type === "table" && b.title.includes("Digital biquad")), "bilinear biquads");
check("sys error", ms.pluginCall("sys", "tf", "sin(s),s+1"), (r) => !r.ok && r.error.includes("polynomial"), "non-polynomial rejected");
check("sys margins", ms.pluginCall("sys", "tf", "1,s^3 + 3s^2 + 2s"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Gain margin" && v.startsWith("15.56 dB")) && b.items.some(([k, v]) => k === "Phase margin" && v.startsWith("53.4"))), "textbook margins");
check("sys feedback", ms.pluginCall("sys", "feedback", "1,s + 1,3"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "DC gain" && v === "0.75")), "closed loop 3/(s+4)");
check("sys rlocus", ms.pluginCall("sys", "rlocus", "1,s^3 + 3s^2 + 2s,100"), (r) => r.ok && r.blocks.some((b) => b.type === "series" && b.series.some((s) => s.label.startsWith("locus"))) && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Verdict" && v.includes("unstable near K"))), "locus + critical gain");

function b_series_ok(r) {
  const s = r.blocks.find((b) => b.type === "series");
  return s && s.x.length === s.series[0].ys.length && s.x.length > 100;
}

console.log(failures === 0 ? `\nALL PASS` : `\n${failures} FAILURES`);
process.exit(failures === 0 ? 0 : 1);
