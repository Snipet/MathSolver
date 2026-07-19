<script lang="ts">
  // The console's entry line, styled as the notebook's next In[n] cell rather
  // than a form field: no box, no button — an accent caret, the In label as
  // the focus indicator, and a ghost ⏎ affordance for mouse/touch users.
  interface Props {
    value: string;
    /** The cell number this line will become when run. */
    index: number;
    ready?: boolean;
    computing?: boolean;
    placeholder?: string;
    /** Ghost text shown at the caret: what can come next (e.g. "<var> | <lo>"). */
    hint?: string;
    onrun: () => void;
    /** Optional pre-handler (history recall, completions); may preventDefault. */
    onkeydownextra?: (e: KeyboardEvent) => void;
  }

  let {
    value = $bindable(""),
    index,
    ready = true,
    computing = false,
    placeholder = "",
    hint = "",
    onrun,
    onkeydownextra,
  }: Props = $props();

  let ta: HTMLTextAreaElement | undefined = $state();
  let ghostEl: HTMLDivElement | undefined = $state();

  const canRun = $derived(ready && !computing && value.trim().length > 0);

  // The ghost renders only while the caret sits at the end of the line — a
  // hint mid-edit would point at the wrong place.
  let focused = $state(false);
  let caretEnd = $state(true);
  function syncCaret() {
    const el = ta;
    if (!el) return;
    caretEnd =
      el.selectionStart === el.selectionEnd &&
      el.selectionEnd === el.value.length;
  }
  function syncScroll() {
    if (ghostEl && ta) ghostEl.scrollTop = ta.scrollTop;
  }
  const showGhost = $derived(!!hint && focused && caretEnd && !computing);

  /** Focus the textarea and place the cursor at the end of its content. */
  export function focusEnd() {
    const el = ta;
    if (!el) return;
    el.focus();
    el.setSelectionRange(el.value.length, el.value.length);
  }

  /**
   * Insert `pre` + selection + `post` at the caret (palette buttons). With an
   * empty selection the caret lands after `pre` (inside a wrapper like √( )),
   * or after `post` when there is no `pre` (postfix marks like ²).
   */
  export function insertAtCursor(pre: string, post = "") {
    const el = ta;
    if (!el) return;
    const start = el.selectionStart ?? value.length;
    const end = el.selectionEnd ?? start;
    const sel = value.slice(start, end);
    value = value.slice(0, start) + pre + sel + post + value.slice(end);
    const caret =
      sel.length > 0 || pre.length === 0
        ? start + pre.length + sel.length + post.length
        : start + pre.length;
    requestAnimationFrame(() => {
      el.focus();
      el.setSelectionRange(caret, caret);
    });
  }

  // Auto-grow: re-measure whenever the value changes.
  $effect(() => {
    void value;
    const el = ta;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = Math.min(el.scrollHeight, 240) + "px";
  });

  function onkeydown(e: KeyboardEvent) {
    onkeydownextra?.(e);
    if (e.defaultPrevented) return;
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      if (canRun) onrun();
    }
  }
</script>

<div class="prompt-line" class:computing>
  <span class="in-label" aria-hidden="true">In[{index}]:=</span>
  <div class="ta-wrap">
    <textarea
      bind:this={ta}
      bind:value
      rows="1"
      {placeholder}
      spellcheck="false"
      autocapitalize="off"
      autocomplete="off"
      aria-label="Console input, line {index}"
      {onkeydown}
      onfocus={() => {
        focused = true;
        syncCaret();
      }}
      onblur={() => (focused = false)}
      onkeyup={syncCaret}
      onclick={syncCaret}
      oninput={syncCaret}
      onscroll={syncScroll}
    ></textarea>
    {#if showGhost}
      <!-- Mirror overlay: invisible copy of the text positions the ghost
           exactly at the caret, wrapping like the textarea does. -->
      <div class="ghost" bind:this={ghostEl} aria-hidden="true" data-testid="ghost-hint">
        <span class="ghost-pre">{value}</span><span class="ghost-text">{hint}</span>
      </div>
    {/if}
  </div>
  <span class="gutter">
    {#if computing}
      <span class="spinner" role="status" aria-label="computing"></span>
    {:else if canRun}
      <button
        class="run-glyph"
        onmousedown={(e) => e.preventDefault() /* keep textarea focus */}
        onclick={onrun}
        title="Run (Enter)"
        aria-label="Run this line"
      >
        <svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">
          <path
            d="M13 3v5a2 2 0 0 1-2 2H4.5M7 6.5 3.5 10 7 13.5"
            fill="none"
            stroke="currentColor"
            stroke-width="1.6"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
        </svg>
      </button>
    {/if}
  </span>
</div>

<style>
  /* Mirrors NotebookCell's In-line geometry so the prompt reads as the
     notebook's next cell. */
  .prompt-line {
    display: grid;
    grid-template-columns: 4.8rem minmax(0, 1fr) auto;
    gap: 0.6rem;
    align-items: start;
    padding: 0.45rem 0.25rem;
    border-radius: calc(var(--radius) / 2);
    transition: background 120ms ease;
  }
  .prompt-line:focus-within {
    background: color-mix(in srgb, var(--accent) 5%, transparent);
  }

  .in-label {
    font-family: var(--font-mono);
    font-size: 0.76rem;
    line-height: 1.97; /* optically centers on the textarea's first line */
    user-select: none;
    white-space: nowrap;
    text-align: right;
    color: var(--fg-muted);
    transition: color 120ms ease;
  }
  .prompt-line:focus-within .in-label {
    color: var(--accent);
    font-weight: 600;
  }

  .ta-wrap {
    position: relative;
    min-width: 0;
  }
  textarea {
    display: block;
    min-width: 0;
    width: 100%;
    resize: none;
    font-family: var(--font-mono);
    font-size: 1rem;
    line-height: 1.5;
    color: var(--fg);
    background: transparent;
    border: none;
    padding: 0;
    margin: 0;
    overflow-y: auto;
    caret-color: var(--accent);
    overflow-wrap: break-word;
  }
  .ghost {
    position: absolute;
    inset: 0;
    overflow: hidden;
    pointer-events: none;
    font-family: var(--font-mono);
    font-size: 1rem;
    line-height: 1.5;
    white-space: pre-wrap;
    overflow-wrap: break-word;
  }
  .ghost-pre {
    visibility: hidden;
  }
  .ghost-text {
    color: var(--fg-muted);
    opacity: 0.55;
  }
  /* The row tint + accent In-label are the focus indicator; the boxed outline
     would re-introduce the form-field look. */
  textarea:focus,
  textarea:focus-visible {
    outline: none;
  }
  textarea::placeholder {
    color: var(--fg-muted);
    opacity: 0.55;
  }

  /* Fixed-width gutter so the line doesn't reflow when the glyph appears. */
  .gutter {
    width: 1.7rem;
    height: 1.5rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
  }
  .run-glyph {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 1.6rem;
    height: 1.45rem;
    color: var(--fg-muted);
    background: transparent;
    border: 1px solid transparent;
    border-radius: calc(var(--radius) / 2);
    padding: 0;
    cursor: pointer;
  }
  .run-glyph:hover {
    color: var(--accent);
    border-color: var(--border);
    background: var(--bg-panel);
  }
  .spinner {
    width: 0.85rem;
    height: 0.85rem;
    border: 2px solid color-mix(in srgb, var(--accent) 25%, transparent);
    border-top-color: var(--accent);
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
  }
  @keyframes spin {
    to {
      transform: rotate(360deg);
    }
  }

  @media (max-width: 640px) {
    .prompt-line {
      grid-template-columns: auto minmax(0, 1fr) auto;
    }
    .in-label {
      text-align: left;
    }
  }
</style>
