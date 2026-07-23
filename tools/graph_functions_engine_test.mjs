// End-to-end test of the console/grapher user-function expansion against the
// real WASM engine: it drives the actual `findInnermostAppl` scanner (bundled
// from functions.ts) through the same two-phase capture-avoiding reduction that
// expand.ts uses, so composition / prime / multi-arg / capture cases are
// verified against the engine — the console's runLine can't run headless.
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

const wasm = process.argv[2] ?? resolve(process.cwd(), "web/src/lib/wasm/mathsolver.js");
const M = await (await import(wasm)).default();
const call = (fn, args) => JSON.parse(M[fn](...args));

const dir = mkdtempSync(join(tmpdir(), "gfe-"));
const entry = join(dir, "entry.ts");
writeFileSync(
  entry,
  `export { findInnermostAppl, freshPlaceholders } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/functions.ts")};
   export { splitTopLevelCommas } from ${JSON.stringify(process.cwd() + "/web/src/lib/graph/classify.ts")};`,
);
const out = join(dir, "bundle.mjs");
execFileSync("npx", ["esbuild", entry, "--bundle", "--format=esm", `--outfile=${out}`], {
  cwd: process.cwd(),
  stdio: ["ignore", "ignore", "inherit"],
});
const { findInnermostAppl, freshPlaceholders, splitTopLevelCommas } = await import(out);
rmSync(dir, { recursive: true, force: true });

// A minimal registry + the expand.ts algorithm, driven by the real scanner.
function makeReg(defs) {
  return { getFn: (n) => defs[n], names: Object.keys(defs) };
}
function betaReduce(params, body, args) {
  const ph = freshPlaceholders(params.length, `${body} ${args.join(" ")}`);
  const r1 = call("subs", [body, params.map((p, k) => `${p}=${ph[k]}`).join(","), false]);
  const r2 = call("subs", [r1.plain, ph.map((z, k) => `${z}=(${args[k]})`).join(","), false]);
  return r2.plain;
}
function expand(text, reg) {
  let s = text;
  for (let g = 0; g < 64; g++) {
    const c = findInnermostAppl(s, reg.names);
    if (!c) break;
    const fn = reg.getFn(c.name);
    const args = splitTopLevelCommas(c.inner).map((a) => a.trim());
    let body = fn.body;
    for (let k = 0; k < c.primes; k++) body = call("derivative", [body, fn.params[0]]).plain;
    s = s.slice(0, c.start) + "(" + betaReduce(fn.params, body, args) + ")" + s.slice(c.end);
  }
  return s;
}
const simp = (s) => call("simplify", [s]).plain;

let pass = 0, fail = 0;
const check = (n, got, want) => {
  if (got === want) { pass++; console.log("PASS  " + n); }
  else { fail++; console.log(`FAIL  ${n}: got ${got}, want ${want}`); }
};

const reg = makeReg({
  f: { params: ["x"], body: "x^2" },
  g: { params: ["t"], body: "t + 1" },
  h: { params: ["a", "b"], body: "a/b" },
  p: { params: ["x"], body: "a*x^2" }, // free slider `a` in the body
});

check("f(3) evaluates", simp(expand("f(3)", reg)), "9");
check("y = f(x) + 1", simp(expand("f(x) + 1", reg)), "x^2 + 1");
check("composition g(f(x))", simp(expand("g(f(x))", reg)), "x^2 + 1");
check("multi-arg capture-safe h(b, a)", simp(expand("h(b, a)", reg)), "b/a");
check("prime f'(x)", simp(expand("f'(x)", reg)), "2*x");
check("prime f''(x)", simp(expand("f''(x)", reg)), "2");
check("free body var kept p(2)", simp(expand("p(2)", reg)), "4*a");
check("expands, then the verb layer simplifies 2*f(x+1)",
  call("expand", [expand("2*f(x+1)", reg)]).plain, "2*x^2 + 4*x + 2");

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
