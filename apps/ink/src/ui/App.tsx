// The interactive Ink REPL. Completed exchanges are printed permanently via
// <Static> (natural terminal scrollback); the live footer holds the prompt, a
// debounced "as parsed" preview, and a spinner while the engine computes.

import { Box, Static, Text, useApp, useStdout } from "ink";
import Spinner from "ink-spinner";
import TextInput from "ink-text-input";
import { useCallback, useEffect, useRef, useState } from "react";
import type { Engine } from "../engine/engine.js";
import { parseLine } from "../core/intent.js";
import { line, type OutLine } from "../core/outline.js";
import { processLine } from "../core/run.js";
import { trim } from "../core/text.js";
import { Lines, ToneText } from "./theme.js";

interface Entry {
  id: number;
  /** The typed line, echoed with a prompt; absent for the banner block. */
  input?: string;
  lines: OutLine[];
}

function bannerEntry(version: string): Entry {
  return {
    id: 0,
    lines: [
      line(`MathSolver ${version} — an Ink terminal for the MathSolver engine`, "result"),
      line('Type an expression or equation, "help" for commands, "quit" to exit.', "muted"),
      line("", "normal"),
    ],
  };
}

function EntryView({ entry }: { entry: Entry }) {
  return (
    <Box flexDirection="column">
      {entry.input !== undefined && (
        <Text>
          <Text color="green">» </Text>
          {entry.input}
        </Text>
      )}
      <Lines lines={entry.lines} />
    </Box>
  );
}

/** Debounced "as parsed" preview for bare expression/equation/system input. */
function usePreview(engine: Engine, input: string): string | null {
  const [preview, setPreview] = useState<string | null>(null);
  const token = useRef(0);

  useEffect(() => {
    const text = trim(input);
    const id = ++token.current;
    if (text === "") {
      setPreview(null);
      return;
    }
    const intent = parseLine(text);
    const previewable =
      intent.kind === "job" &&
      (intent.job.t === "simplify" ||
        intent.job.t === "bareEquation" ||
        intent.job.t === "solveSystem");
    if (!previewable) {
      setPreview(null);
      return;
    }
    const timer = setTimeout(() => {
      void engine
        .analyze(text)
        .then((a) => {
          if (id !== token.current) return; // superseded by newer keystroke
          if (!a.ok) {
            setPreview(null);
            return;
          }
          const vars = a.symbols.length ? `   vars: ${a.symbols.join(", ")}` : "";
          const head = a.kind === "system" ? "system" : a.plain;
          setPreview(`parsed: ${head}${vars}`);
        })
        .catch(() => {
          if (id === token.current) setPreview(null);
        });
    }, 140);
    return () => clearTimeout(timer);
  }, [engine, input]);

  return preview;
}

export function App({ engine, version, latex }: { engine: Engine; version: string; latex: boolean }) {
  const { exit } = useApp();
  const { write } = useStdout();
  const [input, setInput] = useState("");
  const [entries, setEntries] = useState<Entry[]>(() => [bannerEntry(version)]);
  const [pending, setPending] = useState<string | null>(null);
  const [epoch, setEpoch] = useState(0);
  const nextId = useRef(1);
  const preview = usePreview(engine, input);

  const onSubmit = useCallback(
    async (value: string) => {
      const text = trim(value);
      setInput("");
      if (text === "") return;
      setPending(text);
      try {
        const res = await processLine(engine, text, { latex });
        if (res.control === "clear") {
          write("\x1b[2J\x1b[3J\x1b[H"); // clear screen + scrollback
          setEpoch((e) => e + 1);
          setEntries([bannerEntry(version)]);
          return;
        }
        setEntries((prev) => [...prev, { id: nextId.current++, input: text, lines: res.lines }]);
        if (res.control === "quit") {
          setTimeout(exit, 0);
        }
      } finally {
        setPending(null);
      }
    },
    [engine, latex, version, write, exit],
  );

  return (
    <Box flexDirection="column">
      <Static key={epoch} items={entries}>
        {(entry) => <EntryView key={entry.id} entry={entry} />}
      </Static>

      {pending !== null ? (
        <Box flexDirection="column">
          <Text>
            <Text color="green">» </Text>
            {pending}
          </Text>
          <Text color="gray">
            <Text color="cyan">
              <Spinner type="dots" />
            </Text>{" "}
            computing…
          </Text>
        </Box>
      ) : (
        <Box flexDirection="column">
          <Box>
            <Text color="green">» </Text>
            <TextInput value={input} onChange={setInput} onSubmit={onSubmit} />
          </Box>
          {preview !== null && <ToneText line={line(preview, "muted")} />}
        </Box>
      )}
    </Box>
  );
}
