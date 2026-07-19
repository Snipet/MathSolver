<script lang="ts">
  import type { Cell } from "../notebook/notebook.svelte";
  import Katex from "./Katex.svelte";
  import ResultCard from "./ResultCard.svelte";

  interface Props {
    cell: Cell;
    index: number;
    /** Run this cell's input again (appends a fresh cell). */
    onrerun?: (input: string) => void;
    /** Load this cell's input into the prompt for editing. */
    onedit?: (input: string) => void;
  }

  let { cell, index, onrerun, onedit }: Props = $props();

  const result = $derived(cell.result);
</script>

<article class="cell">
  <div class="line in-line">
    <span class="label in-label">In[{index}]:=</span>
    <code class="src">{cell.input}</code>
    <div class="actions">
      <button
        class="action"
        onclick={() => onedit?.(cell.input)}
        title="Edit in the prompt"
        aria-label="Edit input {index} in the prompt"
      >
        edit
      </button>
      <button
        class="action"
        onclick={() => onrerun?.(cell.input)}
        title="Run again"
        aria-label="Run input {index} again"
      >
        rerun
      </button>
    </div>
  </div>

  {#if cell.inputLatex}
    <div class="line typeset-line" data-testid="cell-input-typeset">
      <span class="label" aria-hidden="true"></span>
      <div class="typeset">
        <Katex latex={cell.inputLatex} />
      </div>
    </div>
  {/if}

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
        <ResultCard outcome={result} flat />
      {/if}
    </div>
  </div>
</article>

<style>
  .cell {
    display: flex;
    flex-direction: column;
    gap: 0.45rem;
    padding: 0.8rem 0.25rem;
    border-bottom: 1px solid var(--border);
  }
  .cell:last-child {
    border-bottom: none;
  }
  .line {
    display: grid;
    grid-template-columns: 4.8rem minmax(0, 1fr) auto;
    gap: 0.6rem;
    align-items: start;
  }
  .out-line,
  .typeset-line {
    grid-template-columns: 4.8rem minmax(0, 1fr);
  }
  /* The input as the engine parsed it, typeset under the raw echo. */
  .typeset-line {
    margin-top: -0.15rem;
  }
  .typeset {
    min-width: 0;
    overflow-x: auto;
    font-size: 0.98rem;
    padding: 0.1rem 0 0.05rem 0.6rem;
    border-left: 2px solid var(--border);
  }
  .label {
    font-family: var(--font-mono);
    font-size: 0.76rem;
    line-height: 1.8;
    user-select: none;
    white-space: nowrap;
    text-align: right;
  }
  .in-label {
    color: var(--fg-muted);
  }
  .out-label {
    color: var(--accent);
    font-weight: 600;
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
  .actions {
    display: inline-flex;
    gap: 0.3rem;
    opacity: 0;
    transition: opacity 120ms ease;
  }
  .cell:hover .actions,
  .cell:focus-within .actions {
    opacity: 1;
  }
  .action {
    font: inherit;
    font-size: 0.72rem;
    color: var(--fg-muted);
    background: transparent;
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.05rem 0.55rem;
    cursor: pointer;
    white-space: nowrap;
  }
  .action:hover {
    color: var(--accent);
    border-color: var(--accent);
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
    font-size: 0.88rem;
  }
  .message.muted {
    color: var(--fg-muted);
    font-style: italic;
  }
  .message-title {
    margin: 0 0 0.35rem;
    font-weight: 600;
    color: var(--fg);
  }
  .message-line {
    margin: 0.1rem 0;
    white-space: pre-wrap;
    word-break: break-word;
  }

  @media (max-width: 640px) {
    .line,
    .out-line {
      grid-template-columns: minmax(0, 1fr);
    }
    .label {
      text-align: left;
      line-height: 1.2;
    }
    .actions {
      opacity: 1;
    }
  }
</style>
