#pragma once

// DSP filter-design numerics, separated from the plugin command layer so the
// native test suite can verify the math directly (tests/test_plugin_dsp.cpp).
//
// IIR design pipeline (the classic zpk route, as in scipy/MATLAB): normalized
// analog low-pass prototype (poles/zeros/gain) → analog frequency transform
// (low/high/band-pass/band-stop, edges prewarped) → bilinear transform to the
// z-domain → conjugate-pair grouping into biquad sections. The elliptic
// prototype uses Jacobi elliptic functions evaluated by Landen recursion
// (Orfanidis' formulation). FIR design is windowed-sinc with the response
// normalized to exactly unity at the band's reference frequency.

#include <complex>
#include <stdexcept>
#include <string>
#include <vector>

namespace mathsolver::plugins::dsp {

/// One digital biquad, normalized so a0 = 1:
///   y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
struct Biquad {
    double b0, b1, b2, a1, a2;
};

/// Zeros/poles/gain form: H = k * prod(x - z_i) / prod(x - p_i), in either
/// the s- or z-domain depending on context. Conjugate symmetry is assumed.
struct Zpk {
    std::vector<std::complex<double>> z;
    std::vector<std::complex<double>> p;
    double k = 1.0;
};

/// Bilinear transform s -> z at sample rate fs (zeros at infinity map to -1).
Zpk bilinear_zpk(const Zpk& analog, double fs);

/// Group a conjugate-symmetric digital Zpk into biquad sections
/// (nearest-zero pairing, overall gain folded into the first section).
std::vector<Biquad> zpk_to_biquads(const Zpk& digital);

enum class Family { Butterworth, Cheby1, Cheby2, Elliptic };
enum class Kind { Lowpass, Highpass, Bandpass, Bandstop };

struct DesignSpec {
    Family family = Family::Butterworth;
    Kind kind = Kind::Lowpass;
    int order = 4;      ///< Analog prototype order (band types double it).
    double f1 = 0.0;    ///< Cutoff (LP/HP) or lower edge (BP/BS), Hz.
    double f2 = 0.0;    ///< Upper edge (BP/BS only), Hz.
    double fs = 0.0;    ///< Sample rate, Hz.
    double ripple_db = 1.0;  ///< Cheby1 + Elliptic: passband ripple.
    double atten_db = 40.0;  ///< Cheby2 + Elliptic: stopband attenuation.
};

/// Thrown for an invalid specification (message is user-presentable).
class DesignError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// Design the digital IIR filter for `spec` as a biquad cascade.
std::vector<Biquad> design_iir(const DesignSpec& spec);

/// Convenience wrapper for the Butterworth low/high-pass case.
std::vector<Biquad> design_butter(bool highpass, int order, double fc, double fs);

// --- FIR (windowed sinc) ----------------------------------------------------

enum class FirWindow { Rect, Hann, Hamming, Blackman, Kaiser };

struct FirSpec {
    Kind kind = Kind::Lowpass;
    int taps = 63;      ///< Filter length (odd required for high/band-stop).
    double f1 = 0.0;    ///< Cutoff (LP/HP) or lower edge (BP/BS), Hz.
    double f2 = 0.0;    ///< Upper edge (BP/BS only), Hz.
    double fs = 0.0;    ///< Sample rate, Hz.
    FirWindow window = FirWindow::Hamming;
    double kaiser_beta = 8.6; ///< Kaiser window shape (Kaiser only).
};

/// Linear-phase windowed-sinc taps, normalized to exactly unity gain at the
/// band's reference frequency (DC for LP/BS, Nyquist for HP, the band center
/// for BP).
std::vector<double> design_fir(const FirSpec& spec);

// --- responses --------------------------------------------------------------

/// Frequency-response sample at one frequency.
struct ResponsePoint {
    double mag_db;      ///< 20 log10 |H| (-inf at a true zero).
    double phase_rad;   ///< Sum of per-section principal args (pre-unwrap).
    double group_delay; ///< Samples; non-finite at a true zero.
};
ResponsePoint response_at(const std::vector<Biquad>& sections, double f, double fs);
ResponsePoint fir_response_at(const std::vector<double>& taps, double f, double fs);

/// |H(e^{j 2 pi f / fs})| of the cascade in dB (-inf for a true zero).
double magnitude_db(const std::vector<Biquad>& sections, double f, double fs);

/// First n samples of the cascade's impulse response (direct-form II
/// simulation; the step response is its running sum).
std::vector<double> impulse_response(const std::vector<Biquad>& sections, int n);

} // namespace mathsolver::plugins::dsp
