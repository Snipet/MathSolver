// Typed envelopes mirroring wasm/bindings.cpp exactly.

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

export type AnalyzeResult =
  | ({ ok: true; kind: "expression"; symbols: string[] } & Rendered)
  | { ok: true; kind: "equation"; symbols: string[]; plain: string; latex: string }
  | { ok: true; kind: "system"; symbols: string[] }
  | EngineError;

export type TransformResult = ({ ok: true } & Rendered) | EngineError;

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
      status: "solved" | "numeric" | "noRealSolution" | "allReals" | "unsolved";
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

export type SampleResult = { ok: true; ys: (number | null)[] } | EngineError;

// --- plugins (mathsolver/plugin.hpp, docs/PLUGINS.md) -----------------------

export interface PluginCommandMeta {
  name: string;
  summary: string;
  usage: string;
}

export interface PluginMeta {
  name: string;
  version: string;
  summary: string;
  commands: PluginCommandMeta[];
}

export type PluginCatalogResult =
  | { ok: true; plugins: PluginMeta[] }
  | EngineError;

/** Declarative UI blocks a plugin result is rendered from. */
export type PluginBlock =
  | { type: "kv"; title?: string; items: [string, string][] }
  | { type: "table"; title?: string; columns: string[]; rows: (string | number)[][] }
  | {
      type: "series";
      title?: string;
      xlabel?: string;
      ylabel?: string;
      logx?: boolean;
      x: number[];
      series: { label: string; ys: (number | null)[] }[];
      /** Vertical marker lines (e.g. filter cutoffs). */
      vlines?: { x: number; label: string }[];
    }
  | { type: "text"; lines: string[] };

export type PluginCallResult =
  | { ok: true; title: string; blocks: PluginBlock[] }
  | EngineError;

/** fn name -> [args, result] for the worker protocol. */
export interface EngineApi {
  version: [[], { ok: true; version: string } | EngineError];
  analyze: [[input: string], AnalyzeResult];
  simplify: [[input: string], TransformResult];
  expand: [[input: string], TransformResult];
  factor: [[input: string], TransformResult];
  latex: [[input: string], TransformResult];
  subs: [
    [input: string, assignments: string, simplifyResult: boolean],
    TransformResult,
  ];
  collect: [[input: string, variable: string], TransformResult];
  derivative: [[input: string, variable: string], TransformResult];
  integrate: [[input: string, variable: string], IntegrateResult];
  integrateDefinite: [
    [input: string, variable: string, lo: string, hi: string],
    DefiniteResult,
  ];
  solve: [
    [input: string, variable: string, lo: number, hi: number, useRange: boolean],
    SolveResult,
  ];
  solveSystem: [[input: string, varsCsv: string], SystemResult];
  evaluate: [[input: string, bindings: string], EvaluateResult];
  sample: [
    [input: string, variable: string, lo: number, hi: number, n: number],
    SampleResult,
  ];
  plugins: [[], PluginCatalogResult];
  pluginCall: [[plugin: string, command: string, argsCsv: string], PluginCallResult];
}

export type EngineFn = keyof EngineApi;

export interface WorkerRequest {
  id: number;
  fn: EngineFn;
  args: unknown[];
}

export type WorkerResponse =
  | { id: number; ok: true; result: unknown }
  | { id: number; ok: false; error: string }
  | { id: -1; ready: true }
  | { id: -1; ready: false; error: string };
