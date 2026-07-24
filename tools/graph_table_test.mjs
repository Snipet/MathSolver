// Unit tests for the data-table TSV export (web/src/lib/graph/table.ts).
import { tableToTSV } from "../web/src/lib/graph/table.ts";

let pass = 0, fail = 0;
const check = (n, c, extra = "") => {
  if (c) { pass++; console.log("PASS  " + n); }
  else { fail++; console.log(`FAIL  ${n}${extra ? `  [${extra}]` : ""}`); }
};

// Header row + one line per non-blank point, tab-separated.
{
  const tsv = tableToTSV("x", "y", [{ x: "1", y: "2" }, { x: "3", y: "4" }]);
  check("header + rows, tab-separated", tsv === "x\ty\n1\t2\n3\t4", JSON.stringify(tsv));
}

// Custom column names become the header.
{
  const tsv = tableToTSV("time", "volts", [{ x: "0", y: "1" }]);
  check("custom column names in the header", tsv === "time\tvolts\n0\t1", JSON.stringify(tsv));
}

// Trailing/blank rows (both cells empty) are dropped; cells are trimmed.
{
  const tsv = tableToTSV("x", "y", [{ x: " 1 ", y: "2" }, { x: "", y: "" }, { x: "3", y: " 4" }]);
  check("blank rows dropped, cells trimmed", tsv === "x\ty\n1\t2\n3\t4", JSON.stringify(tsv));
}

// A half-filled row is kept (only both-blank rows are dropped).
{
  const tsv = tableToTSV("x", "y", [{ x: "5", y: "" }]);
  check("half-filled row is kept", tsv === "x\ty\n5\t", JSON.stringify(tsv));
}

// Blank column names fall back to x / y.
{
  const tsv = tableToTSV("  ", "", [{ x: "1", y: "2" }]);
  check("blank names fall back to x / y", tsv === "x\ty\n1\t2", JSON.stringify(tsv));
}

// No data → header only.
check("empty table → header only", tableToTSV("x", "y", []) === "x\ty");

console.log(`\n${fail === 0 ? "ALL PASS" : "FAILURES"}: ${pass} passed, ${fail} failed`);
process.exit(fail === 0 ? 0 : 1);
