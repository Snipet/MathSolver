<script lang="ts">
  import type { Ok } from "../outcome";
  import type { TransformResult } from "../engine/types";
  import Katex from "./Katex.svelte";
  import SourceFields from "./SourceFields.svelte";

  let { result }: { result: Ok<TransformResult> } = $props();
</script>

<Katex latex={result.latex} display />
{#if result.notes?.length}
  <ul class="notes">
    {#each result.notes as note (note)}
      <li>{note}</li>
    {/each}
  </ul>
{/if}
<SourceFields
  fields={[
    { label: "Plain", text: result.plain },
    { label: "LaTeX", text: result.latex },
  ]}
/>

<style>
  .notes {
    margin: 0.5rem 0 0;
    padding-left: 1.1rem;
    font-size: 0.82rem;
    color: var(--muted, #7a7a85);
  }
  .notes li {
    margin: 0.15rem 0;
  }
</style>
