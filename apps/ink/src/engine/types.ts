// Parsed JSON envelopes returned by the WASM engine. These mirror
// wasm/bindings.cpp exactly (and web/src/lib/engine/types.ts). Only the
// shapes the terminal app renders are declared here.

export interface EngineError {
  ok: false;
  error: string;
  /** Byte span into the input for caret display (parse errors only). */
  begin?: number;
  end?: number;
}

export interface Rendered {
  plain: string;
  latex: string;
}

export type VersionResult = { ok: true; version: string } | EngineError;

export type AnalyzeResult =
  | ({ ok: true; kind: "expression"; symbols: string[] } & Rendered)
  | { ok: true; kind: "equation"; symbols: string[]; plain: string; latex: string }
  | { ok: true; kind: "system"; symbols: string[] }
  | EngineError;

/** simplify/expand/factor/cancel/together/latex/collect/apart/derivative/
 *  laplace/subs/vectorOp and the number-theory verbs all share this shape. */
export type TransformResult = ({ ok: true; notes?: string[] } & Rendered) | EngineError;

export type StatsResult =
  | {
      ok: true;
      exact: boolean;
      n: number;
      items: { label: string; plain: string; latex: string }[];
    }
  | EngineError;

export type FitResult =
  | ({ ok: true; model: string; exact: boolean; r2: number; n: number } & Rendered)
  | EngineError;

export type SeqResult =
  | ({
      ok: true;
      kind: "arithmetic" | "geometric" | "polynomial" | "recurrence" | "unknown";
      description: string;
      recurrence?: string;
      next: string[];
      warnings: string[];
    } & Partial<Rendered>)
  | EngineError;

export type LimitResult =
  | ({
      ok: true;
      status: "exact" | "numeric" | "diverges" | "doesNotExist" | "unsolved";
      sign: number;
      approx?: number;
      method: string;
      warnings: string[];
    } & Partial<Rendered>)
  | EngineError;

export type SumResult =
  | ({
      ok: true;
      status: "exact" | "diverges" | "unsolved";
      method: string;
      warnings: string[];
    } & Partial<Rendered>)
  | EngineError;

export type RsolveResult =
  | ({ ok: true; order: number; method: string; warnings: string[] } & Rendered)
  | EngineError;

export type DsolveResult =
  | ({
      ok: true;
      transformPlain: string;
      transformLatex: string;
      implicit: boolean;
      method: string;
      warnings: string[];
    } & Rendered)
  | EngineError;

export type IntegrateResult =
  | ({ ok: true; solved: true; method: string; warnings: string[] } & Rendered)
  | { ok: true; solved: false; method: string; warnings: string[] }
  | EngineError;

export type DefiniteResult =
  | ({
      ok: true;
      status: "exact" | "numeric";
      approx?: number;
      method: string;
      warnings: string[];
    } & Rendered)
  | { ok: true; status: "unsolved"; method: string; warnings: string[] }
  | EngineError;

export interface Solution extends Rendered {
  exact: boolean;
  note: string;
  approx: number | null;
}

export type SolveResult =
  | {
      ok: true;
      status: "solved" | "complex" | "numeric" | "noRealSolution" | "allReals" | "unsolved";
      method: string;
      warnings: string[];
      solutions: Solution[];
    }
  | EngineError;

export interface SystemValue extends Rendered {
  symbol: string;
}

export type SystemResult =
  | {
      ok: true;
      status: "solved" | "noSolution" | "underdetermined" | "unsolved";
      values: SystemValue[];
      free: string[];
      method: string;
      warnings: string[];
    }
  | EngineError;

export type EvaluateResult = { ok: true; value: number | null } | EngineError;
