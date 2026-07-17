<script lang="ts">
  // Placeholder shell proving the worker -> WASM pipeline; the real GUI
  // replaces this component.
  import { call, engineReady } from "./lib/engine";

  let status = $state("loading engine…");
  let demo = $state("");

  $effect(() => {
    (async () => {
      await engineReady();
      const v = await call("version", []);
      status = v.ok ? `engine ready — MathSolver ${v.version}` : v.error;
      const r = await call("simplify", ["2x + 3x"]);
      demo = r.ok ? `simplify(2x + 3x) = ${r.plain}` : r.error;
    })();
  });
</script>

<main style="padding: 2rem; max-width: 640px; margin: 0 auto;">
  <h1>MathSolver</h1>
  <p>{status}</p>
  <p><code>{demo}</code></p>
</main>
