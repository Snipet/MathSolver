// Unit tests for the console transcript serializer
// (web/src/lib/notebook/transcript.ts).
//   node tools/console_transcript_test.mjs
import {
  cellOutputLines,
  serializeTranscript,
} from "../web/src/lib/notebook/transcript.ts";

let pass = 0;
let fail = 0;
function check(name, cond, extra = "") {
  if (cond) {
    pass++;
    console.log(`PASS  ${name}`);
  } else {
    fail++;
    console.log(`FAIL  ${name}${extra ? "  — " + extra : ""}`);
  }
}

// A tiny cell factory (only the fields the serializer reads).
const cell = (input, result) => ({ input, result });

// --- cellOutputLines: one line per result kind ------------------------------
check(
  "transform → plain",
  cellOutputLines({ kind: "transform", result: { ok: true, plain: "2*x + 3", latex: "" } })[0] === "2*x + 3",
);
check(
  "assignment → name = plain",
  cellOutputLines({ kind: "assignment", name: "a", plain: "4", latex: "4" })[0] === "a = 4",
);
check(
  "error → error: message",
  cellOutputLines({ kind: "error", message: "unknown name 'q'", input: "q" })[0] === "error: unknown name 'q'",
);
{
  const lines = cellOutputLines({
    kind: "solve",
    variable: "x",
    result: { ok: true, status: "solved", method: "", warnings: [], solutions: [
      { plain: "2", latex: "2", exact: true, note: "", approx: 2 },
      { plain: "-2", latex: "-2", exact: true, note: "", approx: -2 },
    ] },
  });
  check("solve → one line per solution", lines.length === 2 && lines[0] === "x = 2" && lines[1] === "x = -2", JSON.stringify(lines));
}
check(
  "message → its lines verbatim",
  JSON.stringify(cellOutputLines({ kind: "message", tone: "info", lines: ["a", "b"] })) === JSON.stringify(["a", "b"]),
);
check(
  "plugin → bracketed label",
  cellOutputLines({ kind: "plugin", plugin: "dsp", command: "butter", result: { ok: true, title: "", blocks: [] } })[0] === "[dsp.butter]",
);
check("wave → placeholder", cellOutputLines({ kind: "wave", columns: 8, speed: 1, damping: 0, boundary: "reflect" })[0] === "[interactive wave field]");
check("null result → no lines", cellOutputLines(null).length === 0);

// --- serializeTranscript: In[]/Out[] blocks ---------------------------------
{
  const cells = [
    cell("2x + 3", { kind: "transform", result: { ok: true, plain: "2*x + 3", latex: "" } }),
    cell("a := 4", { kind: "assignment", name: "a", plain: "4", latex: "4" }),
  ];
  const text = serializeTranscript(cells);
  check("heading present", text.startsWith("# MathSolver console session"), text.slice(0, 40));
  check("reports the cell count", text.includes("2 cells"), text);
  check("has In[1] and its output", text.includes("In[1]:  2x + 3") && text.includes("Out[1]: 2*x + 3"));
  check("has In[2] and its output", text.includes("In[2]:  a := 4") && text.includes("Out[2]: a = 4"));
  check("custom title honored", serializeTranscript(cells, { title: "My session" }).startsWith("# My session"));
}
{
  // A multi-line output indents under a bare Out[n]: header.
  const cells = [
    cell("solve x^2=4, x", {
      kind: "solve",
      variable: "x",
      result: { ok: true, status: "solved", method: "", warnings: [], solutions: [
        { plain: "2", latex: "2", exact: true, note: "", approx: 2 },
        { plain: "-2", latex: "-2", exact: true, note: "", approx: -2 },
      ] },
    }),
  ];
  const text = serializeTranscript(cells);
  check("multi-line output uses a bare Out header", text.includes("Out[1]:\n  x = 2\n  x = -2"), text);
}
check("empty session still has a heading + 0 cells", serializeTranscript([]).includes("0 cells"));

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
