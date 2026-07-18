<script lang="ts">
  // Generic renderer for a plugin result: a title plus declarative UI blocks
  // (kv / table / series / text — see mathsolver/plugin.hpp and
  // docs/PLUGINS.md). No plugin-specific code: any plugin that emits the
  // block contract renders here.
  import type { PluginCallResult } from "../engine/types";
  import type { Ok } from "../outcome";
  import { fmt } from "../format";
  import CopyButton from "./CopyButton.svelte";
  import SeriesChart from "./SeriesChart.svelte";

  let {
    plugin,
    command,
    result,
  }: { plugin: string; command: string; result: Ok<PluginCallResult> } = $props();

  /** Display form of a table cell: numbers compacted to ~6 significant
   * digits (the Copy button exports full precision). */
  function cellText(cell: string | number): string {
    return typeof cell === "number" ? fmt(cell) : cell;
  }

  /** Full-precision TSV of a table block, for pasting into other tools. */
  function tableTsv(columns: string[], rows: (string | number)[][]): string {
    return [columns.join("\t"), ...rows.map((r) => r.join("\t"))].join("\n");
  }
</script>

<div class="plugin-result">
  <div class="head">
    <span class="badge">{plugin}.{command}</span>
    <h3 class="title">{result.title}</h3>
  </div>

  {#each result.blocks as block, i (i)}
    {#if block.type === "kv"}
      <dl class="kv">
        {#each block.items as [k, v] (k)}
          <div class="kv-item">
            <dt>{k}</dt>
            <dd>{v}</dd>
          </div>
        {/each}
      </dl>
    {:else if block.type === "table"}
      <div class="block-head">
        {#if block.title}
          <p class="block-title">{block.title}</p>
        {/if}
        <CopyButton
          text={tableTsv(block.columns, block.rows)}
          label="Copy table as TSV (full precision)"
        />
      </div>
      <div class="table-scroll">
        <table>
          <thead>
            <tr>
              {#each block.columns as c (c)}
                <th>{c}</th>
              {/each}
            </tr>
          </thead>
          <tbody>
            {#each block.rows as row, r (r)}
              <tr>
                {#each row as cell, ci (ci)}
                  <td>{cellText(cell)}</td>
                {/each}
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
    {:else if block.type === "series"}
      {#if block.title}
        <p class="block-title">{block.title}</p>
      {/if}
      <SeriesChart
        x={block.x}
        series={block.series}
        xlabel={block.xlabel}
        ylabel={block.ylabel}
        logx={block.logx ?? false}
        equal={block.equal ?? false}
        vlines={block.vlines ?? []}
      />
    {:else if block.type === "text"}
      {#each block.lines as ln (ln)}
        <p class="text-line">{ln}</p>
      {/each}
    {/if}
  {/each}
</div>

<style>
  .plugin-result {
    display: flex;
    flex-direction: column;
    gap: 0.7rem;
    min-width: 0;
  }
  .head {
    display: flex;
    align-items: baseline;
    gap: 0.6rem;
    flex-wrap: wrap;
  }
  .badge {
    font-family: var(--font-mono);
    font-size: 0.75rem;
    color: var(--accent);
    border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
    border-radius: 999px;
    padding: 0.1rem 0.55rem;
    background: color-mix(in srgb, var(--accent) 8%, transparent);
    white-space: nowrap;
  }
  .title {
    margin: 0;
    font-size: 1rem;
    font-weight: 600;
  }
  .kv {
    margin: 0;
    display: flex;
    gap: 0.4rem 1.4rem;
    flex-wrap: wrap;
  }
  .kv-item {
    display: flex;
    align-items: baseline;
    gap: 0.4rem;
  }
  .kv dt {
    font-size: 0.8rem;
    color: var(--fg-muted);
  }
  .kv dd {
    margin: 0;
    font-family: var(--font-mono);
    font-size: 0.85rem;
  }
  .block-title {
    margin: 0 0 -0.35rem;
    font-size: 0.82rem;
    font-weight: 600;
    color: var(--fg-muted);
  }
  .block-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.6rem;
  }
  .block-head .block-title {
    margin: 0;
  }
  .table-scroll {
    overflow-x: auto;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
  }
  table {
    border-collapse: collapse;
    width: 100%;
    font-family: var(--font-mono);
    font-size: 0.82rem;
  }
  th,
  td {
    text-align: right;
    padding: 0.3rem 0.65rem;
    border-bottom: 1px solid var(--border);
    white-space: nowrap;
  }
  th {
    color: var(--fg-muted);
    font-weight: 600;
    background: var(--bg);
    position: sticky;
    top: 0;
  }
  tbody tr:last-child td {
    border-bottom: none;
  }
  .text-line {
    margin: 0;
    font-size: 0.82rem;
    color: var(--fg-muted);
  }
</style>
