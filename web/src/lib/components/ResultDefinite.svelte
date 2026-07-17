<script lang="ts">
  import type { Ok } from "../outcome";
  import type { DefiniteResult } from "../engine/types";
  import { fmt } from "../format";
  import Katex from "./Katex.svelte";
  import CopyField from "./CopyField.svelte";
  import MethodMeta from "./MethodMeta.svelte";

  let {
    from,
    to,
    result,
  }: { from: string; to: string; result: Ok<DefiniteResult> } = $props();
</script>

<p class="range">Definite integral from {from} to {to}</p>
{#if result.status === "unsolved"}
  <p class="banner">No closed form found</p>
  <p class="hint">The engine could not evaluate this integral.</p>
{:else}
  <div class="line">
    <Katex latex={result.latex} display />
    {#if result.approx !== undefined && result.approx !== null && result.status === "exact"}
      <span class="approx">≈ {fmt(result.approx)}</span>
    {/if}
  </div>
  <div class="sources">
    <CopyField label="Plain" text={result.plain} />
    <CopyField label="LaTeX" text={result.latex} />
  </div>
{/if}
<MethodMeta method={result.method} warnings={result.warnings} />

<style>
  .range {
    margin: 0 0 0.35rem;
    font-size: 0.8rem;
    color: var(--fg-muted);
  }
  .line {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex-wrap: wrap;
  }
  .approx {
    font-size: 0.85rem;
    color: var(--fg-muted);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0 0.5rem;
    background: var(--bg);
    white-space: nowrap;
  }
  .banner {
    margin: 0;
    padding: 0.5rem 0.75rem;
    border-radius: calc(var(--radius) / 2);
    font-size: 0.95rem;
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--fg);
  }
  .hint {
    margin: 0.4rem 0 0;
    font-size: 0.85rem;
    color: var(--fg-muted);
  }
  .sources {
    margin-top: 0.75rem;
    display: grid;
    gap: 0.4rem;
  }
</style>
