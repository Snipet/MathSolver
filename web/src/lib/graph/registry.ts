// The user-function registry: the source of truth for which `f(x) = …`
// definitions exist and their metadata, consumed by the row/line expander and
// free-variable detection. Two instances exist: the grapher's (ephemeral,
// reconciled from graph rows each recompute) and the console's session store
// (persistent, committed a line at a time). Both share this class.
//
// Metadata is recomputed as a whole-registry fixpoint on every change, so
// forward references (`f` defined above the `g` it calls) resolve correctly and
// the cycle check sees the complete dependency graph.
import { call, engineReady } from "../engine";
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

const IDENT = /^[A-Za-z][A-Za-z0-9_]*$/;

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

export class FunctionRegistry {
  #defs = new Map<string, FnDef>(); // raw definitions (the source of truth)
  #map = new Map<string, FnBinding>(); // computed bindings (valid only)
  #lastValid = new Map<string, FnBinding>();
  #persistKey?: string;

  constructor(persistKey?: string) {
    this.#persistKey = persistKey;
    if (persistKey) {
      this.#load();
      // Recompute metadata once the engine is up (mirrors the vars store).
      engineReady()
        .then(() => this.#recompute())
        .catch(() => {});
    }
  }

  fnNames(): string[] {
    return [...this.#map.keys()];
  }
  getFn(name: string): FnBinding | undefined {
    return this.#map.get(name);
  }
  isFunctionName(sym: string): boolean {
    return this.#map.has(sym);
  }
  list(): FnBinding[] {
    return [...this.#map.values()];
  }
  freeParamsOf(names: readonly string[]): string[] {
    const out = new Set<string>();
    for (const n of names) {
      const b = this.#map.get(n);
      if (b) for (const s of b.freeParams) out.add(s);
    }
    return [...out];
  }

  /** Replace every definition (the grapher's per-recompute reconcile). */
  async reconcile(defs: FnDef[]): Promise<Map<string, string>> {
    this.#defs = new Map(defs.map((d) => [d.name, d]));
    return this.#recompute();
  }

  /** Upsert one definition (the console). Returns an error string or null. */
  async commit(def: FnDef): Promise<string | null> {
    this.#defs.set(def.name, def);
    const rejects = await this.#recompute();
    return rejects.get(def.name) ?? null;
  }

  /** Remove one definition by name; returns true if it existed. */
  async remove(name: string): Promise<boolean> {
    if (!this.#defs.delete(name)) return false;
    this.#lastValid.delete(name);
    await this.#recompute();
    return true;
  }

  async clear(): Promise<void> {
    this.#defs.clear();
    this.#lastValid.clear();
    await this.#recompute();
  }

  async #recompute(): Promise<Map<string, string>> {
    const rejects = new Map<string, string>();
    const cand = new Map<string, FnDef>();
    for (const d of this.#defs.values()) {
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
      cand.set(d.name, d);
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
        // Transient/invalid body: keep the last valid definition if we have one.
        const prev = this.#lastValid.get(n);
        if (prev) next.set(n, prev);
        continue;
      }
      const exclude = new Set<string>([...d.params, ...(deps.get(n) ?? []), ...acceptedSet, n]);
      const binding: FnBinding = {
        name: n,
        params: d.params,
        body: d.body,
        bodySymbols: a.symbols,
        freeParams: a.symbols.filter((s) => !exclude.has(s)),
        deps: deps.get(n) ?? [],
      };
      next.set(n, binding);
      this.#lastValid.set(n, binding);
    }
    for (const n of [...this.#lastValid.keys()]) if (!names.has(n)) this.#lastValid.delete(n);
    this.#map = next;
    this.#persist();
    return rejects;
  }

  #load(): void {
    try {
      const raw = localStorage.getItem(this.#persistKey!);
      if (!raw) return;
      const store: unknown = JSON.parse(raw);
      const arr = (store as { funcs?: unknown } | null)?.funcs;
      if (!Array.isArray(arr)) return;
      for (const r of arr) {
        if (
          r &&
          typeof r.name === "string" &&
          Array.isArray(r.params) &&
          r.params.every((p: unknown) => typeof p === "string") &&
          typeof r.body === "string"
        ) {
          this.#defs.set(r.name, { name: r.name, params: r.params, body: r.body });
        }
      }
    } catch {
      /* ignore */
    }
  }

  #persist(): void {
    if (!this.#persistKey) return;
    try {
      const funcs = [...this.#defs.values()];
      localStorage.setItem(this.#persistKey, JSON.stringify({ v: 1, funcs }));
    } catch {
      /* storage unavailable */
    }
  }
}

// --- the grapher's ephemeral registry (module API kept for GraphCalculator) --
const graphReg = new FunctionRegistry();

export function reconcileFunctions(defs: FnDef[]): Promise<Map<string, string>> {
  return graphReg.reconcile(defs);
}
export function fnNames(): string[] {
  return graphReg.fnNames();
}
export function getFn(name: string): FnBinding | undefined {
  return graphReg.getFn(name);
}
export function freeParamsOf(names: readonly string[]): string[] {
  return graphReg.freeParamsOf(names);
}
export function isFunctionName(sym: string): boolean {
  return graphReg.isFunctionName(sym);
}

// --- the console's persistent session registry ------------------------------
export const sessionFunctions = new FunctionRegistry("mathsolver.functions");
