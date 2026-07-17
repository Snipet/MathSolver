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
    REQUIRE(cmds.size() == 4);
    CHECK(cmds[0].name == "butter");
    CHECK_THAT(cmds[0].usage, ContainsSubstring("dsp.butter"));
    CHECK(cmds[1].name == "cheby1");
    CHECK(cmds[2].name == "cheby2");
    CHECK(cmds[3].name == "freqz");
    CHECK_THAT(cmds[3].usage, ContainsSubstring("dsp.freqz"));
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
    for (const Family fam : {Family::Butterworth, Family::Cheby1, Family::Cheby2}) {
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
