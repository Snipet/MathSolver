// Discriminated union describing what the result card should render.
import type {
  TransformResult,
  SolveResult,
  SystemResult,
  IntegrateResult,
  DefiniteResult,
  EvaluateResult,
} from "./engine/types";

/** The successful branch of an engine result union. */
export type Ok<T> = Extract<T, { ok: true }>;

export type Outcome =
  | { kind: "transform"; result: Ok<TransformResult> }
  | { kind: "solve"; variable: string; result: Ok<SolveResult> }
  | { kind: "system"; result: Ok<SystemResult> }
  | { kind: "integral"; variable: string; result: Ok<IntegrateResult> }
  | { kind: "definite"; from: string; to: string; result: Ok<DefiniteResult> }
  | { kind: "evaluate"; result: Ok<EvaluateResult> }
  | { kind: "error"; message: string; input: string; begin?: number; end?: number };
