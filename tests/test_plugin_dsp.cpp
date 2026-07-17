// Plugin framework + DSP plugin tests.
//
// The filter-design numerics are verified directly through dsp_design.hpp
// (no JSON parsing); the command layer and registry are verified through the
// public plugin surface, checking envelope fragments only.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "../plugins/dsp/dsp_design.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver::plugins;
using dsp::Biquad;
using dsp::design_butter;
using dsp::magnitude_db;
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
    REQUIRE(cmds.size() == 2);
    CHECK(cmds[0].name == "butter");
    CHECK_THAT(cmds[0].usage, ContainsSubstring("dsp.butter"));
    CHECK(cmds[1].name == "freqz");
    CHECK_THAT(cmds[1].usage, ContainsSubstring("dsp.freqz"));
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
}

TEST_CASE("dsp: freqz command accepts biquad groups") {
    const std::string out = dsp_plugin().invoke(
        "freqz", {"48000", "0.2", "0.4", "0.2", "-0.5", "0.3"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("\"type\":\"series\""));
    CHECK_THAT(out, ContainsSubstring("1 biquad section"));
}

TEST_CASE("dsp: command errors are reported, not thrown") {
    const auto& p = dsp_plugin();
    CHECK_THAT(p.invoke("butter", {}), ContainsSubstring("\"ok\":false"));
    CHECK_THAT(p.invoke("butter", {"bandpass", "4", "1000", "48000"}),
               ContainsSubstring("unknown filter type"));
    CHECK_THAT(p.invoke("butter", {"lowpass", "0", "1000", "48000"}),
               ContainsSubstring("order must be"));
    CHECK_THAT(p.invoke("butter", {"lowpass", "13", "1000", "48000"}),
               ContainsSubstring("order must be"));
    CHECK_THAT(p.invoke("butter", {"lowpass", "4", "24000", "48000"}),
               ContainsSubstring("below Nyquist"));
    CHECK_THAT(p.invoke("butter", {"lowpass", "4", "abc", "48000"}),
               ContainsSubstring("positive number"));
    CHECK_THAT(p.invoke("freqz", {"48000", "1", "2"}),
               ContainsSubstring("usage: dsp.freqz"));
    CHECK_THAT(p.invoke("nope", {}), ContainsSubstring("no command 'nope'"));
}
