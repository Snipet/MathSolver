<script lang="ts">
  // Interactive 2-D wave field: a real-time FDTD simulation (WaveSim) painted
  // to a canvas, with energy introduced by mouse/touch gestures — click to
  // pluck, drag to launch a directional wavefront. Used both inline as a
  // console "wave" cell (compact) and as the full Workbench "Wave" tab.
  import { untrack } from "svelte";
  import { WaveSim, type Boundary, type FilterType } from "../wave/sim";
  import { WAVE_SCENES, buildScene } from "../wave/scenes";
  import { magnitudeSpectrum } from "../wave/spectrum";

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

  const HEIGHT = untrack(() => (compact ? 260 : 460));
  const STEPS_PER_FRAME = 2; // temporal oversampling for smoother propagation

  // --- sizing ----------------------------------------------------------------
  let host: HTMLDivElement | undefined = $state();
  let canvas: HTMLCanvasElement | undefined = $state();
  let width = $state(0);
  let visible = $state(true);

  $effect(() => {
    const el = host;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      width = Math.max(0, Math.floor(entries[0].contentRect.width));
    });
    ro.observe(el);
    const io = new IntersectionObserver(
      (entries) => (visible = entries[0].isIntersecting),
      { threshold: 0 },
    );
    io.observe(el);
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
  const BOUNDARIES: { id: Boundary; label: string; title: string }[] = [
    { id: "fixed", label: "Fixed", title: "Clamped edge (drum) — reflects with inversion" },
    { id: "free", label: "Free", title: "Free edge — reflects without inversion" },
    { id: "robin", label: "Robin", title: "Impedance edge ∂u/∂n + k·u = 0 — springy, tunable between free and fixed" },
    { id: "filtered", label: "Filter", title: "Frequency-dependent edge — the reflection is shaped by a digital filter" },
    { id: "absorbing", label: "Open", title: "Absorbing edge — waves leave the window" },
  ];
</script>

<div class="wave" class:compact bind:this={host}>
  <div class="panel" role="group" aria-label="Wave field controls">
    <div class="bar">
      <div class="tgroup transport">
        <button
          class="icon-btn primary"
          onclick={togglePlay}
          aria-pressed={running}
          aria-label={running ? "Pause" : "Play"}
          title={running ? "Pause" : "Play"}
        >
          {#if running}
            <svg viewBox="0 0 16 16" aria-hidden="true"><rect x="4.5" y="3.5" width="2.4" height="9" rx="0.6" /><rect x="9.1" y="3.5" width="2.4" height="9" rx="0.6" /></svg>
          {:else}
            <svg viewBox="0 0 16 16" aria-hidden="true"><path d="M5.5 3.6v8.8l7-4.4z" /></svg>
          {/if}
        </button>
        <button class="icon-btn" onclick={reset} aria-label="Reset field" title="Reset field">
          <svg viewBox="0 0 16 16" aria-hidden="true" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M12.5 8a4.5 4.5 0 1 1-1.4-3.26" /><path d="M12.6 3.2v2.6h-2.6" /></svg>
        </button>
      </div>

      <span class="sep"></span>

      <div class="tgroup">
        <label class="ctl" title="Wave propagation speed (Courant number)">
          <span>Speed</span>
          <input type="range" min="0.05" max="1" step="0.01" bind:value={speedV} />
        </label>
        <label class="ctl" title="Energy loss over time">
          <span>Damping</span>
          <input type="range" min="0" max="0.6" step="0.01" bind:value={dampingV} />
        </label>
      </div>

      <span class="sep"></span>

      <div class="tgroup">
        <label class="ctl" title="Frequency of the injected wave: low = long waves, high = short">
          <span>Freq</span>
          <input type="range" min="0" max="1" step="0.01" bind:value={freqV} />
        </label>
        <div class="seg" role="group" aria-label="Source mode">
          <button
            class="seg-btn"
            class:active={srcMode === "ripple"}
            title="Click or drag emits a wavelet at the set frequency"
            onclick={() => (srcMode = "ripple")}
          >
            Ripple
          </button>
          <button
            class="seg-btn"
            class:active={srcMode === "drive"}
            title="Hold to run a continuous oscillator (ripple-tank dipper)"
            onclick={() => (srcMode = "drive")}
          >
            Drive
          </button>
        </div>
      </div>

      {#if !compact}
        <span class="sep"></span>
        <div class="tgroup">
          <label class="ctl" title="Source size">
            <span>Brush</span>
            <input type="range" min="1" max="12" step="1" bind:value={brush} />
          </label>
          <label class="ctl" title="Injection strength">
            <span>Strength</span>
            <input type="range" min="0.2" max="2.5" step="0.1" bind:value={strength} />
          </label>
        </div>
      {/if}

      <span
        class="stat"
        title="Total field energy (kinetic + potential, arb. units)"
      >
        <span class="stat-k">E</span>
        <span class="stat-v">{Math.max(0, Math.round(energy * 1000))}</span>
      </span>
    </div>

    <div class="bar row2">
      <div class="tgroup">
        <span class="glabel">Scene</span>
        <label class="ctl select" title="Structured-media presets: obstacles, slits, lenses, refraction">
          <select
            aria-label="Scene preset"
            value={sceneId}
            onchange={(e) => selectScene(e.currentTarget.value)}
          >
            {#each WAVE_SCENES as sc (sc.id)}
              <option value={sc.id} title={sc.hint}>{sc.label}</option>
            {/each}
          </select>
        </label>
      </div>

      <div class="tgroup">
        <span class="glabel">Edge</span>
        <div class="seg" role="group" aria-label="Boundary">
          {#each BOUNDARIES as b (b.id)}
            <button
              class="seg-btn"
              class:active={boundaryV === b.id}
              title={b.title}
              onclick={() => (boundaryV = b.id)}
            >
              {b.label}
            </button>
          {/each}
        </div>

        {#if boundaryV === "robin"}
          <label class="ctl" title="Edge stiffness k: 0 ≈ free, large ≈ fixed">
            <span>Stiffness</span>
            <input type="range" min="0" max="8" step="0.1" bind:value={robinV} />
          </label>
        {/if}

        {#if boundaryV === "filtered"}
          <div class="seg" role="group" aria-label="Reflection filter">
            <button
              class="seg-btn"
              class:active={filterType === "lowpass"}
              title="Low-pass: reflect low frequencies, absorb highs (muffling wall)"
              onclick={() => (filterType = "lowpass")}
            >
              LP
            </button>
            <button
              class="seg-btn"
              class:active={filterType === "highpass"}
              title="High-pass: reflect high frequencies, absorb lows"
              onclick={() => (filterType = "highpass")}
            >
              HP
            </button>
          </div>
          <label class="ctl" title="Filter cutoff — higher reflects more">
            <span>Cutoff</span>
            <input type="range" min="0.02" max="1" step="0.01" bind:value={filterCutoffV} />
          </label>
          <label class="ctl" title="Overall reflectivity — 0 fully absorbs">
            <span>Reflect</span>
            <input type="range" min="0" max="1" step="0.01" bind:value={filterReflectV} />
          </label>
        {/if}
      </div>

      {#if !compact}
        <span class="sep"></span>
        <div class="tgroup">
          <span class="glabel">Measure</span>
          <div class="seg" role="group" aria-label="Field view">
            <button
              class="seg-btn"
              class:active={viewMode === "wave"}
              title="Show the live wave displacement"
              onclick={() => (viewMode = "wave")}
            >
              Wave
            </button>
            <button
              class="seg-btn"
              class:active={viewMode === "intensity"}
              title="Time-averaged intensity ⟨u²⟩ — freezes interference/diffraction fringes into a heatmap"
              onclick={() => (viewMode = "intensity")}
            >
              Intensity
            </button>
          </div>
          <div class="seg" role="group" aria-label="Probe tool">
            <button
              class="seg-btn"
              class:active={probeTool}
              aria-pressed={probeTool}
              title="Probe tool: click the field to drop a receiver that records u(t) and shows its spectrum"
              onclick={() => (probeTool = !probeTool)}
            >
              <svg class="probe-ico" viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="2.2" /><circle cx="8" cy="8" r="5.2" fill="none" stroke="currentColor" stroke-width="1.2" /></svg>
              Probe
            </button>
          </div>
          {#if probes.length > 0}
            <button class="mini-btn" title="Remove all probes" onclick={clearProbes}>
              Clear ({probes.length})
            </button>
          {/if}
        </div>
      {/if}

      {#if !compact}
        <span class="sep"></span>
        <div class="tgroup">
          <label class="ctl" title="Grid resolution">
            <span>Detail</span>
            <input type="range" min="64" max="300" step="4" bind:value={cols} />
          </label>
          <label class="ctl select" title="Color map">
            <span>Color</span>
            <select bind:value={colormap}>
              <option value="coolwarm">coolwarm</option>
              <option value="fire">fire</option>
              <option value="violet">violet</option>
            </select>
          </label>
        </div>
      {/if}
    </div>
  </div>

  <div class="stage">
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
            <button
              class="probe-x"
              title="Remove this probe"
              aria-label={`Remove probe ${idx + 1}`}
              onclick={() => removeProbe(idx)}
            >
              ×
            </button>
          </div>
          <canvas class="spec" bind:this={specCanvases[idx]} aria-label={`Probe ${idx + 1} spectrum`}></canvas>
        </div>
      {/each}
    </div>
  {/if}
</div>

<style>
  .wave {
    display: flex;
    flex-direction: column;
    gap: 0.6rem;
    min-width: 0;
  }

  /* Control card ------------------------------------------------------------ */
  .panel {
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: color-mix(in srgb, var(--bg-panel) 55%, transparent);
  }
  .bar {
    display: flex;
    align-items: center;
    gap: 0.4rem 0.7rem;
    flex-wrap: wrap;
    padding: 0.5rem 0.65rem;
  }
  .bar.row2 {
    border-top: 1px solid var(--border);
  }
  .tgroup {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem 0.7rem;
    flex-wrap: wrap;
  }
  .sep {
    width: 1px;
    align-self: stretch;
    min-height: 1.3rem;
    background: var(--border);
  }
  .glabel {
    font-size: 0.66rem;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--fg-muted);
  }

  /* Transport icon buttons -------------------------------------------------- */
  .icon-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 2rem;
    height: 2rem;
    color: var(--fg-muted);
    background: var(--bg-panel);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 1.5);
    cursor: pointer;
    transition: color 100ms ease, border-color 100ms ease, background 100ms ease;
  }
  .icon-btn svg {
    width: 15px;
    height: 15px;
    fill: currentColor;
  }
  .icon-btn:hover {
    color: var(--accent);
    border-color: var(--accent);
  }
  .icon-btn.primary {
    color: var(--accent-fg, #fff);
    background: var(--accent);
    border-color: var(--accent);
    box-shadow: 0 1px 5px color-mix(in srgb, var(--accent) 40%, transparent);
  }
  .icon-btn.primary:hover {
    filter: brightness(1.06);
  }

  /* Labeled sliders --------------------------------------------------------- */
  .ctl {
    display: inline-flex;
    align-items: center;
    gap: 0.4rem;
    font-size: 0.75rem;
    color: var(--fg-muted);
  }
  .ctl > span {
    white-space: nowrap;
  }
  .ctl input[type="range"] {
    width: 5.4rem;
    height: 1.1rem;
    accent-color: var(--accent);
    cursor: pointer;
  }
  .ctl.select select {
    font: inherit;
    font-size: 0.75rem;
    color: var(--fg);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: calc(var(--radius) / 2);
    padding: 0.12rem 0.35rem;
    cursor: pointer;
  }

  /* Segmented controls ------------------------------------------------------ */
  .seg {
    display: inline-flex;
    padding: 2px;
    gap: 2px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 999px;
  }
  .seg-btn {
    font: inherit;
    font-size: 0.73rem;
    font-weight: 600;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    border-radius: 999px;
    padding: 0.16rem 0.6rem;
    cursor: pointer;
    transition: color 100ms ease, background 100ms ease;
  }
  .seg-btn:hover {
    color: var(--fg);
  }
  .seg-btn.active {
    color: var(--accent-fg, #fff);
    background: var(--accent);
  }

  /* Energy stat chip -------------------------------------------------------- */
  .stat {
    margin-left: auto;
    display: inline-flex;
    align-items: baseline;
    gap: 0.35rem;
    padding: 0.16rem 0.6rem;
    border: 1px solid var(--border);
    border-radius: 999px;
    background: var(--bg);
  }
  .stat-k {
    font-size: 0.64rem;
    font-weight: 700;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    color: var(--fg-muted);
  }
  .stat-v {
    font-family: var(--font-mono);
    font-size: 0.82rem;
    font-variant-numeric: tabular-nums;
    color: var(--fg);
    min-width: 2ch;
    text-align: right;
  }

  /* Canvas stage ------------------------------------------------------------ */
  .stage {
    position: relative;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    overflow: hidden;
    background: var(--bg-panel);
    line-height: 0;
    box-shadow: inset 0 1px 24px rgb(0 0 0 / 22%);
  }
  /* Subtle vignette for depth; never intercepts pointer energy injection. */
  .stage::after {
    content: "";
    position: absolute;
    inset: 0;
    pointer-events: none;
    border-radius: inherit;
    box-shadow: inset 0 0 60px rgb(0 0 0 / 20%);
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
    bottom: 0.8rem;
    transform: translateX(-50%);
    z-index: 1;
    display: inline-flex;
    align-items: center;
    gap: 0.45rem;
    margin: 0;
    padding: 0.3rem 0.8rem;
    font-size: 0.78rem;
    line-height: 1;
    color: var(--fg-muted);
    background: color-mix(in srgb, var(--bg-panel) 82%, transparent);
    border: 1px solid var(--border);
    border-radius: 999px;
    backdrop-filter: blur(3px);
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
    top: 0.8rem;
    color: var(--accent);
    border-color: color-mix(in srgb, var(--accent) 45%, var(--border));
  }

  /* Measure controls -------------------------------------------------------- */
  .probe-ico {
    width: 13px;
    height: 13px;
    fill: currentColor;
    margin-right: 0.25rem;
    vertical-align: -2px;
  }
  .mini-btn {
    font: inherit;
    font-size: 0.72rem;
    font-weight: 600;
    color: var(--fg-muted);
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 999px;
    padding: 0.18rem 0.6rem;
    cursor: pointer;
    transition: color 100ms ease, border-color 100ms ease;
  }
  .mini-btn:hover {
    color: var(--accent);
    border-color: var(--accent);
  }

  /* Probe spectra panel ----------------------------------------------------- */
  .probes {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
    gap: 0.5rem;
  }
  .probe {
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: color-mix(in srgb, var(--bg-panel) 55%, transparent);
    padding: 0.4rem 0.5rem 0.3rem;
    min-width: 0;
  }
  .probe-head {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    margin-bottom: 0.25rem;
  }
  .probe-badge {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 1.15rem;
    height: 1.15rem;
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
    width: 1.1rem;
    height: 1.1rem;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    font-size: 0.95rem;
    line-height: 1;
    color: var(--fg-muted);
    background: transparent;
    border: none;
    border-radius: 4px;
    cursor: pointer;
  }
  .probe-x:hover {
    color: var(--accent);
    background: var(--bg);
  }
  .spec {
    display: block;
    width: 100%;
    height: 40px;
  }
</style>
