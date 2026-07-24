// Serialize a console session (an ordered list of evaluated cells) to a plain
// Markdown transcript, for the "Export" button. Pure and dependency-free — it
// imports only types, so it runs in Node for unit testing and never pulls in
// the Svelte/WASM runtime.
import type { Cell, CellResult } from "./notebook.svelte";

/** The plain-text output line(s) of one evaluated cell (may be empty). */
export function cellOutputLines(result: CellResult | null): string[] {
  if (!result) return [];
  switch (result.kind) {
    case "message":
      return result.lines.slice();
    case "assignment":
      return [`${result.name} = ${result.plain}`];
    case "error":
      return [`error: ${result.message}`];
    case "transform":
    case "dsolve":
      return [result.result.plain];
    case "solve": {
      const sols = result.result.solutions;
      if (sols.length === 0) return [`(${result.result.status})`];
      return sols.map((s) => `${result.variable} = ${s.plain}`);
    }
    case "system": {
      const vals = result.result.values;
      if (vals.length === 0) return [`(${result.result.status})`];
      return vals.map((v) => `${v.symbol} = ${v.plain}`);
    }
    case "integral":
      return result.result.solved
        ? [result.result.plain]
        : ["(no closed form)"];
    case "definite":
      return result.result.status === "unsolved"
        ? ["(unsolved)"]
        : [result.result.plain];
    case "evaluate":
      return [result.result.value === null ? "(undefined)" : String(result.result.value)];
    case "plugin":
      return [`[${result.plugin}.${result.command}]`];
    case "chart":
      return [`[chart: ${result.title}]`];
    case "vecfield":
      return [`[vector field: (${result.fx}, ${result.fy})]`];
    case "wave":
      return ["[interactive wave field]"];
    default:
      return [];
  }
}

/**
 * Render the whole session as a Markdown transcript: a heading, then one
 * `In[k]` / `Out[k]` block per cell. Blank-output cells (bare messages that
 * echo their own input) still show their In line so the sequence is intact.
 */
export function serializeTranscript(
  cells: Cell[],
  opts: { title?: string } = {},
): string {
  const title = opts.title ?? "MathSolver console session";
  const out: string[] = [`# ${title}`, "", `${cells.length} cell${cells.length === 1 ? "" : "s"}`, ""];
  cells.forEach((c, i) => {
    const n = i + 1;
    out.push("```");
    out.push(`In[${n}]:  ${c.input}`);
    const lines = cellOutputLines(c.result);
    if (lines.length === 1) {
      out.push(`Out[${n}]: ${lines[0]}`);
    } else if (lines.length > 1) {
      out.push(`Out[${n}]:`);
      for (const l of lines) out.push(`  ${l}`);
    }
    out.push("```");
    out.push("");
  });
  return out.join("\n");
}
