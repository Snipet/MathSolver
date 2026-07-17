// Console command engine: turn one REPL-style line into a rendered cell.
//
// The grammar mirrors the CLI/REPL (apps/main.cpp, DESIGN §10): a leading verb
// with top-level-comma-separated arguments, a bare expression (simplified) or
// equation (solved), a `name := value` assignment, and the session commands
// help / vars / unset / clear. Every math verb dispatches to the same WASM
// bindings the workbench uses and applies the shared `:=` environment, so a
// console line and the equivalent workbench action compute identically.
import { call } from "../engine";
import type { EngineError } from "../engine/types";
import { hasTopLevelSemicolon, splitTopLevelCommas } from "../format";
import type { Outcome } from "../outcome";
import { vars } from "../vars.svelte";
import { symbolToTyped } from "../vars/resolve";
import { normalizeTypedName } from "../vars/validate";
import {
  applyEnv,
  buildAssignPreview,
  splitAssignment,
  swapEqSegments,
} from "../vars/session";

/** A console-only informational line (help, vars listing, unset/clear echo). */
export interface NotebookMessage {
  kind: "message";
  tone: "info" | "muted";
  title?: string;
  lines: string[];
}

/** What one evaluated cell renders: a normal result card, or a message. */
export type CellResult = Outcome | NotebookMessage;

const MATH_VERBS = new Set([
  "simplify",
  "expand",
  "factor",
  "latex",
  "solve",
  "diff",
  "derivative",
  "integrate",
  "eval",
  "evaluate",
  "subs",
  "collect",
]);

function err(input: string, e: EngineError): Outcome {
  return { kind: "error", message: e.error, input, begin: e.begin, end: e.end };
}

function usage(message: string): Outcome {
  return { kind: "error", message, input: "" };
}

/** First whitespace-delimited token and the remainder (both trimmed). */
function splitHead(line: string): { head: string; rest: string } {
  const m = /^(\S+)\s*([\s\S]*)$/.exec(line);
  if (!m) return { head: line, rest: "" };
  return { head: m[1], rest: m[2].trim() };
}

/**
 * Pick a default variable when the verb was given none: prefer the
 * conventional `x`, else the first free symbol (alphabetical, as `analyze`
 * reports), else `x`.
 */
async function inferVar(exprText: string): Promise<string> {
  const a = await call("analyze", [exprText]);
  if (a.ok && "symbols" in a && a.symbols.length > 0)
    return a.symbols.includes("x") ? "x" : a.symbols[0];
  return "x";
}

/** Left-hand names of `x=...` argument pairs (so the environment won't override them). */
function boundNames(pairs: string[]): string[] {
  return pairs.map((p) => p.split("=")[0].trim()).filter(Boolean);
}

// --- session commands ------------------------------------------------------

function helpMessage(): NotebookMessage {
  return {
    kind: "message",
    tone: "info",
    title: "Console commands",
    lines: [
      "A bare expression is simplified; a bare equation is solved.",
      "simplify <expr>        expand <expr>        factor <expr>",
      "diff <expr>[, <var>]",
      "integrate <expr>[, <var>[, <lo>, <hi>]]",
      "solve <equation>[, <var>]",
      "solve <eq>; <eq>[; …][, <var>, …]   (linear system)",
      "eval <expr>, x=1[, y=2 …]           subs <expr>, x=y+1[, …]",
      "collect <expr>[, <var>]             latex <expr>",
      "<name> := <value>      bind a variable (applies to later lines)",
      "vars      unset <name>      clear      help",
    ],
  };
}

function varsMessage(): NotebookMessage {
  const act = vars.active;
  if (act.length === 0)
    return { kind: "message", tone: "muted", lines: ["no variables set"] };
  return {
    kind: "message",
    tone: "info",
    title: "Variables",
    lines: act.map((b) => `${symbolToTyped(b.name)} := ${b.value}`),
  };
}

function clearVars(): NotebookMessage {
  const n = vars.active.length;
  vars.clearAll();
  return {
    kind: "message",
    tone: "muted",
    lines: [`cleared ${n} assignment${n === 1 ? "" : "s"}`],
  };
}

function unsetVar(rest: string): CellResult {
  const name = rest.trim();
  if (!name) return usage("unset needs a variable name, e.g. unset a");
  const target = normalizeTypedName(name);
  const row = vars.rows.find(
    (r) => r.status.symbol === target || r.name === name,
  );
  if (!row)
    return { kind: "message", tone: "muted", lines: [`no variable '${name}'`] };
  vars.remove(row.id);
  return { kind: "message", tone: "muted", lines: [`unset ${target}`] };
}

// --- assignment ------------------------------------------------------------

async function runAssignment(line: string): Promise<CellResult> {
  const parts = splitAssignment(line)!;
  const st = await buildAssignPreview(parts);
  if (st.error || !st.commit)
    return {
      kind: "error",
      message: st.error ?? "invalid assignment",
      input: parts.value || line,
      begin: st.span?.begin,
      end: st.span?.end,
    };
  const res = vars.commitAssignment(st.commit);
  if (!res.ok) return { kind: "error", message: res.error, input: line };
  return {
    kind: "assignment",
    name: st.commit.symbol,
    plain: `${st.commit.symbol} := ${st.commit.valuePlain}`,
    latex: st.latex!,
  };
}

// --- math verbs ------------------------------------------------------------

async function runVerb(verb: string, rest: string): Promise<CellResult> {
  const args = splitTopLevelCommas(rest);
  const expr = args[0] ?? "";
  if (!expr) return usage(`${verb} needs an expression`);

  switch (verb) {
    case "simplify":
    case "expand":
    case "factor": {
      const env = await applyEnv(expr, [], "expr");
      const r = await call(verb, [env.text]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "latex": {
      // A display verb: never resolves the environment (§10).
      const r = await call("latex", [expr]);
      if (!r.ok) return err(expr, r);
      return { kind: "transform", result: r, computedFrom: null };
    }
    case "diff":
    case "derivative": {
      const v = args[1] ?? (await inferVar(expr));
      const env = await applyEnv(expr, [v], "expr");
      const r = await call("derivative", [env.text, v]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "collect": {
      const v = args[1] ?? (await inferVar(expr));
      const env = await applyEnv(expr, [v], "expr");
      const r = await call("collect", [env.text, v]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "integrate": {
      if (args.length === 3)
        return usage(
          "a definite integral needs both bounds: integrate <expr>, <var>, <lo>, <hi>",
        );
      const v = args[1] ?? (await inferVar(expr));
      const env = await applyEnv(expr, [v], "expr");
      if (args.length >= 4) {
        const from = (await applyEnv(args[2], [], "expr")).text;
        const to = (await applyEnv(args[3], [], "expr")).text;
        const r = await call("integrateDefinite", [env.text, v, from, to]);
        if (!r.ok) return err(env.text, r);
        return {
          kind: "definite",
          from,
          to,
          result: r,
          computedFrom: env.computedFrom,
        };
      }
      const r = await call("integrate", [env.text, v]);
      if (!r.ok) return err(env.text, r);
      return {
        kind: "integral",
        variable: v,
        result: r,
        computedFrom: env.computedFrom,
      };
    }
    case "eval":
    case "evaluate": {
      const pairs = args.slice(1);
      const env = await applyEnv(expr, boundNames(pairs), "expr");
      const r = await call("evaluate", [env.text, pairs.join(",")]);
      if (!r.ok) return err(env.text, r);
      return { kind: "evaluate", result: r, computedFrom: env.computedFrom };
    }
    case "subs": {
      const pairs = args.slice(1);
      if (pairs.length === 0)
        return usage("subs needs at least one substitution, e.g. subs a*x + 3, a=2");
      const env = await applyEnv(expr, boundNames(pairs), "expr");
      const r = await call("subs", [env.text, pairs.join(","), true]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "solve":
      return runSolve(rest, args);
    default:
      return usage(`unknown command '${verb}'`);
  }
}

async function runSolve(rest: string, args: string[]): Promise<CellResult> {
  const target = args[0] ?? "";
  if (hasTopLevelSemicolon(target)) {
    let sv = args.slice(1);
    if (sv.length === 0) {
      const a = await call("analyze", [swapEqSegments(target).text]);
      sv = a.ok && "symbols" in a ? a.symbols : [];
    }
    const env = await applyEnv(target, sv, "solve");
    const r = await call("solveSystem", [env.text, sv.join(",")]);
    if (!r.ok) return err(env.text, r);
    return { kind: "system", result: r, computedFrom: env.computedFrom };
  }
  const v = args[1] ?? (await inferVar(target));
  const env = await applyEnv(target, [v], "solve");
  const r = await call("solve", [env.text, v, -100, 100, false]);
  if (!r.ok) return err(env.text, r);
  return { kind: "solve", variable: v, result: r, computedFrom: env.computedFrom };
}

// --- bare input ------------------------------------------------------------

async function runBare(line: string): Promise<CellResult> {
  const a = await call("analyze", [line]);
  if (!a.ok) return err(line, a);
  if (a.kind === "system") return runSolve(line, [line]);
  if (a.kind === "equation") {
    if (a.symbols.length !== 1)
      return usage(
        `this equation has ${a.symbols.length} variables — say which to solve for, e.g. solve ${line}, ${a.symbols[0] ?? "x"}`,
      );
    return runSolve(line, [line]);
  }
  // expression: simplify
  const env = await applyEnv(line, [], "expr");
  const r = await call("simplify", [env.text]);
  if (!r.ok) return err(env.text, r);
  return { kind: "transform", result: r, computedFrom: env.computedFrom };
}

/** Evaluate one console line to a renderable cell result. Never throws. */
export async function runLine(raw: string): Promise<CellResult> {
  const line = raw.trim();
  if (!line) return { kind: "message", tone: "muted", lines: ["(empty)"] };
  try {
    if (splitAssignment(line)) return await runAssignment(line);

    const { head, rest } = splitHead(line);
    const verb = head.toLowerCase();
    switch (verb) {
      case "help":
        return helpMessage();
      case "vars":
        return varsMessage();
      case "clear":
        return clearVars();
      case "unset":
        return unsetVar(rest);
      case "quit":
      case "exit":
        return {
          kind: "message",
          tone: "muted",
          lines: ["nothing to quit — this is a browser console; your work persists automatically"],
        };
    }
    if (MATH_VERBS.has(verb)) {
      if (!rest) return usage(`${verb} needs an expression`);
      return await runVerb(verb, rest);
    }
    return await runBare(line);
  } catch (e) {
    return {
      kind: "error",
      message: e instanceof Error ? e.message : String(e),
      input: line,
    };
  }
}
