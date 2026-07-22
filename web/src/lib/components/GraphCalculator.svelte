<script lang="ts">
  // Desmos-style graphing calculator: an expression list on the left, an
  // interactive graph on the right. Multiple explicit functions y=f(x) sample
  // through the WASM engine; free variables resolve against the app's session
  // variables and auto-create a slider (app-integrated — `a := 2` set anywhere
  // moves the graph, and a graph slider moves `a` everywhere).
  import { untrack } from "svelte";
  import { call } from "../engine";
  import { vars } from "../vars.svelte";
  import { applyEnv, overrideValue } from "../vars/session";
  import { graph, GRAPH_COLORS, type ExprRow } from "../graph/graph.svelte";
  import { xRange, type DrawSeries } from "../graph/viewport";
  import GraphCanvas from "./GraphCanvas.svelte";

  interface Props {
    /** Console/compact mode: shorter graph, list above. */
    compact?: boolean;
  }
  let { compact = false }: Props = $props();

  let series = $state<DrawSeries[]>([]);
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
  const autoCreated = new Set<string>();
  let overrides = $state<Record<string, number>>({});

  // --- measure the graph area (for sample count + visible range) ------------
  let graphArea: HTMLDivElement | undefined = $state();
  let graphW = $state(800);
  $effect(() => {
    const el = graphArea;
    if (!el) return;
    const ro = new ResizeObserver((e) => (graphW = Math.max(120, Math.floor(e[0].contentRect.width))));
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

  // --- reactive resample -----------------------------------------------------
  let seq = 0;
  $effect(() => {
    // Track: row text/visibility, viewport, session vars, slider overrides.
    const rowsSig = graph.rows.map((r) => `${r.id}:${r.text}:${r.visible}:${r.color}`).join("|");
    const v = graph.view;
    const activeSig = vars.active.map((b) => `${b.name}=${b.value}`).join("|");
    const ovSig = Object.entries(overrides).map(([k, x]) => `${k}=${x}`).join("|");
    const w = graphW;
    void rowsSig;
    void v;
    void activeSig;
    void ovSig;
    void w;
    const my = ++seq;
    const timer = setTimeout(() => void recompute(my), 90);
    return () => clearTimeout(timer);
  });

  function reconcileVars(freeVars: Set<string>): void {
    untrack(() => {
      for (const name of freeVars) {
        const exists = vars.rows.some((r) => r.status.symbol === name);
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
          const row = vars.rows.find((r) => r.status.symbol === name);
          if (row) vars.remove(row.id);
          autoCreated.delete(name);
        }
      }
    });
  }

  async function recompute(my: number): Promise<void> {
    const vis = graph.rows.filter((r) => r.visible && r.text.trim());
    const analyses = await Promise.all(
      vis.map(async (r) => ({ r, a: await call("analyze", [r.text.trim()]) })),
    );
    if (my !== seq) return;

    const errs: Record<number, string> = {};
    const freeVars = new Set<string>();
    const fnRows: ExprRow[] = [];
    for (const { r, a } of analyses) {
      if (!a.ok) {
        errs[r.id] = a.error;
        continue;
      }
      if (a.kind === "expression") {
        if (a.symbols.every((s) => s === "x" || true)) fnRows.push(r);
        for (const s of a.symbols) if (s !== "x") freeVars.add(s);
      } else {
        // equations / systems (implicit, relations) — Phase 3.
        errs[r.id] = "relations & implicit curves are coming soon";
      }
    }
    rowErrors = errs;
    reconcileVars(freeVars);

    // Build the slider list from free vars that resolve to a number.
    const active = untrack(() => vars.active);
    const nextSlots: Slot[] = [];
    for (const name of freeVars) {
      const b = active.find((x) => x.name === name && x.kind === "expression");
      const sessionVal = b ? numericValue(b.value) : null;
      const val = overrides[name] ?? sessionVal;
      if (val === null || val === undefined) continue;
      const rg = ranges.get(name) ?? { lo: -10, hi: 10 };
      ranges.set(name, rg);
      nextSlots.push({ name, value: val, lo: rg.lo, hi: rg.hi });
    }
    slots = nextSlots;

    // Sample each function row over a range wider than the viewport, so a
    // small pan re-projects existing samples without gaps.
    const [xlo, xhi] = xRange(graph.view, graphW);
    const wx = xhi - xlo;
    const lo = xlo - 0.35 * wx;
    const hi = xhi + 0.35 * wx;
    const n = Math.max(200, Math.min(1600, Math.round(graphW * 1.6)));
    const xs = new Array(n);
    for (let i = 0; i < n; i++) xs[i] = lo + ((hi - lo) * i) / (n - 1);

    const out: DrawSeries[] = [];
    for (const r of fnRows) {
      try {
        const env = await applyEnv(r.text.trim(), ["x"], "expr", overrides);
        if (my !== seq) return;
        const sr = await call("sample", [env.text, "x", lo, hi, n]);
        if (my !== seq) return;
        if (sr.ok) {
          out.push({ id: String(r.id), color: r.color, xs, ys: sr.ys, visible: true });
        } else {
          rowErrors = { ...rowErrors, [r.id]: sr.error };
        }
      } catch (e) {
        rowErrors = { ...rowErrors, [r.id]: e instanceof Error ? e.message : String(e) };
      }
    }
    if (my === seq) series = out;
  }

  // --- slider write-through --------------------------------------------------
  function slider(name: string, value: number): void {
    overrides = { ...overrides, [name]: value };
  }
  function commitSlider(name: string, value: number): void {
    overrides = { ...overrides, [name]: value };
    const row = vars.rows.find((r) => r.status.symbol === name);
    if (row) vars.edit(row.id, { value: overrideValue(value) });
  }
  function setRange(name: string, lo: number, hi: number): void {
    if (!Number.isFinite(lo) || !Number.isFinite(hi) || lo >= hi) return;
    ranges.set(name, { lo, hi });
    // nudge a resample so the slot picks up the new range
    overrides = { ...overrides };
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
              <span class="ylead" aria-hidden="true">y =</span>
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
    <GraphCanvas bind:view={graph.view} {series} height={compact ? 320 : 0} />
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
