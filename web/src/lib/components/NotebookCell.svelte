<script lang="ts">
  import { notebook, type Cell } from "../notebook/notebook.svelte";
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

  function num(e: Event): number {
    return Number((e.currentTarget as HTMLInputElement).value);
  }

  /** Track-fill percentage for the styled range input. */
  function pct(s: { value: number; lo: number; hi: number }): number {
    if (s.hi <= s.lo) return 0;
    return Math.min(100, Math.max(0, ((s.value - s.lo) / (s.hi - s.lo)) * 100));
  }

  // --- tall-output folding --------------------------------------------------
  // Outputs taller than TALL can collapse to a CLIP-height preview. Fresh
  // runs stay expanded; restored cells start folded; explicit toggles persist.
  const TALL = 480;
  const CLIP = 300;
  let outH = $state(0);
  const tall = $derived(outH > TALL);
  const isCollapsed = $derived(tall && (cell.collapsed ?? cell.restored));

  function setCollapsed(v: boolean) {
    notebook.setCollapsed(cell.id, v);
  }
</script>

<article class="cell">
  <div class="line in-line">
    <span class="label in-label">In[{index}]:=</span>
    <code class="src">{cell.input}</code>
    <div class="actions">
      {#if tall}
        <button
          class="action"
          onclick={() => setCollapsed(!isCollapsed)}
          aria-expanded={!isCollapsed}
          aria-label="{isCollapsed ? 'Expand' : 'Collapse'} output {index}"
        >
          {isCollapsed ? "expand" : "collapse"}
        </button>
      {/if}
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

  {#if cell.sliders.length > 0}
    <div class="line sliders-line" data-testid="cell-sliders">
      <span class="label" aria-hidden="true"></span>
      <div class="sliders" role="group" aria-label="Variable sliders">
        {#each cell.sliders as s (s.name)}
          <div class="slider-row">
            <code class="s-name" title="linked to the session variable {s.name}">
              {s.name}
            </code>
            <input
              class="s-bound"
              type="number"
              step="any"
              value={s.lo}
              aria-label={`${s.name} minimum`}
              onchange={(e) => notebook.setSliderBounds(cell.id, s.name, num(e), s.hi)}
            />
            <input
              class="s-range"
              type="range"
              min={s.lo}
              max={s.hi}
              step={(s.hi - s.lo) / 200 || "any"}
              value={s.value}
              style:--pct="{pct(s)}%"
              aria-label={`${s.name} value slider`}
              oninput={(e) => notebook.setSlider(cell.id, s.name, num(e))}
            />
            <input
              class="s-bound"
              type="number"
              step="any"
              value={s.hi}
              aria-label={`${s.name} maximum`}
              onchange={(e) => notebook.setSliderBounds(cell.id, s.name, s.lo, num(e))}
            />
            <input
              class="s-value"
              type="number"
              step="any"
              value={s.value}
              aria-label={`${s.name} value`}
              onchange={(e) => notebook.setSlider(cell.id, s.name, num(e))}
            />
          </div>
        {/each}
      </div>
    </div>
  {/if}

  <div class="line out-line">
    <span class="label out-label">Out[{index}]=</span>
    <div class="out-body" class:clipped={isCollapsed} style:--clip="{CLIP}px">
      <div class="out-inner" bind:clientHeight={outH}>
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
      {#if isCollapsed}
        <button
          class="unfold"
          onclick={() => setCollapsed(false)}
          aria-expanded="false"
          aria-label="Show all of output {index}"
        >
          show all ▾
        </button>
      {/if}
    </div>
  </div>

  {#if tall && !isCollapsed}
    <div class="line out-line fold-line">
      <span class="label" aria-hidden="true"></span>
      <button
        class="fold"
        onclick={() => setCollapsed(true)}
        aria-expanded="true"
        aria-label="Collapse output {index}"
      >
        collapse ▴
      </button>
    </div>
  {/if}
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

  /* Manipulate-style variable sliders: re-run this cell with tweaked values. */
  .sliders-line {
    grid-template-columns: 4.8rem minmax(0, 1fr);
    margin-top: 0.1rem;
  }
  .sliders {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    gap: 0.35rem;
    min-width: 0;
    padding: 0.5rem 0.7rem;
    border: 1px dashed var(--border);
    border-radius: calc(var(--radius) / 2);
    overflow-x: auto;
  }
  .slider-row {
    display: grid;
    grid-template-columns: minmax(1.6em, auto) 4.8em minmax(8rem, 22rem) 4.8em 5.4em;
    gap: 0.45rem;
    align-items: center;
  }
  .s-name {
    font-family: var(--font-mono);
    font-size: 0.85rem;
    color: var(--accent);
    font-weight: 600;
    text-align: right;
  }
  .s-bound,
  .s-value {
    font-family: var(--font-mono);
    font-size: 0.78rem;
    width: 100%;
    color: var(--fg-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 0.12rem 0.35rem;
  }
  .s-value {
    color: var(--fg);
  }

  /* Custom range: slim rounded track with an accent fill up to the thumb,
     and a small ringed dot instead of the native indicator. */
  .s-range {
    appearance: none;
    -webkit-appearance: none;
    min-width: 0;
    margin: 0;
    height: 1.15rem;
    background: transparent;
    cursor: pointer;
  }
  .s-range::-webkit-slider-runnable-track {
    height: 4px;
    border-radius: 999px;
    background: linear-gradient(
      to right,
      var(--accent) var(--pct),
      var(--border) var(--pct)
    );
  }
  .s-range::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 13px;
    height: 13px;
    margin-top: -4.5px;
    border-radius: 50%;
    background: var(--bg-panel);
    border: 2.5px solid var(--accent);
    box-shadow: 0 1px 4px rgb(0 0 0 / 25%);
    transition: transform 90ms ease, background 90ms ease;
  }
  .s-range:hover::-webkit-slider-thumb {
    transform: scale(1.18);
  }
  .s-range:active::-webkit-slider-thumb {
    transform: scale(1.25);
    background: var(--accent);
  }
  .s-range:focus-visible {
    outline: 2px solid var(--accent);
    outline-offset: 3px;
    border-radius: 999px;
  }
  .s-range::-moz-range-track {
    height: 4px;
    border-radius: 999px;
    background: var(--border);
  }
  .s-range::-moz-range-progress {
    height: 4px;
    border-radius: 999px;
    background: var(--accent);
  }
  .s-range::-moz-range-thumb {
    width: 13px;
    height: 13px;
    border-radius: 50%;
    background: var(--bg-panel);
    border: 2.5px solid var(--accent);
    box-shadow: 0 1px 4px rgb(0 0 0 / 25%);
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
  /* Tall-output fold: clip to a preview with a fade, content stays in the
     DOM (charts keep their state; text stays findable/assertable). */
  .out-body.clipped {
    position: relative;
    max-height: var(--clip);
    overflow: hidden;
  }
  .out-body.clipped::after {
    content: "";
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    height: 72px;
    background: linear-gradient(transparent, var(--bg));
    pointer-events: none;
  }
  .out-inner {
    min-width: 0;
  }
  .unfold {
    position: absolute;
    bottom: 0.45rem;
    left: 50%;
    transform: translateX(-50%);
    z-index: 1;
    font: inherit;
    font-size: 0.76rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.15rem 0.75rem;
    cursor: pointer;
    white-space: nowrap;
  }
  .unfold:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .fold-line {
    margin-top: 0.3rem;
  }
  .fold {
    justify-self: start;
    font: inherit;
    font-size: 0.76rem;
    color: var(--fg-muted);
    background: transparent;
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.12rem 0.75rem;
    cursor: pointer;
  }
  .fold:hover {
    color: var(--accent);
    border-color: var(--accent);
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
