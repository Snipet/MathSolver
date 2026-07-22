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

  // Expression in one or more variables: offer the common transforms. All are
  // best-effort in the engine, so an inapplicable one simply echoes the input.
  const out: VerbSuggestion[] = [
    { verb: "factor", label: "factor", hint: "factor into irreducible pieces" },
    { verb: "expand", label: "expand", hint: "multiply everything out" },
    { verb: "diff", label: "diff", hint: "differentiate" },
    { verb: "integrate", label: "integrate", hint: "integrate" },
  ];
  if (looksRational(text, hasVar)) {
    out.splice(2, 0, {
      verb: "apart",
      label: "apart",
      hint: "split into partial fractions",
    });
  }
  return out;
}
