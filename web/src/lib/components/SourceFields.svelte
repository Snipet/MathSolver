<script lang="ts">
  // Copyable source rows (Plain / LaTeX / Value), collapsed behind a small
  // disclosure so the typeset math stays the star of the result.
  import CopyField from "./CopyField.svelte";

  let { fields }: { fields: { label: string; text: string }[] } = $props();

  const summary = $derived(fields.map((f) => f.label).join(" / "));
</script>

<details class="sources">
  <summary>
    <svg class="chev" viewBox="0 0 12 12" width="10" height="10" aria-hidden="true">
      <path
        d="M4 2.5 8 6 4 9.5"
        fill="none"
        stroke="currentColor"
        stroke-width="1.5"
        stroke-linecap="round"
        stroke-linejoin="round"
      />
    </svg>
    copy {summary}
  </summary>
  <div class="rows">
    {#each fields as f (f.label)}
      <CopyField label={f.label} text={f.text} />
    {/each}
  </div>
</details>

<style>
  .sources {
    margin-top: 0.6rem;
  }
  summary {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    list-style: none;
    cursor: pointer;
    font-size: 0.76rem;
    color: var(--fg-muted);
    user-select: none;
    border: 1px solid transparent;
    border-radius: 999px;
    padding: 0.12rem 0.55rem 0.12rem 0.4rem;
  }
  summary::-webkit-details-marker {
    display: none;
  }
  summary:hover {
    color: var(--accent);
    border-color: var(--border);
    background: var(--bg-panel);
  }
  .chev {
    transition: transform 120ms ease;
    flex: 0 0 auto;
  }
  details[open] .chev {
    transform: rotate(90deg);
  }
  .rows {
    margin-top: 0.4rem;
    display: grid;
    gap: 0.4rem;
  }
</style>
