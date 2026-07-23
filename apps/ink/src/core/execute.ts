// Run a Job against the engine, resolving variable inference (via analyze)
// where the grammar allowed the variable to be omitted. Returns an Outcome the
// formatter turns into display lines. Mirrors the run_* helpers in
// apps/main.cpp, but talks to the WASM JSON API instead of printing directly.

import type { Engine } from "../engine/engine.js";
import type { AnalyzeResult } from "../engine/types.js";
import type { Job } from "./intent.js";
import { line, type OutLine } from "./outline.js";
import type { Outcome, RenderKind } from "./outcome.js";
import { isSymbolName } from "./text.js";

function renderOut(
  render: RenderKind,
  result: unknown,
  extra: { variable?: string; noun?: "sum" | "product"; source?: string } = {},
): Outcome {
  return { kind: "render", render, result, ...extra };
}
function linesOut(lines: OutLine[]): Outcome {
  return { kind: "lines", lines };
}
function errorOut(message: string): Outcome {
  return linesOut([line(`error: ${message}`, "error")]);
}

/** Split at the first top-level `=` (bracket depth 0), for closed-equation
 *  identity/contradiction checks. Returns null when there is no such `=`. */
function splitAtEquals(s: string): [string, string] | null {
  let depth = 0;
  for (let i = 0; i < s.length; i++) {
    const c = s[i]!;
    if (c === "(" || c === "{" || c === "[") depth++;
    else if (c === ")" || c === "}" || c === "]") depth--;
    else if (c === "=" && depth === 0) {
      const prev = s[i - 1];
      if (prev !== "<" && prev !== ">" && prev !== "!" && prev !== ":" && prev !== "=") {
        return [s.slice(0, i), s.slice(i + 1)];
      }
    }
  }
  return null;
}

/** Resolve the free variable of `input`, or a short-circuit Outcome carrying
 *  the analyze parse error / an inference usage error. Mirrors
 *  choose_variable() in apps/main.cpp. */
async function inferVariable(
  engine: Engine,
  input: string,
  what: string,
): Promise<{ variable: string } | { stop: Outcome }> {
  const a: AnalyzeResult = await engine.analyze(input);
  if (!a.ok) return { stop: renderOut("transform", a, { source: input }) };
  const syms = a.symbols;
  if (syms.length === 1) return { variable: syms[0]! };
  if (syms.length === 0) {
    return {
      stop: errorOut(
        `cannot infer the variable for ${what}: the input has no free symbols; ` +
          "pass the variable explicitly",
      ),
    };
  }
  return {
    stop: errorOut(
      `cannot infer the variable for ${what}: the input has ${syms.length} free ` +
        `symbols (${syms.join(", ")}); pass the variable explicitly`,
    ),
  };
}

/** Identity / contradiction verdict for an equation with no free symbols. */
async function closedEquationVerdict(engine: Engine, input: string): Promise<Outcome> {
  const sides = splitAtEquals(input);
  if (!sides) return errorOut("expected an equation of the form lhs = rhs");
  const diff = `(${sides[0]}) - (${sides[1]})`;
  const s = await engine.simplify(diff);
  if (!s.ok) return renderOut("transform", s, { source: diff });
  if (s.plain === "0") return linesOut([line("equation holds (identity)", "result")]);
  if (/^-?\d+(\/\d+)?$/.test(s.plain)) {
    return linesOut([line("equation is false (contradiction)", "result")]);
  }
  const ev = await engine.evaluate(diff, "");
  if (ev.ok && ev.value !== null) {
    return Math.abs(ev.value) < 1e-12
      ? linesOut([
          line(
            `equation holds numerically (lhs - rhs ≈ ${ev.value}; not verified exactly)`,
            "result",
          ),
        ])
      : linesOut([line(`equation is false numerically (lhs - rhs ≈ ${ev.value})`, "result")]);
  }
  return linesOut([line(`lhs - rhs = ${s.plain}`, "result")]);
}

export async function executeIntent(engine: Engine, jobToRun: Job): Promise<Outcome> {
  const j = jobToRun;
  switch (j.t) {
    case "simplify":
      return renderOut("transform", await engine.simplify(j.input), { source: j.input });
    case "expand":
      return renderOut("transform", await engine.expand(j.input), { source: j.input });
    case "factor":
      return renderOut("transform", await engine.factor(j.input), { source: j.input });
    case "together":
      return renderOut("transform", await engine.together(j.input), { source: j.input });
    case "latex":
      return renderOut("transform", await engine.latex(j.input), { source: j.input });
    case "collect":
      return renderOut("transform", await engine.collect(j.input, j.variable), { source: j.input });
    case "apart":
      return renderOut("transform", await engine.apart(j.input, j.variable), { source: j.input });
    case "laplace":
      return renderOut("transform", await engine.laplace(j.input, j.variable), { source: j.input });
    case "ilaplace":
      return renderOut("transform", await engine.ilaplace(j.input, j.variable), { source: j.input });
    case "subs":
      return renderOut("transform", await engine.subs(j.input, j.assignments, true), {
        source: j.input,
      });
    case "series":
      return renderOut(
        "transform",
        await engine.series(j.input, j.variable, j.center, j.order),
        { source: j.input },
      );
    case "stirling":
      return renderOut("transform", await engine.stirling(j.variable, j.terms));
    case "vector":
      return renderOut("transform", await engine.vectorOp(j.op, j.fieldSemi, j.varsCsv));
    case "nt":
      return renderOut("transform", await engine[j.name](j.arg), { source: j.arg });
    case "solveIneq":
      return renderOut("transform", await engine.solveIneq(j.lhs, j.rhs, j.op, j.variable));
    case "evaluate":
      return renderOut("evaluate", await engine.evaluate(j.input, j.bindings), { source: j.input });
    case "cancel": {
      if (j.variable !== null) {
        if (!isSymbolName(j.variable)) return errorOut(`'${j.variable}' is not a valid variable name`);
        const a = await engine.analyze(j.input);
        if (!a.ok) return renderOut("transform", a, { source: j.input });
        if (a.kind !== "system" && !a.symbols.includes(j.variable)) {
          return errorOut(`'${j.variable}' is not a free variable of the input`);
        }
      }
      return renderOut("transform", await engine.cancel(j.input), { source: j.input });
    }
    case "derivative": {
      let v = j.variable;
      if (v === null || v === "") {
        const inf = await inferVariable(engine, j.input, "diff");
        if ("stop" in inf) return inf.stop;
        v = inf.variable;
      }
      return renderOut("transform", await engine.derivative(j.input, v), { source: j.input });
    }
    case "solve": {
      let v = j.variable;
      if (v === null || v === "") {
        const inf = await inferVariable(engine, j.input, "solve");
        if ("stop" in inf) return inf.stop;
        v = inf.variable;
      }
      return renderOut("solve", await engine.solve(j.input, v, 0, 0, false), {
        variable: v,
        source: j.input,
      });
    }
    case "bareEquation": {
      const a = await engine.analyze(j.input);
      if (!a.ok) return renderOut("transform", a, { source: j.input });
      const syms = a.symbols;
      if (syms.length === 1) {
        return renderOut("solve", await engine.solve(j.input, syms[0]!, 0, 0, false), {
          variable: syms[0]!,
          source: j.input,
        });
      }
      if (syms.length > 1) {
        return errorOut(
          `the equation has ${syms.length} free symbols (${syms.join(", ")}); ` +
            "use: solve <equation>, <variable>",
        );
      }
      return closedEquationVerdict(engine, j.input);
    }
    case "solveSystem":
      return renderOut("system", await engine.solveSystem(j.input, j.varsCsv));
    case "integrate": {
      let v = j.variable;
      if (v === null || v === "") {
        const inf = await inferVariable(engine, j.input, "integrate");
        if ("stop" in inf) return inf.stop;
        v = inf.variable;
      }
      if (j.bounds) {
        return renderOut(
          "definite",
          await engine.integrateDefinite(j.input, v, j.bounds[0], j.bounds[1]),
          { source: j.input },
        );
      }
      return renderOut("integrate", await engine.integrate(j.input, v), { source: j.input });
    }
    case "limit":
      return renderOut("limit", await engine.limit(j.input, j.variable, j.point, j.direction), {
        source: j.input,
      });
    case "mlimit":
      return renderOut("limit", await engine.mlimit(j.input, j.xVar, j.a, j.yVar, j.b), {
        source: j.input,
      });
    case "sum":
      return renderOut(
        "sum",
        j.noun === "product"
          ? await engine.product(j.term, j.variable, j.lo, j.hi)
          : await engine.sum(j.term, j.variable, j.lo, j.hi),
        { noun: j.noun },
      );
    case "rsolve":
      return renderOut("rsolve", await engine.rsolve(j.recurrence, j.conditionsCsv));
    case "dsolve":
      return renderOut("dsolve", await engine.dsolve(j.ode, j.conditionsCsv));
    case "fit":
      return renderOut("fit", await engine.fit(j.data, j.model, j.degree));
    case "stats":
      return renderOut("stats", await engine.stats(j.data));
    case "seq":
      return renderOut("seq", await engine.seq(j.termsCsv));
    default: {
      const _exhaustive: never = j;
      void _exhaustive;
      return errorOut("internal error: unhandled job");
    }
  }
}
