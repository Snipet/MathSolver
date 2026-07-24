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

export type TransformResult =
  | ({ ok: true; notes?: string[] } & Rendered)
  | EngineError;

/** Summary statistics: an ordered list of labelled values (exact where the
 *  data are rational) plus the count and exactness flag. */
export type StatsResult =
  | {
      ok: true;
      exact: boolean;
      n: number;
      items: { label: string; plain: string; latex: string }[];
    }
  | EngineError;

/** Regression fit: the fitted expression (plottable in x) plus its stats. */
export type FitResult =
  | ({
      ok: true;
      /** Model label, e.g. "linear", "quadratic", "exponential". */
      model: string;
      /** True when the polynomial fit was solved exactly over the rationals. */
      exact: boolean;
      /** Coefficient of determination (0..1, may be negative). */
      r2: number;
      /** Number of data points used. */
      n: number;
    } & Rendered)
  | EngineError;

/** Exact polynomial interpolation: the polynomial through the points. */
export type InterpResult =
  | ({
      ok: true;
      /** True when solved exactly over the rationals. */
      exact: boolean;
      /** Degree of the interpolating polynomial (≤ n−1). */
      degree: number;
      /** Number of data points. */
      n: number;
    } & Rendered)
  | EngineError;

/** Exact orthogonal polynomial: the degree-n member of a classical family. */
export type OrthoPolyResult =
  | ({
      ok: true;
      /** Family label, e.g. "Chebyshev T", "Legendre". */
      family: string;
      /** Degree of the polynomial (== n). */
      degree: number;
    } & Rendered)
  | EngineError;

export type SeqCallResult =
  | ({
      ok: true;
      kind: "arithmetic" | "geometric" | "polynomial" | "recurrence" | "unknown";
      description: string;
      recurrence?: string;
      next: string[];
      warnings: string[];
    } & Partial<Rendered>)
  | EngineError;

export type LimitCallResult =
  | ({
      ok: true;
      status: "exact" | "numeric" | "diverges" | "doesNotExist" | "unsolved";
      sign: number;
      approx?: number;
      method: string;
      warnings: string[];
    } & Partial<Rendered>)
  | EngineError;

export type SumCallResult =
  | ({
      ok: true;
      status: "exact" | "diverges" | "unsolved";
      method: string;
      warnings: string[];
    } & Partial<Rendered>)
  | EngineError;

export type RsolveCallResult =
  | ({ ok: true; order: number; method: string; warnings: string[] } & Rendered)
  | EngineError;

export type FieldResult =
  | {
      ok: true;
      n: number;
      x: number[];
      y: number[];
      u: (number | null)[];
      v: (number | null)[];
    }
  | EngineError;

/** A scalar field g(x,y) on a rectangular grid (row-major, y outer). */
export type GridResult =
  | { ok: true; nx: number; ny: number; g: (number | null)[] }
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

export type RootCountResult =
  | { ok: true; count: number; lo?: string; hi?: string }
  | EngineError;

/** One isolated real root: exact rational `value`, or the open interval
 *  (lo, hi) with a numeric `approx` when irrational. */
export interface IsolatedRoot {
  exact: boolean;
  value: string | null;
  lo: number;
  hi: number;
  approx: number;
}

export type IsolateResult =
  | { ok: true; count: number; roots: IsolatedRoot[] }
  | EngineError;

export interface Solution extends Rendered {
  exact: boolean;
  note: string;
  approx: number | null;
}

export type SolveResult =
  | {
      ok: true;
      status:
        | "solved"
        | "complex"
        | "numeric"
        | "noRealSolution"
        | "allReals"
        | "unsolved";
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
  /** A concrete runnable invocation, e.g. "dsp.butter lowpass, 4, 1000, 48000". */
  example: string;
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
      /** Equal units-per-pixel on both axes (e.g. pole-zero maps). */
      equal?: boolean;
      x: number[];
      series: {
        label: string;
        ys: (number | null)[];
        /** Draw markers instead of a connected line (e.g. pole-zero maps). */
        points?: boolean;
        /** Marker shape when points is set. */
        shape?: "x" | "o";
      }[];
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
  trigexpand: [[input: string], TransformResult];
  trigreduce: [[input: string], TransformResult];
  logexpand: [[input: string], TransformResult];
  logcombine: [[input: string], TransformResult];
  cancel: [[input: string], TransformResult];
  together: [[input: string], TransformResult];
  latex: [[input: string], TransformResult];
  subs: [
    [input: string, assignments: string, simplifyResult: boolean],
    TransformResult,
  ];
  collect: [[input: string, variable: string], TransformResult];
  apart: [[input: string, variable: string], TransformResult];
  fit: [[data: string, model: string, degree: string], FitResult];
  interp: [[data: string], InterpResult];
  interpForm: [[data: string, form: string], TransformResult];
  orthopoly: [[family: string, n: number, variable: string], OrthoPolyResult];
  stats: [[data: string], StatsResult];
  dsolve: [[ode: string, conditionsCsv: string], DsolveResult];
  series: [
    [input: string, variable: string, center: string, order: number],
    TransformResult,
  ];
  pade: [
    [input: string, variable: string, m: number, n: number],
    TransformResult,
  ];
  rootcount: [
    [input: string, variable: string, lo: string, hi: string],
    RootCountResult,
  ];
  isolate: [[input: string, variable: string], IsolateResult];
  vectorOp: [[op: string, fieldSemi: string, varsCsv: string], TransformResult];
  limit: [
    [input: string, variable: string, point: string, direction: string],
    LimitCallResult,
  ];
  mlimit: [
    [input: string, xVar: string, a: string, yVar: string, b: string],
    LimitCallResult,
  ];
  stirling: [[variable: string, terms: number], TransformResult];
  seq: [[termsCsv: string], SeqCallResult];
  // Number theory over the integers (exact int64). gcd/lcm take a CSV list;
  // the rest take a single integer. All return a plain/latex result + notes.
  gcd: [[list: string], TransformResult];
  lcm: [[list: string], TransformResult];
  isprime: [[n: string], TransformResult];
  nextprime: [[n: string], TransformResult];
  divisors: [[n: string], TransformResult];
  totient: [[n: string], TransformResult];
  sigma: [[n: string, k: string], TransformResult];
  mobius: [[n: string], TransformResult];
  partitions: [[n: string], TransformResult];
  catalan: [[n: string], TransformResult];
  bernoulli: [[n: string], TransformResult];
  stirling2: [[n: string, k: string], TransformResult];
  bell: [[n: string], TransformResult];
  cfrac: [[value: string], TransformResult];
  discriminant: [[poly: string, variable: string], TransformResult];
  polydiv: [[dividend: string, divisor: string, variable: string], TransformResult];
  polygcd: [[a: string, b: string, variable: string], TransformResult];
  polylcm: [[a: string, b: string, variable: string], TransformResult];
  resultant: [[a: string, b: string, variable: string], TransformResult];
  bezout: [[a: string, b: string, variable: string], TransformResult];
  companion: [[poly: string, variable: string], TransformResult];
  vandermonde: [[nodes: string], TransformResult];
  solveIneq: [[lhs: string, rhs: string, op: string, variable: string], TransformResult];
  mod: [[args: string], TransformResult];
  powmod: [[args: string], TransformResult];
  modinv: [[args: string], TransformResult];
  crt: [[system: string], TransformResult];
  sum: [[term: string, variable: string, lo: string, hi: string], SumCallResult];
  product: [
    [term: string, variable: string, lo: string, hi: string],
    SumCallResult,
  ];
  rsolve: [[recurrence: string, conditionsCsv: string], RsolveCallResult];
  sampleField: [
    [
      fx: string,
      fy: string,
      xVar: string,
      yVar: string,
      xlo: number,
      xhi: number,
      ylo: number,
      yhi: number,
      n: number,
    ],
    FieldResult,
  ];
  sampleGrid: [
    [
      expr: string,
      xVar: string,
      yVar: string,
      x0: number,
      x1: number,
      nx: number,
      y0: number,
      y1: number,
      ny: number,
    ],
    GridResult,
  ];
  derivative: [[input: string, variable: string], TransformResult];
  laplace: [[input: string, timeVar: string], TransformResult];
  ilaplace: [[input: string, freqVar: string], TransformResult];
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
