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
check("cancel", ms.cancel("(x^2-1)/(x-1)"), (r) => r.ok && r.plain === "x + 1", "x + 1");
check("together", ms.together("1/x + 1/y"), (r) => r.ok && r.plain === "(x + y)/(x*y)", "(x + y)/(x*y)");
check("latex frac", ms.latex("sqrt(x)/2"), (r) => r.ok && r.latex.includes("\\frac"), "\\frac");
check("analyze expr", ms.analyze("a*x^2 + b"), (r) => r.ok && r.kind === "expression" && r.symbols.join(",") === "a,b,x", "symbols a,b,x");
check("analyze equation", ms.analyze("x^2 = 4"), (r) => r.ok && r.kind === "equation", "equation");
check("analyze system", ms.analyze("x + y = 3; x - y = 1"), (r) => r.ok && r.kind === "system", "system");
check("parse error span", ms.simplify("2 + \\fraq{1}{2}"), (r) => !r.ok && r.begin === 4 && r.end > r.begin, "span at \\fraq");
check("derivative", ms.derivative("sin(x^2)", "x"), (r) => r.ok && r.plain === "2*x*cos(x^2)", "2*x*cos(x^2)");
check("integrate", ms.integrate("x*sin(x)", "x"), (r) => r.ok && r.solved && r.plain.includes("-x*cos(x)"), "-x*cos(x) + sin(x)");
check("integrate honesty", ms.integrate("e^(x^2)", "x"), (r) => r.ok && !r.solved, "unsolved");
check("definite exact", ms.integrateDefinite("sin(x)", "x", "0", "pi"), (r) => r.ok && r.status === "exact" && r.plain === "2", "value 2");
check("definite gaussian exact (erf)", ms.integrateDefinite("e^(-x^2)", "x", "0", "1"), (r) => r.ok && r.status === "exact" && r.plain.includes("erf"), "sqrt(pi)*erf(1)/2");
check("definite numeric", ms.integrateDefinite("sin(x)/x", "x", "1", "2"), (r) => r.ok && r.status === "numeric" && Math.abs(r.approx - 0.6593299064) < 1e-6, "~0.659330");
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
check("plugin remez lowpass", ms.pluginCall("dsp", "remez", "lowpass,31,1000,1500,8000"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Design" && v.includes("Parks"))) && r.blocks.some((b) => b.type === "table" && b.rows.length === 31), "equiripple design + 31 taps");
check("plugin remez weight deepens stopband", ms.pluginCall("dsp", "remez", "lowpass,41,1000,1500,8000,10"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Stopband atten." && parseFloat(v) > 45)), "weight 10 -> deeper stopband");
check("plugin remez even taps error", ms.pluginCall("dsp", "remez", "lowpass,30,1000,1500,8000"), (r) => !r.ok && r.error.includes("odd"), "Type I needs odd taps");
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
check("sys tfz", ms.pluginCall("sys", "tfz", "z,z^2 - 0.5z + 0.06,8000"), (r) => r.ok && r.blocks.some((b) => b.type === "series" && b.equal === true && b.series.some((s) => s.label === "unit circle")) && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Stability" && v.includes("inside |z| = 1"))), "z-plane analysis");
check("sys tfz unstable", ms.pluginCall("sys", "tfz", "1,z - 1.2,8000"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Stability" && v.includes("outside |z| = 1"))), "unstable z pole");
// series at infinity + mlimit
check("series at inf rational", ms.series("(x+1)/(x-1)", "x", "inf", 3), (r) => r.ok && r.plain.includes("2/x"), "1 + 2/x + ...");
check("series at inf error", ms.series("e^x", "x", "inf", 3), (r) => !r.ok && r.error.includes("no expansion"), "e^x rejected");
check("mlimit dne", ms.mlimit("x*y/(x^2 + y^2)", "x", "0", "y", "0"), (r) => r.ok && r.status === "doesNotExist" && r.warnings.length === 2, "witness paths reported");
check("mlimit parabola trap", ms.mlimit("x*y^2/(x^2 + y^4)", "x", "0", "y", "0"), (r) => r.ok && r.status === "doesNotExist", "x = y^2 path caught");
check("mlimit agree", ms.mlimit("x + 2*y", "x", "1", "y", "2"), (r) => r.ok && r.status === "exact" && r.plain === "5", "continuous point");
// stirling / gamma asymptotics
check("stirling classic series", ms.stirling("x", 3), (r) => r.ok && r.plain.includes("1/(12*x)") && r.plain.includes("1/(360*x^3)") && r.plain.includes("1/(1260*x^5)") && r.notes.length === 3, "exact Bernoulli coefficients + lgamma checks");
check("stirling default var", ms.stirling("", 2), (r) => r.ok && r.plain.includes("ln(x)"), "defaults to x");
check("stirling non-symbol", ms.stirling("x+1", 3), (r) => !r.ok, "non-symbol rejected");
check("stirling terms range", ms.stirling("x", 9), (r) => !r.ok && r.error.includes("[0, 8]"), "term cap enforced");
// elementary + special + integer functions
check("asinh round trip", ms.simplify("sinh(asinh(x)) + acosh(1)"), (r) => r.ok && r.plain === "x", "inverse hyperbolics");
check("gamma exact values", ms.simplify("gamma(5) + gamma(1/2)"), (r) => r.ok && r.plain === "sqrt(pi) + 24", "24 + sqrt(pi)");
check("gamma stays greek when bare", ms.simplify("gamma*2"), (r) => r.ok && r.plain === "2*gamma", "symbol preserved");
check("erf derivative", ms.derivative("erf(x)", "x"), (r) => r.ok && r.plain.includes("e^(-x^2)"), "2e^(-x^2)/sqrt(pi)");
check("gaussian integral", ms.integrate("e^(-x^2)", "x"), (r) => r.ok && r.plain.includes("erf"), "erf antiderivative");
check("binomial folds", ms.simplify("binomial(10, 5)"), (r) => r.ok && r.plain === "252", "= 252");
check("fib and harmonic fold", ms.simplify("fib(10) + harmonic(4)"), (r) => r.ok && r.plain === "685/12", "55 + 25/12");
check("harmonic sum", ms.sum("1/k", "k", "1", "n"), (r) => r.ok && r.status === "exact" && r.plain === "harmonic(n)", "sum 1/k = harmonic(n)");
check("seq fibonacci", ms.seq("0, 1, 1, 2, 3, 5, 8"), (r) => r.ok && r.kind === "recurrence" && r.description.includes("Fibonacci") && r.recurrence === "a(n+2) = a(n+1) + a(n)" && r.next[0] === "13" && r.plain.includes("sqrt(5)"), "Binet + next terms");
check("seq polynomial", ms.seq("1, 4, 9, 16, 25"), (r) => r.ok && r.kind === "polynomial" && r.plain === "n^2 + 2*n + 1", "squares");
check("seq geometric", ms.seq("3, 6, 12, 24, 48"), (r) => r.ok && r.kind === "geometric" && r.next[0] === "96", "ratio 2");
check("seq unknown is honest", ms.seq("1, 1, 2, 5, 29, 866"), (r) => r.ok && r.kind === "unknown", "no false pattern");
check("seq error", ms.seq("1, 2, x, 4"), (r) => !r.ok && r.error.includes("exact numbers"), "non-number rejected");
// sys.dde (delay differential equations)
check("sys dde", ms.pluginCall("sys", "dde", "-x_d,1,1,20"), (r) => r.ok && r.blocks.some((b) => b.type === "series" && b.title === "Delay response") && r.blocks.some((b) => b.type === "kv" && b.items.some(([k]) => k === "Delay tau")), "oscillatory delay decay");
check("sys dde error", ms.pluginCall("sys", "dde", "-x_d + y,1,1,20"), (r) => !r.ok && r.error.includes("found 'y'"), "stray symbol rejected");
// pde plugin
check("pde catalog", ms.plugins(), (r) => r.ok && r.plugins.some((p) => p.name === "pde" && p.commands.length === 3), "pde listed");
check("pde heat", ms.pluginCall("pde", "heat", "1,1,x*(1-x)"), (r) => r.ok && r.blocks.some((b) => b.type === "series" && b.title === "Temperature profiles" && b.series.length === 4), "profile evolution chart");
check("pde wave", ms.pluginCall("pde", "wave", "1,2,sin(pi*x)"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Fundamental period" && v === "1")), "period 2L/c = 1");
check("pde error", ms.pluginCall("pde", "heat", "1,1,x*y"), (r) => !r.ok && r.error.includes("found"), "stray symbol rejected");
// ie plugin
check("ie catalog", ms.plugins(), (r) => r.ok && r.plugins.some((p) => p.name === "ie" && p.commands.length === 2), "ie listed");
check("ie fredholm separable", ms.pluginCall("ie", "fredholm", "x*t,x,1,0,1"), (r) => r.ok && r.blocks.some((b) => b.type === "table" && b.rows.some(([x, u]) => x === "1" && u === "1.5")) && r.blocks.some((b) => b.type === "series" && b.title === "Solution u(x)"), "u(1) = 3/(3-1) = 1.5");
check("ie volterra exp", ms.pluginCall("ie", "volterra", "1,1,1,0,1"), (r) => r.ok && r.blocks.some((b) => b.type === "table" && b.rows.some(([x, u]) => x === "1" && Math.abs(parseFloat(u) - Math.E) < 1e-4)), "u = e^x at x = 1");
check("ie characteristic value", ms.pluginCall("ie", "fredholm", "x*t,x,3,0,1"), (r) => !r.ok && r.error.includes("characteristic"), "lambda = 3 singular");
check("ie error", ms.pluginCall("ie", "fredholm", "x*y,x,1,0,1"), (r) => !r.ok && r.error.includes("found 'y'"), "stray symbol rejected");
// hyb plugin
check("hyb catalog", ms.plugins(), (r) => r.ok && r.plugins.some((p) => p.name === "hyb" && p.commands.length === 1), "hyb listed");
check("hyb bouncing ball", ms.pluginCall("hyb", "sim", "v; -9.81,x,x; -0.8*v,1,0,2"), (r) => r.ok && r.blocks.some((b) => b.type === "table" && b.title.startsWith("Events") && Math.abs(parseFloat(b.rows[0][1]) - Math.sqrt(2 / 9.81)) < 1e-5) && r.blocks.some((b) => b.type === "series" && Array.isArray(b.vlines)), "first bounce at sqrt(2/g)");
check("hyb zeno", ms.pluginCall("hyb", "sim", "v; -9.81,x,x; -0.8*v,1,0,5"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Zeno" && v.includes("accumulation"))), "Zeno note surfaces");
check("hyb error", ms.pluginCall("hyb", "sim", "q; -9.81,x,x; -0.8*v,1,0,2"), (r) => !r.ok && r.error.includes("found 'q'"), "stray symbol rejected");
// fem plugin
check("fem catalog", ms.plugins(), (r) => r.ok && r.plugins.some((p) => p.name === "fem" && p.commands.length === 2), "fem listed");
check("fem bvp sine", ms.pluginCall("fem", "bvp", "1,0,pi^2*sin(pi*x),0,1,u=0,u=0"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Observed convergence order" && parseFloat(v) > 1.6)) && r.blocks.some((b) => b.type === "series" && Math.abs(Math.max(...b.series[0].ys) - 1) < 1e-3), "order ~2 + peak ~1");
check("fem bvp flux", ms.pluginCall("fem", "bvp", "1,0,0,0,1,u=1,u'=2"), (r) => r.ok && r.blocks.some((b) => b.type === "series" && Math.abs(b.series[0].ys[120] - 3) < 1e-9), "u(1) = 3 for u = 1 + 2x");
check("fem modes string", ms.pluginCall("fem", "modes", "1,0,1,0,pi,3"), (r) => r.ok && r.blocks.some((b) => b.type === "table" && Math.abs(parseFloat(b.rows[0][1]) - 1) < 5e-3 && Math.abs(parseFloat(b.rows[2][1]) - 9) < 1e-1), "lambda = 1, 4, 9");
check("fem singular error", ms.pluginCall("fem", "bvp", "1,0,1,0,1,u'=0,u'=0"), (r) => !r.ok && r.error.includes("singular"), "pure Neumann reported");
check("fem symbol error", ms.pluginCall("fem", "bvp", "y,0,1,0,1,u=0,u=0"), (r) => !r.ok && r.error.includes("found 'y'"), "stray symbol rejected");
// pde.simulate
check("pde simulate fisher", ms.pluginCall("pde", "simulate", "10,1,u*(1-u),0.5*sin(pi*x/10),8"), (r) => r.ok && r.blocks.some((b) => b.type === "series" && b.title === "Concentration profiles" && b.series.length === 5 && Math.max(...b.series[4].ys) > 0.9 && Math.max(...b.series[4].ys) < 1.001), "growth toward u = 1");
check("pde simulate newton kv", ms.pluginCall("pde", "simulate", "1,1,2u,sin(pi*x),0.2"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k]) => k === "Newton iterations")), "newton stats reported");
check("pde simulate blow-up", ms.pluginCall("pde", "simulate", "1,1,u^2,50*sin(pi*x),1"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Stopped early" && v.includes("stopped"))), "blow-up note surfaces");
check("pde simulate error", ms.pluginCall("pde", "simulate", "1,1,u+y,sin(pi*x),1"), (r) => !r.ok && r.error.includes("found 'y'"), "stray symbol rejected");
// linalg plugin
check("linalg catalog", ms.plugins(), (r) => r.ok && r.plugins.some((p) => p.name === "linalg" && p.commands.length === 10), "linalg listed with 10 commands");
check("linalg solve", ms.pluginCall("linalg", "solve", "[2 1; 1 3],[3 5]"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "x" && v === "(0.8, 1.4)")), "x = (0.8, 1.4)");
check("linalg symbolic det", ms.pluginCall("linalg", "det", "[a b; c d]"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([, v]) => v.includes("a*d") && v.includes("b*c"))), "ad - bc");
check("linalg eig complex", ms.pluginCall("linalg", "eig", "[0 -1; 1 0]"), (r) => r.ok && JSON.stringify(r.blocks).includes("i"), "±i");
check("linalg svd rank", ms.pluginCall("linalg", "svd", "[1 2; 2 4]"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([k, v]) => k === "Rank" && v === "1")), "rank-1 detected");
check("linalg comma matrix", ms.pluginCall("linalg", "det", "[4,7;2,6]"), (r) => r.ok && r.blocks.some((b) => b.type === "kv" && b.items.some(([, v]) => v === "10")), "comma entries survive arg split");
check("linalg singular error", ms.pluginCall("linalg", "solve", "[1 2; 2 4],[1 1]"), (r) => !r.ok && r.error.includes("singular"), "singular reported");
check("linalg exact eig", ms.pluginCall("linalg", "eig", "[2 1; 1 2]"), (r) => r.ok && JSON.stringify(r.blocks).includes("Characteristic polynomial") && JSON.stringify(r.blocks).includes("(1, 1)"), "exact eigenpairs");
check("linalg symbolic eig", ms.pluginCall("linalg", "eig", "[a 1; 1 a]"), (r) => r.ok && JSON.stringify(r.blocks).includes("a + 1"), "symbolic eigenvalues");
check("linalg trisolve", ms.pluginCall("linalg", "trisolve", "[-1],[2 2],[-1],[1 1]"), (r) => r.ok && JSON.stringify(r.blocks).includes("(1, 1)"), "Thomas solve");
check("linalg toeplitz", ms.pluginCall("linalg", "toeplitz", "[2 1],[3 3]"), (r) => r.ok && JSON.stringify(r.blocks).includes("Levinson"), "Levinson solve");
check("linalg circulant", ms.pluginCall("linalg", "circulant", "[2 1 1],[4 4 4]"), (r) => r.ok && JSON.stringify(r.blocks).includes("(1, 1, 1)"), "DFT solve");
// discrete calculus (discrete.hpp)
check("sum polynomial", ms.sum("k^2", "k", "1", "n"), (r) => r.ok && r.status === "exact" && r.plain.includes("n^3/3"), "Faulhaber n^3/3 + ...");
check("sum infinite geometric", ms.sum("(1/2)^k", "k", "0", "inf"), (r) => r.ok && r.status === "exact" && r.plain === "2", "= 2");
check("sum telescoping", ms.sum("1/(k*(k+1))", "k", "1", "inf"), (r) => r.ok && r.status === "exact" && r.plain === "1", "= 1");
check("sum diverges", ms.sum("2^k", "k", "0", "inf"), (r) => r.ok && r.status === "diverges", "divergence reported");
check("product factorial range", ms.product("k", "k", "1", "5"), (r) => r.ok && r.plain === "120", "5! = 120");
check("rsolve fibonacci", ms.rsolve("a(n+2) = a(n+1) + a(n)", "a(0)=0,a(1)=1"), (r) => r.ok && r.plain.includes("sqrt(5)") && r.order === 2, "Binet's formula");
check("rsolve forced", ms.rsolve("a(n+1) = 2 a(n) + 1", "a(0)=0"), (r) => r.ok && r.plain.includes("2^n"), "2^n - 1");
// vector-calculus review regressions
check("grad rejects multi-component", ms.vectorOp("grad", "x^2; y^2", "x,y"), (r) => !r.ok && r.error.includes("single scalar"), "no silent truncation");
check("field rejects unknown symbols", ms.sampleField("a*x", "y", "x", "y", -1, 1, -1, 1, 3), (r) => !r.ok && r.error.includes("a"), "unbound symbol reported");
// limits (limit.hpp)
check("limit lhopital", ms.limit("sin(x)/x", "x", "0", ""), (r) => r.ok && r.status === "exact" && r.plain === "1", "sin x/x -> 1");
check("limit rational inf", ms.limit("(3x^2+1)/(x^2-5)", "x", "inf", ""), (r) => r.ok && r.status === "exact" && r.plain === "3", "leading coefficients");
check("limit one-sided diverges", ms.limit("1/x", "x", "0", "right"), (r) => r.ok && r.status === "diverges" && r.sign === 1, "+inf from the right");
check("limit does not exist", ms.limit("1/x", "x", "0", ""), (r) => r.ok && r.status === "doesNotExist" && r.warnings.length === 2, "sides disagree");
check("limit numeric", ms.limit("(1+1/x)^x", "x", "inf", ""), (r) => r.ok && r.status === "numeric" && Math.abs(r.approx - Math.E) < 1e-4, "~e numerically");
// vector calculus (vector_calculus.hpp)
check("vector grad", ms.vectorOp("grad", "x^2 + y^2", "x,y"), (r) => r.ok && r.plain === "(2*x, 2*y)", "(2x, 2y)");
check("vector div", ms.vectorOp("div", "x*y; y*z; z*x", "x,y,z"), (r) => r.ok && r.plain === "x + y + z", "x+y+z");
check("vector curl3", ms.vectorOp("curl", "-y; x; 0", "x,y,z"), (r) => r.ok && r.plain === "(0, 0, 2)", "(0,0,2)");
check("vector curl2", ms.vectorOp("curl", "-y; x", "x,y"), (r) => r.ok && r.plain === "2", "scalar curl 2");
check("vector hessian", ms.vectorOp("hessian", "x^3 + x*y^2", "x,y"), (r) => r.ok && r.plain === "[6*x, 2*y; 2*y, 2*x]", "hessian matrix");
check("vector field sample", ms.sampleField("-y", "x", "x", "y", -1, 1, -1, 1, 3), (r) => r.ok && r.x.length === 9 && r.u[0] === 1 && r.v[0] === -1, "rotational field grid");
// partial fractions (apart.hpp)
check("apart linear", ms.apart("(3x+2)/((x+1)(x+2))", "x"), (r) => r.ok && r.plain.includes("4/(x + 2)") && r.plain.includes("-1/(x + 1)"), "-1/(x+1) + 4/(x+2)");
check("apart improper", ms.apart("x^2/(x^2-1)", "x"), (r) => r.ok && r.plain.startsWith("1 "), "polynomial part 1 leads");
check("apart inferred var", ms.apart("1/(x(x+1)^2)", ""), (r) => r.ok && r.plain.includes("(x + 1)^2"), "repeated factor kept");
check("apart error", ms.apart("sin(x)/(x+1)", "x"), (r) => !r.ok && r.error.includes("not a polynomial"), "non-rational rejected");
// Taylor series (series.hpp)
check("series maclaurin", ms.series("sin(x)", "x", "0", 5), (r) => r.ok && r.plain === "x^5/120 - x^3/6 + x", "sin(x) to order 5");
check("series centered", ms.series("ln(x)", "x", "1", 3), (r) => r.ok && r.plain.includes("(x - 1)^3/3"), "ln about 1");
check("series inferred", ms.series("e^x", "", "", 3), (r) => r.ok && r.plain.includes("x^3/6"), "var inferred, default center");
check("series singular", ms.series("ln(x)", "x", "0", 3), (r) => !r.ok && r.error.includes("singular"), "singular center rejected");
// dsolve: linear ODE IVPs by the Laplace method (ode.hpp)
check("dsolve decay", ms.dsolve("y' + y = 0", "y(0)=1"), (r) => r.ok && r.plain === "e^(-t)", "e^(-t)");
check("dsolve forced", ms.dsolve("y'' + 3y' + 2y = e^(-t)", "y(0)=1,y'(0)=0"), (r) => r.ok && r.plain.includes("t*e^(-t)") && r.transformPlain.includes("(s + 1)^2"), "t e^(-t) + e^(-t), Y(s) shown");
check("dsolve resonance", ms.dsolve("y'' + y = sin(t)", "y(0)=0,y'(0)=0"), (r) => r.ok && r.plain.includes("t*cos(t)"), "secular t cos t term");
check("dsolve warning", ms.dsolve("y' + y = 1", ""), (r) => r.ok && r.warnings.length === 1 && r.warnings[0].includes("y(0) = 0"), "assumed-zero IC warning");
check("dsolve error", ms.dsolve("y' + y = ln(t)", ""), (r) => !r.ok && r.error.includes("no Laplace transform"), "untransformable forcing");
check("dsolve first-order linear", ms.dsolve("y' = -2t*y", "y(0)=1"), (r) => r.ok && r.plain === "e^(-t^2)" && r.method === "integrating factor", "variable-coefficient e^(-t^2)");
check("dsolve system rotation", ms.dsolve("x' = y; y' = -x", "x(0)=1,y(0)=0"), (r) => r.ok && r.plain.includes("x(t) = cos(t)") && r.latex.includes("aligned"), "cos/sin system");
check("dsolve system nonlinear error", ms.dsolve("x' = y*y; y' = x", ""), (r) => !r.ok && r.error.includes("linear"), "nonlinear system rejected");
check("dsolve separable", ms.dsolve("y' = y^2", "y(0)=1"), (r) => r.ok && !r.implicit && r.plain.includes("1"), "y = 1/(1-t) family");
check("dsolve general constant", ms.dsolve("y' = -2y", ""), (r) => r.ok && r.plain.includes("C") && r.warnings.length >= 1, "general solution keeps C");
// limit review regressions
check("limit cancellation plateau", ms.limit("(1 - cos(x))/x^4", "x", "0", ""), (r) => r.ok && r.status === "diverges" && r.sign === 1, "proven +inf, not fake 0");
check("limit parameter guard", ms.limit("a*abs(x)/x", "x", "0", ""), (r) => r.ok && r.status === "doesNotExist", "parameters no longer fool the probe");
check("limit boundary layer", ms.limit("x/(x + 10^(-15))", "x", "0", ""), (r) => r.ok && r.status === "exact" && r.plain === "0", "continuous point trusted");
// symbolic Laplace / inverse Laplace transforms (transform.hpp)
check("laplace 1", ms.laplace("1", "t"), (r) => r.ok && r.plain === "1/s", "1/s");
check("laplace sin", ms.laplace("sin(3t)", "t"), (r) => r.ok && r.plain === "3/(s^2 + 9)", "3/(s^2 + 9)");
check("laplace damped", ms.laplace("e^(-t) sin(2t)", "t"), (r) => r.ok && r.plain.includes("(s + 1)^2 + 4"), "s-shift into the table");
check("laplace default t", ms.laplace("cos(3t)", ""), (r) => r.ok && r.plain === "s/(s^2 + 9)", "empty var defaults to t");
check("laplace error", ms.laplace("ln(t)", "t"), (r) => !r.ok && r.error.includes("no Laplace transform rule"), "no rule for ln(t)");
check("ilaplace const", ms.ilaplace("1/s", "s"), (r) => r.ok && r.plain === "1", "1");
check("ilaplace sin", ms.ilaplace("3/(s^2 + 9)", "s"), (r) => r.ok && r.plain === "sin(3*t)", "sin(3t)");
check("ilaplace cos", ms.ilaplace("s/(s^2 + 9)", "s"), (r) => r.ok && r.plain === "cos(3*t)", "cos(3t)");
check("ilaplace default s", ms.ilaplace("1/(s - 2)", ""), (r) => r.ok && r.plain.includes("e^(2*t)"), "empty var defaults to s");
check("ilaplace error", ms.ilaplace("s^2 + 1", "s"), (r) => !r.ok && r.error.length > 0, "polynomial has no inverse");

function b_series_ok(r) {
  const s = r.blocks.find((b) => b.type === "series");
  return s && s.x.length === s.series[0].ys.length && s.x.length > 100;
}

console.log(failures === 0 ? `\nALL PASS` : `\n${failures} FAILURES`);
process.exit(failures === 0 ? 0 : 1);
