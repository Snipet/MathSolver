// Pure helpers for the data-table copy/export. DOM-free and unit-tested
// (tools/graph_table_test.mjs).

export interface TableCell {
  x: string;
  y: string;
}

/**
 * The table's data as tab-separated values with a header row, ready to paste
 * into a spreadsheet. Trailing/blank rows (both cells empty) are dropped, each
 * cell is trimmed, and blank column names fall back to `x` / `y`.
 */
export function tableToTSV(
  xName: string,
  yName: string,
  rows: readonly TableCell[],
): string {
  const xh = (xName ?? "").trim() || "x";
  const yh = (yName ?? "").trim() || "y";
  const body = rows
    .filter((p) => p.x.trim() !== "" || p.y.trim() !== "")
    .map((p) => `${p.x.trim()}\t${p.y.trim()}`);
  return [`${xh}\t${yh}`, ...body].join("\n");
}
