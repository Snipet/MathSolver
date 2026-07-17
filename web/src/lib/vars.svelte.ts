// Session variable environment (docs/proposals/variable-assignment.md §3, §9):
// an insertion-ordered list of `name := value` bindings, persisted to
// localStorage beside history, validated through the engine's `analyze`, and
// applied client-side via the `subs` binding before every computing operation.
import { call, engineReady } from "./engine";
import {
  findCycle,
  cycleMessage,
  symbolToTyped,
  type VarBinding,
} from "./vars/resolve";
import {
  nameVerdict,
  valueVerdict,
  normalizeTypedName,
  EMPTY_VALUE_ERROR,
} from "./vars/validate";

export interface VarStatus {
  /** "pending" until both fields have been analyzed. */
  phase: "pending" | "checked";
  /** Canonical symbol name (free_symbols form, e.g. "x_max") when valid. */
  symbol?: string;
  nameLatex?: string;
  nameError?: string;
  kind?: "expression" | "equation";
  /** Canonical §5 plain print of the value — what persists and resolves. */
  valuePlain?: string;
  valueLatex?: string;
  /** Free symbols of the value (both sides for an equation). */
  symbols?: string[];
  valueError?: string;
  valueSpan?: { begin?: number; end?: number };
  /** Cycle/shadowing verdict from the whole-environment pass (§5.2). */
  envError?: string;
}

export interface VarRow {
  id: number;
  /** Name as typed/displayed (grammar form, e.g. "x_{max}"). */
  name: string;
  /** Value source as typed/displayed. */
  value: string;
  ts: number;
  status: VarStatus;
}

const KEY = "mathsolver.vars";
/** Row cap (§9.2). Enforced at write time (add/commit) as well as at load so
 * the in-memory environment can never outgrow what persistence keeps — rows
 * past the cap used to survive in memory and silently vanish on reload. */
export const VAR_CAP = 32;
const CAP = VAR_CAP;
export const CAP_ERROR = `variable limit reached (${CAP}) — delete a binding to add another`;
const NAME_MAX = 64;
const VALUE_MAX = 1024;

let nextId = Date.now();

// --- §9.2 loader: never throw, never block, never delete user data ---------
// Whole-store failures yield an empty environment; structurally malformed
// rows are dropped; grammar-INVALID rows are kept (flagged inactive after
// validation) — a variable's value is user data, unlike disposable history.
function load(): VarRow[] {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return [];
    const store: unknown = JSON.parse(raw);
    if (!store || typeof store !== "object" || Array.isArray(store)) return [];
    // `v` missing or newer than known: still best-effort parse the rows.
    const arr = (store as Record<string, unknown>).vars;
    if (!Array.isArray(arr)) return [];
    const shaped = arr.filter((r): r is { name: string; value: string; ts?: unknown } => {
      if (!r || typeof r !== "object") return false;
      const o = r as Record<string, unknown>;
      return (
        typeof o.name === "string" &&
        typeof o.value === "string" &&
        o.name.length > 0 &&
        o.name.length <= NAME_MAX &&
        o.value.length > 0 &&
        o.value.length <= VALUE_MAX
      );
    });
    // Duplicate names keep the last occurrence.
    const lastIdx = new Map<string, number>();
    shaped.forEach((r, i) => lastIdx.set(r.name, i));
    return shaped
      .filter((r, i) => lastIdx.get(r.name) === i)
      .slice(0, CAP)
      .map((r) => ({
        id: nextId++,
        name: symbolToTyped(r.name),
        value: r.value,
        ts: typeof r.ts === "number" ? r.ts : Date.now(),
        status: { phase: "pending" } as VarStatus,
      }));
  } catch {
    return [];
  }
}

function isRowValid(r: VarRow): boolean {
  return (
    r.status.phase === "checked" &&
    !r.status.nameError &&
    !r.status.valueError &&
    r.status.symbol !== undefined &&
    r.status.valuePlain !== undefined
  );
}

class VarsStore {
  rows = $state<VarRow[]>(load());
  /** Chip-click focus request; the panel consumes and clears it. */
  focusId = $state<number | null>(null);

  /** Bindings participating in resolution: valid, unshadowed, cycle-free. */
  readonly active: VarBinding[] = $derived(
    this.rows
      .filter((r) => isRowValid(r) && !r.status.envError)
      .map((r) => ({
        name: r.status.symbol!,
        value: r.status.valuePlain!,
        symbols: r.status.symbols ?? [],
        kind: r.status.kind ?? "expression",
      })),
  );

  #timers = new Map<number, ReturnType<typeof setTimeout>>();
  #seq = new Map<number, number>();

  constructor() {
    // Validate restored rows once the engine is up. Deliberately does NOT
    // persist: the loader must never destructively rewrite storage (§9.2).
    engineReady()
      .then(() => {
        for (const r of this.rows) void this.#validate(r.id, false);
      })
      .catch(() => {
        /* engine failed to load; rows stay pending/inactive */
      });
  }

  /** True when no further row may be added (§9.2 cap). */
  get atCap(): boolean {
    return this.rows.length >= CAP;
  }

  add(): number | null {
    if (this.atCap) return null;
    const row: VarRow = {
      id: nextId++,
      name: "",
      value: "",
      ts: Date.now(),
      status: { phase: "pending" },
    };
    this.rows.push(row);
    return row.id;
  }

  edit(id: number, patch: { name?: string; value?: string }) {
    const row = this.rows.find((r) => r.id === id);
    if (!row) return;
    if (patch.name !== undefined) row.name = patch.name;
    if (patch.value !== undefined) row.value = patch.value;
    row.ts = Date.now();
    row.status = { phase: "pending" };
    this.#persist();
    clearTimeout(this.#timers.get(id));
    this.#timers.set(
      id,
      setTimeout(() => void this.#validate(id, true), 300),
    );
  }

  remove(id: number) {
    clearTimeout(this.#timers.get(id));
    this.rows = this.rows.filter((r) => r.id !== id);
    this.#recomputeEnv();
    this.#persist();
  }

  clearAll() {
    for (const t of this.#timers.values()) clearTimeout(t);
    this.#timers.clear();
    this.rows = [];
    this.#persist();
  }

  /**
   * `name := value` accepted from the main input (§2): both parts are already
   * validated by the caller. REPL semantics — a cycle rejects the definition
   * outright (the panel, by contrast, tolerates and flags).
   */
  commitAssignment(v: {
    symbol: string;
    nameLatex: string;
    kind: "expression" | "equation";
    valuePlain: string;
    valueLatex: string;
    symbols: string[];
  }): { ok: true; id: number } | { ok: false; error: string } {
    const others = this.rows
      .filter((r) => isRowValid(r) && r.status.symbol !== v.symbol)
      .map((r) => ({
        name: r.status.symbol!,
        value: r.status.valuePlain!,
        symbols: r.status.symbols ?? [],
        kind: r.status.kind ?? ("expression" as const),
      }));
    const path = findCycle(v.symbol, v.symbols, others);
    if (path) return { ok: false, error: cycleMessage(path) };
    // Redefinition of an existing name is always allowed; only new rows
    // count against the cap.
    if (!this.rows.some((r) => r.status.symbol === v.symbol) && this.atCap)
      return { ok: false, error: CAP_ERROR };

    const status: VarStatus = {
      phase: "checked",
      symbol: v.symbol,
      nameLatex: v.nameLatex,
      kind: v.kind,
      valuePlain: v.valuePlain,
      valueLatex: v.valueLatex,
      symbols: v.symbols,
    };
    let row = this.rows.find((r) => r.status.symbol === v.symbol);
    if (row) {
      row.value = v.valuePlain;
      row.ts = Date.now();
      row.status = status;
    } else {
      row = {
        id: nextId++,
        name: symbolToTyped(v.symbol),
        value: v.valuePlain,
        ts: Date.now(),
        status,
      };
      this.rows.push(row);
    }
    this.#recomputeEnv();
    this.#persist();
    return { ok: true, id: row.id };
  }

  focusRow(id: number) {
    this.focusId = id;
  }

  async #validate(id: number, persistAfter: boolean) {
    const row = this.rows.find((r) => r.id === id);
    if (!row) return;
    const seq = (this.#seq.get(id) ?? 0) + 1;
    this.#seq.set(id, seq);
    const name = row.name.trim();
    const value = row.value.trim();
    const status: VarStatus = { phase: "checked" };
    try {
      if (name) {
        const normalized = normalizeTypedName(name);
        const nv = nameVerdict(normalized, await call("analyze", [normalized]));
        if (nv.ok) {
          status.symbol = nv.symbol;
          status.nameLatex = nv.latex;
        } else {
          status.nameError = nv.error;
        }
      }
      if (value) {
        const vv = valueVerdict(await call("analyze", [value]));
        if (vv.ok) {
          status.kind = vv.kind;
          status.valuePlain = vv.plain;
          status.valueLatex = vv.latex;
          status.symbols = vv.symbols;
        } else {
          status.valueError = vv.error;
          status.valueSpan = { begin: vv.begin, end: vv.end };
        }
      } else if (name) {
        status.valueError = EMPTY_VALUE_ERROR;
      }
    } catch (e) {
      status.valueError = e instanceof Error ? e.message : String(e);
    }
    // Stale checks (row edited again, or deleted) must not clobber state.
    if (this.#seq.get(id) !== seq) return;
    const cur = this.rows.find((r) => r.id === id);
    if (!cur) return;
    cur.status = status;
    this.#recomputeEnv();
    if (persistAfter) this.#persist();
  }

  /**
   * Whole-environment pass: later duplicate rows win (mirroring the loader),
   * and each remaining row is cycle-checked in insertion order against the
   * rows accepted before it (§5.2). Offending rows are flagged inactive —
   * stored, but excluded from resolution — until edited.
   */
  #recomputeEnv() {
    const winners = new Map<string, number>();
    for (const r of this.rows) {
      if (isRowValid(r)) winners.set(r.status.symbol!, r.id);
    }
    const accepted: VarBinding[] = [];
    for (const r of this.rows) {
      if (!isRowValid(r)) continue;
      const sym = r.status.symbol!;
      if (winners.get(sym) !== r.id) {
        r.status.envError = `shadowed by a later '${symbolToTyped(sym)}' row`;
        continue;
      }
      const path = findCycle(sym, r.status.symbols ?? [], accepted);
      if (path) {
        r.status.envError = cycleMessage(path);
        continue;
      }
      if (r.status.envError) r.status.envError = undefined;
      accepted.push({
        name: sym,
        value: r.status.valuePlain!,
        symbols: r.status.symbols ?? [],
        kind: r.status.kind ?? "expression",
      });
    }
  }

  #persist() {
    try {
      const vars = this.rows
        .filter((r) => r.name.trim() && r.value.trim())
        .slice(0, CAP)
        .map((r) => ({
          // Canonical forms when known (§9.2: plain-printed strings survive
          // engine upgrades); the typed source otherwise (invalid rows are
          // user data and persist verbatim).
          name: r.status.symbol ?? r.name.trim(),
          value: r.status.valuePlain ?? r.value.trim(),
          ts: r.ts,
        }));
      localStorage.setItem(KEY, JSON.stringify({ v: 1, vars }));
    } catch {
      /* storage unavailable */
    }
  }
}

export const vars = new VarsStore();
