// Computation history: localStorage-persisted, capped at 50, newest first.
import { TABS, type TabId } from "./tabs";

export interface HistoryEntry {
  id: number;
  tab: TabId;
  input: string;
  params: Record<string, unknown>;
  summary: string;
  ts: number;
}

const KEY = "mathsolver.history";
const CAP = 50;

function load(): HistoryEntry[] {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return [];
    const arr: unknown = JSON.parse(raw);
    if (!Array.isArray(arr)) return [];
    return arr
      .filter((e): e is HistoryEntry => {
        if (!e || typeof e !== "object") return false;
        const h = e as Partial<HistoryEntry>;
        // Drop unknown/legacy tab ids: they cannot be restored meaningfully.
        return typeof h.input === "string" && TABS.some((t) => t.id === h.tab);
      })
      .map((e) => ({
        ...e,
        // Older/corrupt entries may lack params; normalize to a plain object.
        params:
          e.params && typeof e.params === "object" && !Array.isArray(e.params)
            ? e.params
            : {},
      }))
      .slice(0, CAP);
  } catch {
    return [];
  }
}

let nextId = Date.now();

/** Structural equality for JSON-safe param values (key order insensitive). */
function paramsEqual(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  if (typeof a !== "object" || typeof b !== "object" || a === null || b === null)
    return false;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  const ka = Object.keys(a);
  const kb = Object.keys(b);
  if (ka.length !== kb.length) return false;
  return ka.every(
    (k) =>
      k in b &&
      paramsEqual((a as Record<string, unknown>)[k], (b as Record<string, unknown>)[k]),
  );
}

class HistoryStore {
  entries = $state<HistoryEntry[]>(load());

  #persist() {
    try {
      localStorage.setItem(KEY, JSON.stringify(this.entries));
    } catch {
      /* storage unavailable */
    }
  }

  add(e: Omit<HistoryEntry, "id" | "ts">) {
    // Top-of-history dedupe: re-running the newest computation (manually or
    // via a history restore) refreshes it in place instead of appending a clone.
    const top = this.entries[0];
    if (
      top &&
      top.tab === e.tab &&
      top.input === e.input &&
      paramsEqual(top.params, e.params)
    ) {
      this.entries = [
        { ...top, summary: e.summary, ts: Date.now() },
        ...this.entries.slice(1),
      ];
    } else {
      this.entries = [{ ...e, id: nextId++, ts: Date.now() }, ...this.entries].slice(
        0,
        CAP,
      );
    }
    this.#persist();
  }

  clear() {
    this.entries = [];
    this.#persist();
  }
}

export const history = new HistoryStore();
