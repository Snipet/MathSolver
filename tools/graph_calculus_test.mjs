// Unit tests for graph/calculus.ts text scanning (findInnermostCall, stripCalc).
// Run: node tools/graph_calculus_test.mjs
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const SRC_CLASSIFY = "web/src/lib/graph/classify.ts";
const SRC_CALCULUS = "web/src/lib/graph/calculus.ts";

// Transpile the two TS modules to ESM via esbuild (already a dev dep).
const dir = mkdtempSync(join(tmpdir(), "gcalc-"));
try {
  const out = join(dir, "calculus.mjs");
  execFileSync(
    "npx",
    ["esbuild", SRC_CALCULUS, "--bundle", "--format=esm", `--outfile=${out}`],
    { cwd: process.cwd(), stdio: ["ignore", "ignore", "inherit"] },
  );
  const mod = await import(out);
  const { findInnermostCall, stripCalc } = mod;

  let pass = 0;
  let fail = 0;
  function eq(actual, expected, msg) {
    const a = JSON.stringify(actual);
    const e = JSON.stringify(expected);
    if (a === e) {
      pass++;
    } else {
      fail++;
      console.error(`FAIL ${msg}\n  expected ${e}\n  actual   ${a}`);
    }
  }

  // findInnermostCall
  eq(findInnermostCall("x^2"), null, "no call");
  eq(findInnermostCall("diff(x^2)")?.name, "diff", "diff name");
  eq(findInnermostCall("diff(x^2)")?.inner, "x^2", "diff inner");
  eq(findInnermostCall("integral(sin(x))")?.inner, "sin(x)", "integral inner nested paren");
  eq(findInnermostCall("diff(f, x)")?.inner, "f, x", "diff with var arg");
  // innermost first: the outer diff wraps an inner integral → inner returned
  eq(findInnermostCall("diff(integral(x))")?.name, "integral", "innermost is integral");
  eq(findInnermostCall("diff(integral(x))")?.inner, "x", "innermost inner");
  // word boundary: "diffusion(" must not match
  eq(findInnermostCall("diffusion(x)"), null, "word boundary");
  // spaces before paren
  eq(findInnermostCall("diff (x^2)")?.inner, "x^2", "space before paren");

  // stripCalc
  eq(stripCalc("x^2"), "x^2", "strip no-op");
  eq(stripCalc("diff(x^2)"), "(x^2)", "strip diff");
  eq(stripCalc("diff(f) + g"), "(f) + g", "strip keeps outer");
  eq(stripCalc("diff(f, x)"), "(f)", "strip drops var arg");
  eq(stripCalc("integral(diff(sin(x)))"), "((sin(x)))", "strip nested");
  eq(stripCalc("2*integral(t^2, t)"), "2*(t^2)", "strip inside product");

  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
} finally {
  rmSync(dir, { recursive: true, force: true });
}
void writeFileSync;
