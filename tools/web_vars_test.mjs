// Resolution parity harness (variable-assignment spec §10): drives the pure
// TypeScript §5 resolver (web/src/lib/vars/resolve.ts) against the wasm build
// using the SAME vector table tests/test_cli.cpp runs through the C++ REPL —
// tests/resolution_vectors.tsv — so the two implementations of resolve()
// stay honest against each other. Also asserts on every vector:
//   - order independence: the env in reversed definition order produces the
//     identical output;
//   - "resolution IS subs": the closure's serialization is a valid
//     parents-first order with exactly the table's assignment set, and
//     applying it one binding at a time equals the single-call result;
//   - the cycle machinery: findCycle paths/messages, the topo cycle guard,
//     and the depth-bound regression (a 66-deep acyclic chain must resolve).
// Persistence-loader cases (§9.2 malformed stores) are exercised in the
// browser by tools/web_session_test.mjs, where the real localStorage exists.
//
//   node tools/web_vars_test.mjs [path/to/mathsolver.js]
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";
import { readFileSync } from "node:fs";

// Node >= 22.6 strips the types natively; resolve.ts is deliberately free of
// Svelte imports for exactly this use (spec §12/Stage 3).
import {
  closure,
  topoParentsFirst,
  serializeAssignments,
  findCycle,
  cycleMessage,
} from "../web/src/lib/vars/resolve.ts";

const here = dirname(fileURLToPath(import.meta.url));
const modPath = process.argv[2] ?? resolve(here, "../build-wasm/mathsolver.js");
const createModule = (await import(modPath)).default;
const ms = await createModule();

let failures = 0;
function check(name, cond, extra = "") {
  if (cond) {
    console.log(`ok   ${name}`);
  } else {
    failures++;
    console.error(`FAIL ${name}${extra ? `\n  ${extra}` : ""}`);
  }
}

function analyze(text) {
  const r = JSON.parse(ms.analyze(text));
  if (!r.ok) throw new Error(`analyze(${text}): ${r.error}`);
  return r;
}

/** Env entry "name := value" -> VarBinding, exactly as the app builds them
 * (canonical plain value + analyze metadata). */
function toBinding(def) {
  const i = def.indexOf(":=");
  const name = def.slice(0, i).trim();
  const a = analyze(def.slice(i + 2).trim());
  return {
    name,
    value: a.plain,
    symbols: a.symbols,
    kind: a.kind === "equation" ? "equation" : "expression",
  };
}

/** One §5 resolution: closure + a single `subs` engine call (simplified,
 * matching what the REPL probe prints). */
function resolveSimplified(input, bindings, excluded) {
  const env = closure(analyze(input).symbols, bindings, excluded);
  if (env.active.length === 0) {
    return { plain: JSON.parse(ms.simplify(input)).plain, env };
  }
  const r = JSON.parse(ms.subs(input, serializeAssignments(env.active), true));
  if (!r.ok) throw new Error(`subs: ${r.error}`);
  return { plain: r.plain, env };
}

/** A parents-first order is valid iff no binding references one placed
 * before it (§5.2 step 3). */
function parentsFirstValid(active) {
  for (let a = 0; a < active.length; a++) {
    for (let b = 0; b < a; b++) {
      if (active[a].symbols.includes(active[b].name)) return false;
    }
  }
  return true;
}

// --- the shared vector table ------------------------------------------------
const table = readFileSync(resolve(here, "../tests/resolution_vectors.tsv"), "utf8")
  .split("\n")
  .filter((l) => l.length > 0 && !l.startsWith("#"))
  .map((l) => l.split("\t"));
check("vector table loaded", table.length > 0, "empty table");

for (const [env, input, excl, csv, expected] of table) {
  const label = `[${env} | ${input} | excl=${excl}]`;
  const bindings = env.split(";").map((d) => toBinding(d));
  const excluded = excl === "-" ? [] : excl.split(",");

  const fwd = resolveSimplified(input, bindings, excluded);
  check(`${label} resolves to '${expected}'`, fwd.plain === expected, `got '${fwd.plain}'`);

  // Order independence (§5.2): reversed definition order, identical output.
  const rev = resolveSimplified(input, [...bindings].reverse(), excluded);
  check(`${label} order-independent`, rev.plain === expected, `reversed got '${rev.plain}'`);

  // The serialization is the table's assignment set in a valid parents-first
  // order (the exact order may differ among independent bindings).
  const entries = serializeAssignments(fwd.env.active);
  const want = csv === "-" ? [] : csv.split(",").sort();
  const got = entries === "" ? [] : entries.split(",").sort();
  check(
    `${label} active set matches csv`,
    JSON.stringify(got) === JSON.stringify(want),
    `got '${entries}', table '${csv}'`,
  );
  check(`${label} parents-first order`, parentsFirstValid(fwd.env.active), entries);

  // Resolution IS subs (§10 property): one binding at a time, in the closure
  // order, un-simplified until the end — structurally the single-call result.
  if (fwd.env.active.length > 0) {
    let text = input;
    for (const b of fwd.env.active) {
      const r = JSON.parse(ms.subs(text, `${b.name}=${b.value}`, false));
      if (!r.ok) throw new Error(`stepwise subs: ${r.error}`);
      text = r.plain;
    }
    const stepwise = JSON.parse(ms.simplify(text)).plain;
    check(`${label} stepwise == single-call`, stepwise === expected, `got '${stepwise}'`);
  }
}

// --- subscripted names (TS/wasm surface; the CLI subs verb spells these
// differently, so they live here rather than in the shared table) ------------
{
  const bindings = [toBinding("v_{max} := a^3"), toBinding("a := 2")];
  bindings[0].name = "v_max"; // canonical symbol name, as the store keeps it
  const r = resolveSimplified("v_{max} + 1", bindings, []);
  check("subscripted binding resolves", r.plain === "9", `got '${r.plain}'`);
}

// --- depth-bound regression: a 66-deep acyclic chain is NOT a cycle ---------
{
  const chain = [];
  for (let i = 1; i < 66; i++) {
    chain.push({ name: `c_${i}`, value: `c_${i + 1} + 1`, symbols: [`c_${i + 1}`], kind: "expression" });
  }
  chain.push({ name: "c_66", value: "1", symbols: [], kind: "expression" });
  let out = "";
  try {
    const env = closure(["c_1"], chain, []);
    const r = JSON.parse(ms.subs("c_1", serializeAssignments(env.active), true));
    out = r.ok ? r.plain : r.error;
  } catch (e) {
    out = `threw: ${e.message}`;
  }
  check("66-deep chain resolves (no false cycle)", out === "66", `got '${out}'`);
}

// --- cycle machinery ---------------------------------------------------------
{
  const a = { name: "a", value: "b + 1", symbols: ["b"], kind: "expression" };
  const self = findCycle("a", ["a"], []);
  check("self-cycle path", JSON.stringify(self) === '["a","a"]', JSON.stringify(self));
  check(
    "self-cycle message",
    cycleMessage(["a", "a"]) === "'a' cannot be defined in terms of itself",
    cycleMessage(["a", "a"]),
  );
  const indirect = findCycle("b", ["a"], [a]);
  check("indirect cycle path", JSON.stringify(indirect) === '["b","a","b"]', JSON.stringify(indirect));
  check(
    "indirect cycle message",
    cycleMessage(["b", "a", "b"]) === "assignment would create a cycle: b -> a -> b",
    cycleMessage(["b", "a", "b"]),
  );
  check("acyclic env has no cycle", findCycle("c", ["a"], [a]) === null);
  // The topo guard still fires on a truly cyclic active set (belt-and-braces;
  // definition-time checks normally make this unreachable).
  let threw = "";
  try {
    topoParentsFirst([
      { name: "p", value: "q", symbols: ["q"], kind: "expression" },
      { name: "q", value: "p", symbols: ["p"], kind: "expression" },
    ]);
  } catch (e) {
    threw = e.message;
  }
  check("topo guard detects true cycles", threw === "internal error: assignment cycle detected", threw);
}

console.log(failures === 0 ? "\nALL PASS" : `\n${failures} FAILURES`);
process.exit(failures === 0 ? 0 : 1);
