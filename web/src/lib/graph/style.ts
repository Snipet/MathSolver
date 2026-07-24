// Per-row plot style: the value types and the pure mapping from a row's
// line-style / weight to canvas render parameters (stroke width, dash pattern,
// point radius). Kept rune-free so it is unit-tested in node
// (tools/graph_style_test.mjs) and importable by both the store and the canvas.

export type LineStyle = "solid" | "dashed" | "dotted";
export type LineWeight = "thin" | "normal" | "thick";
export const LINE_STYLES: LineStyle[] = ["solid", "dashed", "dotted"];
export const LINE_WEIGHTS: LineWeight[] = ["thin", "normal", "thick"];

/** Stroke width (px) for a weight; also the reference for point-marker size. */
export function strokeWidth(weight: LineWeight): number {
  return weight === "thin" ? 1.4 : weight === "thick" ? 3.6 : 2.2;
}

/** Canvas dash pattern for a line style (empty = solid). */
export function dashArrFor(lineStyle: LineStyle): number[] {
  return lineStyle === "dashed" ? [7, 5] : lineStyle === "dotted" ? [1.5, 5] : [];
}

/** Point-marker radius (px) scaled off the stroke weight (base 4 at normal). */
export function pointRadius(weight: LineWeight): number {
  return 4 * (strokeWidth(weight) / 2.2);
}
