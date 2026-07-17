<script lang="ts">
  import type { Cell } from "../notebook/notebook.svelte";
  import ResultCard from "./ResultCard.svelte";

  let { cell, index }: { cell: Cell; index: number } = $props();

  const result = $derived(cell.result);
  const isMessage = $derived(result?.kind === "message");
</script>

<div class="cell">
  <div class="line in-line">
    <span class="label in-label">In[{index}]:=</span>
    <code class="src">{cell.input}</code>
  </div>

  <div class="line out-line">
    <span class="label out-label">Out[{index}]=</span>
    <div class="out-body">
      {#if result === null}
        <span class="pending">computing…</span>
      {:else if result.kind === "message"}
        <div class="message" class:muted={result.tone === "muted"}>
          {#if result.title}
            <p class="message-title">{result.title}</p>
          {/if}
          {#each result.lines as ln (ln)}
            <p class="message-line">{ln}</p>
          {/each}
        </div>
      {:else}
        <ResultCard outcome={result} />
      {/if}
    </div>
  </div>
</div>

<style>
  .cell {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    padding: 0.35rem 0;
  }
  .line {
    display: grid;
    grid-template-columns: 4.6rem minmax(0, 1fr);
    gap: 0.5rem;
    align-items: start;
  }
  .label {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    line-height: 1.7;
    user-select: none;
    white-space: nowrap;
  }
  .in-label {
    color: var(--fg-muted);
  }
  .out-label {
    color: var(--accent);
  }
  .src {
    font-family: var(--font-mono);
    font-size: 0.95rem;
    line-height: 1.6;
    color: var(--fg);
    white-space: pre-wrap;
    word-break: break-word;
    min-width: 0;
  }
  .out-body {
    min-width: 0;
  }
  .pending {
    font-size: 0.9rem;
    color: var(--fg-muted);
    font-style: italic;
  }
  .message {
    font-family: var(--font-mono);
    font-size: 0.9rem;
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 0.6rem 0.8rem;
  }
  .message.muted {
    color: var(--fg-muted);
    font-style: italic;
    background: transparent;
    border-style: dashed;
  }
  .message-title {
    margin: 0 0 0.4rem;
    font-weight: 600;
    color: var(--fg);
  }
  .message-line {
    margin: 0.1rem 0;
    white-space: pre-wrap;
    word-break: break-word;
  }
</style>
