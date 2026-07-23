// The result of running an Intent: either a set of engine result(s) tagged
// with how to render them, or a ready-made block of output lines (used for
// usage/inference errors and informational notes).

import type { OutLine } from "./outline.js";

export type RenderKind =
  | "transform"
  | "evaluate"
  | "solve"
  | "system"
  | "integrate"
  | "definite"
  | "limit"
  | "sum"
  | "rsolve"
  | "dsolve"
  | "fit"
  | "stats"
  | "seq";

export type Outcome =
  | {
      kind: "render";
      render: RenderKind;
      result: unknown;
      /** Variable name to print with each solution (solve render). */
      variable?: string;
      /** "sum" or "product" (sum render). */
      noun?: "sum" | "product";
      /** The exact string the engine parsed, for caret diagnostics. */
      source?: string;
      /** Always render the LaTeX field (the `latex` verb, regardless of flag). */
      forceLatex?: boolean;
    }
  | { kind: "lines"; lines: OutLine[] };
