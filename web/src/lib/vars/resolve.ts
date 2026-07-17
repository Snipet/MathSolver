// Pure TypeScript implementation of the §5 resolution algorithm from
// docs/proposals/variable-assignment.md: closure over the environment,
// parents-first topological ordering, and definition-time cycle detection.
//
// Deliberately free of Svelte imports so a node harness can drive it against
// the wasm build (tools/web_vars_test.mjs pattern). All inputs are parse
// metadata the client already has from `analyze` — no string re-parsing here.

/** An active (valid, acyclic) binding, ready to participate in resolution. */
export interface VarBinding {
  /** Canonical symbol name as reported by free_symbols (e.g. "x_max"). */
  name: string;
  /** Plain-printed value (§5 round-trip form) — what `subs` receives. */
  value: string;
  /** Free symbols of the parsed value (both sides for an equation). */
  symbols: string[];
  kind: "expression" | "equation";
}

export interface ClosureResult {
  /** Expression bindings to apply, ordered parents-first (§5.2 step 3). */
  active: VarBinding[];
  /** Symbols left free after application (excluded, undefined, or both). */
  residual: string[];
  /** Equation-valued names among the seed symbols (input-level references). */
  directEquationRefs: string[];
  /** Equation-valued names referenced from inside a binding's value (§4 error). */
  nestedEquationRefs: string[];
}

const CYCLE_INTERNAL = "internal error: assignment cycle detected";

function lastByName(bindings: VarBinding[]): Map<string, VarBinding> {
  const m = new Map<string, VarBinding>();
  for (const b of bindings) m.set(b.name, b);
  return m;
}

/**
 * resolve(input, env, excluded), steps 1–3 (§5.2): collect the closure of the
 * seed symbols over the environment, drop excluded names, and order the
 * active set parents-first. Step 4 (the sequential `substitute` pass) is one
 * `subs` engine call with `serializeAssignments(active)`.
 */
export function closure(
  seeds: string[],
  bindings: VarBinding[],
  excluded: string[],
): ClosureResult {
  const byName = lastByName(bindings);
  const skip = new Set(excluded);
  const activeNames = new Set<string>();
  const residual = new Set<string>();
  const directEq = new Set<string>();
  const nestedEq = new Set<string>();
  const seen = new Set<string>();

  // Belt-and-braces depth bound (§5.2): a simple path cannot visit more
  // bindings than exist, so exceeding the environment's size implies a
  // revisit (a true cycle). A fixed constant here once misdiagnosed a legal
  // 66-deep lazy chain as a cycle; the dynamic bound never fires on acyclic
  // input, however deep.
  const maxDepth = byName.size;
  const visit = (sym: string, depth: number, fromValue: boolean) => {
    if (depth > maxDepth) throw new Error(CYCLE_INTERNAL);
    if (skip.has(sym)) {
      residual.add(sym);
      return;
    }
    const b = byName.get(sym);
    if (!b) {
      residual.add(sym);
      return;
    }
    if (b.kind === "equation") {
      // Equation-name placement rule (§4): never applied inside expressions.
      (fromValue ? nestedEq : directEq).add(sym);
      return;
    }
    if (seen.has(sym)) return;
    seen.add(sym);
    activeNames.add(sym);
    for (const s of b.symbols) visit(s, depth + 1, true);
  };
  for (const s of seeds) visit(s, 0, false);

  const active = topoParentsFirst(
    bindings.filter((b) => activeNames.has(b.name) && byName.get(b.name) === b),
  );
  return {
    active,
    residual: [...residual],
    directEquationRefs: [...directEq],
    nestedEquationRefs: [...nestedEq],
  };
}

/**
 * Order bindings parents-first: A precedes B whenever A's value references
 * B's name (§5.2 step 3). The set is acyclic by the environment invariant;
 * the temp-mark check is defensive only.
 */
export function topoParentsFirst(active: VarBinding[]): VarBinding[] {
  const byName = lastByName(active);
  const state = new Map<string, 1 | 2>(); // 1 = visiting, 2 = done
  const out: VarBinding[] = [];
  // The visiting mark (state === 1) detects true cycles; the depth bound is
  // defensive only and sized to the active set so it cannot fire on a legal
  // acyclic chain of any depth.
  const maxDepth = byName.size;
  const visit = (b: VarBinding, depth: number) => {
    if (depth > maxDepth || state.get(b.name) === 1)
      throw new Error(CYCLE_INTERNAL);
    if (state.get(b.name) === 2) return;
    state.set(b.name, 1);
    for (const s of b.symbols) {
      const dep = byName.get(s);
      if (dep && dep.name !== b.name) visit(dep, depth + 1);
    }
    state.set(b.name, 2);
    out.push(b); // dependencies land first…
  };
  for (const b of byName.values()) visit(b, 0);
  return out.reverse(); // …so reversing yields parents-first.
}

/**
 * The `subs` CSV for one sequential engine pass, e.g. "f=g + 1,g=x^2".
 * Values cannot contain commas (§8: a comma inside an expression is a
 * ParseError), so the engine's CSV split is safe.
 */
export function serializeAssignments(active: VarBinding[]): string {
  return active.map((b) => `${b.name}=${b.value}`).join(",");
}

/**
 * Definition-time cycle check (§5.2): would binding `name := (value with
 * free symbols valueSymbols)` make `name` reachable from its own value over
 * the environment-as-it-would-become? Returns the offending path
 * (e.g. ["b", "a", "b"]) or null. Any existing binding of `name` is replaced,
 * not chained (redefinition semantics).
 */
export function findCycle(
  name: string,
  valueSymbols: string[],
  others: VarBinding[],
): string[] | null {
  if (valueSymbols.includes(name)) return [name, name];
  const byName = lastByName(others.filter((b) => b.name !== name));
  // BFS from each value symbol toward `name`, tracking parents for the path.
  const parent = new Map<string, string | null>();
  const queue: string[] = [];
  for (const s of valueSymbols) {
    if (!parent.has(s)) {
      parent.set(s, null);
      queue.push(s);
    }
  }
  while (queue.length > 0) {
    const cur = queue.shift()!;
    if (cur === name) {
      const path = [name];
      let p = parent.get(cur) ?? null;
      while (p !== null) {
        path.push(p);
        p = parent.get(p) ?? null;
      }
      path.push(name);
      return path.reverse();
    }
    const b = byName.get(cur);
    if (!b) continue;
    for (const s of b.symbols) {
      if (!parent.has(s)) {
        parent.set(s, cur);
        queue.push(s);
      }
    }
  }
  return null;
}

/** The §5.2 error strings, verbatim from the spec. */
export function cycleMessage(path: string[]): string {
  if (path.length === 2) return `'${path[0]}' cannot be defined in terms of itself`;
  return `assignment would create a cycle: ${path.join(" -> ")}`;
}

/** §4 placement-rule error for an equation name inside an expression. */
export function equationRefMessage(name: string): string {
  return `'${name}' names an equation and cannot be used inside an expression`;
}

/**
 * Typed form that re-lexes to the canonical symbol name: multi-character
 * subscripts need braces (`x_max` the symbol is typed `x_{max}`; bare
 * `x_max` reads as a*x*x_m under the §4 grammar).
 */
export function symbolToTyped(name: string): string {
  const i = name.indexOf("_");
  if (i < 0) return name;
  const base = name.slice(0, i);
  const sub = name.slice(i + 1);
  return sub.length > 1 && !sub.startsWith("{") ? `${base}_{${sub}}` : name;
}
