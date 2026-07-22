// Verb suggestions for a bare console line. When the user types an expression
// or equation with no leading command, the console still does *something*
// (a bare expression is simplified, an equation solved) — but the other verbs
// that apply are not discoverable. This surfaces them: analyze the line and
// offer the handful of verbs worth trying on it, each a click away from
// running as `<verb> <line>`.
import { call } from "../engine";
import { splitAssignment } from "../vars/session";
import { MATH_VERBS } from "./run";

export interface VerbSuggestion {
  /** The verb prepended to the line when the chip is clicked. */
  verb: string;
  /** Chip label. */
  label: string;
  /** Tooltip: what the verb would do. */
  hint: string;
  /** Appended after the line, e.g. " = 0" so `solve` gets an equation. */
  suffix?: string;
}

/** The command a chip runs on `line`: `<verb> <line><suffix>`. */
export function suggestionCommand(s: VerbSuggestion, line: string): string {
  return `${s.verb} ${line.trim()}${s.suffix ?? ""}`;
}

/** Transcendental content — series/limit territory, and factoring rarely helps. */
function isTranscendental(text: string): boolean {
  return /\b(sin|cos|tan|sec|csc|cot|sinh|cosh|tanh|arc|exp|ln|log)\b/.test(text) ||
    /e\s*\^/.test(text);
}

// Heads that already name a command, so the line is not a bare expression.
// (MATH_VERBS covers the math verbs; these are the console/session + plugin
// words the dispatcher and UI also recognize.)
const OTHER_HEADS = new Set([
  "help", "plugins", "vars", "clear", "unset", "quit", "exit",
  "save", "open", "run", "notebooks", "plot", "wave",
]);

/** True when the line already begins with a recognized command or plugin call. */
function hasCommandHead(line: string): boolean {
  const head = line.split(/\s+/, 1)[0] ?? "";
  const lower = head.toLowerCase();
  if (MATH_VERBS.has(lower) || OTHER_HEADS.has(lower)) return true;
  // plugin.command shape, e.g. `dsp.butter`.
  return /^[A-Za-z_][\w]*\.[A-Za-z_]/.test(head);
}

/** A ratio with a variable — worth offering `apart`. */
function looksRational(text: string, hasVar: boolean): boolean {
  return hasVar && text.includes("/");
}

/**
 * Verbs worth trying on a bare line, in priority order. Empty when the line is
 * a command, an assignment, a parse error, or when no alternative to the
 * default action adds value.
 */
export async function suggestVerbs(line: string): Promise<VerbSuggestion[]> {
  const text = line.trim();
  if (!text || splitAssignment(text) || hasCommandHead(text)) return [];

  const a = await call("analyze", [text]);
  if (!a.ok) return [];
  const symbols = "symbols" in a ? a.symbols : [];
  const hasVar = symbols.length > 0;

  // A system or plain equation is already solved by the default Enter action,
  // and no other verb is a clean fit — keep the row quiet rather than noisy.
  if (a.kind === "system" || a.kind === "equation") return [];

  // Pure numeric expression: the default simplifies exactly; offer a decimal.
  if (!hasVar) {
    return [{ verb: "eval", label: "eval", hint: "evaluate to a decimal number" }];
  }

  // Expression in one or more variables: offer the transforms worth trying, in
  // priority order and gated by structure. All are best-effort in the engine,
  // so an inapplicable one simply echoes the input.
  const single = symbols.length === 1;
  const transcendental = isTranscendental(text);
  const rational = looksRational(text, hasVar);

  const FACTOR = { verb: "factor", label: "factor", hint: "factor into irreducible pieces" };
  const EXPAND = { verb: "expand", label: "expand", hint: "multiply everything out" };
  const APART = { verb: "apart", label: "apart", hint: "split into partial fractions" };
  const DIFF = { verb: "diff", label: "diff", hint: "differentiate" };
  const INTEGRATE = { verb: "integrate", label: "integrate", hint: "integrate" };
  const SERIES = { verb: "series", label: "series", hint: "Taylor series about 0" };
  // solve needs an equation and an unambiguous variable, so offer "= 0" only
  // when there is exactly one symbol.
  const SOLVE0 = {
    verb: "solve",
    label: "solve = 0",
    hint: "solve <expr> = 0 for its roots",
    suffix: " = 0",
  };

  const picks: (VerbSuggestion | null)[] = [
    transcendental ? null : FACTOR,
    single ? SOLVE0 : null,
    transcendental ? null : EXPAND,
    rational ? APART : null,
    DIFF,
    INTEGRATE,
    transcendental && single ? SERIES : null,
  ];
  return picks.filter((s): s is VerbSuggestion => s !== null).slice(0, 6);
}
