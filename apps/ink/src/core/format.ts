// Turn an Outcome into display lines, faithfully mirroring the print_* helpers
// in apps/main.cpp so the terminal app reads the same as the classic REPL.

import { caretDiagnostic } from "./caret.js";
import { line, type OutLine } from "./outline.js";
import type { Outcome } from "./outcome.js";
import type {
  DefiniteResult,
  DsolveResult,
  EngineError,
  EvaluateResult,
  FitResult,
  IntegrateResult,
  LimitResult,
  Rendered,
  RsolveResult,
  SeqResult,
  SolveResult,
  StatsResult,
  SumResult,
  SystemResult,
  TransformResult,
} from "../engine/types.js";

export interface FormatOptions {
  latex?: boolean;
}

/** Trim a floating value to `sig` significant digits (like printf %g). */
function num(x: number, sig = 10): string {
  if (!Number.isFinite(x)) return String(x);
  return String(Number(x.toPrecision(sig)));
}

function pick(r: Rendered, latex: boolean): string {
  return latex ? r.latex : r.plain;
}

/** Parse errors (with a byte span) render as a caret block; other engine
 *  errors render as a single "error: ..." line. */
function formatError(err: EngineError, source?: string): OutLine[] {
  if (err.begin !== undefined && err.end !== undefined && source !== undefined) {
    return [line(caretDiagnostic(source, err.error, err.begin, err.end), "error")];
  }
  return [line(`error: ${err.error}`, "error")];
}

function method(m: string | undefined): OutLine[] {
  return m ? [line(`method: ${m}`, "muted")] : [];
}
function warnings(ws: string[] | undefined): OutLine[] {
  return (ws ?? []).map((w) => line(`warning: ${w}`, "warn"));
}

function formatTransform(r: TransformResult, source: string | undefined, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r, source);
  const out = [line(pick(r, latex), "result")];
  for (const n of r.notes ?? []) out.push(line(n, "muted"));
  return out;
}

function formatEvaluate(r: EvaluateResult, source: string | undefined): OutLine[] {
  if (!r.ok) return formatError(r, source);
  return [line(r.value === null ? "undefined (non-finite)" : String(r.value), "result")];
}

function formatSolve(r: SolveResult, variable: string, source: string | undefined, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r, source);
  const out: OutLine[] = [];
  switch (r.status) {
    case "solved":
    case "numeric":
      for (const s of r.solutions) {
        let text: string;
        if (!s.exact && s.approx !== null) {
          text = `${variable} ≈ ${num(s.approx)}`;
        } else {
          text = `${variable} = ${pick(s, latex)}`;
        }
        if (s.note) text += `    (${s.note})`;
        out.push(line(text, "result"));
      }
      break;
    case "complex":
      out.push(line("no real solutions; complex roots:", "normal"));
      for (const s of r.solutions) out.push(line(`${variable} = ${pick(s, latex)}`, "result"));
      break;
    case "noRealSolution":
      out.push(line("no real solutions", "normal"));
      break;
    case "allReals":
      out.push(line(`true for all ${variable}`, "normal"));
      break;
    case "unsolved":
      out.push(line(`unable to solve for ${variable}`, "normal"));
      break;
  }
  out.push(...method(r.method), ...warnings(r.warnings));
  return out;
}

function formatSystem(r: SystemResult, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r);
  const out: OutLine[] = [];
  switch (r.status) {
    case "solved":
    case "underdetermined":
      for (const v of r.values) out.push(line(`${v.symbol} = ${pick(v, latex)}`, "result"));
      if (r.free.length) out.push(line(`free: ${r.free.join(", ")}`, "normal"));
      break;
    case "noSolution":
      out.push(line("no solution (inconsistent system)", "normal"));
      break;
    case "unsolved":
      out.push(line("unable to solve the system", "normal"));
      break;
  }
  out.push(...method(r.method), ...warnings(r.warnings));
  return out;
}

function formatIntegrate(r: IntegrateResult, source: string | undefined, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r, source);
  const out = r.solved
    ? [line(`${pick(r, latex)} + C`, "result")]
    : [line("unable to integrate", "normal")];
  out.push(...method(r.method), ...warnings(r.warnings));
  return out;
}

function formatDefinite(r: DefiniteResult, source: string | undefined, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r, source);
  const out: OutLine[] = [];
  if (r.status === "exact") out.push(line(`value = ${pick(r, latex)}`, "result"));
  else if (r.status === "numeric") out.push(line(`value ≈ ${r.approx !== undefined ? num(r.approx) : pick(r, latex)}`, "result"));
  else out.push(line("unable to integrate", "normal"));
  out.push(...method(r.method), ...warnings(r.warnings));
  return out;
}

function formatLimit(r: LimitResult, source: string | undefined, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r, source);
  const out: OutLine[] = [];
  switch (r.status) {
    case "exact":
      out.push(line(`limit = ${pick(r as Rendered, latex)}`, "result"));
      break;
    case "numeric":
      out.push(line(`limit ≈ ${r.approx !== undefined ? num(r.approx) : "?"}`, "result"));
      break;
    case "diverges":
      out.push(line(`limit = ${r.sign > 0 ? "+inf" : r.sign < 0 ? "-inf" : "inf (unsigned)"}`, "result"));
      break;
    case "doesNotExist":
      out.push(line("the limit does not exist", "normal"));
      break;
    case "unsolved":
      out.push(line("unable to determine the limit", "normal"));
      break;
  }
  out.push(...method(r.method), ...warnings(r.warnings));
  return out;
}

function formatSum(r: SumResult, noun: "sum" | "product", latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r);
  const out: OutLine[] = [];
  if (r.status === "exact") out.push(line(`${noun} = ${pick(r as Rendered, latex)}`, "result"));
  else if (r.status === "diverges") out.push(line(`the ${noun} diverges`, "normal"));
  else out.push(line("unable to find a closed form", "normal"));
  out.push(...method(r.method), ...warnings(r.warnings));
  return out;
}

function formatRsolve(r: RsolveResult, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r);
  return [line(`a(n) = ${pick(r, latex)}`, "result"), ...method(r.method), ...warnings(r.warnings)];
}

function formatDsolve(r: DsolveResult, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r);
  const out: OutLine[] = [];
  if (r.implicit) {
    out.push(line(`implicit solution: ${pick(r, latex)} = 0`, "result"));
  } else if (latex) {
    out.push(line(r.latex, "result"));
  } else {
    // A first-order system already carries "x(t) = ...\ny(t) = ..." in plain.
    for (const l of r.plain.split("\n")) out.push(line(r.plain.includes("(t) =") ? l : `y(t) = ${l}`, "result"));
  }
  if (r.transformPlain) out.push(line(`Y(s) = ${latex ? r.transformLatex : r.transformPlain}`, "muted"));
  out.push(...method(r.method), ...warnings(r.warnings));
  return out;
}

function formatFit(r: FitResult, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r);
  return [
    line(pick(r, latex), "result"),
    line(`model: ${r.model}${r.exact ? " (exact)" : ""}`, "muted"),
    line(`R^2: ${num(r.r2, 6)}`, "muted"),
  ];
}

function formatStats(r: StatsResult, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r);
  return r.items.map((it) => line(`${it.label} = ${latex ? it.latex : it.plain}`, "result"));
}

function formatSeq(r: SeqResult, latex: boolean): OutLine[] {
  if (!r.ok) return formatError(r);
  const out: OutLine[] = [line(`pattern: ${r.description}`, "normal")];
  if (r.plain !== undefined && r.latex !== undefined) {
    out.push(line(`a(n) = ${latex ? r.latex : r.plain}   (n = 0, 1, 2, ...)`, "result"));
  }
  if (r.recurrence) out.push(line(`recurrence: ${r.recurrence}`, "muted"));
  if (r.next.length) out.push(line(`next: ${r.next.join(", ")}`, "muted"));
  out.push(...warnings(r.warnings));
  return out;
}

/** Render an Outcome to output lines. */
export function formatOutcome(outcome: Outcome, opts: FormatOptions = {}): OutLine[] {
  if (outcome.kind === "lines") return outcome.lines;
  const latex = opts.latex ?? false;
  const { result, source, variable, noun } = outcome;
  switch (outcome.render) {
    case "transform":
      return formatTransform(result as TransformResult, source, latex);
    case "evaluate":
      return formatEvaluate(result as EvaluateResult, source);
    case "solve":
      return formatSolve(result as SolveResult, variable ?? "x", source, latex);
    case "system":
      return formatSystem(result as SystemResult, latex);
    case "integrate":
      return formatIntegrate(result as IntegrateResult, source, latex);
    case "definite":
      return formatDefinite(result as DefiniteResult, source, latex);
    case "limit":
      return formatLimit(result as LimitResult, source, latex);
    case "sum":
      return formatSum(result as SumResult, noun ?? "sum", latex);
    case "rsolve":
      return formatRsolve(result as RsolveResult, latex);
    case "dsolve":
      return formatDsolve(result as DsolveResult, latex);
    case "fit":
      return formatFit(result as FitResult, latex);
    case "stats":
      return formatStats(result as StatsResult, latex);
    case "seq":
      return formatSeq(result as SeqResult, latex);
  }
}
