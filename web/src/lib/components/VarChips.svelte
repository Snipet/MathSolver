<script module lang="ts">
  // Active-assignment indicator chips under the main input (§9.1): which
  // bindings will be substituted into the current input, which are ignored
  // for the operation (amber), and which equation names don't apply (muted).
  export interface Chip {
    kind: "applied" | "ignored" | "muted";
    label: string;
    /** Panel row to focus when the chip is clicked. */
    symbol?: string;
  }
</script>

<script lang="ts">
  import { vars } from "../vars.svelte";

  let { chips }: { chips: Chip[] } = $props();

  function focusSymbol(symbol: string | undefined) {
    if (!symbol) return;
    const row = vars.rows.find((r) => r.status.symbol === symbol);
    if (row) vars.focusRow(row.id);
  }
</script>

{#if chips.length}
  <div class="chips" role="group" aria-label="Active variable assignments">
    {#each chips as c (c.kind + c.label)}
      <button
        class={"chip " + c.kind}
        onclick={() => focusSymbol(c.symbol)}
        title="Show in the Variables panel"
      >
        {c.label}
      </button>
    {/each}
  </div>
{/if}

<style>
  .chips {
    display: flex;
    gap: 0.35rem;
    flex-wrap: wrap;
  }
  .chip {
    font-family: var(--font-mono);
    font-size: 0.75rem;
    border-radius: 999px;
    padding: 0.12rem 0.6rem;
    cursor: pointer;
    border: 1px solid var(--border);
    background: var(--bg-panel);
    color: var(--fg-muted);
    max-width: 100%;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .chip.applied {
    color: var(--accent);
    border-color: color-mix(in srgb, var(--accent) 45%, transparent);
    background: color-mix(in srgb, var(--accent) 8%, var(--bg-panel));
  }
  .chip.ignored {
    color: var(--warn-fg);
    border-color: color-mix(in srgb, var(--warn-fg) 45%, transparent);
    background: var(--warn-bg);
  }
  .chip.muted {
    font-style: italic;
  }
  .chip:hover {
    border-color: var(--accent);
  }
</style>
