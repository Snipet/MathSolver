#pragma once

// DSP filter-design numerics, separated from the plugin command layer so the
// native test suite can verify the math directly (tests/test_plugin_dsp.cpp).
//
// Design pipeline (the classic zpk route, as in scipy/MATLAB): normalized
// analog low-pass prototype (poles/zeros/gain) → analog frequency transform
// (low/high/band-pass/band-stop, edges prewarped) → bilinear transform to the
// z-domain → conjugate-pair grouping into biquad sections.

#include <stdexcept>
#include <string>
#include <vector>

namespace mathsolver::plugins::dsp {

/// One digital biquad, normalized so a0 = 1:
///   y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
struct Biquad {
    double b0, b1, b2, a1, a2;
};

enum class Family { Butterworth, Cheby1, Cheby2 };
enum class Kind { Lowpass, Highpass, Bandpass, Bandstop };

struct DesignSpec {
    Family family = Family::Butterworth;
    Kind kind = Kind::Lowpass;
    int order = 4;      ///< Analog prototype order (band types double it).
    double f1 = 0.0;    ///< Cutoff (LP/HP) or lower edge (BP/BS), Hz.
    double f2 = 0.0;    ///< Upper edge (BP/BS only), Hz.
    double fs = 0.0;    ///< Sample rate, Hz.
    double ripple_db = 1.0;  ///< Cheby1: passband ripple.
    double atten_db = 40.0;  ///< Cheby2: stopband attenuation.
};

/// Thrown for an invalid specification (message is user-presentable).
class DesignError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// Design the digital filter for `spec` as a biquad cascade.
std::vector<Biquad> design_iir(const DesignSpec& spec);

/// Convenience wrapper for the Butterworth low/high-pass case.
std::vector<Biquad> design_butter(bool highpass, int order, double fc, double fs);

/// Frequency-response sample of a cascade at one frequency.
struct ResponsePoint {
    double mag_db;      ///< 20 log10 |H| (-inf at a true zero).
    double phase_rad;   ///< Sum of per-section principal args (pre-unwrap).
    double group_delay; ///< Samples; non-finite at a true zero.
};
ResponsePoint response_at(const std::vector<Biquad>& sections, double f, double fs);

/// |H(e^{j 2 pi f / fs})| of the cascade in dB (-inf for a true zero).
double magnitude_db(const std::vector<Biquad>& sections, double f, double fs);

} // namespace mathsolver::plugins::dsp
