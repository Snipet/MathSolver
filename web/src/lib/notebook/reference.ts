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
      {
        insert: "apart",
        usage: "apart <expr>[, <var>]",
        hint: "Partial-fraction expansion of a rational function",
      },
      { insert: "latex", usage: "latex <expr>", hint: "Convert to LaTeX, unsimplified" },
      {
        insert: "plot",
        usage: "plot <expr>[, <lo>, <hi>]",
        hint: "Sample and chart an expression",
      },
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
      {
        insert: "laplace",
        usage: "laplace <expr>[, <t>]",
        hint: "Laplace transform f(t) → F(s)",
      },
      {
        insert: "ilaplace",
        usage: "ilaplace <expr>[, <s>]",
        hint: "Inverse Laplace F(s) → f(t)",
      },
      {
        insert: "dsolve",
        usage: "dsolve <ode>[, y(0)=v, y'(0)=v, …]",
        hint: "Solve a linear ODE initial-value problem exactly",
      },
      {
        insert: "series",
        usage: "series <expr>[, <var>[, <center>[, <order>]]]",
        hint: "Taylor expansion (center 0, order 6 by default)",
      },
      {
        insert: "limit",
        usage: "limit <expr>, <var>, <point>[, left|right]",
        hint: "Limit at a point or ±inf (exact where possible)",
      },
    ],
  },
  {
    title: "Vector calculus",
    items: [
      {
        insert: "grad",
        usage: "grad <f>, <var>[, <var> …]",
        hint: "Gradient ∇f",
      },
      {
        insert: "div",
        usage: "div <F1; F2; …>, <var> …",
        hint: "Divergence ∇·F (';'-separated field)",
      },
      {
        insert: "curl",
        usage: "curl <F1; F2[; F3]>, <var> …",
        hint: "Curl ∇×F (3-D) or scalar curl (2-D)",
      },
      {
        insert: "laplacian",
        usage: "laplacian <f>, <var> …",
        hint: "Laplacian ∇²f",
      },
      {
        insert: "jacobian",
        usage: "jacobian <F1; F2; …>, <var> …",
        hint: "Jacobian matrix ∂F_i/∂x_j",
      },
      {
        insert: "hessian",
        usage: "hessian <f>, <var> …",
        hint: "Hessian matrix ∂²f/∂x_i∂x_j",
      },
      {
        insert: "vecfield",
        usage: "vecfield <Fx>; <Fy>[, <xlo>, <xhi>, <ylo>, <yhi>]",
        hint: "Quiver plot of a planar vector field",
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
