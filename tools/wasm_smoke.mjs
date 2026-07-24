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
// fit / regression
check("fit exact line", ms.fit("0,1; 1,3; 2,5", "linear", ""), (r) => r.ok && r.plain === "2*x + 1" && r.exact === true && r.r2 === 1, "2x + 1 exactly");
check("fit exact quadratic", ms.fit("0,0; 1,1; 2,4; 3,9", "quadratic", ""), (r) => r.ok && r.plain === "x^2" && r.exact === true, "x^2");
check("fit exact fractions", ms.fit("0,1; 1,2; 2,2; 3,4", "linear", ""), (r) => r.ok && r.plain === "9*x/10 + 9/10" && r.exact === true, "exact rational best fit");
check("fit exponential", ms.fit("0,1; 1,2.71828; 2,7.38906", "exp", ""), (r) => r.ok && r.model === "exponential" && r.r2 > 0.999, "a*e^(bx)");
check("fit error few points", ms.fit("0,0", "linear", ""), (r) => !r.ok && r.error.includes("at least 2"), "needs 2 points");
check("fit error bad model", ms.fit("0,0; 1,1", "wiggle", ""), (r) => !r.ok && r.error.includes("unknown fit model"), "unknown model rejected");
// interp — exact polynomial through the points
check("interp exact quadratic", ms.interp("1,1; 2,4; 3,9"), (r) => r.ok && r.plain === "x^2" && r.exact === true && r.degree === 2 && r.n === 3, "x^2 through 3 points");
check("interp collinear collapses", ms.interp("0,1; 1,3; 2,5"), (r) => r.ok && r.plain === "2*x + 1" && r.degree === 1, "degree drops to 1");
check("interp exact fractions", ms.interp("0,0; 1,1; 2,1"), (r) => r.ok && r.exact === true && r.degree === 2, "rational coefficients");
check("interp single point", ms.interp("5,7"), (r) => r.ok && r.plain === "7" && r.degree === 0 && r.n === 1, "constant polynomial");
check("interp error dup x", ms.interp("1,2; 1,3"), (r) => !r.ok && r.error.includes("distinct"), "duplicate x rejected");
// newton / lagrange interpolation forms (factored construction, not expanded)
check("newton form factored", ms.interpForm("1,1; 2,4; 3,9", "newton"), (r) => r.ok && r.plain.includes("(x - 1)") && r.notes.includes("c0 = 1"), "Newton form + c0=1");
check("lagrange form factored", ms.interpForm("1,1; 2,4; 3,9", "lagrange"), (r) => r.ok && r.plain.includes("(x - 3)") && r.notes.some((n) => n === "w1 = -4"), "Lagrange form + w1=-4");
check("interpForm error dup x", ms.interpForm("1,1; 1,2", "newton"), (r) => !r.ok && r.error.includes("distinct"), "duplicate x rejected");
check("interpForm error bad form", ms.interpForm("1,1; 2,4", "cubic"), (r) => !r.ok && r.error.includes("newton"), "unknown form rejected");
// orthopoly — exact orthogonal polynomials
check("orthopoly chebyshev T5", ms.orthopoly("chebyshev", 5, "x"), (r) => r.ok && r.plain === "16*x^5 - 20*x^3 + 5*x" && r.family === "Chebyshev T" && r.degree === 5, "T_5");
check("orthopoly chebyshev U (second kind)", ms.orthopoly("chebyshevu", 3, "x"), (r) => r.ok && r.plain === "8*x^3 - 4*x" && r.family === "Chebyshev U", "U_3");
check("orthopoly legendre P3", ms.orthopoly("legendre", 3, "x"), (r) => r.ok && r.plain === "5*x^3/2 - 3*x/2" && r.family === "Legendre", "P_3 exact fractions");
check("orthopoly hermite H4", ms.orthopoly("hermite", 4, "x"), (r) => r.ok && r.plain === "16*x^4 - 48*x^2 + 12" && r.family === "Hermite", "H_4");
check("orthopoly named variable", ms.orthopoly("chebyshev", 2, "t"), (r) => r.ok && r.plain === "2*t^2 - 1", "T_2 in t");
check("orthopoly error bad family", ms.orthopoly("wiggle", 3, "x"), (r) => !r.ok && r.error.includes("unknown polynomial family"), "unknown family rejected");
// bezout — extended polynomial gcd with cofactors
check("bezout shared factor", ms.bezout("x^2 - 1", "x^3 - 1", "x"), (r) => r.ok && r.plain === "x - 1" && r.notes.some((n) => n === "s = -x") && r.notes.some((n) => n === "t = 1"), "gcd x-1 with cofactors");
check("bezout coprime → gcd 1", ms.bezout("x^2 + 1", "x - 1", "x"), (r) => r.ok && r.plain === "1" && r.notes.length === 2, "coprime gcd 1");
check("bezout error non-poly", ms.bezout("sin(x)", "x", "x"), (r) => !r.ok && r.error.includes("polynomial"), "non-polynomial rejected");
// companion — companion matrix of a polynomial (MATLAB compan orientation)
check("companion quadratic", ms.companion("x^2 - 3x + 2", "x"), (r) => r.ok && r.plain === "[3, -2; 1, 0]" && r.latex.includes("pmatrix"), "[3,-2;1,0]");
check("companion normalizes leading coeff", ms.companion("2x^2 + 4x - 6", "x"), (r) => r.ok && r.plain === "[-2, 3; 1, 0]", "monic-normalized");
check("companion cubic subdiagonal", ms.companion("x^3 - 1", "x"), (r) => r.ok && r.plain === "[0, 0, 1; 1, 0, 0; 0, 1, 0]", "subdiagonal ones");
check("companion infers single variable", ms.companion("t^2 - 5t + 6", ""), (r) => r.ok && r.plain === "[5, -6; 1, 0]", "var inferred");
check("companion error degree 0", ms.companion("7", "x"), (r) => !r.ok && r.error.includes("degree"), "constant rejected");
check("companion error non-poly", ms.companion("sin(x)", "x"), (r) => !r.ok && r.error.includes("polynomial"), "non-polynomial rejected");
// vandermonde — Vandermonde matrix of a node list
check("vandermonde numeric nodes", ms.vandermonde("1, 2, 3"), (r) => r.ok && r.plain === "[1, 1, 1; 1, 2, 4; 1, 3, 9]" && r.latex.includes("pmatrix"), "[1,1,1;1,2,4;1,3,9]");
check("vandermonde symbolic nodes", ms.vandermonde("a, b"), (r) => r.ok && r.plain === "[1, a; 1, b]", "symbolic stays symbolic");
check("vandermonde zero node", ms.vandermonde("0, 5"), (r) => r.ok && r.plain === "[1, 0; 1, 5]", "x^0 = 1");
check("vandermonde error empty", ms.vandermonde(""), (r) => !r.ok && r.error.includes("node"), "empty rejected");
// sigma (divisor function σ_k) and mobius (μ)
check("sigma sum of divisors", ms.sigma("12", ""), (r) => r.ok && r.plain === "28", "σ(12)=28");
check("sigma of squares", ms.sigma("6", "2"), (r) => r.ok && r.plain === "50", "σ_2(6)=50");
check("sigma count of divisors", ms.sigma("12", "0"), (r) => r.ok && r.plain === "6", "σ_0(12)=6");
check("sigma error non-positive", ms.sigma("0", ""), (r) => !r.ok && r.error.includes("positive"), "n≥1 required");
check("mobius three primes", ms.mobius("30"), (r) => r.ok && r.plain === "-1", "μ(30)=-1");
check("mobius squared factor", ms.mobius("12"), (r) => r.ok && r.plain === "0", "μ(12)=0");
check("mobius of one", ms.mobius("1"), (r) => r.ok && r.plain === "1", "μ(1)=1");
// partitions p(n) and catalan numbers
check("partitions of 10", ms.partitions("10"), (r) => r.ok && r.plain === "42", "p(10)=42");
check("partitions of 100", ms.partitions("100"), (r) => r.ok && r.plain === "190569292", "p(100)");
check("partitions error negative", ms.partitions("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
check("catalan of 10", ms.catalan("10"), (r) => r.ok && r.plain === "16796", "C(10)=16796");
check("catalan C(40) big", ms.catalan("40"), (r) => r.ok && r.plain === "2622127042276492108820", "arbitrary precision");
// bernoulli numbers as exact rationals
check("bernoulli B_2", ms.bernoulli("2"), (r) => r.ok && r.plain === "1/6", "B_2=1/6");
check("bernoulli B_1 convention", ms.bernoulli("1"), (r) => r.ok && r.plain === "-1/2", "B_1=-1/2");
check("bernoulli B_12", ms.bernoulli("12"), (r) => r.ok && r.plain === "-691/2730", "B_12");
check("bernoulli odd vanishes", ms.bernoulli("3"), (r) => r.ok && r.plain === "0", "B_3=0");
check("bernoulli error range", ms.bernoulli("21"), (r) => !r.ok && r.error.includes("[0, 20]"), "index capped");
// stirling numbers of the second kind S(n, k) and bell numbers B(n)
check("stirling2 S(4,2)", ms.stirling2("4", "2"), (r) => r.ok && r.plain === "7", "S(4,2)=7");
check("stirling2 S(5,3)", ms.stirling2("5", "3"), (r) => r.ok && r.plain === "25", "S(5,3)=25");
check("stirling2 k>n", ms.stirling2("3", "5"), (r) => r.ok && r.plain === "0", "S(3,5)=0");
check("stirling2 error negative", ms.stirling2("-1", "2"), (r) => !r.ok && r.error.includes(">= 0"), "n,k≥0 required");
check("bell B(5)", ms.bell("5"), (r) => r.ok && r.plain === "52", "B(5)=52");
check("bell B(10)", ms.bell("10"), (r) => r.ok && r.plain === "115975", "B(10)=115975");
check("bell error negative", ms.bell("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
// derangements (subfactorial !n)
check("derangement !4", ms.derangement("4"), (r) => r.ok && r.plain === "9", "!4=9");
check("derangement !10", ms.derangement("10"), (r) => r.ok && r.plain === "1334961", "!10=1334961");
check("derangement !0", ms.derangement("0"), (r) => r.ok && r.plain === "1", "!0=1");
check("derangement error negative", ms.derangement("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
check("derangement !21 big", ms.derangement("21"), (r) => r.ok && r.plain === "18795307255050944540", "arbitrary precision");
// lucas numbers L(n)
check("lucas L(0)", ms.lucas("0"), (r) => r.ok && r.plain === "2", "L(0)=2");
check("lucas L(10)", ms.lucas("10"), (r) => r.ok && r.plain === "123", "L(10)=123");
check("lucas L(20)", ms.lucas("20"), (r) => r.ok && r.plain === "15127", "L(20)=15127");
check("lucas error negative", ms.lucas("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
check("lucas L(91) big", ms.lucas("91"), (r) => r.ok && r.plain === "10420180999117162549", "arbitrary precision");
// primorial n# (product of primes ≤ n)
check("primorial 7#", ms.primorial("7"), (r) => r.ok && r.plain === "210", "7#=210");
check("primorial 13#", ms.primorial("13"), (r) => r.ok && r.plain === "30030", "13#=30030");
check("primorial 10# = 7#", ms.primorial("10"), (r) => r.ok && r.plain === "210", "no prime in (7,10]");
check("primorial error negative", ms.primorial("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
check("primorial 53# big", ms.primorial("53"), (r) => r.ok && r.plain === "32589158477190044730", "arbitrary precision");
// motzkin numbers M(n)
check("motzkin M(6)", ms.motzkin("6"), (r) => r.ok && r.plain === "51", "M(6)=51");
check("motzkin M(10)", ms.motzkin("10"), (r) => r.ok && r.plain === "2188", "M(10)=2188");
check("motzkin M(20)", ms.motzkin("20"), (r) => r.ok && r.plain === "50852019", "M(20)");
check("motzkin error negative", ms.motzkin("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
check("motzkin M(45) big", ms.motzkin("45"), (r) => r.ok && r.plain === "13603677110519480289", "arbitrary precision");
// euler (secant) numbers E(n)
check("euler E(6)", ms.euler("6"), (r) => r.ok && r.plain === "-61", "E(6)=-61");
check("euler E(8)", ms.euler("8"), (r) => r.ok && r.plain === "1385", "E(8)=1385");
check("euler odd vanishes", ms.euler("7"), (r) => r.ok && r.plain === "0", "E(7)=0");
check("euler error negative", ms.euler("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
check("euler E(24) big", ms.euler("24"), (r) => r.ok && r.plain === "15514534163557086905", "arbitrary precision");
// tribonacci numbers T(n)
check("tribonacci T(10)", ms.tribonacci("10"), (r) => r.ok && r.plain === "81", "T(10)=81");
check("tribonacci T(20)", ms.tribonacci("20"), (r) => r.ok && r.plain === "35890", "T(20)=35890");
check("tribonacci T(2)", ms.tribonacci("2"), (r) => r.ok && r.plain === "1", "T(2)=1");
check("tribonacci error negative", ms.tribonacci("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
check("tribonacci T(75) big", ms.tribonacci("75"), (r) => r.ok && r.plain === "12903063846126135669", "arbitrary precision");
// pell numbers P(n)
check("pell P(2)", ms.pell("2"), (r) => r.ok && r.plain === "2", "P(2)=2");
check("pell P(10)", ms.pell("10"), (r) => r.ok && r.plain === "2378", "P(10)=2378");
check("pell P(20)", ms.pell("20"), (r) => r.ok && r.plain === "15994428", "P(20)=15994428");
check("pell error negative", ms.pell("-1"), (r) => !r.ok && r.error.includes(">= 0"), "n≥0 required");
check("pell P(51) big", ms.pell("51"), (r) => r.ok && r.plain === "11749380235262596085", "arbitrary precision");
// stats — exact summary statistics
const statVal = (r, label) => r.items.find((it) => it.label === label)?.plain;
check("stats exact mean", ms.stats("1, 2, 4"), (r) => r.ok && r.exact && statVal(r, "mean") === "7/3", "mean 7/3");
check("stats exact stdev radical", ms.stats("1, 2, 3, 4, 5"), (r) => r.ok && statVal(r, "stdev (pop)") === "sqrt(2)" && statVal(r, "stdev (sample)") === "sqrt(10)/2", "sqrt radicals");
check("stats quartiles (TI method)", ms.stats("1, 2, 3, 4, 5"), (r) => r.ok && statVal(r, "Q1") === "3/2" && statVal(r, "Q3") === "9/2", "Q1=3/2 Q3=9/2");
check("stats numeric fallback", ms.stats("1, pi, 2"), (r) => r.ok && r.exact === false && r.n === 3, "irrational → numeric");
check("stats error empty", ms.stats(""), (r) => !r.ok && r.error.includes("no data"), "empty rejected");
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
// prob — probability distributions
const kvOf = (r, pfx) => r.blocks[0].items.find(([k]) => k.startsWith(pfx))?.[1];
check("prob normalcdf", ms.pluginCall("prob", "normalcdf", "1.96"), (r) => r.ok && parseFloat(kvOf(r, "P(X <=")) > 0.9749 && parseFloat(kvOf(r, "P(X <=")) < 0.9751 && r.blocks.some((b) => b.type === "series"), "P=0.975 + curve");
check("prob invnorm", ms.pluginCall("prob", "invnorm", "0.975"), (r) => r.ok && Math.abs(parseFloat(kvOf(r, "x")) - 1.95996) < 1e-3, "quantile 1.96");
check("prob binompdf", ms.pluginCall("prob", "binompdf", "10, 0.5, 5"), (r) => r.ok && Math.abs(parseFloat(kvOf(r, "P(X =")) - 0.246094) < 1e-5 && r.blocks.some((b) => b.type === "series" && b.series[0].points === true), "C(10,5)/1024 + stems");
check("prob poissoncdf", ms.pluginCall("prob", "poissoncdf", "3, 2"), (r) => r.ok && Math.abs(parseFloat(kvOf(r, "P(X <=")) - 0.42319) < 1e-4, "0.42319");
check("prob domain error", ms.pluginCall("prob", "normalcdf", "1, 0, -2"), (r) => !r.ok && r.error.includes("positive"), "sigma>0 enforced");
check("prob integer error", ms.pluginCall("prob", "binompdf", "10, 0.5, 2.5"), (r) => !r.ok && r.error.includes("whole number"), "k must be integer");
check("prob tcdf", ms.pluginCall("prob", "tcdf", "2.228, 10"), (r) => r.ok && Math.abs(parseFloat(kvOf(r, "P(X <=")) - 0.975) < 1e-4, "t(10) 97.5th pct");
check("prob chi2cdf", ms.pluginCall("prob", "chi2cdf", "7.815, 3"), (r) => r.ok && Math.abs(parseFloat(kvOf(r, "P(X <=")) - 0.95) < 1e-4, "chi2(3) 95th pct");
check("prob expcdf", ms.pluginCall("prob", "expcdf", "2, 0.5"), (r) => r.ok && Math.abs(parseFloat(kvOf(r, "P(X <=")) - (1 - Math.exp(-1))) < 1e-6, "1 - e^-1");
check("prob unifcdf", ms.pluginCall("prob", "unifcdf", "3, 0, 10"), (r) => r.ok && Math.abs(parseFloat(kvOf(r, "P(X <=")) - 0.3) < 1e-9, "(3-0)/10");
check("prob t df error", ms.pluginCall("prob", "tcdf", "1, -2"), (r) => !r.ok && r.error.includes("degrees of freedom"), "nu>0 enforced");
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

// User-function beta-reduction: the two-phase capture-avoiding `subs` the
// grapher runs for f(args). The multi-arg swap is the exact case a naive
// sequential subs CSV (a=b,b=a) would corrupt to a/a.
const beta = (params, body, args) => {
  const used = new Set(((body + " " + args.join(" ")).match(/[A-Za-z]/g) || []));
  const ph = [];
  for (const c of "ZQWJKVUXYHGNMBTRCPLOSDAF") {
    if (ph.length === params.length) break;
    if (!used.has(c)) ph.push(c);
  }
  const r1 = JSON.parse(ms.subs(body, params.map((p, k) => `${p}=${ph[k]}`).join(","), false));
  const r2 = JSON.parse(ms.subs(r1.plain, ph.map((z, k) => `${z}=(${args[k]})`).join(","), false));
  return r2.plain;
};
check("fn reduce parenthesizes", JSON.stringify({ ok: true, p: beta(["x"], "x^2", ["x+1"]) }), (r) => r.p === "(x + 1)^2", "(x + 1)^2");
check("fn reduce multi-arg swap", JSON.stringify({ ok: true, p: beta(["a", "b"], "a/b", ["b", "a"]) }), (r) => r.p === "b/a", "b/a (capture-safe, not a/a)");

function b_series_ok(r) {
  const s = r.blocks.find((b) => b.type === "series");
  return s && s.x.length === s.series[0].ys.length && s.x.length > 100;
}

console.log(failures === 0 ? `\nALL PASS` : `\n${failures} FAILURES`);
process.exit(failures === 0 ? 0 : 1);
