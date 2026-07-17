// Typed async client for the WASM engine worker.
//
// - Single worker instance, lazily started; requests are matched by id.
// - A request exceeding its timeout terminates and respawns the worker
//   (protecting the UI from pathological inputs), rejecting all pending
//   requests.
import type { EngineApi, EngineFn, WorkerRequest, WorkerResponse } from "./types";

const DEFAULT_TIMEOUT_MS = 20_000;

interface Pending {
  resolve: (v: unknown) => void;
  reject: (e: Error) => void;
  timer: ReturnType<typeof setTimeout>;
}

let worker: Worker | null = null;
let nextId = 1;
const pending = new Map<number, Pending>();
let readyResolvers: (() => void)[] = [];
let isReady = false;

function spawn(): Worker {
  const w = new Worker(new URL("./worker.ts", import.meta.url), { type: "module" });
  w.onmessage = (ev: MessageEvent<WorkerResponse>) => {
    const msg = ev.data;
    if (msg.id === -1) {
      isReady = true;
      readyResolvers.forEach((r) => r());
      readyResolvers = [];
      return;
    }
    const p = pending.get(msg.id);
    if (!p) return;
    pending.delete(msg.id);
    clearTimeout(p.timer);
    if (msg.ok) p.resolve(msg.result);
    else p.reject(new Error(msg.error));
  };
  w.onerror = (e) => {
    failAll(new Error(`engine worker error: ${e.message}`));
  };
  return w;
}

function failAll(err: Error) {
  for (const [, p] of pending) {
    clearTimeout(p.timer);
    p.reject(err);
  }
  pending.clear();
}

function ensureWorker(): Worker {
  if (!worker) {
    isReady = false;
    worker = spawn();
  }
  return worker;
}

function restart() {
  worker?.terminate();
  worker = null;
  isReady = false;
  failAll(new Error("computation timed out; the engine was restarted"));
}

/** Resolves once the WASM module has finished loading. */
export function engineReady(): Promise<void> {
  ensureWorker();
  if (isReady) return Promise.resolve();
  return new Promise((resolve) => readyResolvers.push(resolve));
}

export function call<F extends EngineFn>(
  fn: F,
  args: EngineApi[F][0],
  timeoutMs: number = DEFAULT_TIMEOUT_MS,
): Promise<EngineApi[F][1]> {
  const w = ensureWorker();
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      restart();
      reject(new Error(`computation timed out after ${Math.round(timeoutMs / 1000)}s`));
    }, timeoutMs);
    pending.set(id, { resolve: resolve as (v: unknown) => void, reject, timer });
    const req: WorkerRequest = { id, fn, args: args as unknown[] };
    w.postMessage(req);
  });
}
