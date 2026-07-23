#!/usr/bin/env node
// Entry point. Routes between three modes:
//   - interactive Ink REPL (a TTY with no positional arguments)
//   - one-shot        (positional arguments: run one line, print, exit)
//   - piped batch     (no TTY, no arguments: run each stdin line)
//
// Flags: --latex (LaTeX output), --help/-h, --version.

import { render } from "ink";
import { createEngine, type Engine } from "./engine/engine.js";
import { EngineLoadError } from "./engine/load.js";
import { processLine } from "./core/run.js";
import type { OutLine } from "./core/outline.js";
import { App } from "./ui/App.js";

const USAGE = `MathSolver — Ink terminal app

usage:
  mathsolver-tui                          start the interactive REPL
  mathsolver-tui "simplify 2x + 3x"       run one line and exit
  echo "solve x^2 = 4" | mathsolver-tui   read lines from stdin
  mathsolver-tui --version | --help

options:
  --latex     render results as LaTeX instead of plain text

In the REPL, type "help" for the command list. The engine is the same
MathSolver core used by the web app, loaded from its WASM build
(tools/build_wasm.sh). Set MATHSOLVER_WASM_DIR to point at a build.`;

/** Print output lines, routing errors to stderr like the classic CLI. */
function printLines(lines: OutLine[]): void {
  for (const l of lines) {
    if (l.tone === "error") console.error(l.text);
    else console.log(l.text);
  }
}

function fail(message: string): never {
  console.error(message);
  process.exit(1);
}

async function loadEngineOrExit(): Promise<Engine> {
  try {
    return await createEngine();
  } catch (e) {
    if (e instanceof EngineLoadError) fail(e.message);
    throw e;
  }
}

async function readStdin(): Promise<string> {
  const chunks: Buffer[] = [];
  for await (const chunk of process.stdin) chunks.push(chunk as Buffer);
  return Buffer.concat(chunks).toString("utf8");
}

async function runOneShot(engine: Engine, line: string, latex: boolean): Promise<number> {
  const res = await processLine(engine, line, { latex });
  printLines(res.lines);
  return res.isError ? 1 : 0;
}

async function runBatch(engine: Engine, latex: boolean): Promise<number> {
  const text = await readStdin();
  let exitCode = 0;
  for (const raw of text.split("\n")) {
    if (raw.trim() === "") continue;
    const res = await processLine(engine, raw, { latex });
    printLines(res.lines);
    if (res.isError) exitCode = 1;
    if (res.control === "quit") break;
  }
  return exitCode;
}

async function printVersion(): Promise<number> {
  try {
    const engine = await createEngine();
    const v = await engine.version();
    console.log(`MathSolver ${v.ok ? v.version : "(unknown)"}`);
  } catch (e) {
    if (e instanceof EngineLoadError) {
      console.log("MathSolver terminal app — engine not built (run: bash tools/build_wasm.sh)");
    } else {
      throw e;
    }
  }
  return 0;
}

async function main(): Promise<number> {
  const argv = process.argv.slice(2);
  if (argv.includes("--help") || argv.includes("-h") || argv[0] === "help") {
    console.log(USAGE);
    return 0;
  }
  if (argv.includes("--version")) {
    return printVersion();
  }

  const latex = argv.includes("--latex");
  const positionals = argv.filter((a) => a !== "--latex");

  if (positionals.length > 0) {
    const engine = await loadEngineOrExit();
    return runOneShot(engine, positionals.join(" "), latex);
  }

  // No positionals: interactive when attached to a terminal, else read stdin.
  if (process.stdin.isTTY) {
    const engine = await loadEngineOrExit();
    const v = await engine.version();
    const version = v.ok ? v.version : "(unknown)";
    const { waitUntilExit } = render(<App engine={engine} version={version} latex={latex} />);
    await waitUntilExit();
    return 0;
  }

  const engine = await loadEngineOrExit();
  return runBatch(engine, latex);
}

main().then(
  (code) => process.exit(code),
  (err: unknown) => fail(err instanceof Error ? err.message : String(err)),
);
