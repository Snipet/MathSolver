<script lang="ts">
  // Interactive 2-D wave field: a real-time FDTD simulation (WaveSim) painted
  // to a canvas, with energy introduced by mouse/touch gestures — click to
  // pluck, drag to launch a directional wavefront. Used both inline as a
  // console "wave" cell (compact) and as the full Workbench "Wave" tab.
  import { untrack } from "svelte";
  import {
    WaveSim,
    MAX_MASS,
    MAX_COUPLING_SINE,
    type Boundary,
    type FilterType,
    type FieldModel,
    type Stencil,
  } from "../wave/sim";
  import { WAVE_SCENES, buildScene } from "../wave/scenes";
  import { magnitudeSpectrum } from "../wave/spectrum";
  import { encodeWave, decodeWave, type WaveConfig } from "../wave/share";
  import { call } from "../engine";

  interface Props {
    /** Console cells pass compact=true for a smaller field + condensed controls. */
    compact?: boolean;
    /** Initial grid columns (rows follow the display aspect ratio). */
    columns?: number;
    speed?: number;
    damping?: number;
    boundary?: Boundary;
  }

  let {
    compact = false,
    columns = 180,
    speed = 0.5,
    damping = 0.06,
    boundary = "fixed",
  }: Props = $props();

  function clamp01(v: number): number {
    return v < 0 ? 0 : v > 1 ? 1 : v;
  }
  function clampCols(v: number): number {
    return Math.max(48, Math.min(320, Math.round(v)));
  }

  // --- controls (reactive) ---------------------------------------------------
  // The props are one-time seeds for these editable controls; untrack makes
  // that intent explicit (the component owns the values after mount).
  let running = $state(true);
  let speedV = $state(untrack(() => clamp01(speed)));
  let dampingV = $state(untrack(() => clamp01(damping)));
  let boundaryV = $state<Boundary>(untrack(() => boundary));
  let robinV = $state(0.5); // Robin edge stiffness k (∂u/∂n + k·u = 0)
  // Mouse source: frequency (0 = low/long waves, 1 = high/short) + mode.
  let freqV = $state(0.35);
  let srcMode = $state<"ripple" | "drive">("ripple");
  // Frequency-dependent ("filtered") boundary controls.
  let filterType = $state<FilterType>("lowpass");
  let filterCutoffV = $state(0.5);
  let filterReflectV = $state(0.85);
  let cols = $state(untrack(() => clampCols(columns)));

  // freq → the wavelength of an injected ripple, and the per-step frequency
  // of the continuous drive.
  const wavelength = $derived(3 + (1 - freqV) * 33); // 3 (high) .. 36 (low) cells
  const driveFreq = $derived(0.008 + freqV * 0.052); // cycles/step
  let brush = $state(untrack(() => (compact ? 3 : 4))); // brush radius in cells
  let strength = $state(1);
  let colormap = $state<"coolwarm" | "fire" | "violet">("coolwarm");
  let energy = $state(0);
  let touched = $state(false);
  // Structured-media preset (obstacles / variable speed): "empty" = open water.
  let sceneId = $state("empty");

  // --- instrumentation (Phase 2): probes + intensity ------------------------
  // Field view: the live wave, or the time-averaged intensity ⟨u²⟩ (which
  // freezes interference/diffraction fringes into a quantitative heatmap).
  let viewMode = $state<"wave" | "intensity">("wave");
  // When on, a click drops a probe (a receiver recording u(t)) instead of
  // plucking. Probes carry fractional coords so they survive a grid resize.
  let probeTool = $state(false);
  let probes = $state<{ id: number; xf: number; yf: number }[]>([]);
  let probeSeq = 0;
  // Peak frequency (cycles/step) per probe, refreshed from the FFT in the loop.
  let probePeaks = $state<number[]>([]);
  // Per-probe spectrum sparkline canvases (bound in the {#each}).
  let specCanvases = $state<(HTMLCanvasElement | undefined)[]>([]);
  const measuring = $derived(viewMode === "intensity" || probes.length > 0);

  // --- physics pack (Phase 3): field model + stencil ------------------------
  // The model adds a reaction term (Klein–Gordon mass → dispersion; sine-Gordon
  // → nonlinear kink solitons); the stencil selects the Laplacian. Both reshape
  // the CFL, which the sim folds back into the speed→Courant map so the speed
  // slider stays stable.
  let modelV = $state<FieldModel>("linear");
  let massV = $state(0.1);
  let stencilV = $state<Stencil>("five");
  const massMax = $derived(modelV === "sine-gordon" ? MAX_COUPLING_SINE : MAX_MASS);
  const massLabel = $derived(modelV === "sine-gordon" ? "Coupling" : "Mass");

  // --- authoring (Phase 4): CAS initial conditions + share links ------------
  // u(x,y,0) = f(x,y): a CAS expression sampled onto the grid (via the engine)
  // and seeded as the starting field. Empty = no CAS IC.
  let icExpr = $state("");
  let icError = $state("");
  let icBusy = $state(false);
  let shareLabel = $state("Share link");

  const HEIGHT = untrack(() => (compact ? 240 : 604));
  const STEPS_PER_FRAME = 2; // temporal oversampling for smoother propagation

  // --- sizing ----------------------------------------------------------------
  let host: HTMLDivElement | undefined = $state();
  let canvas: HTMLCanvasElement | undefined = $state();
  let stage: HTMLElement | undefined = $state();
  let width = $state(0);
  let visible = $state(true);

  $effect(() => {
    const vis = host;
    const box = stage;
    if (!vis || !box) return;
    // Measure the canvas's own column (.stage), NOT the whole rail+canvas grid
    // (host): the field is drawn to `width` px wide, so measuring the full grid
    // sized the canvas for rail+canvas and the browser then squashed it back
    // into its narrower column — warping the square cells into ellipses.
    const ro = new ResizeObserver((entries) => {
      width = Math.max(0, Math.floor(entries[0].contentRect.width));
    });
    ro.observe(box);
    const io = new IntersectionObserver(
      (entries) => (visible = entries[0].isIntersecting),
      { threshold: 0 },
    );
    io.observe(vis);
    return () => {
      ro.disconnect();
      io.disconnect();
    };
  });

  // Grid dimensions: cols wide, rows to match the display aspect ratio.
  const gridDims = $derived.by(() => {
    const w = width || 640;
    const aspect = HEIGHT / w;
    const nx = cols;
    const ny = Math.max(24, Math.round(nx * aspect));
    return { nx, ny };
  });

  // --- simulation ------------------------------------------------------------
  // Rebuilt only when the integer grid changes (not on every pixel resize).
  // The effect must depend ONLY on gridDims: reading `sim` (to resample) and
  // the control seeds, and writing `sim`, are untracked — otherwise the write
  // re-triggers the effect (an infinite reactive loop).
  let sim = $state<WaveSim | null>(null);
  $effect(() => {
    const { nx, ny } = gridDims;
    untrack(() => {
      // gridDims is a fresh object each width tick, so this effect re-runs on
      // every resize; rebuild only when the integer grid actually changed.
      if (sim && sim.nx === nx && sim.ny === ny) return;
      const next = new WaveSim(nx, ny, {
        speed: speedV,
        damping: dampingV,
        boundary: boundaryV,
        robin: robinV,
      });
      // Preserve the wave when only the resolution changed: resample the old
      // field into the new grid so a running simulation isn't wiped.
      if (sim) resample(sim, next);
      // Re-establish the scene geometry on the new grid (without re-seeding the
      // demo source or wiping the resampled wave).
      if (sceneId !== "empty") applyScene(next, sceneId, false);
      // Re-place probes on the new grid and carry the measurement flag over.
      next.setMeasuring(measuring);
      for (const p of probes) {
        next.addProbe(Math.round(p.xf * (nx - 1)), Math.round(p.yf * (ny - 1)));
      }
      sim = next;
    });
  });

  // Toggle sim-side measurement (probes + intensity) with the view/probe state.
  $effect(() => {
    sim?.setMeasuring(measuring);
  });

  // Start each intensity average fresh when the view is (re)entered.
  $effect(() => {
    if (viewMode === "intensity") untrack(() => sim?.resetIntensity());
  });

  /** Apply a structured-media preset. `seed` clears the field and starts the
   *  scene's demo source (a fresh selection); false just re-lays the geometry
   *  on an existing field (e.g. after a grid resize). */
  function applyScene(s: WaveSim, id: string, seed: boolean): void {
    const sc = buildScene(id, s.nx, s.ny);
    if (seed) s.reset();
    s.setMedium(sc.medium ?? null);
    s.setSolid(sc.solid ?? null);
    if (seed && sc.boundary) boundaryV = sc.boundary;
    if (seed) sc.start?.(s);
  }

  function selectScene(id: string): void {
    sceneId = id;
    // The drumhead seeds a linear eigenmode; keep the model in sync so the
    // control effect doesn't push a reaction term back over it.
    if (id === "drumhead") modelV = "linear";
    const s = sim;
    if (s) applyScene(s, id, true);
  }

  // --- probes ---------------------------------------------------------------
  const MAX_PROBES = 6;

  /** Drop a probe at grid (gx, gy), mirrored into the sim for recording. */
  function addProbeAt(gx: number, gy: number): void {
    const s = sim;
    if (!s || probes.length >= MAX_PROBES) return;
    probes = [
      ...probes,
      { id: probeSeq++, xf: gx / (s.nx - 1), yf: gy / (s.ny - 1) },
    ];
    // Array push and sim.addProbe both append, so indices stay aligned.
    s.setMeasuring(true);
    s.addProbe(gx, gy);
  }

  /** Remove one probe; rebuild the sim's probe set so indices realign. */
  function removeProbe(idx: number): void {
    probes = probes.filter((_, k) => k !== idx);
    syncProbes();
  }
  function clearProbes(): void {
    probes = [];
    syncProbes();
  }
  /** Re-seat the sim's probes from the component list (order = index). */
  function syncProbes(): void {
    const s = sim;
    if (!s) return;
    s.clearProbes();
    for (const p of probes) {
      s.addProbe(Math.round(p.xf * (s.nx - 1)), Math.round(p.yf * (s.ny - 1)));
    }
  }

  // Push control changes into the live sim without rebuilding it.
  $effect(() => {
    const s = sim;
    if (!s) return;
    s.speed = speedV;
    s.damping = dampingV;
    s.setBoundary(boundaryV);
    s.robin = robinV;
    s.setBoundaryFilter(filterType, filterCutoffV, filterReflectV);
    s.model = modelV;
    s.stencil = stencilV;
    s.mass = massV;
  });

  // Keep the coupling within the model's stable range when the model changes.
  $effect(() => {
    const cap = massMax;
    untrack(() => {
      if (massV > cap) massV = cap;
    });
  });

  /** Seed a sine-Gordon kink soliton (switches to that model) and let it run.
   *  The kink is uniform in y, so a free (Neumann) edge keeps it a clean
   *  vertical soliton — a fixed edge would clamp the ±ends and round it off. */
  function seedKink(): void {
    const s = sim;
    if (!s) return;
    modelV = "sine-gordon";
    boundaryV = "free";
    s.seedKink(0.25);
    massV = s.mass; // seedKink may set a default coupling
    scale = 0.4;
    touched = true;
    running = true;
  }

  // --- CAS initial conditions -----------------------------------------------
  /** Sample u(x,y,0)=f(x,y) over the grid (via the engine) and seed it at rest.
   *  The grid maps to an isotropic square domain x,y ∈ [-D, D·ny/nx]. */
  async function applyIC(): Promise<void> {
    const s = sim;
    const expr = icExpr.trim();
    if (!s || !expr) return;
    icBusy = true;
    icError = "";
    try {
      const D = 3;
      const x0 = -D;
      const x1 = D;
      const dy = (D * (s.ny - 1)) / (s.nx - 1); // equal per-cell spacing (isotropic)
      const gr = await call("sampleGrid", [expr, "x", "y", x0, x1, s.nx, -dy, dy, s.ny]);
      if (!gr.ok) {
        icError = gr.error || "could not evaluate";
        return;
      }
      const g = gr.g;
      let peak = 0;
      for (const v of g) if (v != null && Number.isFinite(v)) peak = Math.max(peak, Math.abs(v));
      if (peak < 1e-12) {
        icError = "expression is flat (all zero)";
        return;
      }
      const norm = 1 / peak;
      s.reset();
      // sampleGrid is row-major (row j, col i) matching sim.idx(i,j).
      for (let k = 0; k < g.length && k < s.cur.length; k++) {
        const v = g[k];
        const d = v != null && Number.isFinite(v) ? v * norm : 0;
        s.cur[k] = d;
        s.prev[k] = d; // start at rest
      }
      scale = 0.4;
      touched = true;
      running = true;
    } catch (e) {
      icError = e instanceof Error ? e.message : "failed";
    } finally {
      icBusy = false;
    }
  }

  // --- share links ----------------------------------------------------------
  /** Snapshot the current controls into a shareable config. */
  function currentConfig(): WaveConfig {
    return {
      v: 1,
      scene: sceneId,
      boundary: boundaryV,
      speed: speedV,
      damping: dampingV,
      freq: freqV,
      src: srcMode,
      brush,
      strength,
      robin: robinV,
      filterType,
      cutoff: filterCutoffV,
      reflect: filterReflectV,
      model: modelV,
      mass: massV,
      stencil: stencilV,
      color: colormap,
      view: viewMode,
      cols,
      ic: icExpr.trim().slice(0, 512),
    };
  }

  /** Apply a decoded config: set every control, then re-lay scene / IC. */
  function applyConfig(cfg: WaveConfig): void {
    speedV = cfg.speed;
    dampingV = cfg.damping;
    freqV = cfg.freq;
    srcMode = cfg.src;
    brush = cfg.brush;
    strength = cfg.strength;
    boundaryV = cfg.boundary;
    robinV = cfg.robin;
    filterType = cfg.filterType;
    filterCutoffV = cfg.cutoff;
    filterReflectV = cfg.reflect;
    modelV = cfg.model;
    massV = cfg.mass;
    stencilV = cfg.stencil;
    colormap = cfg.color;
    viewMode = cfg.view;
    cols = cfg.cols;
    sceneId = cfg.scene;
    icExpr = cfg.ic;
    // Re-establish the field: an IC wins over a scene's demo source.
    untrack(() => {
      const s = sim;
      if (!s) return;
      if (cfg.ic) {
        void applyIC();
      } else if (cfg.scene !== "empty") {
        applyScene(s, cfg.scene, true);
      }
    });
  }

  /** Build a share URL for the current setup and copy it to the clipboard. */
  async function shareLink(): Promise<void> {
    const url = `${location.origin}${location.pathname}#wave=${encodeWave(currentConfig())}`;
    try {
      await navigator.clipboard.writeText(url);
      shareLabel = "Copied!";
    } catch {
      // Clipboard blocked (e.g. insecure context): drop the link into the URL
      // bar so it can still be copied manually.
      location.hash = `wave=${encodeWave(currentConfig())}`;
      shareLabel = "In address bar";
    }
    setTimeout(() => (shareLabel = "Share link"), 1800);
  }

  // Restore a shared setup from the URL hash once, on first mount.
  let restored = false;
  $effect(() => {
    const s = sim;
    if (!s || restored) return;
    restored = true;
    untrack(() => {
      const m = /(?:^|[#&])wave=([^&]+)/.exec(location.hash);
      if (!m) return;
      const cfg = decodeWave(decodeURIComponent(m[1]));
      if (cfg) applyConfig(cfg);
    });
  });

  function resample(from: WaveSim, to: WaveSim): void {
    for (let j = 0; j < to.ny; j++) {
      const fj = Math.min(from.ny - 1, Math.round((j / (to.ny - 1)) * (from.ny - 1)));
      for (let i = 0; i < to.nx; i++) {
        const fi = Math.min(from.nx - 1, Math.round((i / (to.nx - 1)) * (from.nx - 1)));
        const k = to.idx(i, j);
        const fk = from.idx(fi, fj);
        to.cur[k] = from.cur[fk];
        to.prev[k] = from.prev[fk];
      }
    }
  }

  // --- color lookup tables ---------------------------------------------------
  // 256-entry diverging maps over t ∈ [-1, 1]; index = round((t+1)/2·255).
  function buildLut(
    stops: [number, [number, number, number]][],
  ): Uint8ClampedArray {
    const lut = new Uint8ClampedArray(256 * 3);
    for (let m = 0; m < 256; m++) {
      const t = m / 255;
      let a = stops[0];
      let b = stops[stops.length - 1];
      for (let s = 0; s < stops.length - 1; s++) {
        if (t >= stops[s][0] && t <= stops[s + 1][0]) {
          a = stops[s];
          b = stops[s + 1];
          break;
        }
      }
      const span = b[0] - a[0] || 1;
      const f = (t - a[0]) / span;
      lut[m * 3] = a[1][0] + (b[1][0] - a[1][0]) * f;
      lut[m * 3 + 1] = a[1][1] + (b[1][1] - a[1][1]) * f;
      lut[m * 3 + 2] = a[1][2] + (b[1][2] - a[1][2]) * f;
    }
    return lut;
  }

  // Diverging palettes: rest (0) maps to the panel background so the membrane
  // blends into the UI in either theme, and only the wavefronts glow.
  type RGB = [number, number, number];
  const PALETTES: Record<string, { neg: RGB; pos: RGB }> = {
    coolwarm: { neg: [59, 110, 220], pos: [225, 70, 55] },
    fire: { neg: [30, 170, 190], pos: [245, 150, 40] },
    violet: { neg: [40, 205, 225], pos: [215, 70, 200] },
  };

  /** Read a CSS color (hex or rgb()) into an RGB triple. */
  function readRgb(el: Element, name: string, fb: RGB): RGB {
    const v = getComputedStyle(el).getPropertyValue(name).trim();
    const hex = /^#([0-9a-f]{3}|[0-9a-f]{6})$/i.exec(v);
    if (hex) {
      let h = hex[1];
      if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
      return [
        parseInt(h.slice(0, 2), 16),
        parseInt(h.slice(2, 4), 16),
        parseInt(h.slice(4, 6), 16),
      ];
    }
    const rgb = /rgba?\(([^)]+)\)/i.exec(v);
    if (rgb) {
      const p = rgb[1].split(/[,\s/]+/).map(Number);
      if (p.length >= 3) return [p[0], p[1], p[2]];
    }
    return fb;
  }

  let lut: Uint8ClampedArray = new Uint8ClampedArray(256 * 3);
  let lutKey = "";
  // Cache the panel-bg read; getComputedStyle is a style-recalc flush, so it
  // is refreshed only every ~30 frames rather than every rAF frame.
  let bgRgb: RGB = [24, 27, 33];
  let wallRgb: RGB = [120, 124, 132];
  let bgTick = 0;
  function ensureLut(name: string, bg: RGB): void {
    const key = `${name}|${bg.join(",")}`;
    if (key === lutKey) return;
    lutKey = key;
    const p = PALETTES[name] ?? PALETTES.coolwarm;
    lut = buildLut([
      [0, p.neg],
      [0.5, bg],
      [1, p.pos],
    ]);
  }

  // --- render buffer ---------------------------------------------------------
  let field: HTMLCanvasElement | undefined; // offscreen nx×ny buffer
  let fieldCtx: CanvasRenderingContext2D | null = null;
  let imgData: ImageData | null = null;
  let scale = 0.05; // smoothed amplitude normalizer (avoids flicker)
  let scaleI = 0.01; // smoothed intensity normalizer (intensity view)

  // Robust peak: the ~97th-percentile of |u| via a coarse histogram (O(n), no
  // sort). Ignoring a handful of hot cells — the near-field spike of a driven
  // point source — keeps the color scale on the wave the user cares about.
  const histBuf = new Int32Array(64);
  function robustPeak(cur: Float32Array): number {
    let mx = 0;
    for (let k = 0; k < cur.length; k++) {
      const a = cur[k] < 0 ? -cur[k] : cur[k];
      if (a > mx) mx = a;
    }
    if (mx < 1e-9) return mx;
    const bins = histBuf;
    bins.fill(0);
    const s = 63 / mx;
    for (let k = 0; k < cur.length; k++) {
      const a = cur[k] < 0 ? -cur[k] : cur[k];
      bins[(a * s) | 0]++;
    }
    const want = cur.length * 0.97;
    let cum = 0;
    for (let b = 0; b < 64; b++) {
      cum += bins[b];
      if (cum >= want) return ((b + 1) / 64) * mx;
    }
    return mx;
  }

  function paint(): void {
    const s = sim;
    const disp = canvas;
    if (!s || !disp) return;

    // (Re)allocate the offscreen field buffer when the grid changes.
    if (!field || field.width !== s.nx || field.height !== s.ny) {
      field = document.createElement("canvas");
      field.width = s.nx;
      field.height = s.ny;
      fieldCtx = field.getContext("2d");
      imgData = fieldCtx ? fieldCtx.createImageData(s.nx, s.ny) : null;
    }
    if (!fieldCtx || !imgData) return;

    // Ease the color scale toward a *robust* peak (a high percentile of |u|,
    // not the single hottest cell) so faint ripples stay visible while a big
    // pluck doesn't wash everything out — and, crucially, so the large
    // near-field of a driven point source (a scene's source) cannot crush the
    // downstream diffraction/interference pattern to black. The floor keeps a
    // near-rest field fading to the calm background instead of amplifying noise.
    // Intensity view paints the time-averaged ⟨u²⟩ (a one-sided heatmap on the
    // palette's positive arm); wave view paints the live signed displacement.
    const intensityMode = viewMode === "intensity";
    const srcArr = intensityMode ? s.intensityField() : s.cur;
    const peak = robustPeak(srcArr);
    let inv: number;
    if (intensityMode) {
      scaleI += (Math.max(peak, 1e-4) - scaleI) * 0.05;
      inv = scaleI > 1e-9 ? 1 / scaleI : 0;
    } else {
      scale += (Math.max(peak, 0.08) - scale) * 0.08;
      inv = scale > 1e-6 ? 1 / scale : 0;
    }

    // Theme-aware LUT: rest → panel background, so the field blends into the
    // page in light and dark; rebuilt only when the palette or bg changes.
    if (bgTick++ % 30 === 0) {
      bgRgb = readRgb(disp, "--bg-panel", bgRgb);
      wallRgb = readRgb(disp, "--fg-muted", wallRgb);
    }
    ensureLut(colormap, bgRgb);
    const px = imgData.data;
    // Scene overlays: solid cells paint as opaque walls; a slower medium
    // (cScale < 1) is shaded a touch darker so the "glass" is visible.
    const solid = s.hasScene ? s.obstacles : null;
    const med = s.hasScene ? s.medium : null; // per-cell cScale²
    for (let k = 0; k < srcArr.length; k++) {
      const o = k * 4;
      if (solid && solid[k]) {
        px[o] = wallRgb[0];
        px[o + 1] = wallRgb[1];
        px[o + 2] = wallRgb[2];
        px[o + 3] = 255;
        continue;
      }
      let m: number;
      if (intensityMode) {
        // √(I/peak) maps energy → amplitude-like brightness; positive arm only.
        let t = Math.sqrt(Math.max(srcArr[k], 0) * inv);
        t = t > 1 ? 1 : t;
        m = ((0.5 + 0.5 * t ** 0.7) * 255) | 0;
      } else {
        let t = srcArr[k] * inv; // ~[-1, 1]
        t = t < -1 ? -1 : t > 1 ? 1 : t;
        // Signed-gamma boost: emphasize small displacements so ripples read
        // against the near-background rest state.
        const ts = t < 0 ? -((-t) ** 0.7) : t ** 0.7;
        m = ((ts + 1) * 0.5 * 255) | 0;
      }
      const l = m * 3;
      if (med && med[k] < 0.999) {
        const dim = 0.62 + 0.38 * Math.sqrt(med[k]); // slower ⇒ darker
        px[o] = lut[l] * dim;
        px[o + 1] = lut[l + 1] * dim;
        px[o + 2] = lut[l + 2] * dim;
      } else {
        px[o] = lut[l];
        px[o + 1] = lut[l + 1];
        px[o + 2] = lut[l + 2];
      }
      px[o + 3] = 255;
    }
    fieldCtx.putImageData(imgData, 0, 0);

    // Blit the low-res field up to the display canvas (smoothed).
    const dpr = window.devicePixelRatio || 1;
    const cssW = width || disp.clientWidth || s.nx;
    if (disp.width !== Math.round(cssW * dpr) || disp.height !== Math.round(HEIGHT * dpr)) {
      disp.width = Math.round(cssW * dpr);
      disp.height = Math.round(HEIGHT * dpr);
      disp.style.width = cssW + "px";
      disp.style.height = HEIGHT + "px";
    }
    const ctx = disp.getContext("2d");
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(field, 0, 0, s.nx, s.ny, 0, 0, cssW, HEIGHT);

    // Probe markers: a labelled ring at each receiver (drawn in CSS pixels).
    if (probes.length > 0) {
      ctx.font = "600 10px var(--font-mono, monospace)";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      for (let idx = 0; idx < probes.length; idx++) {
        const p = probes[idx];
        const cx = p.xf * cssW;
        const cy = p.yf * HEIGHT;
        ctx.beginPath();
        ctx.arc(cx, cy, 6.5, 0, 2 * Math.PI);
        ctx.fillStyle = "rgba(0,0,0,0.35)";
        ctx.fill();
        ctx.lineWidth = 1.5;
        ctx.strokeStyle = "rgba(255,255,255,0.92)";
        ctx.stroke();
        ctx.fillStyle = "rgba(255,255,255,0.95)";
        ctx.fillText(String(idx + 1), cx, cy + 0.5);
      }
    }
  }

  // --- animation loop --------------------------------------------------------
  // One loop for the component's lifetime; restarts only when the canvas
  // remounts. running/visible/steps are read live inside the callback, so
  // toggling them never re-runs this effect (no rAF leak / double loop).
  $effect(() => {
    const disp = canvas;
    if (!disp) return;
    let raf = 0;
    let frame = 0;
    const loop = () => {
      raf = requestAnimationFrame(loop);
      const s = sim;
      if (!s || !visible) return;
      if (running) s.step(STEPS_PER_FRAME);
      paint();
      if ((frame & 7) === 0) energy = s.energy();
      // Refresh the probe spectra a few times a second (FFT is not free).
      if (probes.length > 0 && (frame & 7) === 3) updateSpectra(s);
      frame++;
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  });

  // --- probe spectra ---------------------------------------------------------
  /** Recompute every probe's spectrum: update the peak readouts and repaint
   *  the sparkline canvases. Called at a low rate from the animation loop. */
  function updateSpectra(s: WaveSim): void {
    const peaks: number[] = [];
    for (let idx = 0; idx < probes.length; idx++) {
      const spec = magnitudeSpectrum(s.probeSeries(idx));
      peaks.push(spec.peakFreq);
      drawSpectrum(specCanvases[idx], spec);
    }
    // Only publish when a value actually moved (avoids per-frame reactivity).
    if (peaks.length !== probePeaks.length || peaks.some((v, i) => Math.abs(v - probePeaks[i]) > 1e-4)) {
      probePeaks = peaks;
    }
  }

  /** Paint a magnitude spectrum as a filled area with the peak bin marked. */
  function drawSpectrum(cv: HTMLCanvasElement | undefined, spec: { freq: Float32Array; mag: Float32Array; peakFreq: number }): void {
    if (!cv) return;
    const dpr = window.devicePixelRatio || 1;
    const cssW = cv.clientWidth || 168;
    const cssH = cv.clientHeight || 40;
    if (cv.width !== Math.round(cssW * dpr) || cv.height !== Math.round(cssH * dpr)) {
      cv.width = Math.round(cssW * dpr);
      cv.height = Math.round(cssH * dpr);
    }
    const ctx = cv.getContext("2d");
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, cssW, cssH);
    const n = spec.mag.length;
    if (n < 2) return;
    const accent = getComputedStyle(cv).getPropertyValue("--accent").trim() || "#4f8cff";
    const muted = getComputedStyle(cv).getPropertyValue("--fg-muted").trim() || "#888";
    const pad = 2;
    const w = cssW - 2 * pad;
    const h = cssH - 2 * pad;
    // Filled area under the magnitude curve (x = freq 0..0.5 → full width).
    ctx.beginPath();
    ctx.moveTo(pad, cssH - pad);
    for (let b = 0; b < n; b++) {
      const x = pad + (b / (n - 1)) * w;
      const y = cssH - pad - Math.min(1, spec.mag[b]) * h;
      ctx.lineTo(x, y);
    }
    ctx.lineTo(pad + w, cssH - pad);
    ctx.closePath();
    ctx.fillStyle = accent + "33";
    ctx.fill();
    ctx.strokeStyle = accent;
    ctx.lineWidth = 1;
    ctx.stroke();
    // Peak marker: a vertical line at the dominant frequency.
    if (spec.peakFreq > 0) {
      const px = pad + (spec.peakFreq / 0.5) * w;
      ctx.strokeStyle = muted;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(px, pad);
      ctx.lineTo(px, cssH - pad);
      ctx.stroke();
    }
  }

  /** Human label for a probe's peak frequency (cycles/step + period). */
  function peakLabel(f: number): string {
    if (!f || f <= 0) return "—";
    return `f ${f.toFixed(3)} · T ${Math.round(1 / f)}`;
  }

  // --- pointer gestures ------------------------------------------------------
  let dragging = false;
  let lastX = 0;
  let lastY = 0;

  function toGrid(e: PointerEvent): { gx: number; gy: number } {
    const s = sim!;
    const rect = canvas!.getBoundingClientRect();
    const fx = (e.clientX - rect.left) / rect.width;
    const fy = (e.clientY - rect.top) / rect.height;
    return {
      gx: Math.max(0, Math.min(s.nx - 1, fx * s.nx)),
      gy: Math.max(0, Math.min(s.ny - 1, fy * s.ny)),
    };
  }

  function onPointerDown(e: PointerEvent): void {
    if (!sim) return;
    touched = true;
    const { gx, gy } = toGrid(e);
    if (probeTool) {
      addProbeAt(gx, gy);
      return; // probes don't inject energy or drag
    }
    canvas!.setPointerCapture(e.pointerId);
    dragging = true;
    lastX = gx;
    lastY = gy;
    if (srcMode === "drive") {
      // Start a continuous oscillator at the pointer (ripple-tank dipper).
      sim.setSource(gx, gy, driveFreq, 0.35 * strength, brush);
    } else {
      // Ripple: emit a wavelet at the set frequency.
      sim.poke(gx, gy, brush, 1.1 * strength, wavelength);
    }
  }

  function onPointerMove(e: PointerEvent): void {
    if (!dragging || !sim) return;
    const { gx, gy } = toGrid(e);
    if (srcMode === "drive") {
      sim.setSource(gx, gy, driveFreq, 0.35 * strength, brush);
      lastX = gx;
      lastY = gy;
      return;
    }
    const dx = gx - lastX;
    const dy = gy - lastY;
    const dist = Math.hypot(dx, dy);
    if (dist < 0.5) return; // ignore jitter
    // A moving source: launch a wavelet directionally (d'Alembert) along the
    // drag, scaled by drag speed.
    const spd = Math.min(dist, 12);
    sim.launch(gx, gy, brush, 0.5 * strength * (spd / 6), dx, dy, wavelength);
    lastX = gx;
    lastY = gy;
  }

  function onPointerUp(e: PointerEvent): void {
    // Only a real drive/ripple gesture (which set `dragging`) should stop the
    // source — a probe placement returns before `dragging` is set, so it must
    // not clear a scene's driven dipper out from under the field.
    if (dragging) sim?.clearSource();
    dragging = false;
    if (canvas?.hasPointerCapture(e.pointerId)) canvas.releasePointerCapture(e.pointerId);
  }

  // --- actions ---------------------------------------------------------------
  function reset(): void {
    sim?.reset();
    scale = 0.08;
    scaleI = 0.01;
    energy = 0;
  }
  function togglePlay(): void {
    running = !running;
  }
  /** Fill fraction (0–100%) of a slider, for the custom filled-track style. */
  function pct(v: number, min: number, max: number): string {
    const f = max > min ? (v - min) / (max - min) : 0;
    return `${Math.max(0, Math.min(1, f)) * 100}%`;
  }
  const MODEL_LABELS: Record<FieldModel, string> = {
    linear: "Linear",
    "klein-gordon": "Klein–Gordon",
    "sine-gordon": "sine-Gordon",
  };
  const BOUNDARIES: { id: Boundary; label: string; title: string }[] = [
    { id: "fixed", label: "Fixed", title: "Clamped edge (drum) — reflects with inversion" },
    { id: "free", label: "Free", title: "Free edge — reflects without inversion" },
    { id: "robin", label: "Robin", title: "Impedance edge ∂u/∂n + k·u = 0 — springy, tunable between free and fixed" },
    { id: "filtered", label: "Filter", title: "Frequency-dependent edge — the reflection is shaped by a digital filter" },
    { id: "absorbing", label: "Open", title: "Absorbing edge — waves leave the window" },
  ];
</script>

<div class="wave" class:compact bind:this={host}>
  <div class="rail" role="group" aria-label="Wave field controls">
    <!-- Transport + live status ------------------------------------------- -->
    <div class="transport">
      <button
        class="play"
        onclick={togglePlay}
        aria-pressed={running}
        aria-label={running ? "Pause" : "Play"}
        title={running ? "Pause" : "Play"}
      >
        {#if running}
          <svg viewBox="0 0 16 16" aria-hidden="true"><rect x="4.5" y="3.5" width="2.4" height="9" rx="0.7" /><rect x="9.1" y="3.5" width="2.4" height="9" rx="0.7" /></svg>
        {:else}
          <svg viewBox="0 0 16 16" aria-hidden="true"><path d="M5.5 3.6v8.8l7-4.4z" /></svg>
        {/if}
      </button>
      <button class="ghost-btn" onclick={reset} aria-label="Reset field" title="Reset field">
        <svg viewBox="0 0 16 16" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M12.5 8a4.5 4.5 0 1 1-1.4-3.26" /><path d="M12.6 3.2v2.6h-2.6" /></svg>
      </button>
      <div class="status" title="Total field energy (kinetic + potential, arb. units)">
        <span class="status-state"><span class="dot" class:live={running}></span>{running ? "Running" : "Paused"}</span>
        <span class="status-energy"><span class="ek">E</span>{Math.max(0, Math.round(energy * 1000))}</span>
      </div>
    </div>

    <!-- Simulation -------------------------------------------------------- -->
    <section class="group">
      <h3 class="group-title">Simulation</h3>
      <label class="field" title="Wave propagation speed (Courant number)">
        <span class="field-head"><span class="field-label">Speed</span><span class="field-val">{speedV.toFixed(2)}</span></span>
        <input class="slider" type="range" min="0.05" max="1" step="0.01" bind:value={speedV} style="--pct:{pct(speedV, 0.05, 1)}" />
      </label>
      <label class="field" title="Energy loss over time">
        <span class="field-head"><span class="field-label">Damping</span><span class="field-val">{dampingV.toFixed(2)}</span></span>
        <input class="slider" type="range" min="0" max="0.6" step="0.01" bind:value={dampingV} style="--pct:{pct(dampingV, 0, 0.6)}" />
      </label>
    </section>

    <!-- Source ------------------------------------------------------------ -->
    <section class="group">
      <h3 class="group-title">Source</h3>
      <label class="field" title="Frequency of the injected wave: low = long waves, high = short">
        <span class="field-head"><span class="field-label">Frequency</span><span class="field-val">{freqV.toFixed(2)}</span></span>
        <input class="slider" type="range" min="0" max="1" step="0.01" bind:value={freqV} style="--pct:{pct(freqV, 0, 1)}" />
      </label>
      <div class="seg wide" role="group" aria-label="Source mode">
        <button class="seg-btn" class:active={srcMode === "ripple"} title="Click or drag emits a wavelet at the set frequency" onclick={() => (srcMode = "ripple")}>Ripple</button>
        <button class="seg-btn" class:active={srcMode === "drive"} title="Hold to run a continuous oscillator (ripple-tank dipper)" onclick={() => (srcMode = "drive")}>Drive</button>
      </div>
      {#if !compact}
        <label class="field" title="Source size">
          <span class="field-head"><span class="field-label">Brush</span><span class="field-val">{brush}</span></span>
          <input class="slider" type="range" min="1" max="12" step="1" bind:value={brush} style="--pct:{pct(brush, 1, 12)}" />
        </label>
        <label class="field" title="Injection strength">
          <span class="field-head"><span class="field-label">Strength</span><span class="field-val">{strength.toFixed(1)}</span></span>
          <input class="slider" type="range" min="0.2" max="2.5" step="0.1" bind:value={strength} style="--pct:{pct(strength, 0.2, 2.5)}" />
        </label>
      {/if}
    </section>

    <!-- Scene + boundary -------------------------------------------------- -->
    <section class="group">
      <h3 class="group-title">Scene</h3>
      <div class="select-field" title="Structured-media presets: obstacles, slits, lenses, refraction">
        <select aria-label="Scene preset" value={sceneId} onchange={(e) => selectScene(e.currentTarget.value)}>
          {#each WAVE_SCENES as sc (sc.id)}
            <option value={sc.id} title={sc.hint}>{sc.label}</option>
          {/each}
        </select>
      </div>
      <span class="subhead">Boundary</span>
      <div class="seg wrap" role="group" aria-label="Boundary">
        {#each BOUNDARIES as b (b.id)}
          <button class="seg-btn" class:active={boundaryV === b.id} title={b.title} onclick={() => (boundaryV = b.id)}>{b.label}</button>
        {/each}
      </div>
      {#if boundaryV === "robin"}
        <label class="field" title="Edge stiffness k: 0 ≈ free, large ≈ fixed">
          <span class="field-head"><span class="field-label">Stiffness</span><span class="field-val">{robinV.toFixed(1)}</span></span>
          <input class="slider" type="range" min="0" max="8" step="0.1" bind:value={robinV} style="--pct:{pct(robinV, 0, 8)}" />
        </label>
      {/if}
      {#if boundaryV === "filtered"}
        <div class="seg wide" role="group" aria-label="Reflection filter">
          <button class="seg-btn" class:active={filterType === "lowpass"} title="Low-pass: reflect low frequencies, absorb highs (muffling wall)" onclick={() => (filterType = "lowpass")}>Low-pass</button>
          <button class="seg-btn" class:active={filterType === "highpass"} title="High-pass: reflect high frequencies, absorb lows" onclick={() => (filterType = "highpass")}>High-pass</button>
        </div>
        <label class="field" title="Filter cutoff — higher reflects more">
          <span class="field-head"><span class="field-label">Cutoff</span><span class="field-val">{filterCutoffV.toFixed(2)}</span></span>
          <input class="slider" type="range" min="0.02" max="1" step="0.01" bind:value={filterCutoffV} style="--pct:{pct(filterCutoffV, 0.02, 1)}" />
        </label>
        <label class="field" title="Overall reflectivity — 0 fully absorbs">
          <span class="field-head"><span class="field-label">Reflect</span><span class="field-val">{filterReflectV.toFixed(2)}</span></span>
          <input class="slider" type="range" min="0" max="1" step="0.01" bind:value={filterReflectV} style="--pct:{pct(filterReflectV, 0, 1)}" />
        </label>
      {/if}
    </section>

    {#if !compact}
      <!-- Physics --------------------------------------------------------- -->
      <section class="group">
        <h3 class="group-title">Physics</h3>
        <div class="select-field" title="Which wave equation the field obeys">
          <select bind:value={modelV} aria-label="Field model">
            <option value="linear">Linear wave</option>
            <option value="klein-gordon">Klein–Gordon</option>
            <option value="sine-gordon">sine-Gordon</option>
          </select>
        </div>
        {#if modelV !== "linear"}
          <label
            class="field"
            title={modelV === "sine-gordon"
              ? "Nonlinear coupling m² — the kink width scales as 1/√m"
              : "Mass m²: adds dispersion; a rest frequency √m and a wavelength-dependent phase speed"}
          >
            <span class="field-head"><span class="field-label">{massLabel}</span><span class="field-val">{massV.toFixed(2)}</span></span>
            <input class="slider" type="range" min="0" max={massMax} step="0.01" bind:value={massV} style="--pct:{pct(massV, 0, massMax)}" />
          </label>
        {/if}
        {#if modelV === "sine-gordon"}
          <button class="action-btn" title="Seed a kink soliton — a 2π twist that glides without spreading" onclick={seedKink}>
            <svg viewBox="0 0 20 12" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"><path d="M2 10 C 7 10, 8 2, 13 2 L 18 2" /></svg>
            Seed kink soliton
          </button>
        {/if}
        <span class="subhead">Laplacian stencil</span>
        <div class="seg wide" role="group" aria-label="Laplacian stencil">
          <button class="seg-btn" class:active={stencilV === "five"} title="5-point Laplacian (standard star stencil)" onclick={() => (stencilV = "five")}>5-point</button>
          <button class="seg-btn" class:active={stencilV === "nine"} title="9-point isotropic Laplacian — reduced grid anisotropy (rounder wavefronts)" onclick={() => (stencilV = "nine")}>9-point</button>
        </div>
      </section>

      <!-- Measure --------------------------------------------------------- -->
      <section class="group">
        <h3 class="group-title">Measure</h3>
        <div class="seg wide" role="group" aria-label="Field view">
          <button class="seg-btn" class:active={viewMode === "wave"} title="Show the live wave displacement" onclick={() => (viewMode = "wave")}>Wave</button>
          <button class="seg-btn" class:active={viewMode === "intensity"} title="Time-averaged intensity ⟨u²⟩ — freezes interference/diffraction fringes into a heatmap" onclick={() => (viewMode = "intensity")}>Intensity</button>
        </div>
        <div class="probe-row">
          <button
            class="action-btn"
            class:on={probeTool}
            aria-pressed={probeTool}
            title="Probe tool: click the field to drop a receiver that records u(t) and shows its spectrum"
            onclick={() => (probeTool = !probeTool)}
          >
            <svg class="probe-ico" viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="2.2" /><circle cx="8" cy="8" r="5.2" fill="none" stroke="currentColor" stroke-width="1.2" /></svg>
            {probeTool ? "Placing probes…" : "Add probe"}
          </button>
          {#if probes.length > 0}
            <button class="mini-btn" title="Remove all probes" onclick={clearProbes}>Clear {probes.length}</button>
          {/if}
        </div>
      </section>

      <!-- Appearance ------------------------------------------------------ -->
      <section class="group">
        <h3 class="group-title">Appearance</h3>
        <label class="field" title="Grid resolution">
          <span class="field-head"><span class="field-label">Detail</span><span class="field-val">{cols}</span></span>
          <input class="slider" type="range" min="64" max="300" step="4" bind:value={cols} style="--pct:{pct(cols, 64, 300)}" />
        </label>
        <div class="select-field" title="Color map">
          <select bind:value={colormap} aria-label="Color map">
            <option value="coolwarm">Coolwarm</option>
            <option value="fire">Fire</option>
            <option value="violet">Violet</option>
          </select>
        </div>
      </section>

      <!-- Authoring: CAS initial condition + share link ------------------- -->
      <section class="group">
        <h3 class="group-title">Authoring</h3>
        <span class="subhead">Initial condition u(x,y,0)</span>
        <form class="ic-row" onsubmit={(e) => { e.preventDefault(); applyIC(); }}>
          <input
            class="ic-input"
            type="text"
            bind:value={icExpr}
            spellcheck="false"
            autocomplete="off"
            autocapitalize="off"
            placeholder="e.g. exp(-4(x^2+y^2))·cos(9x)"
            aria-label="Initial-condition expression f(x, y)"
          />
          <button class="mini-btn" type="submit" disabled={icBusy || !icExpr.trim()} title="Sample f(x,y) onto the grid as the starting field">
            {icBusy ? "…" : "Set"}
          </button>
        </form>
        {#if icError}
          <span class="ic-error" role="alert">{icError}</span>
        {/if}
        <button class="action-btn" onclick={shareLink} title="Copy a link that reproduces this exact setup">
          <svg viewBox="0 0 16 16" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="4.5" cy="8" r="1.8"/><circle cx="11.5" cy="4" r="1.8"/><circle cx="11.5" cy="12" r="1.8"/><path d="M6.1 7.2 9.9 4.8M6.1 8.8l3.8 2.4"/></svg>
          {shareLabel}
        </button>
      </section>
    {/if}
  </div>

  <div class="main">
    <div class="stage" bind:this={stage}>
      <canvas
        bind:this={canvas}
        aria-label="Interactive 2-D wave field — drag to add energy"
        onpointerdown={onPointerDown}
        onpointermove={onPointerMove}
        onpointerup={onPointerUp}
        onpointercancel={onPointerUp}
      ></canvas>
      {#if !touched}
        <div class="hint" aria-hidden="true">
          <svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="2.4" /><circle cx="8" cy="8" r="5.4" fill="none" stroke="currentColor" stroke-width="1" opacity="0.55" /></svg>
          <span>Click to pluck · drag to launch a wavefront</span>
        </div>
      {/if}
      {#if probeTool}
        <div class="hint probe-hint" aria-hidden="true">
          <svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="2.2" /><circle cx="8" cy="8" r="5.2" fill="none" stroke="currentColor" stroke-width="1.2" /></svg>
          <span>Probe tool — click the field to drop a receiver</span>
        </div>
      {/if}
    </div>

    {#if !compact && probes.length > 0}
      <div class="probes" aria-label="Probe spectra">
        {#each probes as p, idx (p.id)}
          <div class="probe">
            <div class="probe-head">
              <span class="probe-badge">{idx + 1}</span>
              <span class="probe-peak" title="Dominant frequency (cycles/step) and period (steps)">
                {peakLabel(probePeaks[idx] ?? 0)}
              </span>
              <button class="probe-x" title="Remove this probe" aria-label={`Remove probe ${idx + 1}`} onclick={() => removeProbe(idx)}>×</button>
            </div>
            <canvas class="spec" bind:this={specCanvases[idx]} aria-label={`Probe ${idx + 1} spectrum`}></canvas>
          </div>
        {/each}
      </div>
    {/if}
  </div>
</div>

<style>
  /* Layout: control rail + hero canvas (full) / stacked toolbar (compact) --- */
  .wave {
    display: grid;
    grid-template-columns: 250px minmax(0, 1fr);
    gap: 0.85rem;
    align-items: start;
    min-width: 0;
  }
  .wave.compact {
    display: flex;
    flex-direction: column;
    gap: 0.55rem;
  }
  .main {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    min-width: 0;
  }

  /* Control rail ------------------------------------------------------------ */
  .rail {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    padding: 0.85rem 0.85rem 0.95rem;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg-panel);
    min-width: 0;
  }
  .wave.compact .rail {
    flex-direction: row;
    flex-wrap: wrap;
    align-items: center;
    gap: 0.4rem 0.65rem;
    padding: 0.5rem 0.65rem;
  }

  /* Transport + status ------------------------------------------------------ */
  .transport {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .play {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 2.4rem;
    height: 2.4rem;
    flex: 0 0 auto;
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border: none;
    border-radius: var(--radius);
    cursor: pointer;
    box-shadow: 0 1px 3px color-mix(in srgb, var(--accent) 30%, transparent);
    transition: filter 120ms ease, transform 120ms ease;
  }
  .play:hover {
    filter: brightness(1.07);
  }
  .play:active {
    transform: scale(0.96);
  }
  .play svg {
    width: 17px;
    height: 17px;
    fill: currentColor;
  }
  .ghost-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 2.4rem;
    height: 2.4rem;
    flex: 0 0 auto;
    color: var(--fg-muted);
    background: transparent;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    cursor: pointer;
    transition: color 120ms ease, border-color 120ms ease;
  }
  .ghost-btn:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .ghost-btn svg {
    width: 15px;
    height: 15px;
  }
  .status {
    margin-left: auto;
    display: flex;
    flex-direction: column;
    align-items: flex-end;
    gap: 1px;
    text-align: right;
  }
  .wave.compact .status {
    flex-direction: row;
    align-items: center;
    gap: 0.6rem;
  }
  .status-state {
    display: inline-flex;
    align-items: center;
    gap: 0.35rem;
    font-size: 0.68rem;
    font-weight: 600;
    color: var(--fg-muted);
  }
  .dot {
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--fg-muted);
    opacity: 0.45;
  }
  .dot.live {
    background: #46c26a;
    opacity: 1;
    box-shadow: 0 0 0 3px color-mix(in srgb, #46c26a 22%, transparent);
  }
  .status-energy {
    font-family: var(--font-mono);
    font-size: 0.84rem;
    font-variant-numeric: tabular-nums;
    color: var(--fg);
  }
  .ek {
    font-size: 0.6rem;
    font-weight: 700;
    letter-spacing: 0.04em;
    color: var(--fg-muted);
    margin-right: 0.3rem;
  }

  /* Groups ------------------------------------------------------------------ */
  .group {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    padding-top: 0.65rem;
    border-top: 1px solid color-mix(in srgb, var(--border) 72%, transparent);
  }
  .group-title {
    margin: 0;
    font-size: 0.62rem;
    font-weight: 700;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--fg-muted);
  }
  .subhead {
    font-size: 0.62rem;
    font-weight: 700;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    color: var(--fg-muted);
    margin-top: 0.05rem;
  }
  .wave.compact .group {
    flex-direction: row;
    align-items: center;
    gap: 0.4rem 0.6rem;
    padding-top: 0;
    border-top: none;
  }
  .wave.compact .group + .group {
    padding-left: 0.65rem;
    border-left: 1px solid var(--border);
  }
  .wave.compact .group-title,
  .wave.compact .subhead {
    display: none;
  }

  /* Sliders ----------------------------------------------------------------- */
  .field {
    display: flex;
    flex-direction: column;
    gap: 0.35rem;
    cursor: pointer;
  }
  .field-head {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 0.5rem;
  }
  .field-label {
    font-size: 0.76rem;
    color: var(--fg);
  }
  .field-val {
    font-family: var(--font-mono);
    font-size: 0.72rem;
    font-variant-numeric: tabular-nums;
    color: var(--fg-muted);
  }
  .slider {
    -webkit-appearance: none;
    appearance: none;
    width: 100%;
    height: 16px;
    background: transparent;
    cursor: pointer;
  }
  .slider::-webkit-slider-runnable-track {
    height: 5px;
    border-radius: 999px;
    background: linear-gradient(
      to right,
      var(--accent) 0 var(--pct, 50%),
      var(--track) var(--pct, 50%) 100%
    );
  }
  .slider::-moz-range-track {
    height: 5px;
    border-radius: 999px;
    background: linear-gradient(
      to right,
      var(--accent) 0 var(--pct, 50%),
      var(--track) var(--pct, 50%) 100%
    );
  }
  .slider::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: 14px;
    height: 14px;
    margin-top: -4.5px;
    border-radius: 50%;
    background: var(--accent);
    border: 2px solid var(--bg-panel);
    box-shadow: 0 1px 2px rgb(0 0 0 / 25%);
  }
  .slider::-moz-range-thumb {
    width: 13px;
    height: 13px;
    border-radius: 50%;
    background: var(--accent);
    border: 2px solid var(--bg-panel);
    box-shadow: 0 1px 2px rgb(0 0 0 / 25%);
  }
  .slider:focus-visible {
    outline: none;
  }
  .slider:focus-visible::-webkit-slider-thumb {
    box-shadow: 0 0 0 3px color-mix(in srgb, var(--accent) 32%, transparent);
  }
  .wave.compact .field {
    flex-direction: row;
    align-items: center;
    gap: 0.45rem;
  }
  .wave.compact .field-head {
    flex: 0 0 auto;
  }
  .wave.compact .field-val {
    display: none;
  }
  .wave.compact .slider {
    width: 5rem;
    flex: 0 0 auto;
  }

  /* Segmented controls ------------------------------------------------------ */
  .seg {
    display: inline-flex;
    padding: 3px;
    gap: 2px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 1.3);
  }
  .seg.wide {
    display: flex;
  }
  .seg.wide .seg-btn {
    flex: 1;
  }
  /* Fixed-option segmented control with more items than fit one row: wrap it
     and let each row's buttons grow to fill the full width, so five options
     read as an even 3-over-2 instead of a grid with an orphaned empty cell. */
  .seg.wrap {
    display: flex;
    flex-wrap: wrap;
  }
  .seg.wrap .seg-btn {
    flex: 1 1 auto;
    text-align: center;
  }
  .wave:not(.compact) .seg.wrap .seg-btn {
    min-width: 3.5rem;
  }
  .seg-btn {
    flex: 0 0 auto;
    font: inherit;
    font-size: 0.74rem;
    font-weight: 600;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    border-radius: calc(var(--radius) / 1.8);
    padding: 0.3rem 0.55rem;
    cursor: pointer;
    white-space: nowrap;
    transition: color 120ms ease, background 120ms ease;
  }
  .seg-btn:hover {
    color: var(--fg);
  }
  /* A refined soft-tint selected state (matching the top-nav idiom), not a
     loud solid-blue fill — the solid --accent is reserved for the primary
     Play/Compute actions. */
  .seg-btn.active {
    color: var(--accent);
    background: var(--accent-soft);
    box-shadow: inset 0 0 0 1px var(--accent-line);
  }
  .seg-btn.active:hover {
    color: var(--accent);
  }
  .wave.compact .seg-btn {
    padding: 0.2rem 0.5rem;
    font-size: 0.72rem;
  }

  /* Selects ----------------------------------------------------------------- */
  .select-field {
    position: relative;
    display: block;
  }
  .select-field select {
    width: 100%;
    font: inherit;
    font-size: 0.78rem;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 1.5);
    padding: 0.42rem 1.9rem 0.42rem 0.6rem;
    cursor: pointer;
    appearance: none;
    -webkit-appearance: none;
    transition: border-color 120ms ease;
  }
  .select-field select:hover {
    border-color: var(--accent);
  }
  .select-field::after {
    content: "";
    position: absolute;
    right: 0.7rem;
    top: 50%;
    width: 0.44rem;
    height: 0.44rem;
    border-right: 1.5px solid var(--fg-muted);
    border-bottom: 1.5px solid var(--fg-muted);
    transform: translateY(-70%) rotate(45deg);
    pointer-events: none;
  }
  .wave.compact .select-field select {
    padding: 0.26rem 1.7rem 0.26rem 0.5rem;
    font-size: 0.75rem;
  }

  /* Action / mini buttons --------------------------------------------------- */
  .action-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 0.45rem;
    width: 100%;
    font: inherit;
    font-size: 0.76rem;
    font-weight: 600;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 1.5);
    padding: 0.44rem 0.6rem;
    cursor: pointer;
    transition: color 120ms ease, border-color 120ms ease, background 120ms ease;
  }
  .action-btn:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .action-btn.on {
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border-color: var(--accent);
  }
  .action-btn svg {
    width: 17px;
    height: 12px;
    flex: 0 0 auto;
  }
  .probe-ico {
    width: 14px;
    height: 14px;
    fill: currentColor;
  }
  .probe-row {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }
  .probe-row .action-btn {
    flex: 1;
  }
  .mini-btn {
    flex: 0 0 auto;
    font: inherit;
    font-size: 0.73rem;
    font-weight: 600;
    color: var(--fg-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 1.5);
    padding: 0.44rem 0.6rem;
    cursor: pointer;
    white-space: nowrap;
    transition: color 120ms ease, border-color 120ms ease;
  }
  .mini-btn:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .mini-btn:disabled {
    opacity: 0.5;
    cursor: default;
  }
  .mini-btn:disabled:hover {
    color: var(--fg-muted);
    border-color: var(--border);
  }

  /* CAS initial-condition input --------------------------------------------- */
  .ic-row {
    display: flex;
    gap: 0.4rem;
    align-items: stretch;
  }
  .ic-input {
    flex: 1;
    min-width: 0;
    font: inherit;
    font-family: var(--font-mono);
    font-size: 0.74rem;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 1.5);
    padding: 0.4rem 0.5rem;
    transition: border-color 120ms ease;
  }
  .ic-input:focus {
    outline: none;
    border-color: var(--accent);
  }
  .ic-error {
    font-size: 0.7rem;
    color: var(--danger, #e5484d);
    word-break: break-word;
  }

  /* Canvas stage ------------------------------------------------------------ */
  .stage {
    position: relative;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: hidden;
    background: var(--bg-panel);
    line-height: 0;
    box-shadow: inset 0 1px 30px rgb(0 0 0 / 24%);
  }
  .stage::after {
    content: "";
    position: absolute;
    inset: 0;
    pointer-events: none;
    border-radius: inherit;
    box-shadow: inset 0 0 70px rgb(0 0 0 / 22%);
  }
  canvas {
    display: block;
    width: 100%;
    max-width: 100%;
    touch-action: none;
    cursor: crosshair;
  }
  .hint {
    position: absolute;
    left: 50%;
    bottom: 0.85rem;
    transform: translateX(-50%);
    z-index: 1;
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    margin: 0;
    padding: 0.34rem 0.85rem;
    font-size: 0.78rem;
    line-height: 1;
    color: var(--fg-muted);
    background: color-mix(in srgb, var(--bg-panel) 84%, transparent);
    border: 1px solid var(--border);
    border-radius: 999px;
    backdrop-filter: blur(4px);
    pointer-events: none;
    white-space: nowrap;
  }
  .hint svg {
    width: 15px;
    height: 15px;
    fill: currentColor;
    flex: 0 0 auto;
    opacity: 0.8;
  }
  .probe-hint {
    bottom: auto;
    top: 0.85rem;
    color: var(--accent);
    border-color: color-mix(in srgb, var(--accent) 45%, var(--border));
  }
  .probe-hint svg {
    fill: none;
    stroke: currentColor;
    stroke-width: 1.2;
    opacity: 1;
  }

  /* Probe spectra ----------------------------------------------------------- */
  .probes {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(190px, 1fr));
    gap: 0.5rem;
  }
  .probe {
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--bg-panel);
    padding: 0.45rem 0.55rem 0.35rem;
    min-width: 0;
  }
  .probe-head {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    margin-bottom: 0.3rem;
  }
  .probe-badge {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 1.2rem;
    height: 1.2rem;
    flex: 0 0 auto;
    font-family: var(--font-mono);
    font-size: 0.66rem;
    font-weight: 700;
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border-radius: 999px;
  }
  .probe-peak {
    font-family: var(--font-mono);
    font-size: 0.7rem;
    color: var(--fg-muted);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    flex: 1 1 auto;
  }
  .probe-x {
    flex: 0 0 auto;
    width: 1.2rem;
    height: 1.2rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 1rem;
    line-height: 1;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    border-radius: 5px;
    cursor: pointer;
  }
  .probe-x:hover {
    color: var(--accent);
    background: var(--bg);
  }
  .spec {
    display: block;
    width: 100%;
    height: 42px;
  }

  /* Narrow screens: collapse the rail above the canvas --------------------- */
  @media (max-width: 640px) {
    .wave:not(.compact) {
      grid-template-columns: 1fr;
    }
    .wave:not(.compact) .rail {
      flex-direction: row;
      flex-wrap: wrap;
      align-items: flex-end;
    }
  }
</style>
