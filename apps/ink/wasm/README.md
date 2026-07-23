# Engine staging directory

`tools/build_wasm.sh` stages the compiled engine here as `mathsolver.js` +
`mathsolver.wasm` (the same node/web ES module the Svelte web app consumes).
Those two files are build artifacts and are git-ignored — run the build once:

```sh
bash ../../../tools/build_wasm.sh   # from anywhere; requires Emscripten
```

At runtime the app also looks in `../../build-wasm` and `../../web/src/lib/wasm`,
and honors `MATHSOLVER_WASM_DIR` if you want to point it at a build elsewhere.
