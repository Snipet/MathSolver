<script lang="ts">
  import { tick } from "svelte";
  import { notebook } from "../notebook/notebook.svelte";
  import ExpressionInput from "./ExpressionInput.svelte";
  import NotebookCell from "./NotebookCell.svelte";

  let { ready = false, engineError = "" }: { ready?: boolean; engineError?: string } =
    $props();

  let input = $state("");
  let computing = $state(false);
  let recall = $state<number | null>(null);
  let listEl: HTMLDivElement | undefined = $state();
  let exprInput: ReturnType<typeof ExpressionInput> | undefined = $state();

  const EXAMPLES = [
    "2x + 3x",
    "solve x^2 = 4, x",
    "diff sin(x^2), x",
    "integrate x*sin(x), x",
    "factor x^2 - 5x + 6",
    "eval x^2 + y, x=3, y=0.5",
  ];

  async function scrollToBottom() {
    await tick();
    if (listEl) listEl.scrollTop = listEl.scrollHeight;
  }

  async function run() {
    const text = input.trim();
    if (!text || computing || !ready) return;
    computing = true;
    input = "";
    recall = null;
    void scrollToBottom();
    try {
      await notebook.run(text);
    } finally {
      computing = false;
      void scrollToBottom();
      exprInput?.focusEnd();
    }
  }

  async function runExample(ex: string) {
    if (computing || !ready) return;
    input = ex;
    await run();
  }

  function clearConsole() {
    notebook.clear();
    recall = null;
    exprInput?.focusEnd();
  }

  // Shell-style history recall on ArrowUp/ArrowDown (single-line input only, so
  // multi-line editing is never hijacked).
  function onkeydownextra(e: KeyboardEvent) {
    if (e.key !== "ArrowUp" && e.key !== "ArrowDown") {
      recall = null;
      return;
    }
    const inputs = notebook.inputs;
    if (inputs.length === 0) return;
    const ta = e.target as HTMLTextAreaElement | null;
    if (ta && ta.value.includes("\n")) return;
    e.preventDefault();
    if (e.key === "ArrowUp") {
      recall = recall === null ? inputs.length - 1 : Math.max(0, recall - 1);
      input = inputs[recall];
    } else if (recall !== null) {
      if (recall < inputs.length - 1) {
        recall += 1;
        input = inputs[recall];
      } else {
        recall = null;
        input = "";
      }
    }
  }
</script>

<div class="console">
  <div class="console-head">
    <div class="head-text">
      <span class="head-title">Console</span>
      <span class="head-hint">
        Type math line by line — <code>solve&nbsp;x^2&nbsp;=&nbsp;4,&nbsp;x</code>,
        <code>diff&nbsp;sin(x),&nbsp;x</code>, <code>a&nbsp;:=&nbsp;2</code>. Run
        <code>help</code> for the full grammar.
      </span>
    </div>
    <button
      class="clear-btn"
      onclick={clearConsole}
      disabled={notebook.cells.length === 0}
    >
      Clear
    </button>
  </div>

  <div class="cells" bind:this={listEl} role="log" aria-label="Console output">
    {#if notebook.cells.length === 0}
      <div class="empty">
        <p class="empty-lead">
          A programmable math console. Enter any expression to simplify it, an
          equation to solve it, or call a function directly.
        </p>
        <div class="examples" role="group" aria-label="Examples">
          {#each EXAMPLES as ex (ex)}
            <button class="example-chip" onclick={() => void runExample(ex)}>
              {ex}
            </button>
          {/each}
        </div>
      </div>
    {:else}
      {#each notebook.cells as cell, i (cell.id)}
        <NotebookCell {cell} index={i + 1} />
      {/each}
    {/if}
  </div>

  <div class="prompt">
    <ExpressionInput
      bind:this={exprInput}
      bind:value={input}
      placeholder={'Enter a command — e.g. solve x^2 = 4, x   (type "help")'}
      {computing}
      computeDisabled={!ready || computing || !input.trim()}
      oncompute={run}
      {onkeydownextra}
    />
    {#if engineError}
      <p class="engine-error" role="alert">
        The math engine failed to load ({engineError}). Reload the page to try
        again.
      </p>
    {:else if !ready}
      <p class="loading-note">loading engine…</p>
    {/if}
  </div>
</div>

<style>
  .console {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    padding-top: 1rem;
  }
  .console-head {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 1rem;
  }
  .head-text {
    display: flex;
    flex-direction: column;
    gap: 0.15rem;
    min-width: 0;
  }
  .head-title {
    font-weight: 700;
    font-size: 1.05rem;
    letter-spacing: -0.01em;
  }
  .head-hint {
    font-size: 0.85rem;
    color: var(--fg-muted);
  }
  .head-hint code {
    font-family: var(--font-mono);
    font-size: 0.82em;
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 0.05em 0.35em;
    white-space: nowrap;
  }
  .clear-btn {
    flex: 0 0 auto;
    font: inherit;
    font-size: 0.82rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.25rem 0.8rem;
    cursor: pointer;
  }
  .clear-btn:hover:not(:disabled) {
    color: var(--accent);
    border-color: var(--accent);
  }
  .clear-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }

  .cells {
    display: flex;
    flex-direction: column;
    gap: 0.2rem;
    max-height: min(60vh, 620px);
    overflow-y: auto;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg);
    padding: 0.6rem 0.85rem;
    scroll-behavior: smooth;
  }

  .empty {
    display: flex;
    flex-direction: column;
    gap: 0.75rem;
    padding: 0.75rem 0.25rem;
  }
  .empty-lead {
    margin: 0;
    font-size: 0.92rem;
    color: var(--fg-muted);
    max-width: 46ch;
  }
  .examples {
    display: flex;
    gap: 0.4rem;
    flex-wrap: wrap;
  }
  .example-chip {
    font-family: var(--font-mono);
    font-size: 0.8rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.2rem 0.7rem;
    cursor: pointer;
  }
  .example-chip:hover {
    color: var(--accent);
    border-color: var(--accent);
  }

  .prompt {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .loading-note {
    margin: 0;
    font-size: 0.82rem;
    color: var(--fg-muted);
    font-style: italic;
  }
  .engine-error {
    margin: 0;
    font-size: 0.88rem;
    color: var(--error);
    background: color-mix(in srgb, var(--error) 8%, transparent);
    border: 1px solid color-mix(in srgb, var(--error) 40%, transparent);
    border-radius: var(--radius);
    padding: 0.55rem 0.75rem;
  }
</style>
