<script lang="ts">
  import type { Ok } from "../outcome";
  import type { SolveResult } from "../engine/types";
  import { fmt, varLatex } from "../format";
  import Katex from "./Katex.svelte";
  import MethodMeta from "./MethodMeta.svelte";

  let { variable, result }: { variable: string; result: Ok<SolveResult> } =
    $props();
</script>

{#if result.status === "noRealSolution"}
  <p class="banner muted-banner">No real solutions</p>
{:else if result.status === "allReals"}
  <p class="banner ok-banner">True for all values of {variable}</p>
{:else if result.status === "unsolved"}
  <p class="banner muted-banner">Could not solve</p>
{:else}
  <ul class="solutions">
    {#each result.solutions as sol, i (i)}
      <li>
        <div class="line">
          <Katex latex={`${varLatex(variable)} = ${sol.latex}`} />
          {#if sol.exact && sol.approx !== null}
            <span class="approx">≈ {fmt(sol.approx)}</span>
          {/if}
        </div>
        {#if sol.note}
          <p class="note">{sol.note}</p>
        {/if}
      </li>
    {/each}
  </ul>
{/if}
<MethodMeta method={result.method} warnings={result.warnings} />

<style>
  .banner {
    margin: 0;
    padding: 0.5rem 0.75rem;
    border-radius: calc(var(--radius) / 2);
    font-size: 0.95rem;
  }
  .muted-banner {
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--fg-muted);
  }
  .ok-banner {
    background: color-mix(in srgb, var(--ok) 12%, transparent);
    border: 1px solid color-mix(in srgb, var(--ok) 40%, transparent);
    color: var(--fg);
  }
  .solutions {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    gap: 0.5rem;
  }
  .line {
    display: flex;
    align-items: baseline;
    gap: 0.6rem;
    flex-wrap: wrap;
    font-size: 1.1rem;
    overflow-x: auto;
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
  .note {
    margin: 0.1rem 0 0;
    font-size: 0.8rem;
    color: var(--fg-muted);
  }
</style>
