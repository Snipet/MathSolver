import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";

// Static SPA build; the WASM engine runs in a module worker.
export default defineConfig({
  plugins: [svelte()],
  base: "./",
  worker: {
    format: "es",
  },
  build: {
    target: "es2022",
  },
});
