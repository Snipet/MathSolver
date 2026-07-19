// Live typeset preview of the console prompt: understands the command
// grammar and renders the math the line will compute, as math —
//   diff sin(x^2), x        →  d/dx ( sin x² )
//   integrate f, x, 0, pi   →  ∫₀^π f dx
//   solve x^2 = 4, x        →  x² = 4   (for x)
//   a := 2                  →  a := 2
// Parse errors surface with the engine's caret span into the offending
// fragment. Plugin and session commands yield "none" (the usage hint covers
// those).
import { call } from "../engine";
import { splitTopLevelCommas, varLatex } from "../format";
import { splitAssignment, buildAssignPreview } from "../vars/session";

export type ConsolePreview =
  | { kind: "none" }
  | { kind: "math"; latex: string; note?: string }
  | {
      kind: "error";
      error: string;
      /** The fragment the span indexes into (shown with the caret mark). */
      source?: string;
      begin?: number;
      end?: number;
    };

const SESSION_WORDS = new Set([
  "help",
  "vars",
  "clear",
  "unset",
  "plugins",
  "quit",
  "exit",
  "save",
  "open",
  "run",
  "notebooks",
]);

function splitHead(line: string): { head: string; rest: string } {
  const m = /^(\S+)\s*([\s\S]*)$/.exec(line);
  if (!m) return { head: line, rest: "" };
  return { head: m[1], rest: m[2].trim() };
}

const NONE: ConsolePreview = { kind: "none" };

/** Analyze a fragment; math preview of its parsed form, or an error. */
async function fragment(
  text: string,
): Promise<{ latex: string } | { error: string; begin?: number; end?: number }> {
  const a = await call("analyze", [text]);
  if (!a.ok) return { error: a.error, begin: a.begin, end: a.end };
  if (a.kind === "system") {
    // Render each equation; analyze exposes no per-segment latex for
    // systems, so re-analyze the segments.
    const parts = text.split(";").map((s) => s.trim()).filter(Boolean);
    const rendered: string[] = [];
    for (const p of parts) {
      const pa = await call("analyze", [p]);
      if (!pa.ok) return { error: pa.error, begin: pa.begin, end: pa.end };
      if (pa.kind === "system") return { error: "nested system" };
      rendered.push(pa.latex);
    }
    return { latex: rendered.join(" ;\\;\\; ") };
  }
  return { latex: a.latex };
}

/** Latex for a definite-integral bound (best effort; raw text fallback). */
async function boundLatex(text: string): Promise<string> {
  const a = await call("analyze", [text]);
  if (a.ok && a.kind === "expression") return a.latex;
  return `\\text{${text.replace(/[\\{}]/g, "")}}`;
}

export async function buildConsolePreview(raw: string): Promise<ConsolePreview> {
  const line = raw.trim();
  if (!line) return NONE;

  // Assignments reuse the workbench's validated preview.
  if (splitAssignment(line)) {
    const p = await buildAssignPreview(splitAssignment(line)!);
    if (p.error) {
      return {
        kind: "error",
        error: p.error,
        source: p.value || undefined,
        begin: p.span?.begin,
        end: p.span?.end,
      };
    }
    return { kind: "math", latex: p.latex!, note: "assignment" };
  }

  const { head, rest } = splitHead(line);
  const verb = head.toLowerCase();
  if (SESSION_WORDS.has(verb)) return NONE;
  // Plugin commands (dsp.butter …) have their own usage hints.
  if (/^[A-Za-z_][A-Za-z0-9_]*\.[A-Za-z_][A-Za-z0-9_]*$/.test(head)) return NONE;

  const wrap = (r: { error: string; begin?: number; end?: number }, src: string):
    ConsolePreview => ({
    kind: "error",
    error: r.error,
    source: src,
    begin: r.begin,
    end: r.end,
  });

  const args = splitTopLevelCommas(rest);
  const expr = args[0] ?? "";

  switch (verb) {
    case "simplify":
    case "expand":
    case "factor":
    case "latex": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      return "latex" in f ? { kind: "math", latex: f.latex, note: verb } : wrap(f, expr);
    }
    case "collect": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      return {
        kind: "math",
        latex: f.latex,
        note: args[1] ? `collect in ${args[1]}` : "collect",
      };
    }
    case "dsolve":
      // The ODE prime grammar (y'' …) is not an expression; no live preview.
      return NONE;
    case "sum":
    case "product": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const v = args[1] ?? "k";
      const lo = args[2] ?? "?";
      const hi =
        args[3] === "inf" || args[3] === "oo" ? "\\infty" : (args[3] ?? "?");
      const op = verb === "sum" ? "\\sum" : "\\prod";
      return {
        kind: "math",
        latex: `${op}_{${v}=${lo}}^{${hi}} ${f.latex}`,
      };
    }
    case "rsolve":
      // The a(n+k) grammar is not an expression; no live preview.
      return NONE;
    case "seq": {
      if (!expr) return NONE;
      return {
        kind: "math",
        latex: `${args.slice(0, 6).join(",\\; ")}${args.length > 6 ? ",\\; \\dots" : ",\\; \\dots"}`,
        note: "sequence recognition",
      };
    }
    case "stirling": {
      const v = args[0] || "x";
      const terms = args[1] ?? "3";
      return {
        kind: "math",
        latex: `\\ln \\Gamma(${v})`,
        note: `Stirling series, ${terms} correction terms`,
      };
    }
    case "mlimit": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const x = args[1] ?? "x";
      const y = args[3] ?? "y";
      const a = args[2] ?? "?";
      const b = args[4] ?? "?";
      return {
        kind: "math",
        latex: `\\lim_{(${x},${y}) \\to (${a},${b})} ${f.latex}`,
      };
    }
    case "limit": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const v = args[1] ?? "x";
      const pt =
        args[2] === "inf" || args[2] === "oo"
          ? "\\infty"
          : args[2] === "-inf" || args[2] === "-oo"
            ? "-\\infty"
            : (args[2] ?? "?");
      const side = args[3] === "left" ? "^-" : args[3] === "right" ? "^+" : "";
      return {
        kind: "math",
        latex: `\\lim_{${v} \\to ${pt}${side}} ${f.latex}`,
      };
    }
    case "grad":
    case "div":
    case "curl":
    case "laplacian":
    case "jacobian":
    case "hessian":
    case "vecfield": {
      // Preview the first field component (';'-separated fields aren't a
      // single expression); the note names the operator.
      const first = (expr.split(";")[0] ?? "").trim();
      if (!first) return NONE;
      const f = await fragment(first);
      if (!("latex" in f)) return NONE;
      const vars = args.slice(1).join(", ");
      return {
        kind: "math",
        latex: f.latex,
        note: vars ? `${verb} · vars ${vars}` : verb,
      };
    }
    case "series": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const about = args[2] ?? "0";
      const ord = args[3] ?? "6";
      return {
        kind: "math",
        latex: f.latex,
        note: `Taylor about ${about}, order ${ord}`,
      };
    }
    case "apart": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      return {
        kind: "math",
        latex: f.latex,
        note: args[1] ? `partial fractions in ${args[1]}` : "partial fractions",
      };
    }
    case "plot": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const note =
        args.length >= 3 ? `plot on [${args[1]}, ${args[2]}]` : "plot";
      return { kind: "math", latex: f.latex, note };
    }
    case "diff":
    case "derivative": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const v = varLatex(args[1] ?? "x");
      return {
        kind: "math",
        latex: `\\frac{d}{d${v}}\\left(${f.latex}\\right)`,
      };
    }
    case "laplace": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      return {
        kind: "math",
        latex: `\\mathcal{L}\\left\\{${f.latex}\\right\\}`,
        note: args[1] ? `in ${args[1]}` : undefined,
      };
    }
    case "ilaplace": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      return {
        kind: "math",
        latex: `\\mathcal{L}^{-1}\\left\\{${f.latex}\\right\\}`,
        note: args[1] ? `in ${args[1]}` : undefined,
      };
    }
    case "integrate": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const v = varLatex(args[1] ?? "x");
      if (args.length >= 4) {
        const lo = await boundLatex(args[2]);
        const hi = await boundLatex(args[3]);
        return {
          kind: "math",
          latex: `\\int_{${lo}}^{${hi}} ${f.latex} \\, d${v}`,
        };
      }
      return { kind: "math", latex: `\\int ${f.latex} \\, d${v}` };
    }
    case "solve": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const vars = args.slice(1).filter(Boolean);
      return {
        kind: "math",
        latex: f.latex,
        note: vars.length > 0 ? `solve for ${vars.join(", ")}` : "solve",
      };
    }
    case "eval":
    case "evaluate": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      const binds = args.slice(1).filter(Boolean).join(",\\;");
      const latex = binds
        ? `${f.latex}\\;\\Big|_{${binds.replace(/[\\{}]/g, "")}}`
        : f.latex;
      return { kind: "math", latex, note: "evaluate" };
    }
    case "subs": {
      if (!expr) return NONE;
      const f = await fragment(expr);
      if (!("latex" in f)) return wrap(f, expr);
      return {
        kind: "math",
        latex: f.latex,
        note: args.length > 1 ? `with ${args.slice(1).join(", ")}` : "subs",
      };
    }
    default: {
      // Bare math: expression, equation, or system.
      const f = await fragment(line);
      return "latex" in f ? { kind: "math", latex: f.latex } : wrap(f, line);
    }
  }
}
