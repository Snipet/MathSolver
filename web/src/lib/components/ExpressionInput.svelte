<script lang="ts">
  interface Props {
    value: string;
    placeholder?: string;
    computing?: boolean;
    computeDisabled?: boolean;
    oncompute: () => void;
    /** Optional pre-handler (e.g. console history recall); may preventDefault. */
    onkeydownextra?: (e: KeyboardEvent) => void;
  }

  let {
    value = $bindable(""),
    placeholder = "",
    computing = false,
    computeDisabled = false,
    oncompute,
    onkeydownextra,
  }: Props = $props();

  let ta: HTMLTextAreaElement | undefined = $state();

  /** Focus the textarea and place the cursor at the end of its content. */
  export function focusEnd() {
    const el = ta;
    if (!el) return;
    el.focus();
    el.setSelectionRange(el.value.length, el.value.length);
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
    {computing ? "Computing…" : "Compute"}
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
    font-size: 1rem;
    line-height: 1.45;
    color: var(--fg);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: var(--radius);
    padding: 0.6rem 0.75rem;
    overflow-y: auto;
  }
  textarea::placeholder {
    color: var(--fg-muted);
    opacity: 0.8;
  }
  textarea:focus {
    outline: 2px solid var(--accent);
    outline-offset: -1px;
  }
  .compute {
    font: inherit;
    font-weight: 600;
    font-size: 0.95rem;
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border: none;
    border-radius: var(--radius);
    padding: 0.6rem 1.1rem;
    cursor: pointer;
    flex: 0 0 auto;
  }
  .compute:disabled {
    opacity: 0.5;
    cursor: default;
  }
  @media (max-width: 480px) {
    .row {
      flex-direction: column;
      align-items: stretch;
    }
  }
</style>
