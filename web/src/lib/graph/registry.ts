// The user-function registry: the source of truth for which `f(x) = …`
// definitions exist and their metadata, consumed by the grapher's row expander
// (resolveRow) and free-variable detection. Grapher-scoped for now; a later
// phase lifts it so the console shares the same store.
//
// Metadata is recomputed as a whole-registry fixpoint on every reconcile, so
// forward references (`f` defined above the `g` it calls) resolve correctly and
// the cycle check sees the complete dependency graph.
import { call } from "../engine";
import { RESERVED_NAMES, stripCalls } from "./functions";

export interface FnBinding {
  name: string;
  params: string[];
  /** Plain-printed body. */
  body: string;
  /** Free symbols of the body with calls stripped (arg + genuine body vars). */
  bodySymbols: string[];
  /** Body symbols that should become sliders: bodySymbols minus params, deps,
   *  every registry name, and the function's own name. */
  freeParams: string[];
  /** Registered function names this body calls. */
  deps: string[];
}

export interface FnDef {
  name: string;
  params: string[];
  body: string;
}

let registry = new Map<string, FnBinding>();
// Last successfully-analyzed binding per name, so a transient parse error while
// editing a body keeps the function usable (no call-site flicker to `f*x`).
const lastValid = new Map<string, FnBinding>();

/** Names of the currently-registered functions. */
export function fnNames(): string[] {
  return [...registry.keys()];
}

export function getFn(name: string): FnBinding | undefined {
  return registry.get(name);
}

/** Union of the slider-worthy free symbols across the named functions. */
export function freeParamsOf(names: readonly string[]): string[] {
  const out = new Set<string>();
  for (const n of names) {
    const b = registry.get(n);
    if (b) for (const s of b.freeParams) out.add(s);
  }
  return [...out];
}

/** Any function whose name a bare identifier `sym` would refer to. */
export function isFunctionName(sym: string): boolean {
  return registry.has(sym);
}

function findCycles(deps: Map<string, string[]>): Set<string> {
  const bad = new Set<string>();
  for (const start of deps.keys()) {
    const seen = new Set<string>();
    const stack = [...(deps.get(start) ?? [])];
    while (stack.length) {
      const cur = stack.pop()!;
      if (cur === start) {
        bad.add(start);
        break;
      }
      if (seen.has(cur)) continue;
      seen.add(cur);
      for (const d of deps.get(cur) ?? []) stack.push(d);
    }
  }
  return bad;
}

const IDENT = /^[A-Za-z][A-Za-z0-9_]*$/;

/**
 * Replace the whole registry with `defs`. Returns a map of rejected names to a
 * user-facing error (reserved name, bad parameters, or recursion); rejected
 * names are not registered (unless a last-valid binding lets a transient body
 * error keep the previous definition alive).
 */
export async function reconcileFunctions(defs: FnDef[]): Promise<Map<string, string>> {
  const rejects = new Map<string, string>();
  const cand = new Map<string, FnDef>();
  for (const d of defs) {
    if (RESERVED_NAMES.has(d.name)) {
      rejects.set(d.name, `'${d.name}' is a built-in name — pick another`);
      continue;
    }
    if (!d.params.length || d.params.some((p) => !IDENT.test(p))) {
      rejects.set(d.name, `invalid parameter list in ${d.name}(…)`);
      continue;
    }
    if (new Set(d.params).size !== d.params.length) {
      rejects.set(d.name, `duplicate parameter in ${d.name}(…)`);
      continue;
    }
    cand.set(d.name, d); // last definition of a name wins
  }

  const names = new Set(cand.keys());
  const deps = new Map<string, string[]>();
  for (const [n, d] of cand) {
    const called = stripCalls(d.body, [...names]).calledFns.filter((c) => names.has(c));
    deps.set(n, [...new Set(called)]);
  }
  for (const n of findCycles(deps)) {
    rejects.set(n, `'${n}' is defined recursively (not supported)`);
  }

  const accepted = [...cand.keys()].filter((n) => !rejects.has(n));
  const acceptedSet = new Set(accepted);
  const next = new Map<string, FnBinding>();
  for (const n of accepted) {
    const d = cand.get(n)!;
    const stripped = stripCalls(d.body, [...acceptedSet, n]).text;
    const a = await call("analyze", [stripped]);
    if (!a.ok || !("symbols" in a)) {
      // Transient/invalid body: keep the last valid definition if we have one,
      // else drop it (the def row surfaces its own parse error).
      const prev = lastValid.get(n);
      if (prev) next.set(n, prev);
      continue;
    }
    const exclude = new Set<string>([
      ...d.params,
      ...(deps.get(n) ?? []),
      ...acceptedSet,
      n,
    ]);
    const bodySymbols = a.symbols;
    const binding: FnBinding = {
      name: n,
      params: d.params,
      body: d.body,
      bodySymbols,
      freeParams: bodySymbols.filter((s) => !exclude.has(s)),
      deps: deps.get(n) ?? [],
    };
    next.set(n, binding);
    lastValid.set(n, binding);
  }

  // Forget last-valid entries for names no longer defined at all.
  for (const n of [...lastValid.keys()]) if (!names.has(n)) lastValid.delete(n);
  registry = next;
  return rejects;
}
