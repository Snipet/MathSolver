// Saved notebooks: named, ordered lists of console commands, persisted to
// localStorage. A notebook stores inputs only — running one replays the
// commands in a fresh variable scope (vars/session.ts ScopeEnv), so a saved
// notebook behaves like a script, not a snapshot of session state.

export interface NotebookDoc {
  name: string;
  lines: string[];
  ts: number;
}

const KEY = "mathsolver.notebooks";
const CAP = 40;
export const NAME_RE = /^[A-Za-z0-9_-]{1,40}$/;
const MAX_LINES = 200;
const MAX_LINE_LEN = 2000;

function load(): NotebookDoc[] {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return [];
    const store: unknown = JSON.parse(raw);
    if (!store || typeof store !== "object") return [];
    const arr = (store as Record<string, unknown>).notebooks;
    if (!Array.isArray(arr)) return [];
    const out: NotebookDoc[] = [];
    for (const d of arr) {
      if (!d || typeof d !== "object") continue;
      const o = d as Record<string, unknown>;
      if (typeof o.name !== "string" || !NAME_RE.test(o.name)) continue;
      if (!Array.isArray(o.lines)) continue;
      const lines = o.lines
        .filter((l): l is string => typeof l === "string" && l.length > 0)
        .slice(0, MAX_LINES)
        .map((l) => l.slice(0, MAX_LINE_LEN));
      if (lines.length === 0) continue;
      if (out.some((x) => x.name === o.name)) continue;
      out.push({
        name: o.name,
        lines,
        ts: typeof o.ts === "number" ? o.ts : Date.now(),
      });
    }
    return out.slice(0, CAP);
  } catch {
    return [];
  }
}

class DocsStore {
  docs = $state<NotebookDoc[]>(load());

  get(name: string): NotebookDoc | undefined {
    return this.docs.find((d) => d.name === name);
  }

  /** Save (or overwrite) a notebook. Returns an error string on rejection. */
  save(name: string, lines: string[]): string | null {
    if (!NAME_RE.test(name))
      return "notebook names are 1-40 letters, digits, '_' or '-'";
    if (lines.length === 0) return "nothing to save — the console is empty";
    const doc: NotebookDoc = {
      name,
      lines: lines.slice(0, MAX_LINES).map((l) => l.slice(0, MAX_LINE_LEN)),
      ts: Date.now(),
    };
    const i = this.docs.findIndex((d) => d.name === name);
    if (i >= 0) this.docs[i] = doc;
    else {
      if (this.docs.length >= CAP)
        return `at most ${CAP} notebooks — delete one first`;
      this.docs.push(doc);
    }
    this.#persist();
    return null;
  }

  remove(name: string): void {
    this.docs = this.docs.filter((d) => d.name !== name);
    this.#persist();
  }

  #persist(): void {
    try {
      localStorage.setItem(
        KEY,
        JSON.stringify({ v: 1, notebooks: this.docs }),
      );
    } catch {
      /* storage unavailable or quota exceeded */
    }
  }
}

export const docs = new DocsStore();
