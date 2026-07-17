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
    this.entries = [{ ...e, id: nextId++, ts: Date.now() }, ...this.entries].slice(0, CAP);
    this.#persist();
  }

  clear() {
    this.entries = [];
    this.#persist();
  }
}

export const history = new HistoryStore();
