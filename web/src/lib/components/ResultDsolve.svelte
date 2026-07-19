<script lang="ts">
  import type { Ok } from "../outcome";
  import type { DsolveResult } from "../engine/types";
  import Katex from "./Katex.svelte";
  import SourceFields from "./SourceFields.svelte";

  let { result }: { result: Ok<DsolveResult> } = $props();
</script>

{#if result.implicit}
  <Katex latex={`${result.latex} = 0`} display />
{:else if result.latex.startsWith("\\begin{aligned}")}
  <!-- A system: the aligned block already names each component. -->
  <Katex latex={result.latex} display />
{:else}
  <Katex latex={`y(t) = ${result.latex}`} display />
{/if}
{#if result.transformLatex}
  <p class="transform">
    <span class="lead">via</span>
    <Katex latex={`Y(s) = ${result.transformLatex}`} />
  </p>
{:else if result.method}
  <p class="transform"><span class="lead">method</span>{result.method}</p>
{/if}
{#each result.warnings as w (w)}
  <p class="warning">{w}</p>
{/each}
<SourceFields
  fields={[
    { label: "Plain", text: result.plain },
    { label: "LaTeX", text: result.latex },
  ]}
/>

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
</style>
