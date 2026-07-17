<script lang="ts">
  import type { Ok } from "../outcome";
  import type { EvaluateResult } from "../engine/types";
  import { fmt } from "../format";
  import CopyField from "./CopyField.svelte";

  let { result }: { result: Ok<EvaluateResult> } = $props();
</script>

{#if result.value === null}
  <p class="undef">Undefined for the given values</p>
{:else}
  <p class="value">{fmt(result.value)}</p>
  <div class="sources">
    <CopyField label="Value" text={fmt(result.value)} />
  </div>
{/if}

<style>
  .sources {
    margin-top: 0.75rem;
    display: grid;
    gap: 0.4rem;
  }
  .value {
    margin: 0;
    font-family: var(--font-mono);
    font-size: 2rem;
    font-weight: 600;
    overflow-wrap: anywhere;
  }
  .undef {
    margin: 0;
    color: var(--fg-muted);
    font-size: 1rem;
  }
</style>
