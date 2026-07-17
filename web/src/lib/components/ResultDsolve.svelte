<script lang="ts">
  import type { Ok } from "../outcome";
  import type { DsolveResult } from "../engine/types";
  import Katex from "./Katex.svelte";
  import CopyField from "./CopyField.svelte";

  let { result }: { result: Ok<DsolveResult> } = $props();
</script>

<Katex latex={`y(t) = ${result.latex}`} display />
<p class="transform">
  <span class="lead">via</span>
  <Katex latex={`Y(s) = ${result.transformLatex}`} />
</p>
{#each result.warnings as w (w)}
  <p class="warning">{w}</p>
{/each}
<div class="sources">
  <CopyField label="Plain" text={result.plain} />
  <CopyField label="LaTeX" text={result.latex} />
</div>

<style>
  .transform {
    margin: 0.35rem 0 0;
    color: var(--text-dim);
    font-size: 0.85rem;
  }
  .transform .lead {
    margin-right: 0.35rem;
  }
  .warning {
    margin: 0.3rem 0 0;
    color: var(--warn, #b58900);
    font-size: 0.8rem;
  }
  .sources {
    margin-top: 0.75rem;
    display: grid;
    gap: 0.4rem;
  }
</style>
