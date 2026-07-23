<script lang="ts">
  // Desmos-style graphing calculator: an expression list on the left, an
  // interactive graph on the right. Multiple explicit functions y=f(x) sample
  // through the WASM engine; free variables resolve against the app's session
  // variables and auto-create a slider (app-integrated — `a := 2` set anywhere
  // moves the graph, and a graph slider moves `a` everywhere).
  import { untrack, onDestroy } from "svelte";
  import { call } from "../engine";
  import { vars } from "../vars.svelte";
  import { applyEnv, overrideValue } from "../vars/session";
  import { graph, GRAPH_COLORS } from "../graph/graph.svelte";
  import {
    xRange,
    yRange,
    type DrawSeries,
    type PointHandle,
    type AxisSource,
  } from "../graph/viewport";
  import {
    classifyRow,
    splitTopLevelCommas,
    parseRestriction,
    coordAtom,
    rebuildPointRow,
    type RowKind,
    type Comparison,
    type RelOp,
  } from "../graph/classify";
  import { marchingSquares, inequalityMask } from "../graph/contour";
  import { findInnermostCall, stripCalc } from "../graph/calculus";
  import { encodeState } from "../graph/share";
  import GraphCanvas from "./GraphCanvas.svelte";

  interface Props {
    /** Console/compact mode: shorter graph, list above. */
    compact?: boolean;
  }
  let { compact = false }: Props = $props();

  let series = $state<DrawSeries[]>([]);
  // Draggable point handles per points-series id (aligned with its xs/ys).
  let handles = $state<Record<string, PointHandle[]>>({});
  let rowErrors = $state<Record<number, string>>({});
  interface Slot {
    name: string;
    value: number;
    lo: number;
    hi: number;
  }
  let slots = $state<Slot[]>([]);

  // Per-variable slider ranges (persist within the session), and the live
  // drag overrides that keep the curve responsive before the session store's
  // debounced validation catches up.
  const ranges = new Map<string, { lo: number; hi: number }>();
  const autoCreated = new Set<string>(); // slider vars we created
  const definedByGraph = new Set<string>(); // `name = expr` definitions we created
  let overrides = $state<Record<string, number>>({});

  // --- measure the graph area (for sample count + visible range) ------------
  let graphArea: HTMLDivElement | undefined = $state();
  let graphW = $state(800);
  let graphH = $state(460);
  $effect(() => {
    const el = graphArea;
    if (!el) return;
    const ro = new ResizeObserver((e) => {
      graphW = Math.max(120, Math.floor(e[0].contentRect.width));
      graphH = Math.max(120, Math.floor(e[0].contentRect.height));
    });
    ro.observe(el);
    return () => ro.disconnect();
  });

  function numericValue(text: string): number | null {
    const d = Number(text);
    if (Number.isFinite(d)) return d;
    const m = /^\s*(-?\d+)\s*\/\s*(\d+)\s*$/.exec(text);
    if (!m) return null;
    const q = Number(m[2]);
    return q !== 0 && Number.isFinite(Number(m[1]) / q) ? Number(m[1]) / q : null;
  }

  // Persist the viewport on pan/zoom/reset (each reassigns graph.view to a new
  // object). Debounced so a drag doesn't write every frame.
  $effect(() => {
    void graph.view;
    graph.persistSoon();
  });

  // --- reactive resample -----------------------------------------------------
  let seq = 0;
  $effect(() => {
    // Track: row text/visibility, viewport, session vars, slider overrides.
    const rowsSig = graph.rows.map((r) => `${r.id}:${r.text}:${r.visible}:${r.color}`).join("|");
    const v = graph.view;
    const activeSig = vars.active.map((b) => `${b.name}=${b.value}`).join("|");
    const ovSig = Object.entries(overrides).map(([k, x]) => `${k}=${x}`).join("|");
    const w = graphW;
    const h = graphH;
    void rowsSig;
    void v;
    void activeSig;
    void ovSig;
    void w;
    void h;
    const my = ++seq;
    const timer = setTimeout(() => void recompute(my), 90);
    return () => clearTimeout(timer);
  });

  // Upsert `name := expr` session variables from `name = expr` graph rows, and
  // remove ones we defined that are no longer present.
  function reconcileDefines(defs: Map<string, string>): void {
    untrack(() => {
      for (const [name, expr] of defs) {
        const row = vars.rows.find((r) => r.status.symbol === name || r.name === name);
        if (row) {
          if (row.value.trim() !== expr.trim()) vars.edit(row.id, { value: expr });
        } else {
          const id = vars.add();
          if (id != null) vars.edit(id, { name, value: expr });
        }
        definedByGraph.add(name);
      }
      for (const name of [...definedByGraph]) {
        if (!defs.has(name)) {
          const row = vars.rows.find((r) => r.status.symbol === name || r.name === name);
          if (row) vars.remove(row.id);
          definedByGraph.delete(name);
        }
      }
    });
  }

  /** Expand diff(...) / integral(...) calls via the engine (innermost first),
   *  resolving session variables inside each argument. */
  async function resolveCalculus(text: string): Promise<string> {
    let s = text;
    for (let guard = 0; guard < 16; guard++) {
      const c = findInnermostCall(s);
      if (!c) break;
      const parts = splitTopLevelCommas(c.inner);
      const isSeries = c.name === "series" || c.name === "taylor";
      // series(f, center, order): the trailing args are center/order, not a
      // variable — Taylor is always in x here. diff/integral take (f, var).
      const variable = isSeries ? "x" : (parts[1] ?? "x").trim();
      const argEnv = await applyEnv(parts[0]?.trim() ?? "", [variable], "expr", overrides);
      const isDiff = c.name === "diff" || c.name === "derivative";
      let plain: string;
      if (isSeries) {
        const center = (parts[1] ?? "0").trim() || "0";
        const order = Math.max(1, Math.min(12, Math.round(Number(parts[2]) || 6)));
        const r = await call("series", [argEnv.text, variable, center, order]);
        if (!r.ok) throw new Error(r.error);
        plain = r.plain;
      } else if (isDiff) {
        const r = await call("derivative", [argEnv.text, variable]);
        if (!r.ok) throw new Error(r.error);
        plain = r.plain;
      } else {
        const r = await call("integrate", [argEnv.text, variable]);
        if (!r.ok) throw new Error(r.error);
        if (!r.solved) throw new Error("no closed-form antiderivative");
        plain = r.plain;
      }
      s = s.slice(0, c.start) + "(" + plain + ")" + s.slice(c.end);
    }
    return s;
  }

  function reconcileVars(freeVars: Set<string>): void {
    untrack(() => {
      for (const name of freeVars) {
        if (definedByGraph.has(name)) continue; // a defined name, not a slider
        // Match a pending (not-yet-validated) row by its typed name too, so a
        // sub-debounce race can't create a duplicate that shadows a user var.
        const exists = vars.rows.some((r) => r.status.symbol === name || r.name.trim() === name);
        if (!exists && !autoCreated.has(name)) {
          const id = vars.add();
          if (id != null) {
            vars.edit(id, { name, value: "1" });
            autoCreated.add(name);
          }
        }
      }
      for (const name of [...autoCreated]) {
        if (!freeVars.has(name)) {
          const row = vars.rows.find((r) => r.status.symbol === name || r.name.trim() === name);
          // Only reclaim a still-pristine slider that nothing else references —
          // never delete a value the user adopted or a var others depend on.
          const referenced = vars.active.some((b) => b.name !== name && b.symbols.includes(name));
          if (row && row.value.trim() === "1" && !referenced) vars.remove(row.id);
          autoCreated.delete(name);
        }
      }
    });
  }

  const TWO_PI = 2 * Math.PI;
  const ANGLE_VARS = ["theta", "θ", "t"];
  function angleVar(syms: string[]): string {
    return syms.find((s) => ANGLE_VARS.includes(s)) ?? "theta";
  }
  /** The parameter of a `(x(u), y(u))` row (t/theta/θ), or null for plain points. */
  function paramVar(syms: string[]): string | null {
    return syms.find((s) => ANGLE_VARS.includes(s)) ?? null;
  }
  // The plot/parameter variables of a row (never turned into sliders). Depends
  // on the row's free symbols (the polar angle, or `t` for a parametric point).
  function plotVars(spec: RowKind, syms: string[]): Set<string> {
    switch (spec.t) {
      case "function":
        return new Set(["x", "y"]); // y is the axis, not a slider (restrictions may name it)
      case "functionY":
        return new Set(["x", "y"]);
      case "polar":
        return new Set([angleVar(syms)]);
      case "relation":
        return new Set(["x", "y"]);
      case "define":
        return new Set(["x", "y"]); // x/y in a definition are the graph axes
      case "pointish": {
        const p = paramVar(syms);
        return p ? new Set([p]) : new Set();
      }
      default:
        return new Set();
    }
  }
  // Expressions within a row whose symbols matter for free-variable detection.
  function specExprs(spec: RowKind): string[] {
    let base: string[];
    switch (spec.t) {
      case "function":
      case "functionY":
      case "polar":
        base = [spec.expr];
        break;
      case "relation":
        base = [spec.lhs, spec.rhs];
        break;
      case "define":
        base = [spec.expr];
        break;
      case "pointish":
        base = spec.coords.flatMap((c) => c);
        break;
      default:
        return [];
    }
    // Restriction sides carry symbols too (e.g. `a` in `{a < x}` → a slider).
    const rExprs = parseRestriction(spec.restrict ?? []).flatMap((c) => [c.lhs, c.rhs]);
    return [...base, ...rExprs];
  }

  function linspace(lo: number, hi: number, n: number): number[] {
    const xs = new Array(n);
    for (let i = 0; i < n; i++) xs[i] = lo + ((hi - lo) * i) / (n - 1);
    return xs;
  }
  function sampleN(): number {
    return Math.max(200, Math.min(1600, Math.round(graphW * 1.6)));
  }

  /** Resolve session vars + slider overrides into one coordinate and evaluate it. */
  async function evalCoord(expr: string): Promise<number | null> {
    try {
      const env = await applyEnv(expr, [], "expr", overrides);
      const r = await call("evaluate", [env.text, ""]);
      return r.ok && r.value !== null && Number.isFinite(r.value) ? r.value : null;
    } catch {
      return null;
    }
  }

  // --- domain restrictions ({ … }) -------------------------------------------
  function cmpTrue(op: RelOp, d: number): boolean {
    switch (op) {
      case "<": return d < 0; // d = lhs − rhs
      case "<=": return d <= 0;
      case ">": return d > 0;
      case ">=": return d >= 0;
      case "=": return Math.abs(d) < 1e-9;
    }
  }
  function flipOp(op: RelOp): RelOp {
    return op === "<" ? ">" : op === "<=" ? ">=" : op === ">" ? "<" : op === ">=" ? "<=" : "=";
  }
  /** Numeric [lo,hi] bounds a restriction places directly on the sample var. */
  async function paramBounds(cmps: Comparison[], sv: string): Promise<{ lo?: number; hi?: number }> {
    let lo: number | undefined;
    let hi: number | undefined;
    for (const c of cmps) {
      const L = c.lhs === sv;
      const R = c.rhs === sv;
      if (L === R) continue; // sv on both/neither side → not a plain bound
      const val = await evalCoord(L ? c.rhs : c.lhs);
      if (val === null) continue;
      const op = R ? flipOp(c.op) : c.op; // normalize to `sv op val`
      if (op === ">" || op === ">=") lo = lo === undefined ? val : Math.max(lo, val);
      else if (op === "<" || op === "<=") hi = hi === undefined ? val : Math.min(hi, val);
    }
    return { lo, hi };
  }
  /** Boolean keep-mask over sv ∈ [lo,hi] (n samples); null if nothing evaluable. */
  async function restrictMask(
    cmps: Comparison[],
    sv: string,
    lo: number,
    hi: number,
    n: number,
  ): Promise<Uint8Array | null> {
    const mask = new Uint8Array(n).fill(1);
    let any = false;
    for (const c of cmps) {
      const env = await applyEnv(`(${c.lhs}) - (${c.rhs})`, [sv], "expr", overrides);
      const sr = await call("sample", [env.text, sv, lo, hi, n]);
      if (!sr.ok) continue; // clause not expressible over sv → fail open
      any = true;
      for (let i = 0; i < n; i++) {
        const d = sr.ys[i];
        if (d === null || !Number.isFinite(d) || !cmpTrue(c.op, d)) mask[i] = 0;
      }
    }
    return any ? mask : null;
  }
  function blank(ys: (number | null)[], mask: Uint8Array | null): (number | null)[] {
    return mask ? ys.map((y, i) => (mask[i] ? y : null)) : ys;
  }

  // --- draggable-point axis classification -----------------------------------
  // A bare coordinate is draggable if it's a settable session variable; x/y/r
  // and non-numeric bindings are never settable.
  function isSettableVar(name: string): boolean {
    if (name === "x" || name === "y" || name === "r") return false;
    // A `name = expr` graph definition drives the var from its row, not the
    // session store — dragging it would just be reverted by reconcileDefines.
    if (definedByGraph.has(name)) return false;
    if (slots.some((s) => s.name === name)) return true;
    return vars.active.some((b) => b.name === name && numericValue(b.value) !== null);
  }
  function axisSource(raw: string): AxisSource {
    const t = raw.trim();
    const a = coordAtom(t);
    if (a === "literal") return { kind: "literal" };
    if (a === "ident" && isSettableVar(t)) return { kind: "var", name: t };
    return { kind: "expr" };
  }

  interface Built {
    series: DrawSeries[];
    error?: string;
    handles?: PointHandle[];
  }

  async function buildDrawable(
    id: number,
    color: string,
    spec: RowKind,
    syms: string[],
  ): Promise<Built> {
    if (spec.t === "function" || spec.t === "functionY") {
      const along = spec.t === "function" ? "x" : "y";
      const [lo0, hi0] = spec.t === "function" ? xRange(graph.view, graphW) : yRange(graph.view, graphH);
      const span = hi0 - lo0;
      const lo = lo0 - 0.35 * span;
      const hi = hi0 + 0.35 * span;
      const n = sampleN();
      const resolved = await resolveCalculus(spec.expr);
      const env = await applyEnv(resolved, [along], "expr", overrides);
      const sr = await call("sample", [env.text, along, lo, hi, n]);
      if (!sr.ok) return { series: [], error: sr.error };
      const cmps = parseRestriction(spec.restrict ?? []);
      const masked = blank(sr.ys, cmps.length ? await restrictMask(cmps, along, lo, hi, n) : null);
      const axis = linspace(lo, hi, n);
      const series: DrawSeries =
        spec.t === "function"
          ? { id: String(id), color, visible: true, kind: "line", xs: axis, ys: masked }
          : { id: String(id), color, visible: true, kind: "line", xs: masked, ys: axis };
      return { series: [series] };
    }

    if (spec.t === "pointish") {
      const param = paramVar(syms);
      if (param) {
        // Parametric curve (x(u), y(u)); the parameter (t/theta/θ) sweeps
        // [0, 2π] by default, or the numeric bounds from a `{ … }` restriction.
        if (spec.coords.length !== 1)
          return { series: [], error: "a parametric curve is a single (x(t), y(t)) pair" };
        const cmps = parseRestriction(spec.restrict ?? []);
        const b = cmps.length ? await paramBounds(cmps, param) : {};
        const lo = b.lo ?? 0;
        const hi = b.hi ?? TWO_PI;
        if (!(hi > lo)) return { series: [], error: "empty parameter domain" };
        const [xe0, ye0] = spec.coords[0];
        const [xe, ye] = [await resolveCalculus(xe0), await resolveCalculus(ye0)];
        const n = 720;
        const ex = await applyEnv(xe, [param], "expr", overrides);
        const sx = await call("sample", [ex.text, param, lo, hi, n]);
        const ey = await applyEnv(ye, [param], "expr", overrides);
        const sy = await call("sample", [ey.text, param, lo, hi, n]);
        if (!sx.ok) return { series: [], error: sx.error };
        if (!sy.ok) return { series: [], error: sy.error };
        const mask = cmps.length ? await restrictMask(cmps, param, lo, hi, n) : null;
        return {
          series: [
            {
              id: String(id),
              color,
              visible: true,
              kind: "line",
              xs: blank(sx.ys, mask),
              ys: blank(sy.ys, mask),
            },
          ],
        };
      }
      const xs: number[] = [];
      const ys: (number | null)[] = [];
      const hs: PointHandle[] = []; // aligned with xs/ys (skip un-evaluable points together)
      for (let ci = 0; ci < spec.coords.length; ci++) {
        const [xe, ye] = spec.coords[ci];
        const x = await evalCoord(xe);
        const y = await evalCoord(ye);
        if (x !== null && y !== null) {
          xs.push(x);
          ys.push(y);
          hs.push({ rowId: id, coordIndex: ci, x: axisSource(xe), y: axisSource(ye) });
        }
      }
      if (xs.length === 0) return { series: [], error: "point coordinates must be numbers" };
      return {
        series: [{ id: String(id), color, visible: true, kind: "points", xs, ys }],
        handles: hs,
      };
    }

    if (spec.t === "relation") {
      // g = lhs − rhs, with session vars / slider overrides resolved so only
      // x and y remain; contour at 0 (implicit curve) or shade the region.
      const gExpr = await resolveCalculus(`(${spec.lhs}) - (${spec.rhs})`);
      const env = await applyEnv(gExpr, ["x", "y"], "expr", overrides);
      const [xlo, xhi] = xRange(graph.view, graphW);
      const [ylo, yhi] = yRange(graph.view, graphH);
      const mx = 0.2 * (xhi - xlo);
      const myy = 0.2 * (yhi - ylo);
      const x0 = xlo - mx;
      const x1 = xhi + mx;
      const y0 = ylo - myy;
      const y1 = yhi + myy;
      const nx = Math.max(40, Math.min(220, Math.round(graphW / 5)));
      const ny = Math.max(40, Math.min(220, Math.round(graphH / 5)));
      const gr = await call("sampleGrid", [env.text, "x", "y", x0, x1, nx, y0, y1, ny]);
      if (!gr.ok) return { series: [], error: gr.error };
      if (spec.op === "=") {
        const c = marchingSquares(gr.g, nx, ny, x0, x1, y0, y1, 0);
        return { series: [{ id: String(id), color, visible: true, kind: "line", xs: c.xs, ys: c.ys }] };
      }
      const mask = inequalityMask(gr.g, spec.op);
      return {
        series: [
          {
            id: String(id),
            color,
            visible: true,
            kind: "region",
            xs: [],
            ys: [],
            region: { x0, x1, y0, y1, nx, ny, mask },
          },
        ],
      };
    }
    if (spec.t === "polar") {
      // r = f(θ), converted to (r·cosθ, r·sinθ). θ sweeps [0, 2π] by default,
      // or the numeric bounds from a `{ … }` restriction (e.g. spirals).
      const ang = angleVar(syms);
      const n = 720;
      const cmps = parseRestriction(spec.restrict ?? []);
      const b = cmps.length ? await paramBounds(cmps, ang) : {};
      const lo = b.lo ?? 0;
      const hi = b.hi ?? TWO_PI;
      if (!(hi > lo)) return { series: [], error: "empty angle domain" };
      const resolved = await resolveCalculus(spec.expr);
      const env = await applyEnv(resolved, [ang], "expr", overrides);
      const sr = await call("sample", [env.text, ang, lo, hi, n]);
      if (!sr.ok) return { series: [], error: sr.error };
      const th = linspace(lo, hi, n);
      const mask = cmps.length ? await restrictMask(cmps, ang, lo, hi, n) : null;
      const xs: (number | null)[] = new Array(n);
      const ys: (number | null)[] = new Array(n);
      for (let i = 0; i < n; i++) {
        const r = sr.ys[i];
        if (r === null || !Number.isFinite(r) || (mask && !mask[i])) {
          xs[i] = null;
          ys[i] = null;
        } else {
          xs[i] = r * Math.cos(th[i]);
          ys[i] = r * Math.sin(th[i]);
        }
      }
      return { series: [{ id: String(id), color, visible: true, kind: "line", xs, ys }] };
    }
    return { series: [] };
  }

  async function recompute(my: number): Promise<void> {
    const vis = graph.rows.filter((r) => r.visible && r.text.trim());
    const items = vis.map((r) => ({ r, spec: classifyRow(r.text.trim()) }));

    // Free-variable detection: analyze each row's sampling expressions.
    const analyzed = await Promise.all(
      items.map(async ({ r, spec }) => {
        const syms = new Set<string>();
        let err: string | null = null;
        for (const ex of specExprs(spec)) {
          const a = await call("analyze", [stripCalc(ex)]);
          if (!a.ok) {
            err = a.error;
            break;
          }
          if ("symbols" in a) for (const s of a.symbols) syms.add(s);
        }
        return { r, spec, syms: [...syms], err };
      }),
    );
    if (my !== seq) return;

    // Commit `name = expr` rows as session variables before slider reconcile,
    // so referencing them elsewhere resolves and they never become sliders.
    const defs = new Map<string, string>();
    for (const { spec } of analyzed) if (spec.t === "define") defs.set(spec.name, spec.expr);
    reconcileDefines(defs);

    const errs: Record<number, string> = {};
    const freeVars = new Set<string>();
    for (const { r, spec, syms, err } of analyzed) {
      if (err) {
        errs[r.id] = err;
        continue;
      }
      const pv = plotVars(spec, syms);
      for (const s of syms) if (!pv.has(s)) freeVars.add(s);
    }
    reconcileVars(freeVars);

    const active = untrack(() => vars.active);
    const nextSlots: Slot[] = [];
    const nextOv = { ...overrides };
    let droppedOverride = false;
    for (const name of freeVars) {
      const b = active.find((x) => x.name === name && x.kind === "expression");
      const sessionVal = b ? numericValue(b.value) : null;
      // Release a slider override once write-through has settled (the session
      // value now matches it) so a later external edit moves the graph again.
      if (name in nextOv && sessionVal !== null && nextOv[name] === sessionVal) {
        delete nextOv[name];
        droppedOverride = true;
      }
      const val = nextOv[name] ?? sessionVal;
      if (val === null || val === undefined) continue;
      const rg = ranges.get(name) ?? { lo: -10, hi: 10 };
      ranges.set(name, rg);
      nextSlots.push({ name, value: val, lo: rg.lo, hi: rg.hi });
    }
    slots = nextSlots;

    const out: DrawSeries[] = [];
    const nextHandles: Record<string, PointHandle[]> = {};
    for (const { r, spec, syms, err } of analyzed) {
      if (err) continue;
      try {
        const built = await buildDrawable(r.id, r.color, spec, syms);
        if (my !== seq) return;
        if (built.error) errs[r.id] = built.error;
        for (const s of built.series) out.push(s);
        if (built.handles && built.handles.length) nextHandles[String(r.id)] = built.handles;
      } catch (e) {
        errs[r.id] = e instanceof Error ? e.message : String(e);
      }
    }
    rowErrors = errs;
    if (my === seq) {
      series = out;
      handles = nextHandles;
      if (droppedOverride) overrides = nextOv; // settled overrides released
    }
  }

  // --- slider write-through --------------------------------------------------
  // Round to the session store's stored precision so the committed override
  // equals numericValue(valuePlain) and the release check (recompute) fires —
  // otherwise a full-precision override pins the variable forever.
  const snap = (v: number) => Number(overrideValue(v));
  // After a commit the session var holds the value, so the override is only a
  // bridge over the validation debounce. Release it unconditionally soon after
  // (cleared if a new interaction starts) so a later external edit always wins —
  // the "drop when equal" pass in recompute can't fire if an external edit
  // changes the session value before the override and session ever coincide.
  const releaseTimers = new Map<string, ReturnType<typeof setTimeout>>();
  function clearRelease(name: string): void {
    const t = releaseTimers.get(name);
    if (t) {
      clearTimeout(t);
      releaseTimers.delete(name);
    }
  }
  function scheduleRelease(name: string): void {
    clearRelease(name);
    releaseTimers.set(
      name,
      setTimeout(() => {
        releaseTimers.delete(name);
        if (name in overrides) {
          const { [name]: _drop, ...rest } = overrides;
          overrides = rest;
        }
      }, 350),
    );
  }
  function slider(name: string, value: number): void {
    clearRelease(name); // interaction ongoing — don't let a pending release fire
    overrides = { ...overrides, [name]: value };
  }
  function commitSlider(name: string, value: number): void {
    overrides = { ...overrides, [name]: snap(value) };
    // Match a pending (not-yet-validated) row by typed name too, so a freshly
    // auto-created variable still commits on the first drag.
    const row = vars.rows.find((r) => r.status.symbol === name || r.name.trim() === name);
    if (row) vars.edit(row.id, { value: overrideValue(value) });
    scheduleRelease(name);
  }
  function setRange(name: string, lo: number, hi: number): void {
    if (!Number.isFinite(lo) || !Number.isFinite(hi) || lo >= hi) return;
    ranges.set(name, { lo, hi });
    // nudge a resample so the slot picks up the new range
    overrides = { ...overrides };
  }

  // --- draggable points ------------------------------------------------------
  // Live var writes are throttled so the recompute debounce (90ms) still fires
  // mid-drag and rows referencing the dragged var move too.
  let overridesBeforeDrag: Record<string, number> = {};
  let dragWriteTimer: ReturnType<typeof setTimeout> | null = null;
  let lastDragWrite = 0;
  function throttledWrite(fn: () => void): void {
    const wait = 120 - (Date.now() - lastDragWrite);
    if (wait <= 0) {
      lastDragWrite = Date.now();
      fn();
    } else {
      if (dragWriteTimer) clearTimeout(dragWriteTimer);
      dragWriteTimer = setTimeout(() => {
        lastDragWrite = Date.now();
        dragWriteTimer = null;
        fn();
      }, wait);
    }
  }
  function clearDragTimer(): void {
    if (dragWriteTimer) {
      clearTimeout(dragWriteTimer);
      dragWriteTimer = null;
    }
  }
  // Resolve the axis kinds at write time from the live row text (never trust a
  // grab-time snapshot that a recompute could stale).
  function pointSource(rowId: number, coordIndex: number): { x: AxisSource; y: AxisSource } | null {
    const row = graph.rows.find((r) => r.id === rowId);
    if (!row) return null;
    const spec = classifyRow(row.text.trim());
    if (spec.t !== "pointish" || coordIndex < 0 || coordIndex >= spec.coords.length) return null;
    const [xe, ye] = spec.coords[coordIndex];
    return { x: axisSource(xe), y: axisSource(ye) };
  }
  function onPointDragStart(): void {
    overridesBeforeDrag = { ...overrides };
  }
  function onPointDrag(rowId: number, coordIndex: number, wx: number, wy: number): void {
    const src = pointSource(rowId, coordIndex);
    if (!src) return;
    if (src.x.kind === "var") clearRelease(src.x.name);
    if (src.y.kind === "var") clearRelease(src.y.name);
    throttledWrite(() => {
      let ov = overrides;
      if (src.x.kind === "var") ov = { ...ov, [src.x.name]: snap(wx) };
      if (src.y.kind === "var") ov = { ...ov, [src.y.name]: snap(wy) };
      if (ov !== overrides) overrides = ov;
    });
  }
  function onPointCommit(rowId: number, coordIndex: number, wx: number, wy: number): void {
    clearDragTimer();
    const src = pointSource(rowId, coordIndex);
    if (!src) return;
    if (src.x.kind === "var") commitSlider(src.x.name, wx);
    if (src.y.kind === "var") commitSlider(src.y.name, wy);
    const xLit = src.x.kind === "literal" ? String(snap(wx)) : null;
    const yLit = src.y.kind === "literal" ? String(snap(wy)) : null;
    if (xLit !== null || yLit !== null) {
      const row = graph.rows.find((r) => r.id === rowId);
      if (row) graph.setText(rowId, rebuildPointRow(row.text, coordIndex, xLit, yLit));
    }
  }
  function onPointDragCancel(): void {
    clearDragTimer();
    overrides = { ...overridesBeforeDrag };
  }

  // A "y =" lead only for a bare function expression (clarifies that a bare
  // expression is y=f(x)); rows that already carry x=/r=/=/(…) are self-evident.
  function leadFor(text: string): string {
    return classifyRow(text).t === "function" && !text.includes("=") ? "y =" : "";
  }

  // --- share & export --------------------------------------------------------
  let canvasComp: ReturnType<typeof GraphCanvas> | undefined = $state();
  let shareState = $state<"idle" | "copied" | "error">("idle");
  let shareTimer: ReturnType<typeof setTimeout> | null = null;

  const PLOT_SYMS = new Set(["x", "y", "r", "t", "theta", "θ"]);
  // Every variable the document actually depends on — across ALL rows (visible
  // or hidden) and resolved to its concrete numeric value through the session
  // (following indirect refs like b := a := 3) — so a link reproduces the graph
  // exactly. Definitions are recreated by their own `name = expr` rows.
  async function gatherSharedVars(): Promise<{ name: string; value: string }[]> {
    const syms = new Set<string>();
    for (const row of graph.rows) {
      if (!row.text.trim()) continue;
      const spec = classifyRow(row.text.trim());
      for (const ex of specExprs(spec)) {
        const a = await call("analyze", [stripCalc(ex)]);
        if (a.ok && "symbols" in a) for (const s of a.symbols) syms.add(s);
      }
    }
    const out: { name: string; value: string }[] = [];
    for (const name of syms) {
      if (PLOT_SYMS.has(name) || definedByGraph.has(name)) continue;
      const v = await evalCoord(name);
      if (v !== null) out.push({ name, value: overrideValue(v) });
    }
    return out;
  }
  // Legacy clipboard path for contexts where navigator.clipboard is blocked.
  function legacyCopy(text: string): boolean {
    try {
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.select();
      const ok = document.execCommand("copy");
      ta.remove();
      return ok;
    } catch {
      return false;
    }
  }

  async function shareLink(): Promise<void> {
    const snap = graph.snapshot();
    const varList = await gatherSharedVars();
    const encoded = encodeState({ v: 1, rows: snap.rows, view: snap.view, vars: varList });
    const url = `${location.origin}${location.pathname}#g=${encoded}`;
    let copied = false;
    try {
      await navigator.clipboard.writeText(url);
      copied = true;
    } catch {
      copied = legacyCopy(url);
    }
    shareState = copied ? "copied" : "error";
    if (shareTimer) clearTimeout(shareTimer);
    shareTimer = setTimeout(() => (shareState = "idle"), 2200);
  }

  onDestroy(() => {
    if (shareTimer) clearTimeout(shareTimer);
    clearDragTimer();
    for (const t of releaseTimers.values()) clearTimeout(t);
  });

  async function savePNG(): Promise<void> {
    const blob = await canvasComp?.exportBlob();
    if (!blob) return;
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "mathsolver-graph.png";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  // --- expression list actions ----------------------------------------------
  let editingColor = $state<number | null>(null);
  function num(e: Event): number {
    return Number((e.currentTarget as HTMLInputElement).value);
  }
  function onRowKey(e: KeyboardEvent, i: number): void {
    if (e.key === "Enter") {
      e.preventDefault();
      if (i === graph.rows.length - 1) graph.addRow();
    }
  }
</script>

<div class="calc" class:compact>
  <div class="list" role="group" aria-label="Expressions">
    {#if !compact}
      <div class="calc-toolbar">
        <button class="tool-btn" onclick={shareLink} title="Copy a shareable link to this graph">
          {shareState === "copied" ? "✓ Link copied" : shareState === "error" ? "Copy failed" : "Share"}
        </button>
        <button class="tool-btn" onclick={savePNG} title="Download the graph as a PNG image">Save PNG</button>
      </div>
    {/if}
    <ul class="rows">
      {#each graph.rows as row, i (row.id)}
        <li class="row" class:hasError={!!rowErrors[row.id]}>
          <div class="row-main">
            <button
              class="dot"
              style:background={row.visible ? row.color : "transparent"}
              style:border-color={row.color}
              title={row.visible ? "Hide" : "Show"}
              aria-label={row.visible ? "Hide expression" : "Show expression"}
              onclick={() => graph.toggleVisible(row.id)}
            ></button>
            <div class="expr-wrap">
              {#if leadFor(row.text)}
                <span class="ylead" aria-hidden="true">{leadFor(row.text)}</span>
              {/if}
              <input
                class="expr"
                type="text"
                value={row.text}
                placeholder="f(x)"
                spellcheck="false"
                autocapitalize="off"
                autocomplete="off"
                aria-label={`Expression ${i + 1}`}
                oninput={(e) => graph.setText(row.id, (e.currentTarget as HTMLInputElement).value)}
                onkeydown={(e) => onRowKey(e, i)}
              />
            </div>
            <button
              class="row-del"
              title="Delete"
              aria-label={`Delete expression ${i + 1}`}
              onclick={() => graph.removeRow(row.id)}
            >×</button>
          </div>
          {#if editingColor === row.id}
            <div class="swatches" role="group" aria-label="Color">
              {#each GRAPH_COLORS as c (c)}
                <button
                  class="swatch"
                  style:background={c}
                  aria-label={`Color ${c}`}
                  onclick={() => {
                    graph.setColor(row.id, c);
                    editingColor = null;
                  }}
                ></button>
              {/each}
              <input
                class="swatch-custom"
                type="color"
                value={row.color}
                title="Custom color"
                aria-label="Custom color"
                oninput={(e) => graph.setColor(row.id, (e.currentTarget as HTMLInputElement).value)}
              />
            </div>
          {/if}
          {#if rowErrors[row.id]}
            <p class="row-err">{rowErrors[row.id]}</p>
          {/if}
          <button
            class="color-toggle"
            title="Change color"
            aria-label="Change color"
            onclick={() => (editingColor = editingColor === row.id ? null : row.id)}
          ></button>
        </li>
      {/each}
    </ul>
    <button class="add-row" onclick={() => graph.addRow()}>＋ Add expression</button>

    {#if slots.length > 0}
      <div class="sliders" role="group" aria-label="Variables">
        {#each slots as s (s.name)}
          <div class="slot">
            <div class="slot-head">
              <code class="slot-name">{s.name}</code>
              <span class="slot-eq">=</span>
              <input
                class="slot-val"
                type="number"
                step="any"
                value={s.value}
                aria-label={`${s.name} value`}
                onchange={(e) => commitSlider(s.name, num(e))}
              />
            </div>
            <div class="slot-slider">
              <input
                class="slot-bound"
                type="number"
                step="any"
                value={s.lo}
                aria-label={`${s.name} minimum`}
                onchange={(e) => setRange(s.name, num(e), s.hi)}
              />
              <input
                type="range"
                min={s.lo}
                max={s.hi}
                step={(s.hi - s.lo) / 200 || "any"}
                value={s.value}
                aria-label={`${s.name} slider`}
                oninput={(e) => slider(s.name, num(e))}
                onchange={(e) => commitSlider(s.name, num(e))}
              />
              <input
                class="slot-bound"
                type="number"
                step="any"
                value={s.hi}
                aria-label={`${s.name} maximum`}
                onchange={(e) => setRange(s.name, s.lo, num(e))}
              />
            </div>
          </div>
        {/each}
      </div>
    {/if}
  </div>

  <div class="graph-area" bind:this={graphArea}>
    <GraphCanvas
      bind:this={canvasComp}
      bind:view={graph.view}
      {series}
      {handles}
      height={compact ? 320 : 0}
      {onPointDragStart}
      {onPointDrag}
      {onPointCommit}
      {onPointDragCancel}
    />
  </div>
</div>

<style>
  .calc {
    display: grid;
    grid-template-columns: minmax(240px, 340px) minmax(0, 1fr);
    gap: 0.75rem;
    min-height: 30rem;
    min-width: 0;
  }
  .calc.compact {
    grid-template-columns: 1fr;
    min-height: 0;
  }

  /* Expression list --------------------------------------------------------- */
  .list {
    display: flex;
    flex-direction: column;
    min-width: 0;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg-panel);
    overflow: hidden;
  }
  .calc-toolbar {
    display: flex;
    gap: 0.35rem;
    padding: 0.4rem 0.5rem;
    border-bottom: 1px solid var(--border);
    flex: 0 0 auto;
  }
  .tool-btn {
    font: inherit;
    font-size: 0.78rem;
    color: var(--fg-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 5px;
    padding: 0.2rem 0.6rem;
    cursor: pointer;
  }
  .tool-btn:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .rows {
    list-style: none;
    margin: 0;
    padding: 0;
    overflow-y: auto;
  }
  .row {
    position: relative;
    border-bottom: 1px solid var(--border);
    padding: 0.35rem 0.4rem 0.35rem 0;
  }
  .row.hasError {
    background: color-mix(in srgb, var(--error) 5%, transparent);
  }
  .row-main {
    display: flex;
    align-items: center;
    gap: 0.15rem;
  }
  .dot {
    flex: 0 0 auto;
    width: 1.15rem;
    height: 1.15rem;
    margin: 0 0.3rem 0 0.5rem;
    border-radius: 50%;
    border: 2px solid;
    cursor: pointer;
    padding: 0;
  }
  .expr-wrap {
    display: flex;
    align-items: baseline;
    gap: 0.25rem;
    flex: 1;
    min-width: 0;
  }
  .ylead {
    font-family: var(--font-mono);
    font-size: 0.82rem;
    color: var(--fg-muted);
    flex: 0 0 auto;
  }
  .expr {
    flex: 1;
    min-width: 0;
    font-family: var(--font-mono);
    font-size: 0.92rem;
    color: var(--fg);
    background: transparent;
    border: none;
    padding: 0.2rem 0;
  }
  .expr:focus {
    outline: none;
  }
  .expr::placeholder {
    color: var(--fg-muted);
    opacity: 0.6;
  }
  .row-del {
    flex: 0 0 auto;
    width: 1.4rem;
    height: 1.4rem;
    font-size: 1rem;
    line-height: 1;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    border-radius: 4px;
    cursor: pointer;
    opacity: 0;
  }
  .row:hover .row-del,
  .row:focus-within .row-del {
    opacity: 1;
  }
  .row-del:hover {
    color: var(--error);
  }
  .color-toggle {
    position: absolute;
    left: 0.5rem;
    top: 0.4rem;
    width: 1.15rem;
    height: 1.15rem;
    border-radius: 50%;
    border: none;
    background: transparent;
    cursor: pointer;
  }
  .swatches {
    display: flex;
    flex-wrap: wrap;
    gap: 0.25rem;
    padding: 0.35rem 0.5rem 0.1rem 2.2rem;
  }
  .swatch {
    width: 1.1rem;
    height: 1.1rem;
    border-radius: 50%;
    border: 1px solid color-mix(in srgb, var(--fg) 15%, transparent);
    cursor: pointer;
  }
  .swatch-custom {
    width: 1.3rem;
    height: 1.3rem;
    padding: 0;
    border: 1px solid color-mix(in srgb, var(--fg) 15%, transparent);
    border-radius: 50%;
    background: none;
    cursor: pointer;
  }
  .swatch-custom::-webkit-color-swatch-wrapper {
    padding: 1px;
  }
  .swatch-custom::-webkit-color-swatch {
    border: none;
    border-radius: 50%;
  }
  .row-err {
    margin: 0.1rem 0 0 2.2rem;
    font-size: 0.72rem;
    color: var(--error);
  }
  .add-row {
    flex: 0 0 auto;
    text-align: left;
    font: inherit;
    font-size: 0.82rem;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    border-bottom: 1px solid var(--border);
    padding: 0.5rem 0.75rem;
    cursor: pointer;
  }
  .add-row:hover {
    color: var(--accent);
    background: color-mix(in srgb, var(--accent) 5%, transparent);
  }

  /* Auto-variable sliders --------------------------------------------------- */
  .sliders {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    padding: 0.6rem 0.7rem;
  }
  .slot {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .slot-head {
    display: flex;
    align-items: baseline;
    gap: 0.35rem;
    font-size: 0.85rem;
  }
  .slot-name {
    font-family: var(--font-mono);
    color: var(--accent);
    font-weight: 600;
  }
  .slot-eq {
    color: var(--fg-muted);
  }
  .slot-val {
    width: 4.5em;
    font-family: var(--font-mono);
    font-size: 0.8rem;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 0.1rem 0.3rem;
  }
  .slot-slider {
    display: grid;
    grid-template-columns: 3.4em minmax(0, 1fr) 3.4em;
    align-items: center;
    gap: 0.4rem;
  }
  .slot-slider input[type="range"] {
    width: 100%;
    accent-color: var(--accent);
  }
  .slot-bound {
    width: 100%;
    font-family: var(--font-mono);
    font-size: 0.72rem;
    color: var(--fg-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 0.08rem 0.25rem;
  }

  /* Graph ------------------------------------------------------------------- */
  .graph-area {
    min-width: 0;
    display: flex;
  }
  .graph-area :global(.graph) {
    flex: 1;
  }

  @media (max-width: 720px) {
    .calc {
      grid-template-columns: 1fr;
    }
    .graph-area {
      min-height: 24rem;
    }
  }
</style>
