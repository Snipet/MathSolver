// Tie parsing, execution, and formatting together for one input line. Shared by
// the interactive Ink UI and the one-shot / piped-batch CLI paths.

import type { Engine } from "../engine/engine.js";
import { executeIntent } from "./execute.js";
import { formatOutcome, type FormatOptions } from "./format.js";
import { helpLines } from "./help.js";
import { parseLine } from "./intent.js";
import { line, type OutLine } from "./outline.js";
import { trim } from "./text.js";

export interface RunResult {
  lines: OutLine[];
  /** UI control signal (quit the app, or clear the scrollback). */
  control?: "quit" | "clear";
  /** True when the result is an error (drives exit codes / stderr routing). */
  isError: boolean;
}

/** Process one input line end-to-end. `rawLine` need not be trimmed. */
export async function processLine(
  engine: Engine,
  rawLine: string,
  opts: FormatOptions = {},
): Promise<RunResult> {
  const input = trim(rawLine);
  const intent = parseLine(input);
  switch (intent.kind) {
    case "empty":
      return { lines: [], isError: false };
    case "help":
      return { lines: helpLines(), isError: false };
    case "quit":
      return { lines: [], control: "quit", isError: false };
    case "clear":
      return { lines: [], control: "clear", isError: false };
    case "note":
      return { lines: [line(`note: ${intent.text}`, "note")], isError: false };
    case "usage":
      return { lines: [line(`error: ${intent.message}`, "error")], isError: true };
    case "job": {
      try {
        const outcome = await executeIntent(engine, intent.job);
        const lines = formatOutcome(outcome, opts);
        return { lines, isError: lines.some((l) => l.tone === "error") };
      } catch (e) {
        // A rejected engine call (e.g. a WASM abort) must not crash the REPL.
        const message = e instanceof Error ? e.message : String(e);
        return { lines: [line(`error: ${message}`, "error")], isError: true };
      }
    }
  }
}
