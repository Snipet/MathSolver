<script lang="ts">
  import { tick } from "svelte";
  import { vars, CAP_ERROR, type VarRow } from "../vars.svelte";
  import Katex from "./Katex.svelte";
  import SpanHighlight from "./SpanHighlight.svelte";

  let confirmClear = $state(false);
  let listEl: HTMLUListElement | undefined = $state();

  // Chip clicks request focus on a row (§9.1); consume and clear.
  $effect(() => {
    const id = vars.focusId;
    if (id === null) return;
    vars.focusId = null;
    void tick().then(() => {
      const el = listEl?.querySelector<HTMLInputElement>(
        `li[data-var-id="${id}"] input.value`,
      );
      el?.focus();
      el?.scrollIntoView({ block: "nearest" });
    });
  });

  function rowError(r: VarRow): string | undefined {
    return r.status.nameError ?? r.status.valueError ?? r.status.envError;
  }

  /** Inactive: stored but excluded from resolution (invalid, cycle, shadow). */
  function isInactive(r: VarRow): boolean {
    if (r.status.phase !== "checked") return true;
    if (rowError(r) !== undefined) return true;
    return !(r.status.symbol && r.status.valuePlain);
  }

  function previewLatex(r: VarRow): string | null {
    const s = r.status;
    if (s.phase !== "checked" || rowError(r) || !s.nameLatex || !s.valueLatex)
      return null;
    return `${s.nameLatex} \\mathrel{:=} ${s.valueLatex}`;
  }

  async function addRow() {
    const id = vars.add();
    if (id === null) return; // at the §9.2 cap; the button is disabled anyway
    await tick();
    listEl
      ?.querySelector<HTMLInputElement>(`li[data-var-id="${id}"] input.name`)
      ?.focus();
  }

  function clearAll() {
    if (!confirmClear) {
      confirmClear = true;
      return;
    }
    confirmClear = false;
    vars.clearAll();
  }
</script>

<div class="vars" data-testid="vars-panel">
  <div class="head">
    <h2>Variables</h2>
    {#if vars.rows.length}
      <button
        class="clear"
        class:armed={confirmClear}
        onclick={clearAll}
        onblur={() => (confirmClear = false)}
      >
        {confirmClear ? "Really clear?" : "Clear all"}
      </button>
    {/if}
  </div>

  {#if vars.rows.length === 0}
    <p class="empty">
      Assign values with <code>name := value</code> — here or in the main input.
    </p>
  {:else}
    <ul bind:this={listEl}>
      {#each vars.rows as r (r.id)}
        <li data-var-id={r.id} class:inactive={isInactive(r)}>
          <div class="fields">
            <input
              class="name"
              type="text"
              placeholder="name"
              aria-label="Variable name"
              spellcheck="false"
              autocapitalize="off"
              autocomplete="off"
              value={r.name}
              oninput={(e) => vars.edit(r.id, { name: e.currentTarget.value })}
            />
            <span class="op">:=</span>
            <input
              class="value"
              type="text"
              placeholder="value"
              aria-label="Variable value"
              spellcheck="false"
              autocapitalize="off"
              autocomplete="off"
              value={r.value}
              oninput={(e) => vars.edit(r.id, { value: e.currentTarget.value })}
            />
            {#if r.status.kind === "equation" && !rowError(r)}
              <span class="badge" title="equation value — applies on the Solve tab">eq</span>
            {/if}
            <button
              class="del"
              aria-label={`Delete ${r.name.trim() || "variable"}`}
              onclick={() => vars.remove(r.id)}
            >
              ×
            </button>
          </div>
          {#if rowError(r)}
            <p class="row-error">{rowError(r)}</p>
            {#if r.status.valueSpan && r.status.valueError}
              <SpanHighlight
                input={r.value.trim()}
                begin={r.status.valueSpan.begin}
                end={r.status.valueSpan.end}
              />
            {/if}
            <p class="inactive-note">inactive until edited</p>
          {:else}
            {@const latex = previewLatex(r)}
            {#if latex}
              <div class="row-preview">
                <Katex {latex} />
              </div>
            {/if}
          {/if}
        </li>
      {/each}
    </ul>
  {/if}

  <button
    class="add"
    onclick={() => void addRow()}
    disabled={vars.atCap}
    title={vars.atCap ? CAP_ERROR : undefined}
  >
    + add
  </button>
  {#if vars.atCap}
    <p class="cap-note">{CAP_ERROR}</p>
  {/if}
</div>

<style>
  .vars {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    min-width: 0;
  }
  .head {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    padding-bottom: 0.35rem;
    border-bottom: 1px solid var(--rule);
  }
  h2 {
    margin: 0;
    font-family: var(--font-serif);
    font-size: 1.08rem;
    font-weight: 600;
    letter-spacing: 0;
    color: var(--fg);
  }
  .clear {
    font: inherit;
    font-size: 0.8rem;
    color: var(--fg-muted);
    background: none;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.1rem 0.5rem;
    cursor: pointer;
  }
  .clear:hover,
  .clear.armed {
    color: var(--error);
    border-color: var(--error);
  }
  .empty {
    margin: 0;
    font-size: 0.85rem;
    color: var(--fg-muted);
  }
  .empty code {
    font-family: var(--font-mono);
    font-size: 0.8rem;
  }
  ul {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
    max-height: 45vh;
    overflow-y: auto;
    scrollbar-width: thin;
  }
  li {
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.45rem 0.6rem;
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
    min-width: 0;
  }
  li.inactive {
    opacity: 0.75;
    border-style: dashed;
  }
  .fields {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    min-width: 0;
  }
  .fields input {
    font: inherit;
    font-family: var(--font-mono);
    font-size: 0.85rem;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.2rem 0.4rem;
    min-width: 0;
  }
  input.name {
    width: 4.5em;
    flex: 0 1 auto;
  }
  input.value {
    flex: 1 1 3em;
  }
  .op {
    font-family: var(--font-mono);
    font-size: 0.85rem;
    color: var(--fg-muted);
    flex: 0 0 auto;
  }
  .badge {
    font-family: var(--font-mono);
    font-size: 0.68rem;
    font-weight: 600;
    color: var(--warn-fg);
    background: var(--warn-bg);
    border-radius: 999px;
    padding: 0.05rem 0.45rem;
    flex: 0 0 auto;
  }
  .del {
    font: inherit;
    font-size: 0.95rem;
    line-height: 1;
    color: var(--fg-muted);
    background: none;
    border: none;
    padding: 0.1rem 0.25rem;
    cursor: pointer;
    flex: 0 0 auto;
  }
  .del:hover {
    color: var(--error);
  }
  .row-error {
    margin: 0;
    font-size: 0.78rem;
    color: var(--error);
    overflow-wrap: anywhere;
  }
  .inactive-note {
    margin: 0;
    font-size: 0.72rem;
    font-style: italic;
    color: var(--fg-muted);
  }
  .row-preview {
    font-size: 0.9rem;
    overflow-x: auto;
  }
  .add {
    font: inherit;
    font-size: 0.82rem;
    color: var(--fg-muted);
    background: none;
    border: 1px dashed var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.25rem 0.6rem;
    cursor: pointer;
    align-self: flex-start;
  }
  .add:hover:not(:disabled) {
    color: var(--accent);
    border-color: var(--accent);
  }
  .add:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  .cap-note {
    margin: 0;
    font-size: 0.72rem;
    font-style: italic;
    color: var(--fg-muted);
  }
</style>
