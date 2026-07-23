// Map an OutLine tone to its terminal color and render one line.

import { Text } from "ink";
import type { OutLine, Tone } from "../core/outline.js";

const COLOR: Record<Tone, { color?: string; dimColor?: boolean; bold?: boolean }> = {
  result: { color: "cyan", bold: true },
  normal: {},
  muted: { dimColor: true },
  warn: { color: "yellow" },
  note: { color: "gray" },
  error: { color: "red" },
};

export function ToneText({ line }: { line: OutLine }) {
  const style = COLOR[line.tone];
  // Empty strings must still occupy a row inside a flex column.
  return (
    <Text color={style.color} dimColor={style.dimColor} bold={style.bold}>
      {line.text === "" ? " " : line.text}
    </Text>
  );
}

export function Lines({ lines }: { lines: OutLine[] }) {
  return (
    <>
      {lines.map((l, i) => (
        <ToneText key={i} line={l} />
      ))}
    </>
  );
}
