import assert from "node:assert/strict";
import { test } from "node:test";
import { caretDiagnostic } from "../src/core/caret.js";

test("caret spans an offending token, matching the classic CLI", () => {
  const out = caretDiagnostic("2 + \\fraq{1}{2}", "unknown command '\\fraq'", 4, 9);
  assert.equal(
    out,
    "error: unknown command '\\fraq'\n" + "    2 + \\fraq{1}{2}\n" + "        ^~~~~",
  );
});

test("a single-byte span renders a bare caret", () => {
  const out = caretDiagnostic("a?b", "unexpected character '?'", 1, 2);
  assert.equal(out, "error: unexpected character '?'\n" + "    a?b\n" + "     ^");
});

test("tabs collapse to one display cell", () => {
  const out = caretDiagnostic("a\tb", "bad", 2, 3);
  assert.equal(out, "error: bad\n" + "    a b\n" + "      ^");
});
