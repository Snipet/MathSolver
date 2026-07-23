<script lang="ts">
  // Console side panel: a segmented toggle between the flat per-command
  // reference (quick lookup + click-to-insert) and the cookbook (worked,
  // explained recipes). Both feed the prompt through the same consoleUi
  // channel, so this wrapper only owns which view is showing.
  import CommandReference from "./CommandReference.svelte";
  import Cookbook from "./Cookbook.svelte";

  let view = $state<"commands" | "cookbook">("commands");
</script>

<div class="console-ref">
  <div class="seg" role="tablist" aria-label="Console reference view">
    <button
      role="tab"
      aria-selected={view === "commands"}
      class:active={view === "commands"}
      onclick={() => (view = "commands")}
    >
      Commands
    </button>
    <button
      role="tab"
      aria-selected={view === "cookbook"}
      class:active={view === "cookbook"}
      onclick={() => (view = "cookbook")}
    >
      Cookbook
    </button>
  </div>

  {#if view === "commands"}
    <CommandReference />
  {:else}
    <Cookbook />
  {/if}
</div>

<style>
  .console-ref {
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
  }
  .seg {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 2px;
    padding: 3px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 1.3);
  }
  .seg button {
    font: inherit;
    font-size: 0.78rem;
    font-weight: 600;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    border-radius: calc(var(--radius) / 1.8);
    padding: 0.3rem 0.5rem;
    cursor: pointer;
    transition: color 120ms ease, background 120ms ease;
  }
  .seg button:hover {
    color: var(--fg);
  }
  .seg button.active {
    color: var(--accent);
    background: var(--accent-soft);
    box-shadow: inset 0 0 0 1px var(--accent-line);
  }
</style>
