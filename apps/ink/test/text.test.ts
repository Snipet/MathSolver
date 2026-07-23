import assert from "node:assert/strict";
import { test } from "node:test";
import {
  findInequality,
  hasTopLevelEquals,
  hasTopLevelSemicolon,
  isSymbolName,
  parseIntArg,
  splitTopLevel,
  trim,
} from "../src/core/text.js";

test("splitTopLevel splits on top-level delimiters and trims", () => {
  assert.deepEqual(splitTopLevel("a, b, c", ","), ["a", "b", "c"]);
  assert.deepEqual(splitTopLevel("f(x, y), z", ","), ["f(x, y)", "z"]);
  assert.deepEqual(splitTopLevel("[1, 2; 3, 4], v", ","), ["[1, 2; 3, 4]", "v"]);
  assert.deepEqual(splitTopLevel("", ","), [""]);
});

test("hasTopLevelSemicolon ignores bracketed semicolons", () => {
  assert.equal(hasTopLevelSemicolon("x + y; x - y"), true);
  assert.equal(hasTopLevelSemicolon("f(a; b)"), false);
  assert.equal(hasTopLevelSemicolon("x + 1"), false);
});

test("hasTopLevelEquals distinguishes equations from relations/assignments", () => {
  assert.equal(hasTopLevelEquals("x = 4"), true);
  assert.equal(hasTopLevelEquals("x <= 4"), false);
  assert.equal(hasTopLevelEquals("x >= 4"), false);
  assert.equal(hasTopLevelEquals("a := 2"), false);
  assert.equal(hasTopLevelEquals("f(x=1)"), false);
  assert.equal(hasTopLevelEquals("2x + 3"), false);
});

test("findInequality locates the top-level relational operator", () => {
  const lt = findInequality("x^2 < 4");
  assert.equal(lt?.op, "<");
  assert.equal(trim(lt!.lhs), "x^2");
  assert.equal(trim(lt!.rhs), "4");

  assert.equal(findInequality("x >= 2")?.op, ">=");
  assert.equal(findInequality("x <= 2")?.op, "<=");
  assert.equal(findInequality("x ≤ 2")?.op, "<=");
  assert.equal(findInequality("x ≥ 2")?.op, ">=");
  assert.equal(findInequality("f(a < b) + 1"), null);
  assert.equal(findInequality("x + 1"), null);
});

test("isSymbolName accepts names, rejects leading digits", () => {
  assert.equal(isSymbolName("x"), true);
  assert.equal(isSymbolName("x_1"), true);
  assert.equal(isSymbolName("alpha"), true);
  assert.equal(isSymbolName("2x"), false);
  assert.equal(isSymbolName(""), false);
});

test("parseIntArg parses integers only", () => {
  assert.equal(parseIntArg("3"), 3);
  assert.equal(parseIntArg(" -2 "), -2);
  assert.equal(parseIntArg("3.5"), null);
  assert.equal(parseIntArg(""), null);
  assert.equal(parseIntArg("abc"), null);
});
