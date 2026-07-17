<script lang="ts">
  import type { Ok } from "../outcome";
  import type { SystemResult } from "../engine/types";
  import { varLatex } from "../format";
  import Katex from "./Katex.svelte";
  import MethodMeta from "./MethodMeta.svelte";
  import CopyField from "./CopyField.svelte";
  import CopyButton from "./CopyButton.svelte";

  let { result }: { result: Ok<SystemResult> } = $props();

  const allPlain = $derived(
    result.values.map((v) => `${v.symbol} = ${v.plain}`).join("; "),
  );
  const allLatex = $derived(
    result.values.map((v) => `${varLatex(v.symbol)} = ${v.latex}`).join(",\\; "),
  );
</script>

{#if result.status === "noSolution"}
  <p class="banner">No solution (inconsistent system)</p>
{:else if result.status === "unsolved"}
  <p class="banner">Could not solve the system</p>
{:else}
  <ul class="values">
    {#each result.values as v (v.symbol)}
      <li class="line">
        <Katex latex={`${varLatex(v.symbol)} = ${v.latex}`} />
        <CopyButton
          text={`${varLatex(v.symbol)} = ${v.latex}`}
          label={`Copy ${v.symbol} as LaTeX`}
        />
      </li>
    {/each}
  </ul>
  {#if result.values.length > 0}
    <div class="sources">
      <CopyField label="Plain" text={allPlain} />
      <CopyField label="LaTeX" text={allLatex} />
    </div>
  {/if}
  {#if result.free.length}
    <div class="free-row">
      {#each result.free as f (f)}
        <span class="free-chip">free: {f}</span>
      {/each}
    </div>
  {/if}
{/if}
<MethodMeta method={result.method} warnings={result.warnings} />

<style>
  .banner {
    margin: 0;
    padding: 0.5rem 0.75rem;
    border-radius: calc(var(--radius) / 2);
    font-size: 0.95rem;
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--fg-muted);
  }
  .values {
    list-style: none;
    margin: 0;
    padding: 0;
    display: grid;
    gap: 0.5rem;
  }
  .line {
    display: flex;
    align-items: center;
    gap: 0.6rem;
    font-size: 1.1rem;
    overflow-x: auto;
  }
  .sources {
    margin-top: 0.75rem;
    display: grid;
    gap: 0.4rem;
  }
  .free-row {
    margin-top: 0.6rem;
    display: flex;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .free-chip {
    font-size: 0.8rem;
    color: var(--fg-muted);
    border: 1px dashed var(--border);
    border-radius: 999px;
    padding: 0.05rem 0.6rem;
    background: var(--bg);
  }
</style>
