# Deploying MathSolver

The web app (Svelte 5 + Vite, in `web/`) embeds the C++23 engine compiled to
WebAssembly. CI builds and deploys are defined in `.github/workflows/`:

- `ci.yml` — on every push to `main` and every pull request:
  - **native**: CMake build with GCC 14 (C++23) on `ubuntu-latest`, then
    `ctest` (Catch2 suites + the TSV acceptance battery run via
    `tools/run_acceptance.py`).
  - **wasm-web**: installs Emscripten (pinned via `EMSDK_VERSION`), runs
    `tools/build_wasm.sh`, the node smoke test `tools/wasm_smoke.mjs`, then
    `npm ci`, `npm run check`, and `npm run build` in `web/`. The built
    `web/dist` is uploaded as the `web-dist` artifact.
- `deploy.yml` — on push to `main` (or manual dispatch): rebuilds the WASM
  module and the web app, then publishes `web/dist` to GitHub Pages with
  `actions/upload-pages-artifact` + `actions/deploy-pages`.

## Enabling GitHub Pages (one-time setup)

1. Push the repository to GitHub.
2. In the repository, open **Settings → Pages**.
3. Under **Build and deployment → Source**, choose **GitHub Actions**
   (not "Deploy from a branch").
4. Push to `main` (or run the "Deploy to GitHub Pages" workflow manually from
   the **Actions** tab). The site URL appears on the workflow run's
   `github-pages` environment and in Settings → Pages, typically
   `https://<user>.github.io/<repo>/`.

No secrets are required: `deploy.yml` uses the standard Pages permissions
block (`pages: write`, `id-token: write`) with the built-in OIDC token.

## Local build sequence

Prerequisites: CMake ≥ 3.24, a C++23 compiler, Emscripten (`emcc`/`emcmake`
on PATH), Node.js ≥ 20 (CI uses 24).

```sh
# 1. Native build + full test suite
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# 2. WASM module (builds into build-wasm/ and stages
#    mathsolver.js/.wasm into web/src/lib/wasm/)
bash tools/build_wasm.sh
node tools/wasm_smoke.mjs        # 27-check smoke battery

# 3. Web app
cd web
npm ci                           # uses web/package-lock.json
npm run check                    # svelte-check
npm run build                    # emits web/dist/
npm run preview                  # optional: serve the production build
```

## Subpath note (vite `base`)

`web/vite.config.ts` sets `base: "./"`, so all asset URLs in the built
`index.html` are relative. This makes the same `dist/` work both at a domain
root and under a repository subpath like `https://<user>.github.io/MathSolver/`
— no per-repo `base` configuration is needed when deploying to project Pages.
