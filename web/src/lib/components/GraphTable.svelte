<script lang="ts">
  // Editable (x, y) data table for the grapher, with a regression selector.
  // Points plot as a scatter; choosing a model overlays the fitted curve and
  // shows its equation + R² (polynomial fits come back exact, via the CAS
  // `fit` verb — see GraphCalculator.buildTable).
  import { onDestroy } from "svelte";
  import { graph, type ExprRow, type FitModelName } from "../graph/graph.svelte";
  import { tableToTSV } from "../graph/table";

  interface ColStats {
    n: number;
    mean: string;
    median: string;
    stdev: string;
  }
  interface TableInfo {
    equation?: string;
    r2?: number;
    exact?: boolean;
    model?: string;
    error?: string;
    statsX?: ColStats;
    statsY?: ColStats;
  }
  interface Props {
    row: ExprRow;
    info?: TableInfo;
  }
  let { row, info }: Props = $props();

  const FIT_OPTIONS: { value: FitModelName; label: string }[] = [
    { value: "", label: "Points only" },
    { value: "linear", label: "Linear" },
    { value: "quadratic", label: "Quadratic" },
    { value: "cubic", label: "Cubic" },
    { value: "exp", label: "Exponential" },
    { value: "power", label: "Power" },
    { value: "log", label: "Logarithmic" },
  ];

  function onCell(index: number, axis: "x" | "y", e: Event): void {
    const value = (e.currentTarget as HTMLInputElement).value;
    graph.setPoint(row.id, index, axis === "x" ? { x: value } : { y: value });
  }
  function onName(axis: "x" | "y", e: Event): void {
    graph.setColName(row.id, axis, (e.currentTarget as HTMLInputElement).value);
  }
  function isBlank(index: number): boolean {
    const p = row.points[index];
    return !!p && p.x.trim() === "" && p.y.trim() === "";
  }

  const xName = $derived(row.xName ?? "x");
  const yName = $derived(row.yName ?? "y");

  let copyState = $state<"idle" | "copied" | "error">("idle");
  let copyTimer: ReturnType<typeof setTimeout> | undefined;
  async function copyTSV(): Promise<void> {
    const tsv = tableToTSV(xName, yName, row.points);
    try {
      await navigator.clipboard.writeText(tsv);
      copyState = "copied";
    } catch {
      copyState = "error";
    }
    clearTimeout(copyTimer);
    copyTimer = setTimeout(() => (copyState = "idle"), 1800);
  }
  onDestroy(() => clearTimeout(copyTimer));
</script>

<div class="table">
  <div class="grid" role="group" aria-label="Data points">
    <input
      class="head-name"
      value={xName}
      maxlength="16"
      spellcheck="false"
      aria-label="x column name"
      oninput={(e) => onName("x", e)}
    />
    <input
      class="head-name"
      value={yName}
      maxlength="16"
      spellcheck="false"
      aria-label="y column name"
      oninput={(e) => onName("y", e)}
    />
    <button
      class="copy"
      title="Copy the table as tab-separated values (with headers)"
      aria-label="Copy table data"
      onclick={copyTSV}
    >{copyState === "copied" ? "✓" : copyState === "error" ? "!" : "⧉"}</button>
    {#each row.points as p, i (i)}
      <input
        class="cell"
        inputmode="decimal"
        value={p.x}
        placeholder="—"
        spellcheck="false"
        aria-label={`x of point ${i + 1}`}
        oninput={(e) => onCell(i, "x", e)}
      />
      <input
        class="cell"
        inputmode="decimal"
        value={p.y}
        placeholder="—"
        spellcheck="false"
        aria-label={`y of point ${i + 1}`}
        oninput={(e) => onCell(i, "y", e)}
      />
      {#if isBlank(i)}
        <span class="del-spacer" aria-hidden="true"></span>
      {:else}
        <button
          class="del"
          title="Remove point"
          aria-label={`Remove point ${i + 1}`}
          onclick={() => graph.removePoint(row.id, i)}>×</button
        >
      {/if}
    {/each}
  </div>

  <label class="fit-row">
    <span class="fit-label">Fit</span>
    <select
      value={row.fit}
      onchange={(e) => graph.setFit(row.id, (e.currentTarget as HTMLSelectElement).value as FitModelName)}
    >
      {#each FIT_OPTIONS as o (o.value)}
        <option value={o.value}>{o.label}</option>
      {/each}
    </select>
  </label>

  {#if info?.error}
    <p class="fit-err">{info.error}</p>
  {:else if info?.equation}
    <div class="fit-out">
      <code class="eqn" style:color={row.color}>y = {info.equation}</code>
      <span class="stats">
        {#if info.exact}<span class="badge" title="Coefficients are exact (rational)">exact</span>{/if}
        {#if info.r2 !== undefined}<span class="r2">R² = {info.r2.toFixed(4)}</span>{/if}
      </span>
    </div>
  {/if}

  {#if info?.statsX || info?.statsY}
    <div class="col-stats" title="Exact summary statistics of each column">
      {#if info.statsX}
        <div class="stat-row">
          <span class="stat-col">{xName}</span>
          <span>mean {info.statsX.mean}</span><span>med {info.statsX.median}</span
          ><span>s {info.statsX.stdev}</span>
        </div>
      {/if}
      {#if info.statsY}
        <div class="stat-row">
          <span class="stat-col">{yName}</span>
          <span>mean {info.statsY.mean}</span><span>med {info.statsY.median}</span
          ><span>s {info.statsY.stdev}</span>
        </div>
      {/if}
    </div>
  {/if}
</div>

<style>
  .table {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    width: 100%;
  }
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr auto;
    gap: 0.25rem;
    align-items: center;
  }
  .head-name {
    width: 100%;
    box-sizing: border-box;
    padding: 0.15rem 0.3rem;
    font-family: var(--font-mono);
    font-size: 0.75rem;
    font-weight: 600;
    text-align: center;
    color: var(--fg-muted);
    border: 1px solid transparent;
    border-radius: 5px;
    background: transparent;
  }
  .head-name:hover {
    border-color: var(--border);
  }
  .head-name:focus {
    outline: none;
    border-color: var(--accent);
    color: var(--fg);
    background: var(--bg-inset);
  }
  .copy {
    width: 1.25rem;
    height: 1.25rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: none;
    border-radius: 4px;
    background: transparent;
    color: var(--fg-muted);
    font-size: 0.85rem;
    line-height: 1;
    cursor: pointer;
  }
  .copy:hover {
    color: var(--accent);
    background: var(--bg-inset);
  }
  .cell {
    width: 100%;
    box-sizing: border-box;
    padding: 0.25rem 0.4rem;
    font-family: var(--font-mono);
    font-size: 0.85rem;
    font-variant-numeric: tabular-nums;
    text-align: right;
    border: 1px solid var(--border);
    border-radius: 5px;
    background: var(--bg-inset);
    color: var(--fg);
  }
  .cell:focus {
    outline: none;
    border-color: var(--accent);
  }
  .del {
    width: 1.25rem;
    height: 1.25rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    border: none;
    border-radius: 4px;
    background: transparent;
    color: var(--fg-muted);
    font-size: 1rem;
    line-height: 1;
    cursor: pointer;
  }
  .del:hover {
    color: var(--error);
    background: var(--bg-inset);
  }
  .del-spacer {
    width: 1.25rem;
  }
  .fit-row {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.8rem;
  }
  .fit-label {
    color: var(--fg-muted);
    font-weight: 600;
  }
  .fit-row select {
    flex: 1;
    padding: 0.2rem 0.35rem;
    font: inherit;
    font-size: 0.82rem;
    border: 1px solid var(--border);
    border-radius: 5px;
    background: var(--bg-inset);
    color: var(--fg);
  }
  .fit-out {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    padding: 0.35rem 0.45rem;
    border-radius: 6px;
    background: var(--bg-inset);
    border: 1px solid var(--border);
  }
  .eqn {
    font-family: var(--font-mono);
    font-size: 0.82rem;
    word-break: break-word;
  }
  .stats {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    font-size: 0.75rem;
    color: var(--fg-muted);
    font-variant-numeric: tabular-nums;
  }
  .badge {
    padding: 0.05rem 0.35rem;
    border-radius: 999px;
    background: var(--accent);
    color: var(--accent-fg);
    font-size: 0.68rem;
    font-weight: 600;
    letter-spacing: 0.02em;
  }
  .fit-err {
    margin: 0;
    font-size: 0.78rem;
    color: var(--error);
  }
  .col-stats {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
    font-size: 0.75rem;
    color: var(--fg-muted);
    font-variant-numeric: tabular-nums;
  }
  .stat-row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.55rem;
    align-items: baseline;
  }
  .stat-col {
    font-family: var(--font-mono);
    font-weight: 600;
    color: var(--fg);
    min-width: 0.8rem;
  }
</style>
