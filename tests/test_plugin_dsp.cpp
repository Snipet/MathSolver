// Plugin framework + DSP plugin tests.
//
// The filter-design numerics are verified directly through dsp_design.hpp
// (no JSON parsing); the command layer and registry are verified through the
// public plugin surface, checking envelope fragments only.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include "../plugins/dsp/dsp_design.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver::plugins;
using dsp::Biquad;
using dsp::design_butter;
using dsp::design_iir;
using dsp::DesignSpec;
using dsp::Family;
using dsp::Kind;
using dsp::magnitude_db;
using dsp::response_at;
using Catch::Matchers::ContainsSubstring;

namespace {

const Plugin& dsp_plugin() {
    register_builtin_plugins();
    const Plugin* p = find("dsp");
    REQUIRE(p != nullptr);
    return *p;
}

} // namespace

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

TEST_CASE("plugins: builtin registration is idempotent and discoverable") {
    register_builtin_plugins();
    register_builtin_plugins(); // second call must not duplicate
    int dsp_count = 0;
    for (const auto& p : registry()) {
        if (p->name() == "dsp") {
            ++dsp_count;
        }
    }
    CHECK(dsp_count == 1);
    CHECK(find("dsp") != nullptr);
    CHECK(find("nope") == nullptr);
}

TEST_CASE("plugins: dsp advertises its commands with usage strings") {
    const auto cmds = dsp_plugin().commands();
    REQUIRE(cmds.size() == 7);
    CHECK(cmds[0].name == "butter");
    CHECK_THAT(cmds[0].usage, ContainsSubstring("dsp.butter"));
    CHECK(cmds[1].name == "cheby1");
    CHECK(cmds[2].name == "cheby2");
    CHECK(cmds[3].name == "ellip");
    CHECK(cmds[4].name == "fir");
    CHECK(cmds[5].name == "remez");
    CHECK_THAT(cmds[5].usage, ContainsSubstring("dsp.remez"));
    CHECK(cmds[6].name == "freqz");
    CHECK_THAT(cmds[6].usage, ContainsSubstring("dsp.freqz"));
}

// ---------------------------------------------------------------------------
// Butterworth design numerics (properties, not golden values)
// ---------------------------------------------------------------------------

TEST_CASE("dsp: butterworth low-pass hits the defining gain points") {
    const double fc = 1000.0;
    const double fs = 48000.0;
    for (const int order : {1, 2, 3, 4, 7, 8}) {
        const auto sos = design_butter(false, order, fc, fs);
        CHECK(sos.size() == static_cast<std::size_t>((order + 1) / 2));
        // DC gain exactly 1 (0 dB): the bilinear transform preserves H(s=0).
        CHECK(std::abs(magnitude_db(sos, fs / 2.0 * 1e-6, fs)) < 1e-6);
        // -3.0103 dB at the (prewarped) cutoff, every order.
        CHECK(std::abs(magnitude_db(sos, fc, fs) - (-3.0103)) < 0.01);
        // Deep in the stopband: at 4*fc the attenuation is at least the
        // asymptotic -6*order dB/octave over 2 octaves, minus slack.
        CHECK(magnitude_db(sos, 4.0 * fc, fs) < -6.0 * order * 2.0 + 3.0);
    }
}

TEST_CASE("dsp: butterworth low-pass response is monotonically decreasing") {
    const auto sos = design_butter(false, 4, 1000.0, 48000.0);
    double prev = magnitude_db(sos, 10.0, 48000.0);
    for (double f = 100.0; f < 23000.0; f += 100.0) {
        const double cur = magnitude_db(sos, f, 48000.0);
        CHECK(cur <= prev + 1e-9);
        prev = cur;
    }
}

TEST_CASE("dsp: butterworth high-pass mirrors the low-pass contract") {
    const double fc = 2000.0;
    const double fs = 44100.0;
    for (const int order : {1, 2, 5, 6}) {
        const auto sos = design_butter(true, order, fc, fs);
        // Passband side: at Nyquist the gain is 0 dB (bilinear maps s=inf to
        // z=-1, preserving the analog high-pass limit).
        CHECK(std::abs(magnitude_db(sos, fs / 2.0, fs)) < 1e-6);
        CHECK(std::abs(magnitude_db(sos, fc, fs) - (-3.0103)) < 0.01);
        // Stopband: an octave below cutoff, at least ~6*order dB down.
        CHECK(magnitude_db(sos, fc / 2.0, fs) < -6.0 * order + 3.0);
    }
}

TEST_CASE("dsp: second-order sections match the analog prototype factors") {
    // For N=2 the digital denominator must come from s^2 + sqrt(2) wc s + wc^2;
    // spot-check via the poles' radius/angle relation instead of goldens:
    // a biquad from a conjugate pair has a2 = r^2 < 1 (stable) and
    // a1 = -2 r cos(phi) with |a1| < 1 + a2.
    for (const int order : {2, 4, 6, 8}) {
        for (const bool hp : {false, true}) {
            for (const auto& s : design_butter(hp, order, 3000.0, 48000.0)) {
                CHECK(s.a2 < 1.0);              // pole radius < 1
                CHECK(s.a2 > -1.0);
                CHECK(std::abs(s.a1) < 1.0 + s.a2 + 1e-12); // stability triangle
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Chebyshev families
// ---------------------------------------------------------------------------

namespace {

DesignSpec spec_of(Family fam, Kind kind, int order, double f1, double f2,
                   double fs, double param = 0.0) {
    DesignSpec s;
    s.family = fam;
    s.kind = kind;
    s.order = order;
    s.f1 = f1;
    s.f2 = f2;
    s.fs = fs;
    if (fam == Family::Cheby1) s.ripple_db = param;
    if (fam == Family::Cheby2) s.atten_db = param;
    return s;
}

} // namespace

TEST_CASE("dsp: chebyshev I low-pass respects the ripple contract") {
    const double fc = 1000.0;
    const double fs = 48000.0;
    const double rp = 1.0;
    for (const int order : {2, 3, 5, 6}) {
        const auto sos =
            design_iir(spec_of(Family::Cheby1, Kind::Lowpass, order, fc, 0, fs, rp));
        // The cutoff is the ripple edge: exactly -rp dB there, all orders.
        CHECK(std::abs(magnitude_db(sos, fc, fs) - (-rp)) < 0.02);
        // DC: 0 dB for odd orders, -rp for even.
        const double dc = magnitude_db(sos, fs / 2.0 * 1e-6, fs);
        CHECK(std::abs(dc - (order % 2 == 1 ? 0.0 : -rp)) < 0.02);
        // Everywhere in the passband the gain stays within [-rp, 0] (+tol).
        for (double f = fc / 50.0; f <= fc; f *= 1.15) {
            const double m = magnitude_db(sos, f, fs);
            CHECK(m < 0.02);
            CHECK(m > -rp - 0.02);
        }
    }
    // Chebyshev's defining advantage: for 3 dB ripple (epsilon ~ 1) the deep
    // stopband beats same-order Butterworth by ~20 log10(2^(N-1)) dB.
    for (const int order : {4, 6}) {
        const auto ch =
            design_iir(spec_of(Family::Cheby1, Kind::Lowpass, order, fc, 0, fs, 3.0));
        const auto bu = design_butter(false, order, fc, fs);
        CHECK(magnitude_db(ch, 4.0 * fc, fs) <
              magnitude_db(bu, 4.0 * fc, fs) - 10.0);
    }
}

TEST_CASE("dsp: chebyshev II low-pass respects the attenuation contract") {
    const double fc = 2000.0;
    const double fs = 48000.0;
    const double rs = 40.0;
    for (const int order : {2, 3, 4, 7}) {
        const auto sos =
            design_iir(spec_of(Family::Cheby2, Kind::Lowpass, order, fc, 0, fs, rs));
        // The cutoff is the stopband edge: exactly -rs dB there.
        CHECK(std::abs(magnitude_db(sos, fc, fs) - (-rs)) < 0.05);
        // DC gain 1.
        CHECK(std::abs(magnitude_db(sos, fs / 2.0 * 1e-6, fs)) < 0.02);
        // Equiripple stopband: never rises above -rs beyond the edge.
        for (double f = fc; f < fs / 2.0 * 0.98; f *= 1.07) {
            CHECK(magnitude_db(sos, f, fs) <= -rs + 0.1);
        }
    }
}

// ---------------------------------------------------------------------------
// Band-pass / band-stop
// ---------------------------------------------------------------------------

TEST_CASE("dsp: butterworth band-pass hits -3 dB at both edges") {
    const double f1 = 500.0;
    const double f2 = 2000.0;
    const double fs = 48000.0;
    for (const int order : {2, 3, 4}) {
        const auto sos =
            design_iir(spec_of(Family::Butterworth, Kind::Bandpass, order, f1, f2, fs));
        CHECK(sos.size() == static_cast<std::size_t>(order)); // 2N poles
        CHECK(std::abs(magnitude_db(sos, f1, fs) - (-3.0103)) < 0.05);
        CHECK(std::abs(magnitude_db(sos, f2, fs) - (-3.0103)) < 0.05);
        // In-band: near 0 dB at the (geometric) center.
        CHECK(magnitude_db(sos, std::sqrt(f1 * f2), fs) > -1.0);
        // Out of band: well down an octave beyond each edge.
        CHECK(magnitude_db(sos, f1 / 2.0, fs) < -6.0 * order + 3.0);
        CHECK(magnitude_db(sos, f2 * 2.0, fs) < -6.0 * order + 3.0);
    }
}

TEST_CASE("dsp: butterworth band-stop notches the band and passes outside") {
    const double f1 = 500.0;
    const double f2 = 2000.0;
    const double fs = 48000.0;
    const auto sos =
        design_iir(spec_of(Family::Butterworth, Kind::Bandstop, 3, f1, f2, fs));
    CHECK(std::abs(magnitude_db(sos, f1, fs) - (-3.0103)) < 0.05);
    CHECK(std::abs(magnitude_db(sos, f2, fs) - (-3.0103)) < 0.05);
    CHECK(magnitude_db(sos, std::sqrt(f1 * f2), fs) < -40.0); // deep notch
    CHECK(std::abs(magnitude_db(sos, 50.0, fs)) < 0.1);        // passbands ~0 dB
    CHECK(std::abs(magnitude_db(sos, 20000.0, fs)) < 0.1);
}

TEST_CASE("dsp: every family/type/order combination is stable") {
    const double fs = 48000.0;
    for (const Family fam : {Family::Butterworth, Family::Cheby1, Family::Cheby2,
                             Family::Elliptic}) {
        const double param = fam == Family::Cheby1 ? 0.5 : 50.0;
        for (const Kind kind :
             {Kind::Lowpass, Kind::Highpass, Kind::Bandpass, Kind::Bandstop}) {
            for (const int order : {1, 2, 3, 5, 8}) {
                const bool band = kind == Kind::Bandpass || kind == Kind::Bandstop;
                const auto sos = design_iir(spec_of(fam, kind, order, 1000.0,
                                                    band ? 4000.0 : 0.0, fs, param));
                for (const Biquad& s : sos) {
                    CHECK(s.a2 < 1.0); // stability triangle
                    CHECK(s.a2 > -1.0);
                    CHECK(std::abs(s.a1) < 1.0 + s.a2 + 1e-9);
                }
            }
        }
    }
}

TEST_CASE("dsp: design errors carry user-presentable messages") {
    CHECK_THROWS_AS(
        design_iir(spec_of(Family::Butterworth, Kind::Lowpass, 4, 30000, 0, 48000)),
        dsp::DesignError);
    CHECK_THROWS_AS(
        design_iir(spec_of(Family::Butterworth, Kind::Bandpass, 4, 2000, 500, 48000)),
        dsp::DesignError);
}

// ---------------------------------------------------------------------------
// Elliptic (Cauer)
// ---------------------------------------------------------------------------

namespace {

DesignSpec ellip_spec(Kind kind, int order, double rp, double rs, double f1,
                      double f2, double fs) {
    DesignSpec s;
    s.family = Family::Elliptic;
    s.kind = kind;
    s.order = order;
    s.f1 = f1;
    s.f2 = f2;
    s.fs = fs;
    s.ripple_db = rp;
    s.atten_db = rs;
    return s;
}

} // namespace

TEST_CASE("dsp: elliptic low-pass is equiripple in both bands") {
    const double fc = 1000.0;
    const double fs = 48000.0;
    const double rp = 1.0;
    const double rs = 60.0;
    for (const int order : {3, 4, 5, 7}) {
        const auto sos =
            design_iir(ellip_spec(Kind::Lowpass, order, rp, rs, fc, 0, fs));
        // Passband edge sits exactly at -rp.
        CHECK(std::abs(magnitude_db(sos, fc, fs) - (-rp)) < 0.03);
        // DC: 0 dB for odd orders, -rp for even.
        const double dc = magnitude_db(sos, fs / 2.0 * 1e-6, fs);
        CHECK(std::abs(dc - (order % 2 == 1 ? 0.0 : -rp)) < 0.03);
        // Passband corridor [-rp, 0] (+tol), and the ripple actually reaches
        // near both walls (equiripple, not just bounded).
        double pass_max = -1e9;
        double pass_min = 1e9;
        for (double f = fc / 40.0; f <= fc; f *= 1.03) {
            const double m = magnitude_db(sos, f, fs);
            CHECK(m < 0.03);
            CHECK(m > -rp - 0.03);
            pass_max = std::max(pass_max, m);
            pass_min = std::min(pass_min, m);
        }
        if (order >= 4) {
            CHECK(pass_max > -0.15);      // touches ~0
            CHECK(pass_min < -rp + 0.15); // touches ~-rp
        }
        // Stopband: find the first crossing under -rs. The transition ratio
        // ws/wp follows the degree equation — wide for low orders, sharp for
        // higher ones (measured 4.9x for N=3 down to <1.5x for N=7 at these
        // rp/rs). Bound it per order with margin.
        double f_stop = 0.0;
        for (double f = fc; f < fs / 2.0; f *= 1.01) {
            if (magnitude_db(sos, f, fs) <= -rs) {
                f_stop = f;
                break;
            }
        }
        REQUIRE(f_stop > 0.0);
        const double ratio_bound = order == 3   ? 6.0
                                   : order == 4 ? 3.2
                                   : order == 5 ? 2.0
                                                : 1.7;
        CHECK(f_stop < ratio_bound * fc);
        // ...and stays equiripple-bounded below -rs after it, while still
        // returning close to the -rs line (finite transmission zeros).
        double stop_max = -1e9;
        for (double f = f_stop * 1.001; f < fs / 2.0 * 0.98; f *= 1.05) {
            const double m = magnitude_db(sos, f, fs);
            CHECK(m <= -rs + 0.1);
            stop_max = std::max(stop_max, m);
        }
        CHECK(stop_max > -rs - 6.0);
    }
}

TEST_CASE("dsp: elliptic transition is sharper than chebyshev of equal order") {
    const double fc = 1000.0;
    const double fs = 48000.0;
    const auto el = design_iir(ellip_spec(Kind::Lowpass, 5, 1.0, 60.0, fc, 0, fs));
    const auto ch =
        design_iir(spec_of(Family::Cheby1, Kind::Lowpass, 5, fc, 0, fs, 1.0));
    // At 1.5*fc the elliptic (with finite zeros) is far deeper down.
    CHECK(magnitude_db(el, 1.5 * fc, fs) < magnitude_db(ch, 1.5 * fc, fs) - 10.0);
}

// ---------------------------------------------------------------------------
// FIR (windowed sinc)
// ---------------------------------------------------------------------------

namespace {

dsp::FirSpec fir_spec(Kind kind, int taps, double f1, double f2, double fs,
                      dsp::FirWindow w = dsp::FirWindow::Hamming,
                      double beta = 8.6) {
    dsp::FirSpec s;
    s.kind = kind;
    s.taps = taps;
    s.f1 = f1;
    s.f2 = f2;
    s.fs = fs;
    s.window = w;
    s.kaiser_beta = beta;
    return s;
}

double fir_mag(const std::vector<double>& h, double f, double fs) {
    return dsp::fir_response_at(h, f, fs).mag_db;
}

} // namespace

TEST_CASE("dsp: fir low-pass is symmetric, unity at DC, linear phase") {
    const auto h = dsp::design_fir(fir_spec(Kind::Lowpass, 101, 1000, 0, 48000));
    REQUIRE(h.size() == 101);
    for (std::size_t n = 0; n < h.size(); ++n) {
        CHECK(std::abs(h[n] - h[h.size() - 1 - n]) < 1e-15); // exact symmetry
    }
    CHECK(std::abs(fir_mag(h, 48000.0 / 2 * 1e-6, 48000)) < 1e-6); // 0 dB at DC
    // Hamming stopband: comfortably below -40 dB past the transition.
    for (double f = 3000; f < 23000; f += 500) {
        CHECK(fir_mag(h, f, 48000) < -40.0);
    }
    // Exact linear phase: group delay = (taps-1)/2 everywhere it is defined.
    for (const double f : {100.0, 500.0, 900.0}) {
        CHECK(std::abs(dsp::fir_response_at(h, f, 48000).group_delay - 50.0) < 1e-6);
    }
}

TEST_CASE("dsp: fir kaiser window deepens the stopband") {
    const auto hamming = dsp::design_fir(fir_spec(Kind::Lowpass, 101, 1000, 0, 48000));
    const auto kaiser = dsp::design_fir(
        fir_spec(Kind::Lowpass, 101, 1000, 0, 48000, dsp::FirWindow::Kaiser, 10.0));
    double worst_h = -1e9;
    double worst_k = -1e9;
    for (double f = 3500; f < 23000; f += 250) {
        worst_h = std::max(worst_h, fir_mag(hamming, f, 48000));
        worst_k = std::max(worst_k, fir_mag(kaiser, f, 48000));
    }
    CHECK(worst_k < worst_h - 10.0); // beta 10 ~ -70+ dB vs hamming ~ -53
    CHECK(worst_k < -65.0);
}

TEST_CASE("dsp: fir high/band types hit their reference frequencies exactly") {
    const double fs = 48000.0;
    const auto hp = dsp::design_fir(fir_spec(Kind::Highpass, 101, 2000, 0, fs));
    CHECK(std::abs(fir_mag(hp, fs / 2.0, fs)) < 1e-6); // unity at Nyquist
    CHECK(fir_mag(hp, 200.0, fs) < -40.0);             // deep stopband at DC side

    // Band edges must sit further from DC/band center than the window's
    // transition width (~3.3 fs/taps ≈ 1.3 kHz here) for the skirts to reach
    // the stopband floor.
    const auto bp = dsp::design_fir(fir_spec(Kind::Bandpass, 121, 2000, 6000, fs));
    CHECK(std::abs(fir_mag(bp, (2000.0 + 6000.0) / 2.0, fs)) < 1e-6); // unity center
    CHECK(fir_mag(bp, 200.0, fs) < -40.0);
    CHECK(fir_mag(bp, 16000.0, fs) < -40.0);

    const auto bs = dsp::design_fir(fir_spec(Kind::Bandstop, 121, 2000, 6000, fs));
    CHECK(std::abs(fir_mag(bs, fs / 2.0 * 1e-6, fs)) < 1e-6); // unity at DC
    CHECK(fir_mag(bs, 4000.0, fs) < -35.0);                   // notched band
    CHECK(std::abs(fir_mag(bs, 20000.0, fs)) < 0.2);          // upper passband
}

TEST_CASE("dsp: fir validation errors") {
    CHECK_THROWS_AS(dsp::design_fir(fir_spec(Kind::Lowpass, 3, 1000, 0, 48000)),
                    dsp::DesignError);
    CHECK_THROWS_AS(dsp::design_fir(fir_spec(Kind::Highpass, 100, 1000, 0, 48000)),
                    dsp::DesignError); // even taps for HP
    CHECK_THROWS_AS(dsp::design_fir(fir_spec(Kind::Lowpass, 101, 30000, 0, 48000)),
                    dsp::DesignError);
}

// ---------------------------------------------------------------------------
// Parks–McClellan equiripple FIR (Remez exchange)
// ---------------------------------------------------------------------------

namespace {

/// Zero-phase amplitude A(ω) of a symmetric Type-I FIR at normalized f.
double fir_amp_at(const std::vector<double>& h, double norm_f) {
    const double w = 2.0 * std::numbers::pi * norm_f;
    const double m = (static_cast<double>(h.size()) - 1.0) / 2.0;
    double a = 0.0;
    for (std::size_t n = 0; n < h.size(); ++n) {
        a += h[n] * std::cos(w * (static_cast<double>(n) - m));
    }
    return a;
}

/// Worst absolute deviation from `target` over the normalized band [lo, hi].
double worst_dev(const std::vector<double>& h, double lo, double hi,
                 double target) {
    double worst = 0.0;
    for (double f = lo; f <= hi; f += 0.0005) {
        worst = std::max(worst, std::abs(fir_amp_at(h, f) - target));
    }
    return worst;
}

} // namespace

TEST_CASE("dsp: remez low-pass is symmetric and equiripple in both bands") {
    const auto r = dsp::remez_fir(31, {{0.0, 0.2, 1.0, 1.0}, {0.25, 0.5, 0.0, 1.0}});
    REQUIRE(r.taps.size() == 31);
    CHECK(r.converged);
    for (std::size_t n = 0; n < r.taps.size(); ++n) {
        CHECK(std::abs(r.taps[n] - r.taps[r.taps.size() - 1 - n]) < 1e-12);
    }
    // Equal weights -> equal passband and stopband ripple, both ~ deviation.
    const double dp = worst_dev(r.taps, 0.0, 0.2, 1.0);
    const double ds = worst_dev(r.taps, 0.25, 0.5, 0.0);
    CHECK(std::abs(dp - ds) < 0.02 * dp);          // equiripple
    CHECK(std::abs(dp - r.deviation) < 0.02 * dp); // deviation matches
}

TEST_CASE("dsp: remez weighting trades passband ripple for stopband depth") {
    const auto r =
        dsp::remez_fir(31, {{0.0, 0.2, 1.0, 1.0}, {0.25, 0.5, 0.0, 10.0}});
    const double dp = worst_dev(r.taps, 0.0, 0.2, 1.0);
    const double ds = worst_dev(r.taps, 0.25, 0.5, 0.0);
    // A weight of 10 on the stopband makes its ripple ~ 10x smaller.
    CHECK(dp / ds > 8.0);
    CHECK(dp / ds < 12.0);
}

TEST_CASE("dsp: remez beats a windowed-sinc filter of equal length") {
    // For the same length and transition band, the optimal filter's peak
    // stopband ripple is smaller than a Hamming window's.
    const auto opt =
        dsp::remez_fir(41, {{0.0, 0.18, 1.0, 1.0}, {0.25, 0.5, 0.0, 1.0}});
    const double opt_stop = worst_dev(opt.taps, 0.25, 0.5, 0.0);
    const auto win = dsp::design_fir(
        fir_spec(Kind::Lowpass, 41, 0.215 * 48000, 0, 48000)); // edge mid-band
    double win_stop = 0.0;
    for (double f = 0.25; f <= 0.5; f += 0.0005) {
        win_stop = std::max(win_stop, std::abs(fir_amp_at(win, f)));
    }
    CHECK(opt_stop < win_stop);
}

TEST_CASE("dsp: remez high-pass and band-pass reach their bands") {
    const auto hp =
        dsp::remez_fir(31, {{0.0, 0.2, 0.0, 1.0}, {0.25, 0.5, 1.0, 1.0}});
    CHECK(std::abs(fir_amp_at(hp.taps, 0.5) - 1.0) < 0.05); // ~unity at Nyquist
    CHECK(worst_dev(hp.taps, 0.0, 0.2, 0.0) < 0.05);        // stopband at DC

    const auto bp = dsp::remez_fir(
        41, {{0.0, 0.12, 0.0, 1.0}, {0.18, 0.32, 1.0, 1.0}, {0.38, 0.5, 0.0, 1.0}});
    CHECK(std::abs(fir_amp_at(bp.taps, 0.25) - 1.0) < 0.08); // ~unity mid-band
    CHECK(worst_dev(bp.taps, 0.0, 0.12, 0.0) < 0.08);
    CHECK(worst_dev(bp.taps, 0.38, 0.5, 0.0) < 0.08);
}

TEST_CASE("dsp: remez validation errors") {
    CHECK_THROWS_AS(dsp::remez_fir(30, {{0.0, 0.2, 1.0, 1.0}, {0.25, 0.5, 0.0, 1.0}}),
                    dsp::DesignError); // even taps
    CHECK_THROWS_AS(dsp::remez_fir(31, {{0.0, 0.3, 1.0, 1.0}, {0.2, 0.5, 0.0, 1.0}}),
                    dsp::DesignError); // overlapping bands
    CHECK_THROWS_AS(dsp::remez_fir(31, {{0.0, 0.6, 1.0, 1.0}}),
                    dsp::DesignError); // single band + out of range
}

// ---------------------------------------------------------------------------
// Time responses
// ---------------------------------------------------------------------------

TEST_CASE("dsp: impulse response of a cascade sums toward the DC gain") {
    const auto sos = design_butter(false, 4, 1000.0, 48000.0);
    const auto h = dsp::impulse_response(sos, 400);
    double sum = 0.0;
    for (const double v : h) sum += v;
    // Step response settles at H(1) = 1 for a unity-DC low-pass.
    CHECK(std::abs(sum - 1.0) < 1e-3);
    // And the first sample matches direct evaluation of the cascade at n=0:
    // product of b0 terms.
    double b0 = 1.0;
    for (const auto& s : sos) b0 *= s.b0;
    CHECK(std::abs(h[0] - b0) < 1e-12);
}

TEST_CASE("dsp: impulse response of a pure delay is the shifted unit sample") {
    const std::vector<Biquad> delay{{0.0, 1.0, 0.0, 0.0, 0.0}};
    const auto h = dsp::impulse_response(delay, 8);
    CHECK(h[0] == 0.0);
    CHECK(h[1] == 1.0);
    for (std::size_t i = 2; i < h.size(); ++i) CHECK(h[i] == 0.0);
}

// ---------------------------------------------------------------------------
// Phase and group delay
// ---------------------------------------------------------------------------

TEST_CASE("dsp: a pure one-sample delay has unit group delay and linear phase") {
    // H(z) = z^-1 as a single biquad: b = [0, 1, 0], a = [0, 0].
    const std::vector<Biquad> delay{{0.0, 1.0, 0.0, 0.0, 0.0}};
    const double fs = 48000.0;
    for (const double f : {100.0, 1000.0, 6000.0, 12000.0, 20000.0}) {
        const auto r = response_at(delay, f, fs);
        CHECK(std::abs(r.mag_db) < 1e-9);
        CHECK(std::abs(r.group_delay - 1.0) < 1e-9);
        // phase = -w = -2 pi f / fs.
        CHECK(std::abs(r.phase_rad - (-2.0 * std::numbers::pi * f / fs)) < 1e-9);
    }
}

TEST_CASE("dsp: minimum-phase low-pass group delay peaks near the cutoff") {
    const auto sos = design_butter(false, 4, 1000.0, 48000.0);
    const double at_fc = response_at(sos, 1000.0, 48000.0).group_delay;
    const double far_below = response_at(sos, 20.0, 48000.0).group_delay;
    const double far_above = response_at(sos, 20000.0, 48000.0).group_delay;
    CHECK(at_fc > far_below);
    CHECK(at_fc > far_above);
    CHECK(far_below > 0.0); // causal filter: positive delay
}

// ---------------------------------------------------------------------------
// Command layer (JSON envelope fragments)
// ---------------------------------------------------------------------------

TEST_CASE("dsp: butter command returns the block envelope") {
    const std::string out =
        dsp_plugin().invoke("butter", {"lowpass", "4", "1000", "48000"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("\"blocks\":["));
    CHECK_THAT(out, ContainsSubstring("\"type\":\"kv\""));
    CHECK_THAT(out, ContainsSubstring("\"type\":\"table\""));
    CHECK_THAT(out, ContainsSubstring("\"type\":\"series\""));
    CHECK_THAT(out, ContainsSubstring("\"logx\":true"));
    CHECK_THAT(out, ContainsSubstring("Butterworth"));
    // Cutoff marked on the charts; both magnitude and phase are present.
    CHECK_THAT(out, ContainsSubstring("\"vlines\":[{\"x\":1000,\"label\":\"fc\"}]"));
    CHECK_THAT(out, ContainsSubstring("Phase response"));
}

TEST_CASE("dsp: cheby commands and band forms return the block envelope") {
    const auto& p = dsp_plugin();
    const std::string c1 =
        p.invoke("cheby1", {"lowpass", "5", "1", "1000", "48000"});
    CHECK_THAT(c1, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(c1, ContainsSubstring("Chebyshev I"));
    CHECK_THAT(c1, ContainsSubstring("Passband ripple"));

    const std::string c2 =
        p.invoke("cheby2", {"highpass", "4", "40", "2000", "48000"});
    CHECK_THAT(c2, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(c2, ContainsSubstring("Chebyshev II"));
    CHECK_THAT(c2, ContainsSubstring("Stopband attenuation"));
    CHECK_THAT(c2, ContainsSubstring("\"Gain at cutoff\",\"-40.00 dB\""));

    const std::string bp =
        p.invoke("butter", {"bandpass", "3", "500", "2000", "48000"});
    CHECK_THAT(bp, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(bp, ContainsSubstring("band-pass"));
    CHECK_THAT(bp, ContainsSubstring("\"Gain at f1\",\"-3.01 dB\""));
    CHECK_THAT(bp, ContainsSubstring("\"Gain at f2\",\"-3.01 dB\""));

    const std::string notch =
        p.invoke("butter", {"notch", "2", "500", "2000", "48000"});
    CHECK_THAT(notch, ContainsSubstring("band-stop"));
}

TEST_CASE("dsp: ellip and fir commands return the block envelope") {
    const auto& p = dsp_plugin();
    const std::string el =
        p.invoke("ellip", {"lowpass", "5", "1", "60", "1000", "48000"});
    CHECK_THAT(el, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(el, ContainsSubstring("Elliptic"));
    CHECK_THAT(el, ContainsSubstring("\"Passband ripple\",\"1 dB\""));
    CHECK_THAT(el, ContainsSubstring("\"Stopband attenuation\",\"60 dB\""));
    CHECK_THAT(el, ContainsSubstring("\"Gain at cutoff\",\"-1.00 dB\""));
    CHECK_THAT(el, ContainsSubstring("Time response"));

    const std::string f =
        p.invoke("fir", {"lowpass", "101", "1000", "48000", "kaiser", "10"});
    CHECK_THAT(f, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(f, ContainsSubstring("FIR windowed-sinc"));
    CHECK_THAT(f, ContainsSubstring("Kaiser (beta 10)"));
    CHECK_THAT(f, ContainsSubstring("\"Group delay\",\"50 samples (linear phase)\""));
    CHECK_THAT(f, ContainsSubstring("Coefficients"));
    CHECK_THAT(f, ContainsSubstring("Time response"));

    CHECK_THAT(p.invoke("fir", {"highpass", "100", "1000", "48000"}),
               ContainsSubstring("odd tap count"));
    CHECK_THAT(p.invoke("fir", {"lowpass", "101", "1000", "48000", "welch"}),
               ContainsSubstring("unknown window"));
    CHECK_THAT(p.invoke("ellip", {"lowpass", "5", "11", "10", "1000", "48000"}),
               ContainsSubstring("attenuation must exceed"));
}

TEST_CASE("dsp: freqz command accepts biquad groups") {
    const std::string out = dsp_plugin().invoke(
        "freqz", {"48000", "0.2", "0.4", "0.2", "-0.5", "0.3"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("\"type\":\"series\""));
    CHECK_THAT(out, ContainsSubstring("1 biquad section"));
    CHECK_THAT(out, ContainsSubstring("Phase response"));
    CHECK_THAT(out, ContainsSubstring("Group delay"));
}

TEST_CASE("dsp: command errors are reported, not thrown") {
    const auto& p = dsp_plugin();
    CHECK_THAT(p.invoke("butter", {}), ContainsSubstring("\"ok\":false"));
    CHECK_THAT(p.invoke("butter", {"allpass", "4", "1000", "48000"}),
               ContainsSubstring("unknown filter type"));
    CHECK_THAT(p.invoke("butter", {"lowpass", "0", "1000", "48000"}),
               ContainsSubstring("order must be"));
    CHECK_THAT(p.invoke("butter", {"lowpass", "13", "1000", "48000"}),
               ContainsSubstring("order must be"));
    CHECK_THAT(p.invoke("butter", {"lowpass", "4", "24000", "48000"}),
               ContainsSubstring("(0, fs/2)"));
    CHECK_THAT(p.invoke("butter", {"lowpass", "4", "abc", "48000"}),
               ContainsSubstring("positive number"));
    CHECK_THAT(p.invoke("butter", {"bandpass", "4", "1000", "48000"}),
               ContainsSubstring("band-pass takes 5 arguments"));
    CHECK_THAT(p.invoke("cheby1", {"lowpass", "4", "0", "1000", "48000"}),
               ContainsSubstring("ripple must be"));
    CHECK_THAT(p.invoke("cheby2", {"lowpass", "4", "5", "1000", "48000"}),
               ContainsSubstring("attenuation must be"));
    CHECK_THAT(p.invoke("freqz", {"48000", "1", "2"}),
               ContainsSubstring("usage: dsp.freqz"));
    CHECK_THAT(p.invoke("nope", {}), ContainsSubstring("no command 'nope'"));
}
