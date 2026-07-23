// Parse one REPL line into a declarative Intent. This is a pure function (no
// engine, no I/O) so the grammar can be unit-tested exhaustively. It mirrors
// the dispatch in apps/main.cpp (repl_line / repl_command); the notable
// difference is that session assignments (`name := value`, `vars`, `unset`,
// `clear`-as-forget) are not modelled — the terminal app has no session
// environment yet — so those inputs resolve to a friendly note.

import {
  findInequality,
  hasTopLevelEquals,
  hasTopLevelSemicolon,
  isSymbolName,
  parseIntArg,
  splitCommas,
  trim,
  type IneqOp,
} from "./text.js";

/** A unit of work the engine can execute. `t` selects the executor branch. */
export type Job =
  | { t: "simplify"; input: string }
  | { t: "expand"; input: string }
  | { t: "factor"; input: string }
  | { t: "together"; input: string }
  | { t: "latex"; input: string }
  | { t: "collect"; input: string; variable: string }
  | { t: "apart"; input: string; variable: string }
  | { t: "cancel"; input: string; variable: string | null }
  | { t: "derivative"; input: string; variable: string | null }
  | { t: "laplace"; input: string; variable: string }
  | { t: "ilaplace"; input: string; variable: string }
  | { t: "subs"; input: string; assignments: string }
  | { t: "evaluate"; input: string; bindings: string }
  | { t: "vector"; op: string; fieldSemi: string; varsCsv: string }
  | { t: "stirling"; variable: string; terms: number }
  | { t: "series"; input: string; variable: string; center: string; order: number }
  | { t: "nt"; name: NtName; arg: string }
  | { t: "solve"; input: string; variable: string | null }
  | { t: "bareEquation"; input: string }
  | { t: "solveSystem"; input: string; varsCsv: string }
  | { t: "solveIneq"; lhs: string; rhs: string; op: IneqOp; variable: string }
  | { t: "integrate"; input: string; variable: string | null; bounds: [string, string] | null }
  | { t: "limit"; input: string; variable: string; point: string; direction: string }
  | { t: "mlimit"; input: string; xVar: string; a: string; yVar: string; b: string }
  | { t: "sum"; noun: "sum" | "product"; term: string; variable: string; lo: string; hi: string }
  | { t: "rsolve"; recurrence: string; conditionsCsv: string }
  | { t: "dsolve"; ode: string; conditionsCsv: string }
  | { t: "fit"; data: string; model: string; degree: string }
  | { t: "stats"; data: string }
  | { t: "seq"; termsCsv: string };

export type NtName =
  | "gcd"
  | "lcm"
  | "isprime"
  | "nextprime"
  | "divisors"
  | "totient"
  | "cfrac"
  | "mod"
  | "powmod"
  | "modinv"
  | "crt";

export type Intent =
  | { kind: "empty" }
  | { kind: "help" }
  | { kind: "quit" }
  | { kind: "clear" }
  | { kind: "note"; text: string }
  | { kind: "usage"; message: string }
  | { kind: "job"; job: Job };

const REPL_COMMANDS = new Set([
  "simplify", "expand", "factor", "cancel", "together", "solve", "diff",
  "integrate", "eval", "latex", "debug", "subs", "collect", "laplace",
  "ilaplace", "apart", "dsolve", "series", "grad", "div", "curl", "laplacian",
  "jacobian", "hessian", "limit", "sum", "product", "rsolve", "mlimit",
  "stirling", "seq", "fit", "regress", "stats", "gcd", "lcm", "isprime",
  "nextprime", "divisors", "totient", "cfrac", "mod", "powmod", "modinv", "crt",
]);

const VECTOR_OPS = new Set(["grad", "div", "curl", "laplacian", "jacobian", "hessian"]);

const NO_SESSION_NOTE =
  "session variables (`:=`, `vars`, `unset`) aren't supported in the Ink app yet — " +
  "use the classic REPL (build/mathsolver) when you need them.";

function usage(message: string): Intent {
  return { kind: "usage", message };
}
function job(j: Job): Intent {
  return { kind: "job", job: j };
}

/** Route a bare (command-less) line to solve/system/inequality/simplify. */
function parseBare(line: string): Intent {
  const ineq = findInequality(line);
  if (ineq) {
    return job({ t: "solveIneq", lhs: trim(ineq.lhs), rhs: trim(ineq.rhs), op: ineq.op, variable: "" });
  }
  if (hasTopLevelSemicolon(line)) {
    return job({ t: "solveSystem", input: line, varsCsv: "" });
  }
  if (hasTopLevelEquals(line)) {
    return job({ t: "bareEquation", input: line });
  }
  return job({ t: "simplify", input: line });
}

/** Parse a command word plus its argument string (already command-stripped). */
function parseCommand(word: string, rest: string): Intent {
  // Vector calculus: "<field>, <var>[, <var> ...]" (field is ';'-separated).
  if (VECTOR_OPS.has(word)) {
    const parts = splitCommas(rest);
    if (parts.length < 2) {
      return usage(
        `usage: ${word} <field>, <var>[, <var> ...]   ` +
          `(a vector field is ';'-separated, e.g. "x*y; y*z; z*x")`,
      );
    }
    return job({ t: "vector", op: word, fieldSemi: parts[0]!, varsCsv: parts.slice(1).join(",") });
  }

  if (word === "rsolve") {
    if (trim(rest) === "") {
      return usage(
        "usage: rsolve <recurrence>[, a(0)=v, ...]   e.g. " +
          "rsolve a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1",
      );
    }
    const parts = splitCommas(rest);
    return job({ t: "rsolve", recurrence: parts[0]!, conditionsCsv: parts.slice(1).join(",") });
  }

  if (word === "sum" || word === "product") {
    const parts = splitCommas(rest);
    if (parts.length !== 4) return usage(`usage: ${word} <term>, <variable>, <lo>, <hi>`);
    return job({
      t: "sum",
      noun: word,
      term: parts[0]!,
      variable: parts[1]!,
      lo: parts[2]!,
      hi: parts[3]!,
    });
  }

  if (word === "seq") {
    const parts = splitCommas(rest);
    if (parts.length < 4) {
      return usage("usage: seq <a0>, <a1>, <a2>, <a3>[, ...]   (at least 4 terms)");
    }
    return job({ t: "seq", termsCsv: rest });
  }

  if (word === "stirling") {
    const parts = splitCommas(rest);
    if (parts.length > 2) return usage("usage: stirling [<variable>[, <terms>]]");
    const variable = parts[0] ?? "";
    let terms = 3;
    if (parts.length > 1 && trim(parts[1]!) !== "") {
      const n = parseIntArg(parts[1]!);
      if (n === null) return usage(`stirling terms must be an integer, got '${trim(parts[1]!)}'`);
      terms = n;
    }
    return job({ t: "stirling", variable, terms });
  }

  if (word === "dsolve") {
    if (trim(rest) === "") {
      return usage(
        "usage: dsolve <ode>[, y(0)=v, y'(0)=v, ...]   e.g. " +
          "dsolve y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0",
      );
    }
    const parts = splitCommas(rest);
    return job({ t: "dsolve", ode: parts[0]!, conditionsCsv: parts.slice(1).join(",") });
  }

  if (word === "fit" || word === "regress") {
    let data = rest;
    let model = "";
    let degree = "";
    const bar = rest.lastIndexOf("|");
    if (bar !== -1) {
      data = rest.slice(0, bar);
      const opts = trim(rest.slice(bar + 1));
      const sp = opts.search(/\s/);
      if (sp === -1) {
        model = opts;
      } else {
        model = opts.slice(0, sp);
        degree = trim(opts.slice(sp + 1));
      }
    }
    if (trim(data) === "") return usage("usage: fit <x,y; x,y; ...> [| <model> [<degree>]]");
    return job({ t: "fit", data, model, degree });
  }

  if (word === "stats") {
    if (trim(rest) === "") return usage("usage: stats <v1, v2, v3, ...>");
    return job({ t: "stats", data: rest });
  }

  if (word === "gcd" || word === "lcm" || word === "isprime" || word === "nextprime" ||
      word === "divisors" || word === "totient") {
    if (trim(rest) === "") {
      const many = word === "gcd" || word === "lcm";
      return usage(`usage: ${word} <integer${many ? ", integer, ..." : ""}>`);
    }
    return job({ t: "nt", name: word, arg: rest });
  }

  if (word === "cfrac") {
    if (trim(rest) === "") return usage("usage: cfrac <rational | sqrt(n) | real>");
    return job({ t: "nt", name: "cfrac", arg: rest });
  }

  if (word === "mod" || word === "powmod" || word === "modinv" || word === "crt") {
    if (trim(rest) === "") return usage(`usage: ${word} <arguments>`);
    return job({ t: "nt", name: word, arg: rest });
  }

  if (word === "debug") {
    return {
      kind: "note",
      text: "debug (s-expression dump) isn't available in the Ink app; use build/mathsolver.",
    };
  }

  // The comma-delimited general path (input is the first segment).
  const parts = splitCommas(rest);
  if (parts[0] === "" || parts.length === 0) {
    const suffix =
      word === "solve" || word === "diff" || word === "collect"
        ? "[, <variable>]"
        : word === "subs"
          ? ", <name>=<expr>[, ...]"
          : "";
    return usage(`usage: ${word} <input>${suffix}`);
  }
  const input = parts[0]!;
  const rest1 = parts.slice(1);

  switch (word) {
    case "solve": {
      if (hasTopLevelSemicolon(input)) {
        return job({ t: "solveSystem", input, varsCsv: rest1.join(",") });
      }
      const ineq = findInequality(input);
      if (ineq) {
        return job({
          t: "solveIneq",
          lhs: trim(ineq.lhs),
          rhs: trim(ineq.rhs),
          op: ineq.op,
          variable: rest1[0] ?? "",
        });
      }
      if (rest1.length > 1) return usage("too many arguments: usage: solve <input>[, <variable>]");
      return job({ t: "solve", input, variable: rest1[0] ?? null });
    }
    case "diff":
      if (rest1.length > 1) return usage("too many arguments: usage: diff <input>[, <variable>]");
      return job({ t: "derivative", input, variable: rest1[0] ?? null });
    case "collect":
      if (rest1.length > 1)
        return usage("too many arguments: usage: collect <input>[, <variable>]");
      return job({ t: "collect", input, variable: rest1[0] ?? "" });
    case "apart":
      if (rest1.length > 1) return usage("too many arguments: usage: apart <input>[, <variable>]");
      return job({ t: "apart", input, variable: rest1[0] ?? "" });
    case "cancel":
      if (rest1.length > 1) return usage("too many arguments: usage: cancel <input>[, <variable>]");
      return job({ t: "cancel", input, variable: rest1[0] ?? null });
    case "integrate": {
      if (parts.length === 3 || parts.length > 4) {
        return usage("usage: integrate <expression>[, <variable>[, <lo>, <hi>]]");
      }
      const variable = parts.length >= 2 ? parts[1]! : null;
      const bounds: [string, string] | null = parts.length === 4 ? [parts[2]!, parts[3]!] : null;
      return job({ t: "integrate", input, variable, bounds });
    }
    case "mlimit": {
      if (parts.length !== 5) return usage("usage: mlimit <expression>, <x var>, <a>, <y var>, <b>");
      return job({ t: "mlimit", input, xVar: parts[1]!, a: parts[2]!, yVar: parts[3]!, b: parts[4]! });
    }
    case "limit": {
      if (parts.length < 3 || parts.length > 4) {
        return usage("usage: limit <expression>, <variable>, <point>[, left|right]");
      }
      return job({ t: "limit", input, variable: parts[1]!, point: parts[2]!, direction: parts[3] ?? "" });
    }
    case "series": {
      if (parts.length > 4) {
        return usage("usage: series <expression>[, <variable>[, <center>[, <order>]]]");
      }
      const orderText = parts[3] ?? "";
      let order = 6;
      if (trim(orderText) !== "") {
        const n = parseIntArg(orderText);
        if (n === null) return usage(`series order must be an integer, got '${trim(orderText)}'`);
        order = n;
      }
      return job({ t: "series", input, variable: parts[1] ?? "", center: parts[2] ?? "", order });
    }
    case "laplace":
      if (rest1.length > 1) return usage("too many arguments: usage: laplace <input>[, <variable>]");
      return job({ t: "laplace", input, variable: rest1[0] ?? "" });
    case "ilaplace":
      if (rest1.length > 1)
        return usage("too many arguments: usage: ilaplace <input>[, <variable>]");
      return job({ t: "ilaplace", input, variable: rest1[0] ?? "" });
    case "eval":
      return job({ t: "evaluate", input, bindings: rest1.join(",") });
    case "subs":
      if (rest1.length === 0) {
        return usage("subs needs at least one name=expression argument (e.g. x=y+1)");
      }
      return job({ t: "subs", input, assignments: rest1.join(",") });
    case "simplify":
      return job({ t: "simplify", input });
    case "expand":
      return job({ t: "expand", input });
    case "factor":
      return job({ t: "factor", input });
    case "together":
      return job({ t: "together", input });
    case "latex":
      return job({ t: "latex", input });
    default:
      // Unreachable: `word` was checked against REPL_COMMANDS by the caller.
      return usage(`unknown command '${word}'`);
  }
}

/** Parse a single input line (assumed already trimmed) into an Intent. */
export function parseLine(line: string): Intent {
  if (line === "") return { kind: "empty" };
  if (line === "help") return { kind: "help" };
  if (line === "quit" || line === "exit") return { kind: "quit" };

  // Assignment is recognized before command dispatch, mirroring main.cpp.
  const assignAt = line.indexOf(":=");
  if (assignAt !== -1 && trim(line.slice(0, assignAt)) !== "") {
    return { kind: "note", text: NO_SESSION_NOTE };
  }

  // Leading alphabetic run selects command mode when it is a known command.
  const m = /^[A-Za-z]+/.exec(line);
  const word = m ? m[0] : "";
  const wordEnd = word.length;
  const terminated = wordEnd === line.length || /\s/.test(line[wordEnd]!);

  if (line === "vars") return { kind: "note", text: NO_SESSION_NOTE };
  if (line === "clear") return { kind: "clear" };
  if (word === "unset" && terminated) return { kind: "note", text: NO_SESSION_NOTE };

  if (terminated && REPL_COMMANDS.has(word)) {
    return parseCommand(word, trim(line.slice(wordEnd)));
  }
  return parseBare(line);
}
