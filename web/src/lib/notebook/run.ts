// Console command engine: turn one REPL-style line into a rendered cell.
//
// The grammar mirrors the CLI/REPL (apps/main.cpp, DESIGN §10): a leading verb
// with top-level-comma-separated arguments, a bare expression (simplified) or
// equation (solved), a `name := value` assignment, and the session commands
// help / vars / unset / clear. Every math verb dispatches to the same WASM
// bindings the workbench uses and applies the shared `:=` environment, so a
// console line and the equivalent workbench action compute identically.
import { call } from "../engine";
import type { EngineError, PluginMeta } from "../engine/types";
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
  type EnvOverrides,
  type ScopeEnv,
} from "../vars/session";
import type { Boundary } from "../wave/sim";

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
  "apart",
  "laplace",
  "ilaplace",
  "dsolve",
  "series",
  "grad",
  "div",
  "curl",
  "laplacian",
  "jacobian",
  "hessian",
  "vecfield",
  "limit",
  "mlimit",
  "stirling",
  "seq",
  "sum",
  "product",
  "rsolve",
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

// --- plugins (docs/PLUGINS.md) ---------------------------------------------

let catalogPromise: Promise<PluginMeta[]> | null = null;

/** The compiled-in plugin catalog, fetched once per session. */
export function getCatalog(): Promise<PluginMeta[]> {
  catalogPromise ??= call("plugins", []).then((r) => (r.ok ? r.plugins : []));
  return catalogPromise;
}

/** `dsp.butter` -> {plugin: "dsp", command: "butter"}; null if not that shape. */
function splitPluginHead(head: string): { plugin: string; command: string } | null {
  const m = /^([A-Za-z_][A-Za-z0-9_]*)\.([A-Za-z_][A-Za-z0-9_]*)$/.exec(head);
  if (!m) return null;
  return { plugin: m[1], command: m[2] };
}

async function runPluginCommand(
  plugin: string,
  command: string,
  rest: string,
  line: string,
  ov?: EnvOverrides,
  scope?: ScopeEnv,
): Promise<CellResult> {
  const catalog = await getCatalog();
  const meta = catalog.find((p) => p.name === plugin);
  if (!meta) {
    return usage(
      `no plugin named '${plugin}' — run plugins to list what is available`,
    );
  }
  const cmd = meta.commands.find((c) => c.name === command);
  if (!cmd) {
    const names = meta.commands.map((c) => `${plugin}.${c.name}`).join(", ");
    return usage(`${plugin} has no command '${command}' — it offers: ${names}`);
  }
  // Arguments are the top-level comma-separated remainder, re-joined for the
  // engine's CSV convention (plugin args cannot contain commas). A pure
  // identifier argument whose session binding resolves to a closed numeric
  // value is substituted (f_c := 1000 → dsp.butter lowpass, 4, f_c, 48000);
  // anything else — keywords, polynomials, ODEs — passes through verbatim.
  const rawArgs = splitTopLevelCommas(rest);
  const resolved = await Promise.all(
    rawArgs.map(async (arg) => {
      if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(arg)) return arg;
      try {
        const env = await applyEnv(arg, [], "expr", ov, scope);
        if (env.text === arg) return arg;
        const a = await call("analyze", [env.text]);
        if (a.ok && "symbols" in a && a.symbols.length === 0) return env.text;
      } catch {
        /* unresolvable — pass the original through */
      }
      return arg;
    }),
  );
  const args = resolved.join(",");
  const r = await call("pluginCall", [plugin, command, args]);
  if (!r.ok)
    return { kind: "error", message: r.error, input: line, begin: r.begin, end: r.end };
  return { kind: "plugin", plugin, command, result: r };
}

async function pluginsMessage(): Promise<NotebookMessage> {
  const catalog = await getCatalog();
  if (catalog.length === 0)
    return { kind: "message", tone: "muted", lines: ["no plugins compiled in"] };
  const lines: string[] = [];
  for (const p of catalog) {
    lines.push(`${p.name} ${p.version} — ${p.summary}`);
    for (const c of p.commands) lines.push(`  ${c.usage}`);
  }
  return { kind: "message", tone: "info", title: "Plugins", lines };
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
      "apart <expr>[, <var>]               partial fractions",
      "laplace <expr>[, <t>]      f(t) → F(s)   ilaplace <expr>[, <s>]  F(s) → f(t)",
      "dsolve <ode>[, y(0)=v, y'(0)=v, …]  solve an IVP, e.g.",
      "       dsolve y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0",
      "series <expr>[, <var>[, <center>[, <order>]]]   Taylor expansion",
      "limit <expr>, <var>, <point>[, left|right]   (point: number, inf, -inf)",
      "mlimit <expr>, <x>, <a>, <y>, <b>   2-D limit by path sampling",
      "series <expr>, <var>, inf[, <order>]   asymptotic expansion in 1/x",
      "sum <term>, <var>, <lo>, <hi>       product <term>, <var>, <lo>, <hi>",
      "rsolve <recurrence>[, a(0)=v, …]    e.g. rsolve a(n+2)=a(n+1)+a(n), a(0)=0, a(1)=1",
      "grad <f>, <vars…>    div/curl <F1;F2;…>, <vars…>    laplacian <f>, <vars…>",
      "jacobian <F1;F2;…>, <vars…>    hessian <f>, <vars…>",
      "vecfield <Fx>; <Fy>[, <xlo>, <xhi>, <ylo>, <yhi>]   quiver plot",
      "plot <expr>[, <lo>, <hi>]           chart an expression",
      "wave [<columns>][, fixed|free|robin|filtered|absorbing]   interactive 2D wave field (drag to add energy)",
      "<name> := <value>      bind a variable (applies to later lines)",
      "save <name>    save this session's commands as a notebook",
      "open <name>    load a notebook's commands (without running)",
      "run <name>     run a notebook top-to-bottom in a fresh scope",
      "notebooks      list saved notebooks",
      "<plugin>.<command> …   call a plugin (run plugins for the catalog),",
      "                       e.g. dsp.butter lowpass, 4, 1000, 48000",
      "vars      unset <name>      clear      plugins      help",
    ],
  };
}

function varsMessage(scope?: ScopeEnv): NotebookMessage {
  const act = scope ? scope.bindings : vars.active;
  if (act.length === 0)
    return { kind: "message", tone: "muted", lines: ["no variables set"] };
  return {
    kind: "message",
    tone: "info",
    title: scope ? "Variables (notebook scope)" : "Variables",
    lines: act.map((b) => `${symbolToTyped(b.name)} := ${b.value}`),
  };
}

function clearVars(scope?: ScopeEnv): NotebookMessage {
  if (scope) {
    const n = scope.bindings.length;
    scope.bindings.length = 0;
    return {
      kind: "message",
      tone: "muted",
      lines: [`cleared ${n} scope assignment${n === 1 ? "" : "s"}`],
    };
  }
  const n = vars.active.length;
  vars.clearAll();
  return {
    kind: "message",
    tone: "muted",
    lines: [`cleared ${n} assignment${n === 1 ? "" : "s"}`],
  };
}

function unsetVar(rest: string, scope?: ScopeEnv): CellResult {
  const name = rest.trim();
  if (!name) return usage("unset needs a variable name, e.g. unset a");
  const target = normalizeTypedName(name);
  if (scope) {
    const i = scope.bindings.findIndex((b) => b.name === target);
    if (i < 0)
      return { kind: "message", tone: "muted", lines: [`no variable '${name}'`] };
    scope.bindings.splice(i, 1);
    return { kind: "message", tone: "muted", lines: [`unset ${target}`] };
  }
  const row = vars.rows.find(
    (r) => r.status.symbol === target || r.name === name,
  );
  if (!row)
    return { kind: "message", tone: "muted", lines: [`no variable '${name}'`] };
  vars.remove(row.id);
  return { kind: "message", tone: "muted", lines: [`unset ${target}`] };
}

// --- assignment ------------------------------------------------------------

async function runAssignment(
  line: string,
  scope?: ScopeEnv,
): Promise<CellResult> {
  const parts = splitAssignment(line)!;
  const st = await buildAssignPreview(parts, scope?.bindings);
  if (st.error || !st.commit)
    return {
      kind: "error",
      message: st.error ?? "invalid assignment",
      input: parts.value || line,
      begin: st.span?.begin,
      end: st.span?.end,
    };
  if (scope) {
    // Bind into the run's scope; the session store is untouched.
    const b = {
      name: st.commit.symbol,
      value: st.commit.valuePlain,
      symbols: st.commit.symbols,
      kind: st.commit.kind,
    };
    const i = scope.bindings.findIndex((x) => x.name === b.name);
    if (i >= 0) scope.bindings[i] = b;
    else scope.bindings.push(b);
  } else {
    const res = vars.commitAssignment(st.commit);
    if (!res.ok) return { kind: "error", message: res.error, input: line };
  }
  return {
    kind: "assignment",
    name: st.commit.symbol,
    plain: `${st.commit.symbol} := ${st.commit.valuePlain}`,
    latex: st.latex!,
  };
}

// --- math verbs ------------------------------------------------------------

async function runVerb(
  verb: string,
  rest: string,
  ov?: EnvOverrides,
  scope?: ScopeEnv,
): Promise<CellResult> {
  const args = splitTopLevelCommas(rest);
  const expr = args[0] ?? "";
  if (!expr) return usage(`${verb} needs an expression`);

  switch (verb) {
    case "simplify":
    case "expand":
    case "factor": {
      const env = await applyEnv(expr, [], "expr", ov, scope);
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
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      const r = await call("derivative", [env.text, v]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "collect": {
      const v = args[1] ?? (await inferVar(expr));
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      const r = await call("collect", [env.text, v]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "apart": {
      const v = args[1] ?? (await inferVar(expr));
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      const r = await call("apart", [env.text, v]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "dsolve": {
      // args[0] is the ODE, the rest are initial conditions. The prime
      // grammar is handled by the engine; no environment resolution.
      const r = await call("dsolve", [expr, args.slice(1).join(",")]);
      if (!r.ok) return err(expr, r);
      return { kind: "dsolve", result: r, computedFrom: null };
    }
    case "series": {
      if (args.length > 4)
        return usage("usage: series <expr>[, <var>[, <center>[, <order>]]]");
      const v = args[1] ?? (await inferVar(expr));
      const order = args[3] ? Number.parseInt(args[3], 10) : 6;
      if (Number.isNaN(order))
        return usage(`series order must be an integer, got '${args[3]}'`);
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      const r = await call("series", [env.text, v, args[2] ?? "", order]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "stirling": {
      // stirling [<var>[, <terms>]] — no expression argument.
      if (args.length > 2) return usage("usage: stirling [<var>[, <terms>]]");
      const v = args[0] || "x";
      const terms = args[1] ? Number.parseInt(args[1], 10) : 3;
      if (Number.isNaN(terms))
        return usage(`stirling terms must be an integer, got '${args[1]}'`);
      const r = await call("stirling", [v, terms]);
      if (!r.ok) return err(v, r);
      return {
        kind: "transform",
        result: {
          ...r,
          plain: `ln Gamma(${v}) ~ ${r.plain}`,
          latex: `\\ln \\Gamma(${v}) \\sim ${r.latex}`,
        },
        computedFrom: null,
      };
    }
    case "seq": {
      // seq <a0>, <a1>, <a2>, <a3>[, ...] — recognize the pattern.
      if (args.length < 4)
        return usage("usage: seq <a0>, <a1>, <a2>, <a3>[, ...] (at least 4 terms)");
      const r = await call("seq", [args.join(",")]);
      if (!r.ok) return err(args.join(", "), r);
      const notes = [r.description];
      if (r.recurrence) notes.push(`recurrence: ${r.recurrence}`);
      if (r.next.length) notes.push(`next: ${r.next.join(", ")}`);
      notes.push(...r.warnings);
      if (r.plain && r.latex) {
        return {
          kind: "transform",
          result: {
            ok: true,
            plain: `a(n) = ${r.plain}`,
            latex: `a(n) = ${r.latex}`,
            notes,
          },
          computedFrom: null,
        };
      }
      return { kind: "message", tone: "info", lines: notes };
    }
    case "limit": {
      // limit <expr>, <var>, <point>[, left|right]
      if (args.length < 3 || args.length > 4)
        return usage("usage: limit <expr>, <var>, <point>[, left|right]");
      const v = args[1];
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      const r = await call("limit", [env.text, v, args[2], args[3] ?? ""]);
      if (!r.ok) return err(env.text, r);
      if (r.status === "exact" && r.plain && r.latex) {
        return {
          kind: "transform",
          result: { ok: true, plain: r.plain, latex: r.latex },
          computedFrom: env.computedFrom,
        };
      }
      const lines: string[] = [];
      if (r.status === "numeric") lines.push(`limit ≈ ${r.approx}`);
      else if (r.status === "diverges")
        lines.push(`limit = ${r.sign > 0 ? "+∞" : r.sign < 0 ? "−∞" : "∞ (unsigned)"}`);
      else if (r.status === "doesNotExist") lines.push("the limit does not exist");
      else lines.push("unable to determine the limit");
      if (r.method) lines.push(`method: ${r.method}`);
      lines.push(...r.warnings);
      return { kind: "message", tone: "info", lines };
    }
    case "mlimit": {
      if (args.length !== 5)
        return usage("usage: mlimit <expr>, <x>, <a>, <y>, <b>");
      const env = await applyEnv(expr, [args[1], args[3]], "expr", ov, scope);
      const r = await call("mlimit", [env.text, args[1], args[2], args[3], args[4]]);
      if (!r.ok) return err(env.text, r);
      if (r.status === "exact" && r.plain && r.latex) {
        return {
          kind: "transform",
          result: { ok: true, plain: r.plain, latex: r.latex },
          computedFrom: env.computedFrom,
        };
      }
      const lines: string[] = [];
      if (r.status === "numeric") lines.push(`limit ≈ ${r.approx}`);
      else if (r.status === "doesNotExist") lines.push("the limit does not exist");
      else if (r.status === "diverges")
        lines.push(`limit = ${r.sign > 0 ? "+∞" : r.sign < 0 ? "−∞" : "∞"}`);
      else lines.push("unable to determine the limit");
      if (r.method) lines.push(`method: ${r.method}`);
      lines.push(...r.warnings);
      return { kind: "message", tone: "info", lines };
    }
    case "sum":
    case "product": {
      if (args.length !== 4)
        return usage(
          `usage: ${verb} <term>, <var>, <lo>, <hi>` +
            (verb === "sum" ? "   (hi may be inf)" : ""),
        );
      const v = args[1];
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      const r = await call(verb, [env.text, v, args[2], args[3]]);
      if (!r.ok) return err(env.text, r);
      if (r.status === "exact" && r.plain && r.latex) {
        return {
          kind: "transform",
          result: { ok: true, plain: r.plain, latex: r.latex },
          computedFrom: env.computedFrom,
        };
      }
      const lines = [
        r.status === "diverges"
          ? `the ${verb} diverges`
          : "unable to find a closed form",
      ];
      if (r.method) lines.push(`method: ${r.method}`);
      lines.push(...r.warnings);
      return { kind: "message", tone: "info", lines };
    }
    case "rsolve": {
      const r = await call("rsolve", [expr, args.slice(1).join(",")]);
      if (!r.ok) return err(expr, r);
      return {
        kind: "transform",
        result: { ok: true, plain: `a(n) = ${r.plain}`, latex: `a(n) = ${r.latex}` },
        computedFrom: null,
      };
    }
    case "grad":
    case "div":
    case "curl":
    case "laplacian":
    case "jacobian":
    case "hessian": {
      if (args.length < 2)
        return usage(
          `usage: ${verb} <field>, <var>[, <var> …]  (a vector field is ` +
            `';'-separated, e.g. "x*y; y*z; z*x")`,
        );
      const vars = args.slice(1);
      // Resolve session variables into each field component (vars excluded).
      const comps = args[0].split(";").map((c) => c.trim());
      const resolved: string[] = [];
      for (const c of comps) {
        resolved.push((await applyEnv(c, vars, "expr", ov, scope)).text);
      }
      const r = await call("vectorOp", [verb, resolved.join(";"), vars.join(",")]);
      if (!r.ok) return err(args[0], r);
      return { kind: "transform", result: r, computedFrom: null };
    }
    case "vecfield": {
      // vecfield Fx; Fy [, xlo, xhi, ylo, yhi] — quiver plot over x,y.
      const comps = args[0].split(";").map((c) => c.trim());
      if (comps.length !== 2)
        return usage(
          'usage: vecfield <Fx>; <Fy>[, <xlo>, <xhi>, <ylo>, <yhi>]  (two ' +
            "components over x and y)",
        );
      // Bounds: none (default box) or exactly four finite numbers with
      // xlo < xhi and ylo < yhi — partial/malformed bounds are an error,
      // not a silent default.
      let xlo = -2;
      let xhi = 2;
      let ylo = -2;
      let yhi = 2;
      if (args.length > 1) {
        const b = args.slice(1).map((s) => Number.parseFloat(s));
        if (
          b.length !== 4 ||
          !b.every((n) => Number.isFinite(n)) ||
          b[0] >= b[1] ||
          b[2] >= b[3]
        )
          return usage(
            "vecfield bounds must be four finite numbers with xlo < xhi and " +
              "ylo < yhi: vecfield <Fx>; <Fy>, <xlo>, <xhi>, <ylo>, <yhi>",
          );
        [xlo, xhi, ylo, yhi] = b;
      }
      const fx = (await applyEnv(comps[0], ["x", "y"], "expr", ov, scope)).text;
      const fy = (await applyEnv(comps[1], ["x", "y"], "expr", ov, scope)).text;
      const r = await call("sampleField", [
        fx,
        fy,
        "x",
        "y",
        xlo,
        xhi,
        ylo,
        yhi,
        13,
      ]);
      if (!r.ok) return err(args[0], r);
      return { kind: "vecfield", fx: comps[0], fy: comps[1], result: r };
    }
    case "laplace": {
      // Time variable defaults to t (not inferred): L{f(t)} = F(s).
      const v = args[1] ?? "t";
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      const r = await call("laplace", [env.text, v]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "ilaplace": {
      // Frequency variable defaults to s: L^-1{F(s)} = f(t).
      const v = args[1] ?? "s";
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      const r = await call("ilaplace", [env.text, v]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "integrate": {
      if (args.length === 3)
        return usage(
          "a definite integral needs both bounds: integrate <expr>, <var>, <lo>, <hi>",
        );
      const v = args[1] ?? (await inferVar(expr));
      const env = await applyEnv(expr, [v], "expr", ov, scope);
      if (args.length >= 4) {
        const from = (await applyEnv(args[2], [], "expr", ov, scope)).text;
        const to = (await applyEnv(args[3], [], "expr", ov, scope)).text;
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
      const env = await applyEnv(expr, boundNames(pairs), "expr", ov, scope);
      const r = await call("evaluate", [env.text, pairs.join(",")]);
      if (!r.ok) return err(env.text, r);
      return { kind: "evaluate", result: r, computedFrom: env.computedFrom };
    }
    case "subs": {
      const pairs = args.slice(1);
      if (pairs.length === 0)
        return usage("subs needs at least one substitution, e.g. subs a*x + 3, a=2");
      const env = await applyEnv(expr, boundNames(pairs), "expr", ov, scope);
      const r = await call("subs", [env.text, pairs.join(","), true]);
      if (!r.ok) return err(env.text, r);
      return { kind: "transform", result: r, computedFrom: env.computedFrom };
    }
    case "solve":
      return runSolve(rest, args, ov, scope);
    default:
      return usage(`unknown command '${verb}'`);
  }
}

/** Bound argument: a plain number, or any constant expression ("2pi"). */
async function evalBound(text: string, fallback: number): Promise<number | null> {
  if (!text) return fallback;
  const n = Number(text);
  if (Number.isFinite(n)) return n;
  const r = await call("evaluate", [text, ""]);
  return r.ok && r.value !== null && Number.isFinite(r.value) ? r.value : null;
}

async function runPlot(
  rest: string,
  ov?: EnvOverrides,
  scope?: ScopeEnv,
): Promise<CellResult> {
  const args = splitTopLevelCommas(rest);
  const expr = args[0] ?? "";
  if (!expr) return usage("plot needs an expression, e.g. plot sin(x)/x, -20, 20");
  const lo = await evalBound(args[1] ?? "", -10);
  const hi = await evalBound(args[2] ?? "", 10);
  if (lo === null || hi === null || !(hi > lo))
    return usage("plot bounds must be numbers (or constants like 2pi) with lo < hi");
  const env = await applyEnv(expr, [], "expr", ov, scope);
  const a = await call("analyze", [env.text]);
  if (!a.ok) return err(env.text, a);
  if (a.kind !== "expression")
    return usage("plot takes an expression — solve equations with solve");
  if (a.symbols.length > 1)
    return usage(
      `plot needs at most one free variable, got: ${a.symbols.join(", ")}`,
    );
  const v = a.symbols[0] ?? "x";
  const n = 400;
  const r = await call("sample", [env.text, v, lo, hi, n]);
  if (!r.ok) return err(env.text, r);
  const xs = Array.from({ length: n }, (_, k) => lo + ((hi - lo) * k) / (n - 1));
  return {
    kind: "chart",
    title: `plot ${env.text}`,
    x: xs,
    series: [{ label: env.text, ys: r.ys }],
    xlabel: v,
    ylabel: "",
  };
}

async function runSolve(
  rest: string,
  args: string[],
  ov?: EnvOverrides,
  scope?: ScopeEnv,
): Promise<CellResult> {
  const target = args[0] ?? "";
  if (hasTopLevelSemicolon(target)) {
    let sv = args.slice(1);
    if (sv.length === 0) {
      const a = await call("analyze", [swapEqSegments(target, scope?.bindings).text]);
      sv = a.ok && "symbols" in a ? a.symbols : [];
    }
    const env = await applyEnv(target, sv, "solve", ov, scope);
    const r = await call("solveSystem", [env.text, sv.join(",")]);
    if (!r.ok) return err(env.text, r);
    return { kind: "system", result: r, computedFrom: env.computedFrom };
  }
  const v = args[1] ?? (await inferVar(target));
  const env = await applyEnv(target, [v], "solve", ov, scope);
  const r = await call("solve", [env.text, v, -100, 100, false]);
  if (!r.ok) return err(env.text, r);
  return { kind: "solve", variable: v, result: r, computedFrom: env.computedFrom };
}

// --- interactive tools -----------------------------------------------------

/** Map friendly boundary spellings to the sim's canonical names. */
function normalizeBoundary(raw: string | undefined): Boundary | null {
  const s = (raw ?? "").trim().toLowerCase();
  if (!s) return null;
  if (["fixed", "reflect", "clamp", "clamped", "dirichlet"].includes(s)) return "fixed";
  if (["free", "neumann"].includes(s)) return "free";
  if (["robin", "impedance", "mixed", "springy"].includes(s)) return "robin";
  if (["filtered", "filter", "frequency"].includes(s)) return "filtered";
  if (["absorbing", "absorb", "open", "mur"].includes(s)) return "absorbing";
  return null;
}

/**
 * `wave [<columns>][, fixed|free|robin|filtered|absorbing]` — a live 2-D wave
 * field cell. Like plot, this produces plain-data (a serializable seed)
 * rather than calling the engine; WaveField owns the simulation.
 */
function runWave(rest: string): CellResult {
  const args = rest ? splitTopLevelCommas(rest) : [];
  let columns = 180;
  if (args[0]) {
    const n = Number(args[0]);
    if (!Number.isFinite(n))
      return usage("wave [<columns>][, fixed|free|robin|filtered|absorbing]");
    columns = Math.max(48, Math.min(320, Math.round(n)));
  }
  const b = normalizeBoundary(args[1]);
  if (args[1] && b === null)
    return usage("wave boundary must be one of: fixed, free, robin, filtered, absorbing");
  return {
    kind: "wave",
    columns,
    speed: 0.5,
    damping: 0.08,
    boundary: b ?? "fixed",
  };
}

// --- bare input ------------------------------------------------------------

async function runBare(
  line: string,
  ov?: EnvOverrides,
  scope?: ScopeEnv,
): Promise<CellResult> {
  const a = await call("analyze", [line]);
  if (!a.ok) return err(line, a);
  if (a.kind === "system") return runSolve(line, [line], ov, scope);
  if (a.kind === "equation") {
    if (a.symbols.length !== 1)
      return usage(
        `this equation has ${a.symbols.length} variables — say which to solve for, e.g. solve ${line}, ${a.symbols[0] ?? "x"}`,
      );
    return runSolve(line, [line], ov, scope);
  }
  // expression: simplify
  const env = await applyEnv(line, [], "expr", ov, scope);
  const r = await call("simplify", [env.text]);
  if (!r.ok) return err(env.text, r);
  return { kind: "transform", result: r, computedFrom: env.computedFrom };
}

/**
 * Evaluate one console line to a renderable cell result. Never throws.
 * `ov` (cell slider overrides) shadows numeric session bindings during
 * environment resolution, so a cell can be re-run with tweaked values.
 */
export async function runLine(
  raw: string,
  ov?: EnvOverrides,
  scope?: ScopeEnv,
): Promise<CellResult> {
  const line = raw.trim();
  if (!line) return { kind: "message", tone: "muted", lines: ["(empty)"] };
  try {
    if (splitAssignment(line)) return await runAssignment(line, scope);

    const { head, rest } = splitHead(line);
    const pluginHead = splitPluginHead(head);
    if (pluginHead)
      return await runPluginCommand(
        pluginHead.plugin,
        pluginHead.command,
        rest,
        line,
        ov,
        scope,
      );
    const verb = head.toLowerCase();
    switch (verb) {
      case "help":
        return helpMessage();
      case "plugins":
        return await pluginsMessage();
      case "vars":
        return varsMessage(scope);
      case "clear":
        return clearVars(scope);
      case "unset":
        return unsetVar(rest, scope);
      case "quit":
      case "exit":
        return {
          kind: "message",
          tone: "muted",
          lines: ["nothing to quit — this is a browser console; your work persists automatically"],
        };
    }
    if (verb === "plot") {
      if (!rest) return usage("plot needs an expression, e.g. plot sin(x)/x, -20, 20");
      return await runPlot(rest, ov, scope);
    }
    if (verb === "wave") return runWave(rest);
    if (MATH_VERBS.has(verb)) {
      if (!rest) return usage(`${verb} needs an expression`);
      return await runVerb(verb, rest, ov, scope);
    }
    return await runBare(line, ov, scope);
  } catch (e) {
    return {
      kind: "error",
      message: e instanceof Error ? e.message : String(e),
      input: line,
    };
  }
}
