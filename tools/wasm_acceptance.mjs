#!/usr/bin/env node
// wasm_acceptance.mjs — run the native CLI acceptance battery THROUGH the WASM
// module (build-wasm/mathsolver.js) to prove engine parity.
//
// The TSV batteries in tests/acceptance/ (see README.md there) describe CLI
// invocations. This harness adapts each row to the embind API surface
// (wasm/bindings.cpp) and reconstructs the text the CLI would print (see
// apps/main.cpp: print_solve_result / print_system_result / run_integrate /
// run_eval) so the original expected_kind semantics (exact / regex / contains /
// approx with tolerance |a-e| <= 1e-6*max(1,|e|)) apply unchanged.
//
// Rows that only exercise the CLI argument surface (exit-code-2 usage errors,
// --latex output-style flag) are skipped and reported with a reason.
//
// Usage:
//   node tools/wasm_acceptance.mjs \
//     [--binary-path build-wasm/mathsolver.js] \
//     [--cases tests/acceptance/cases.tsv]...   (repeatable; default: cases.tsv + eval_cases.tsv)
//     [--verbose]
//
// Exit code: 0 when every adapted case passes, 1 otherwise.

import { readFileSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, resolve } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..");

// ---------------------------------------------------------------------------
// CLI arguments
// ---------------------------------------------------------------------------

let binaryPath = resolve(root, "build-wasm/mathsolver.js");
const casePaths = [];
let verbose = false;
{
  const argv = process.argv.slice(2);
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--binary-path") {
      binaryPath = resolve(process.cwd(), argv[++i]);
    } else if (a === "--cases") {
      casePaths.push(resolve(process.cwd(), argv[++i]));
    } else if (a === "--verbose" || a === "-v") {
      verbose = true;
    } else if (a === "--help" || a === "-h") {
      console.log(
        "usage: node tools/wasm_acceptance.mjs [--binary-path mathsolver.js] [--cases file.tsv]... [--verbose]",
      );
      process.exit(0);
    } else {
      console.error(`unknown argument '${a}' (see --help)`);
      process.exit(2);
    }
  }
}
if (casePaths.length === 0) {
  casePaths.push(
    resolve(root, "tests/acceptance/cases.tsv"),
    resolve(root, "tests/acceptance/eval_cases.tsv"),
  );
}

// ---------------------------------------------------------------------------
// TSV parsing
// ---------------------------------------------------------------------------

function parseTsv(path) {
  const rows = [];
  const lines = readFileSync(path, "utf8").split("\n");
  for (let n = 0; n < lines.length; n++) {
    const line = lines[n].replace(/\r$/, "");
    if (line.trim() === "" || line.startsWith("#")) continue;
    const cols = line.split("\t");
    if (cols.length !== 7) {
      throw new Error(`${path}:${n + 1}: expected 7 tab-separated columns, got ${cols.length}`);
    }
    const [id, subcommand, input, extraArgs, expectedKind, expected, expectsExit] = cols;
    rows.push({
      id,
      subcommand,
      input,
      extraArgs,
      expectedKind,
      expected,
      expectsExit: Number(expectsExit),
      source: `${path}:${n + 1}`,
    });
  }
  return rows;
}

// ---------------------------------------------------------------------------
// Helpers mirroring the CLI's conventions (apps/main.cpp)
// ---------------------------------------------------------------------------

class HarnessError extends Error {}

function extraTokens(extraArgs) {
  return extraArgs === "-" ? [] : extraArgs.split(/\s+/).filter((t) => t !== "");
}

function hasTopLevelSemicolon(s) {
  let depth = 0;
  for (const c of s) {
    if (c === "(" || c === "{" || c === "[") depth++;
    else if (c === ")" || c === "}" || c === "]") depth--;
    else if (c === ";" && depth <= 0) return true;
  }
  return false;
}

/** Single free symbol of the input, via the analyze binding (CLI: choose_variable). */
function inferVariable(ms, input, what) {
  const a = JSON.parse(ms.analyze(input));
  const symbols = a.symbols ?? [];
  if (a.ok && symbols.length === 1) return symbols[0];
  throw new HarnessError(
    `cannot infer the variable for ${what}: symbols = [${symbols.join(", ")}]`,
  );
}

/** Does a plain rendering look like a bare Number (proxy for kind()==Number)? */
function looksLikeBareNumber(plain) {
  return /^[-+]?[0-9]+(\.[0-9]+)?(\/[0-9]+)?([eE][-+]?[0-9]+)?$/.test(plain);
}

function methodAndWarnings(lines, r) {
  if (r.method) lines.push(`method: ${r.method}`);
  for (const w of r.warnings ?? []) lines.push(`warning: ${w}`);
}

// Text reconstruction, shaped exactly like apps/main.cpp printing ------------

function solveText(r, v) {
  const lines = [];
  if (r.status === "solved" || r.status === "numeric") {
    for (const s of r.solutions) {
      let line;
      if (!s.exact && s.approx !== null && looksLikeBareNumber(s.plain)) {
        line = `${v} ≈ ${s.approx}`; // CLI: `x ≈ <double>` for inexact Numbers
      } else {
        line = `${v} = ${s.plain}`;
      }
      if (s.note) line += `    (${s.note})`;
      lines.push(line);
    }
  } else if (r.status === "noRealSolution") {
    lines.push("no real solutions");
  } else if (r.status === "allReals") {
    lines.push(`true for all ${v}`);
  } else {
    lines.push(`unable to solve for ${v}`);
  }
  methodAndWarnings(lines, r);
  return lines.join("\n");
}

function systemText(r) {
  const lines = [];
  if (r.status === "solved" || r.status === "underdetermined") {
    for (const v of r.values) lines.push(`${v.symbol} = ${v.plain}`);
    if (r.free.length > 0) lines.push(`free: ${r.free.join(", ")}`);
  } else if (r.status === "noSolution") {
    lines.push("no solution (inconsistent system)");
  } else {
    lines.push("unable to solve the system");
  }
  methodAndWarnings(lines, r);
  return lines.join("\n");
}

function indefiniteIntegralText(r) {
  const lines = [];
  if (r.solved) lines.push(`${r.plain} + C`);
  else lines.push("unable to integrate");
  methodAndWarnings(lines, r);
  return lines.join("\n");
}

function definiteIntegralText(r) {
  const lines = [];
  if (r.status === "exact") lines.push(`value = ${r.plain}`);
  else if (r.status === "numeric") lines.push(`value ≈ ${r.approx}`);
  else lines.push("unable to integrate");
  methodAndWarnings(lines, r);
  return lines.join("\n");
}

// ---------------------------------------------------------------------------
// Case adaptation: CLI row -> binding call(s) -> { ok, stream, raw }
//   ok     — success flag standing in for "exit code 0"
//   stream — reconstructed stdout (ok) / stderr (error) text
//   raw    — the JSON envelope(s), for failure triage
// ---------------------------------------------------------------------------

function runCase(ms, c) {
  const toks = extraTokens(c.extraArgs);
  const errStream = (r) => ({ ok: false, stream: `error: ${r.error}`, raw: r });

  switch (c.subcommand) {
    case "simplify":
    case "expand":
    case "factor": {
      const r = JSON.parse(ms[c.subcommand](c.input));
      return r.ok ? { ok: true, stream: r.plain, raw: r } : errStream(r);
    }

    case "latex": {
      const r = JSON.parse(ms.latex(c.input));
      return r.ok ? { ok: true, stream: r.latex, raw: r } : errStream(r);
    }

    case "diff": {
      const v = toks[0] ?? inferVariable(ms, c.input, "diff");
      const r = JSON.parse(ms.derivative(c.input, v));
      return r.ok ? { ok: true, stream: r.plain, raw: r } : errStream(r);
    }

    case "eval": {
      const bindings = toks.join(","); // CLI "x=3 y=0.5" -> binding "x=3,y=0.5"
      const r = JSON.parse(ms.evaluate(c.input, bindings));
      return r.ok ? { ok: true, stream: String(r.value), raw: r } : errStream(r);
    }

    case "subs": {
      // CLI "x=y+1 z=2" -> assignments "x=y+1,z=2" (values are expressions).
      const r = JSON.parse(ms.subs(c.input, toks.join(",")));
      return r.ok ? { ok: true, stream: r.plain, raw: r } : errStream(r);
    }

    case "collect": {
      const v = toks[0] ?? "";
      const r = JSON.parse(ms.collect(c.input, v));
      return r.ok ? { ok: true, stream: r.plain, raw: r } : errStream(r);
    }

    case "solve": {
      if (hasTopLevelSemicolon(c.input)) {
        const r = JSON.parse(ms.solveSystem(c.input, toks.join(",")));
        return r.ok ? { ok: true, stream: systemText(r), raw: r } : errStream(r);
      }
      let lo = 0;
      let hi = 0;
      let useRange = false;
      const pos = [];
      for (let i = 0; i < toks.length; i++) {
        if (toks[i] === "--range") {
          lo = Number(toks[i + 1]);
          hi = Number(toks[i + 2]);
          useRange = true;
          i += 2;
        } else {
          pos.push(toks[i]);
        }
      }
      const v = pos[0] ?? inferVariable(ms, c.input, "solve");
      const r = JSON.parse(ms.solve(c.input, v, lo, hi, useRange));
      return r.ok ? { ok: true, stream: solveText(r, v), raw: r } : errStream(r);
    }

    case "integrate": {
      let from = null;
      let to = null;
      const pos = [];
      for (let i = 0; i < toks.length; i++) {
        if (toks[i] === "--from") from = toks[++i];
        else if (toks[i] === "--to") to = toks[++i];
        else pos.push(toks[i]);
      }
      const v = pos[0] ?? inferVariable(ms, c.input, "integrate");
      if (from === null && to === null) {
        const r = JSON.parse(ms.integrate(c.input, v));
        return r.ok ? { ok: true, stream: indefiniteIntegralText(r), raw: r } : errStream(r);
      }
      // CLI contract (run_integrate): bounds that fail constant evaluation are
      // answered with "unable to integrate" + a warning, exit 0. Mirror that
      // wrapper here: the wording lives in main.cpp, not the engine.
      for (const bound of [from, to]) {
        const b = JSON.parse(ms.evaluate(bound, ""));
        if (!b.ok || !Number.isFinite(b.value)) {
          return {
            ok: true,
            stream:
              "unable to integrate\nwarning: integration bounds must evaluate to finite numbers",
            raw: b,
          };
        }
      }
      const r = JSON.parse(ms.integrateDefinite(c.input, v, from, to));
      return r.ok ? { ok: true, stream: definiteIntegralText(r), raw: r } : errStream(r);
    }

    default:
      throw new HarnessError(`unknown subcommand '${c.subcommand}'`);
  }
}

// ---------------------------------------------------------------------------
// Expectation matching (tests/acceptance/README.md semantics)
// ---------------------------------------------------------------------------

function matches(kind, expected, stream) {
  const text = stream.replace(/\s+$/, "");
  switch (kind) {
    case "exact":
      return text === expected;
    case "contains":
      return text.includes(expected);
    case "regex":
      // Python re.MULTILINE | re.DOTALL === JS flags "m" + "s".
      return new RegExp(expected, "ms").test(text);
    case "approx": {
      const tokens = text.match(/[-+]?\d+\.?\d*([eE][-+]?\d+)?/g) ?? [];
      const numbers = tokens.map(Number);
      return expected.split(",").every((e) => {
        const want = Number(e.trim());
        const tol = 1e-6 * Math.max(1, Math.abs(want));
        return numbers.some((n) => Math.abs(n - want) <= tol);
      });
    }
    default:
      throw new HarnessError(`unknown expected_kind '${kind}'`);
  }
}

// ---------------------------------------------------------------------------
// Skip policy: rows that only exercise the CLI surface
// ---------------------------------------------------------------------------

function skipReason(c) {
  if (c.expectsExit === 2) {
    return "usage error (exit 2): CLI argument-surface validation (flag arity / binding syntax / variable-count usage), no WASM equivalent";
  }
  if (extraTokens(c.extraArgs).includes("--latex")) {
    return "--latex flag variant: CLI output-style flag (the envelope's latex field is covered by the lx-* rows)";
  }
  return null;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

const createModule = (await import(pathToFileURL(binaryPath).href)).default;
const ms = await createModule();

const cases = casePaths.flatMap(parseTsv);
const skipped = [];
const failures = [];
let passed = 0;

for (const c of cases) {
  const reason = skipReason(c);
  if (reason) {
    skipped.push({ id: c.id, reason });
    continue;
  }
  let outcome;
  try {
    outcome = runCase(ms, c);
  } catch (e) {
    failures.push({ c, detail: `harness exception: ${e.message}`, raw: null });
    continue;
  }
  const wantOk = c.expectsExit === 0;
  if (outcome.ok !== wantOk) {
    failures.push({
      c,
      detail: `success/failure mismatch: expected ${wantOk ? "success (exit 0)" : `error (exit ${c.expectsExit})`}, envelope gave ${outcome.ok ? "ok:true" : "ok:false"}`,
      stream: outcome.stream,
      raw: outcome.raw,
    });
    continue;
  }
  if (c.expected === "-") {
    // exit-code-only case
    passed++;
    if (verbose) console.log(`ok   ${c.id} (exit-status only)`);
    continue;
  }
  if (matches(c.expectedKind, c.expected, outcome.stream)) {
    passed++;
    if (verbose) console.log(`ok   ${c.id}`);
  } else {
    failures.push({
      c,
      detail: `${c.expectedKind} mismatch`,
      stream: outcome.stream,
      raw: outcome.raw,
    });
  }
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

console.log(`\ncases:   ${cases.length} (from ${casePaths.length} file(s))`);
console.log(`adapted: ${cases.length - skipped.length}`);
console.log(`passed:  ${passed}`);
console.log(`failed:  ${failures.length}`);
console.log(`skipped: ${skipped.length}`);

if (skipped.length > 0) {
  const byReason = new Map();
  for (const s of skipped) {
    if (!byReason.has(s.reason)) byReason.set(s.reason, []);
    byReason.get(s.reason).push(s.id);
  }
  console.log("\nskips by reason:");
  for (const [reason, ids] of byReason) {
    console.log(`  [${ids.length}] ${ids.join(", ")}\n      ${reason}`);
  }
}

for (const f of failures) {
  console.error(`\nFAIL ${f.c.id} (${f.c.source})`);
  console.error(`  command:  ${f.c.subcommand} '${f.c.input}' ${f.c.extraArgs}`);
  console.error(`  expected: [${f.c.expectedKind}] ${f.c.expected} (exit ${f.c.expectsExit})`);
  console.error(`  problem:  ${f.detail}`);
  if (f.stream !== undefined) {
    console.error(`  stream:   ${JSON.stringify(f.stream)}`);
  }
  if (f.raw) console.error(`  envelope: ${JSON.stringify(f.raw)}`);
}

console.log(failures.length === 0 ? "\nALL ADAPTED CASES PASS" : `\n${failures.length} FAILURES`);
process.exit(failures.length === 0 ? 0 : 1);
