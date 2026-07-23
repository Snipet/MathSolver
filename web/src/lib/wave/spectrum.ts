// Magnitude spectrum of a probe time-series (Phase 2 wave instrumentation).
//
// A self-contained, dependency-free radix-2 FFT plus a windowed magnitude
// spectrum. The FDTD probe records u(t) sampled once per step, so frequencies
// are expressed in cycles/step: bin b of an N-point transform sits at b/N,
// spanning 0 (DC) to 0.5 (Nyquist). The engine's driven source uses the same
// cycles/step unit, so a drive at f shows up as a peak at f here — the whole
// point of the instrument.
//
// DOM-free and deterministic; unit-tested in tools/wave_sim_test.mjs alongside
// the sim (a pure sinusoid must produce a single sharp peak at its frequency).

/**
 * In-place iterative Cooley–Tukey FFT (decimation-in-time). `re`/`im` are the
 * real/imaginary parts of a signal whose length MUST be a power of two; on
 * return they hold the transform. `sign` = -1 for the forward transform,
 * +1 for the inverse (the caller scales the inverse by 1/N if needed).
 */
export function fftRadix2(re: Float32Array, im: Float32Array, sign = -1): void {
  const n = re.length;
  if (n <= 1) return;
  // Bit-reversal permutation.
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const tr = re[i];
      re[i] = re[j];
      re[j] = tr;
      const ti = im[i];
      im[i] = im[j];
      im[j] = ti;
    }
  }
  // Butterflies over successively larger halves.
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (sign * 2 * Math.PI) / len;
    const wr = Math.cos(ang);
    const wi = Math.sin(ang);
    const half = len >> 1;
    for (let i = 0; i < n; i += len) {
      let cr = 1;
      let ci = 0;
      for (let k = 0; k < half; k++) {
        const a = i + k;
        const b = a + half;
        const tr = re[b] * cr - im[b] * ci;
        const ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
        const ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

/** Largest power of two ≤ n (and ≥ 1). */
function floorPow2(n: number): number {
  let p = 1;
  while (p * 2 <= n) p *= 2;
  return p;
}

export interface Spectrum {
  /** Bin frequencies in cycles/step, 0 (DC) … 0.5 (Nyquist). */
  freq: Float32Array;
  /** Magnitude at each bin, normalized so the largest non-DC bin is 1. */
  mag: Float32Array;
  /** Frequency (cycles/step) of the dominant non-DC bin, or 0 if none. */
  peakFreq: number;
  /** Number of time samples that fed the transform. */
  n: number;
}

const EMPTY: Spectrum = {
  freq: new Float32Array(0),
  mag: new Float32Array(0),
  peakFreq: 0,
  n: 0,
};

/**
 * One-sided magnitude spectrum of a real signal. The signal is *linearly*
 * detrended (best-fit line removed, so neither a DC offset nor a slow
 * settling ramp swamps the lowest bins), Hann-windowed to curb spectral
 * leakage, and transformed over the largest power-of-two window that fits
 * (using the most recent samples). Magnitudes are normalized to the dominant
 * non-DC bin so the shape is what matters, not the absolute scale.
 *
 * Returns an empty spectrum for fewer than `minLen` (default 8) samples.
 */
export function magnitudeSpectrum(
  samples: Float32Array | number[],
  minLen = 8,
): Spectrum {
  const total = samples.length;
  if (total < minLen) return EMPTY;

  const N = floorPow2(total);
  const off = total - N; // use the freshest N samples
  const re = new Float32Array(N);
  const im = new Float32Array(N);

  // Linear detrend: subtract the least-squares line a + b·k. Removing the DC
  // offset AND the slope kills the low-bin leakage from a transient that is
  // still ramping up — a mean-only detrend would leave that in bin 1 and let
  // it masquerade as the dominant frequency. With x = k, mean_x = (N−1)/2 and
  // Σ(k−mean_x)² = N(N²−1)/12.
  const meanX = (N - 1) / 2;
  let meanY = 0;
  for (let k = 0; k < N; k++) meanY += samples[off + k];
  meanY /= N;
  let sxy = 0;
  for (let k = 0; k < N; k++) sxy += (k - meanX) * (samples[off + k] - meanY);
  const sxx = (N * (N * N - 1)) / 12;
  const slope = sxx > 0 ? sxy / sxx : 0;
  const intercept = meanY - slope * meanX;

  // Hann window: w = ½(1 − cos(2πk/(N−1))).
  const wnorm = (2 * Math.PI) / (N - 1);
  for (let k = 0; k < N; k++) {
    const w = 0.5 * (1 - Math.cos(wnorm * k));
    re[k] = (samples[off + k] - (intercept + slope * k)) * w;
  }

  fftRadix2(re, im, -1);

  const half = N >> 1;
  const freq = new Float32Array(half + 1);
  const mag = new Float32Array(half + 1);
  let peakMag = 0;
  let peakBin = 0;
  for (let b = 0; b <= half; b++) {
    freq[b] = b / N;
    const m = Math.hypot(re[b], im[b]);
    mag[b] = m;
    if (b > 0 && m > peakMag) {
      peakMag = m;
      peakBin = b;
    }
  }
  // Normalize to the dominant non-DC bin (fall back to any positive value).
  const norm = peakMag > 0 ? 1 / peakMag : 0;
  if (norm > 0) for (let b = 0; b <= half; b++) mag[b] *= norm;

  return { freq, mag, peakFreq: peakBin / N, n: N };
}
