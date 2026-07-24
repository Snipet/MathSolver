// Discriminated union describing what the result card should render.
import type {
  TransformResult,
  DsolveResult,
  FieldResult,
  SolveResult,
  SystemResult,
  IntegrateResult,
  DefiniteResult,
  EvaluateResult,
  ExplainResult,
  PluginCallResult,
  Rendered,
} from "./engine/types";
import type { Boundary } from "./wave/sim";

/** The successful branch of an engine result union. */
export type Ok<T> = Extract<T, { ok: true }>;

/**
 * When session variables were substituted into the input, the resolved form
 * that actually fed the engine ("computed from", §9.1) — null/absent when
 * the input was used verbatim.
 */
export interface EnvApplied {
  computedFrom?: Rendered | null;
}

export type Outcome =
  | ({ kind: "transform"; result: Ok<TransformResult> } & EnvApplied)
  | ({ kind: "dsolve"; result: Ok<DsolveResult> } & EnvApplied)
  | ({ kind: "solve"; variable: string; result: Ok<SolveResult> } & EnvApplied)
  | ({ kind: "system"; result: Ok<SystemResult> } & EnvApplied)
  | ({ kind: "integral"; variable: string; result: Ok<IntegrateResult> } & EnvApplied)
  | ({ kind: "steps"; variable: string; result: Ok<ExplainResult> } & EnvApplied)
  | ({ kind: "definite"; from: string; to: string; result: Ok<DefiniteResult> } & EnvApplied)
  | ({ kind: "evaluate"; result: Ok<EvaluateResult> } & EnvApplied)
  | { kind: "plugin"; plugin: string; command: string; result: Ok<PluginCallResult> }
  | {
      kind: "chart";
      title: string;
      x: number[];
      series: { label: string; ys: (number | null)[] }[];
      xlabel: string;
      ylabel: string;
    }
  | { kind: "vecfield"; fx: string; fy: string; result: Ok<FieldResult> }
  | {
      // Interactive 2-D wave field. The outcome carries only the serializable
      // seed (grid + physics params) so a console cell survives a localStorage
      // round-trip; WaveField reconstructs a fresh WaveSim from it on mount.
      kind: "wave";
      columns: number;
      speed: number;
      damping: number;
      boundary: Boundary;
    }
  | { kind: "assignment"; name: string; plain: string; latex: string }
  | { kind: "error"; message: string; input: string; begin?: number; end?: number };
