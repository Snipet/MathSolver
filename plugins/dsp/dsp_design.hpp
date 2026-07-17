#pragma once

// DSP filter-design numerics, separated from the plugin command layer so the
// native test suite can verify the math directly (tests/test_plugin_dsp.cpp).

#include <vector>

namespace mathsolver::plugins::dsp {

/// One digital biquad, normalized so a0 = 1:
///   y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
struct Biquad {
    double b0, b1, b2, a1, a2;
};

/// Butterworth low-/high-pass as a biquad cascade: analog prototype poles,
/// cutoff prewarped, bilinear transform per section. Preconditions (checked
/// by the command layer): order >= 1, 0 < fc < fs/2.
std::vector<Biquad> design_butter(bool highpass, int order, double fc, double fs);

/// |H(e^{j 2 pi f / fs})| of the cascade in dB (-inf for a true zero).
double magnitude_db(const std::vector<Biquad>& sections, double f, double fs);

} // namespace mathsolver::plugins::dsp
