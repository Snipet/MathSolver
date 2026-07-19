<script lang="ts">
  // Saved-notebook manager: save the current session's commands under a
  // name, open a notebook into the console, run one in a fresh scope, or
  // delete it. Mirrors the save / open / run / notebooks console verbs.
  import { docs, NAME_RE } from "../notebook/docs.svelte";
  import { notebook } from "../notebook/notebook.svelte";

  let name = $state("");

  const nameOk = $derived(NAME_RE.test(name.trim()));

  function save() {
    const n = name.trim();
    if (!NAME_RE.test(n)) return;
    notebook.saveDoc(n);
    name = "";
  }

  function fmtDate(ts: number): string {
    return new Date(ts).toLocaleDateString(undefined, {
      month: "short",
      day: "numeric",
    });
  }
</script>

<section class="nb-panel" data-testid="notebooks-panel" aria-label="Saved notebooks">
  <h2 class="nb-title">Notebooks</h2>
  <p class="nb-sub">
    Save this session's commands; <em>run</em> replays them in a fresh
    variable scope.
  </p>

  <form
    class="save-row"
    onsubmit={(e) => {
      e.preventDefault();
      save();
    }}
  >
    <input
      class="name-input"
      type="text"
      bind:value={name}
      placeholder="notebook name"
      aria-label="Notebook name"
      spellcheck="false"
      autocapitalize="off"
    />
    <button class="nb-btn" type="submit" disabled={!nameOk}>save</button>
  </form>

  {#if docs.docs.length === 0}
    <p class="nb-empty">nothing saved yet</p>
  {:else}
    <ul class="nb-list">
      {#each docs.docs as d (d.name)}
        <li class="nb-row">
          <div class="nb-meta">
            <code class="nb-name">{d.name}</code>
            <span class="nb-count">
              {d.lines.length} cmd{d.lines.length === 1 ? "" : "s"} · {fmtDate(d.ts)}
            </span>
          </div>
          <div class="nb-actions">
            <button class="nb-btn" onclick={() => void notebook.openDoc(d.name)}>
              open
            </button>
            <button class="nb-btn" onclick={() => void notebook.runDoc(d.name)}>
              run
            </button>
            <button
              class="nb-btn del"
              onclick={() => docs.remove(d.name)}
              aria-label={`Delete notebook ${d.name}`}
            >
              ×
            </button>
          </div>
        </li>
      {/each}
    </ul>
  {/if}
</section>

<style>
  .nb-panel {
    min-width: 0;
  }
  .nb-title {
    margin: 0;
    font-size: 0.95rem;
    font-weight: 700;
    letter-spacing: -0.01em;
  }
  .nb-sub {
    margin: 0.15rem 0 0.6rem;
    font-size: 0.75rem;
    color: var(--fg-muted);
  }
  .save-row {
    display: flex;
    gap: 0.35rem;
    margin-bottom: 0.6rem;
  }
  .name-input {
    flex: 1;
    min-width: 0;
    font-family: var(--font-mono);
    font-size: 0.82rem;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.25rem 0.5rem;
  }
  .nb-btn {
    font: inherit;
    font-size: 0.76rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.15rem 0.6rem;
    cursor: pointer;
    white-space: nowrap;
  }
  .nb-btn:hover:not(:disabled) {
    color: var(--accent);
    border-color: var(--accent);
  }
  .nb-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }
  .nb-btn.del:hover {
    color: var(--error);
    border-color: var(--error);
  }
  .nb-empty {
    margin: 0;
    font-size: 0.78rem;
    color: var(--fg-muted);
    font-style: italic;
  }
  .nb-list {
    list-style: none;
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .nb-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 0.5rem;
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.3rem 0.55rem;
    background: var(--bg-panel);
  }
  .nb-meta {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }
  .nb-name {
    font-family: var(--font-mono);
    font-size: 0.85rem;
    color: var(--fg);
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .nb-count {
    font-size: 0.7rem;
    color: var(--fg-muted);
  }
  .nb-actions {
    display: flex;
    gap: 0.25rem;
    flex: 0 0 auto;
  }
</style>
