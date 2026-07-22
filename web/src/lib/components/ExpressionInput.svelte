<script lang="ts">
  interface Props {
    value: string;
    placeholder?: string;
    computing?: boolean;
    computeDisabled?: boolean;
    oncompute: () => void;
    /** Optional pre-handler (e.g. console history recall); may preventDefault. */
    onkeydownextra?: (e: KeyboardEvent) => void;
    /** Label of the action button (defaults to "Compute"). */
    buttonLabel?: string;
  }

  let {
    value = $bindable(""),
    placeholder = "",
    computing = false,
    computeDisabled = false,
    oncompute,
    onkeydownextra,
    buttonLabel = "Compute",
  }: Props = $props();

  let ta: HTMLTextAreaElement | undefined = $state();

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
    el.style.height = Math.min(el.scrollHeight + 2, 240) + "px";
  });

  function onkeydown(e: KeyboardEvent) {
    onkeydownextra?.(e);
    if (e.defaultPrevented) return;
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      if (!computeDisabled) oncompute();
    }
  }
</script>

<div class="row">
  <textarea
    bind:this={ta}
    bind:value
    rows="1"
    {placeholder}
    spellcheck="false"
    autocapitalize="off"
    autocomplete="off"
    aria-label="Expression input"
    {onkeydown}
  ></textarea>
  <button class="compute" onclick={() => oncompute()} disabled={computeDisabled}>
    {computing ? "Computing…" : buttonLabel}
  </button>
</div>

<style>
  .row {
    display: flex;
    gap: 0.5rem;
    align-items: flex-start;
  }
  textarea {
    flex: 1;
    min-width: 0;
    resize: none;
    font-family: var(--font-mono);
    font-size: 1.02rem;
    line-height: 1.5;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 0.65rem 0.8rem;
    overflow-y: auto;
    transition: border-color 130ms ease, box-shadow 130ms ease;
  }
  textarea::placeholder {
    color: var(--fg-muted);
    opacity: 0.75;
  }
  textarea:focus {
    outline: none;
    border-color: var(--accent);
    box-shadow: 0 0 0 3px var(--accent-soft);
  }
  .compute {
    font-family: var(--font-sans);
    font-weight: 600;
    font-size: 0.92rem;
    letter-spacing: 0.01em;
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border: 1px solid var(--accent-strong);
    border-radius: var(--radius);
    padding: 0.65rem 1.25rem;
    cursor: pointer;
    flex: 0 0 auto;
    box-shadow: var(--shadow-sm);
    transition: background 130ms ease;
  }
  .compute:hover:not(:disabled) {
    background: var(--accent-strong);
  }
  .compute:disabled {
    opacity: 0.45;
    cursor: default;
    box-shadow: none;
  }
  @media (max-width: 480px) {
    .row {
      flex-direction: column;
      align-items: stretch;
    }
  }
</style>
