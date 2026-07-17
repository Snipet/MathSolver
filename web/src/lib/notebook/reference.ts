// Console command reference: one source of truth for the reference panel,
// the autocomplete popup, and the inline usage hints. Built-in commands are
// static; plugin commands are appended from the live catalog.
import type { PluginMeta } from "../engine/types";
import { getCatalog } from "./run";

export interface RefItem {
  /** Text inserted into the prompt when picked (trailing space added). */
  insert: string;
  /** Full usage synopsis, e.g. "solve <equation>[, <variable>]". */
  usage: string;
  /** One-line description. */
  hint: string;
}

export interface RefGroup {
  title: string;
  items: RefItem[];
}

export const BUILTIN_GROUPS: RefGroup[] = [
  {
    title: "Compute",
    items: [
      {
        insert: "simplify",
        usage: "simplify <expr>   (or just type a bare expression)",
        hint: "Simplify an expression",
      },
      { insert: "expand", usage: "expand <expr>", hint: "Multiply everything out" },
      { insert: "factor", usage: "factor <expr>", hint: "Best-effort factoring" },
      {
        insert: "collect",
        usage: "collect <expr>[, <var>]",
        hint: "Regroup as a polynomial in one variable",
      },
      { insert: "latex", usage: "latex <expr>", hint: "Convert to LaTeX, unsimplified" },
    ],
  },
  {
    title: "Calculus",
    items: [
      {
        insert: "diff",
        usage: "diff <expr>[, <var>]",
        hint: "Symbolic derivative",
      },
      {
        insert: "integrate",
        usage: "integrate <expr>[, <var>[, <lo>, <hi>]]",
        hint: "Antiderivative, or a definite integral with bounds",
      },
    ],
  },
  {
    title: "Solve & evaluate",
    items: [
      {
        insert: "solve",
        usage: "solve <equation>[, <var>]  ·  solve <eq>; <eq>[, <vars…>]",
        hint: "Equations, and ;-separated linear systems",
      },
      {
        insert: "eval",
        usage: "eval <expr>, x=1[, y=2 …]",
        hint: "Numeric value with variable bindings",
      },
      {
        insert: "subs",
        usage: "subs <expr>, x=y+1[, …]",
        hint: "Substitute expressions, then simplify",
      },
    ],
  },
  {
    title: "Session variables",
    items: [
      {
        insert: ":=",
        usage: "<name> := <value>   e.g.  a := 2   E_1 := x + y = 3",
        hint: "Bind a variable; applies to later lines",
      },
      { insert: "vars", usage: "vars", hint: "List current bindings" },
      { insert: "unset", usage: "unset <name>", hint: "Remove one binding" },
      { insert: "clear", usage: "clear", hint: "Remove all bindings" },
    ],
  },
  {
    title: "Console",
    items: [
      { insert: "help", usage: "help", hint: "Show the full grammar" },
      { insert: "plugins", usage: "plugins", hint: "List compiled-in plugins" },
    ],
  },
];

/** Plugin catalog rendered as reference groups (one per plugin). */
export function pluginGroups(catalog: PluginMeta[]): RefGroup[] {
  return catalog.map((p) => ({
    title: `Plugin: ${p.name} ${p.version}`,
    items: p.commands.map((c) => ({
      insert: `${p.name}.${c.name}`,
      usage: c.usage,
      hint: c.summary,
    })),
  }));
}

/** Everything, for the reference panel. */
export async function allGroups(): Promise<RefGroup[]> {
  try {
    return [...BUILTIN_GROUPS, ...pluginGroups(await getCatalog())];
  } catch {
    return BUILTIN_GROUPS;
  }
}

/**
 * Flat completion list for the prompt: every insertable command word
 * (excluding `:=`, which is not a leading token).
 */
export async function completionItems(): Promise<RefItem[]> {
  const groups = await allGroups();
  const out: RefItem[] = [];
  for (const g of groups) {
    for (const it of g.items) {
      if (it.insert !== ":=") out.push(it);
    }
  }
  return out;
}
