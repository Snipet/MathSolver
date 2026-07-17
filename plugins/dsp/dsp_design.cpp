// DSP filter-design numerics (dsp_design.hpp).

#include "dsp_design.hpp"

#include <cmath>
#include <complex>
#include <numbers>

namespace mathsolver::plugins::dsp {

namespace {

/// Bilinear transform of an analog second-order section
/// (B2 s^2 + B1 s + B0)/(A2 s^2 + A1 s + A0) with K = 2*fs.
Biquad bilinear2(double B2, double B1, double B0, double A2, double A1, double A0,
                 double K) {
    const double K2 = K * K;
    const double a0 = A2 * K2 + A1 * K + A0;
    return Biquad{
        (B2 * K2 + B1 * K + B0) / a0,
        (2.0 * B0 - 2.0 * B2 * K2) / a0,
        (B2 * K2 - B1 * K + B0) / a0,
        (2.0 * A0 - 2.0 * A2 * K2) / a0,
        (A2 * K2 - A1 * K + A0) / a0,
    };
}

/// Bilinear transform of an analog first-order section
/// (B1 s + B0)/(A1 s + A0); the resulting biquad has b2 = a2 = 0.
Biquad bilinear1(double B1, double B0, double A1, double A0, double K) {
    const double a0 = A1 * K + A0;
    return Biquad{(B1 * K + B0) / a0, (B0 - B1 * K) / a0, 0.0,
                  (A0 - A1 * K) / a0, 0.0};
}

} // namespace

std::vector<Biquad> design_butter(bool highpass, int order, double fc, double fs) {
    const double wc = 2.0 * fs * std::tan(std::numbers::pi * fc / fs); // prewarped
    const double K = 2.0 * fs;
    std::vector<Biquad> sections;
    // Conjugate pole pairs of the Butterworth prototype, scaled to wc:
    // s^2 + 2 wc sin(theta_k) s + wc^2, theta_k = pi (2k+1) / (2 order).
    for (int k = 0; k < order / 2; ++k) {
        const double theta = std::numbers::pi * (2.0 * k + 1.0) / (2.0 * order);
        const double A1 = 2.0 * wc * std::sin(theta);
        const double A0 = wc * wc;
        if (highpass) {
            sections.push_back(bilinear2(1.0, 0.0, 0.0, 1.0, A1, A0, K));
        } else {
            sections.push_back(bilinear2(0.0, 0.0, wc * wc, 1.0, A1, A0, K));
        }
    }
    if (order % 2 == 1) { // odd order: one real pole at -wc
        if (highpass) {
            sections.push_back(bilinear1(1.0, 0.0, 1.0, wc, K));
        } else {
            sections.push_back(bilinear1(0.0, wc, 1.0, wc, K));
        }
    }
    return sections;
}

double magnitude_db(const std::vector<Biquad>& sections, double f, double fs) {
    const double w = 2.0 * std::numbers::pi * f / fs;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = std::polar(1.0, -2.0 * w);
    std::complex<double> h{1.0, 0.0};
    for (const Biquad& s : sections) {
        const std::complex<double> num = s.b0 + s.b1 * z1 + s.b2 * z2;
        const std::complex<double> den = 1.0 + s.a1 * z1 + s.a2 * z2;
        h *= num / den;
    }
    return 20.0 * std::log10(std::abs(h));
}

} // namespace mathsolver::plugins::dsp
