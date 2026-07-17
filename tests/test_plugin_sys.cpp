// sys plugin tests: transfer-function numerics through sys_lti.hpp directly,
// and the command layer through the plugin surface (envelope fragments).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

#include "../plugins/sys/sys_lti.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver::plugins;
using sys::cd;
using sys::RationalTF;
using Catch::Matchers::ContainsSubstring;

namespace {

const Plugin& sys_plugin() {
    register_builtin_plugins();
    const Plugin* p = find("sys");
    REQUIRE(p != nullptr);
    return *p;
}

std::vector<cd> sorted_by_re(std::vector<cd> v) {
    std::sort(v.begin(), v.end(), [](const cd& a, const cd& b) {
        return a.real() != b.real() ? a.real() < b.real() : a.imag() < b.imag();
    });
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// Polynomial extraction (via the CAS)
// ---------------------------------------------------------------------------

TEST_CASE("sys: polynomial coefficients from any polynomial spelling") {
    const auto c1 = sys::poly_from_expr("s^2 + 3s + 2");
    REQUIRE(c1.size() == 3);
    CHECK(std::abs(c1[0] - 2.0) < 1e-12);
    CHECK(std::abs(c1[1] - 3.0) < 1e-12);
    CHECK(std::abs(c1[2] - 1.0) < 1e-12);

    // A factored spelling extracts identically (derivatives do the expansion).
    const auto c2 = sys::poly_from_expr("(s+1)(s+2)");
    REQUIRE(c2.size() == 3);
    for (std::size_t k = 0; k < 3; ++k) {
        CHECK(std::abs(c2[k] - c1[k]) < 1e-12);
    }

    const auto c3 = sys::poly_from_expr("5");
    REQUIRE(c3.size() == 1);
    CHECK(std::abs(c3[0] - 5.0) < 1e-12);
}

TEST_CASE("sys: non-polynomials and foreign symbols are rejected") {
    CHECK_THROWS_AS(sys::poly_from_expr("sin(s)"), sys::SysError);
    CHECK_THROWS_AS(sys::poly_from_expr("1/s"), sys::SysError);
    CHECK_THROWS_AS(sys::poly_from_expr("s + x"), sys::SysError);
    CHECK_THROWS_AS(sys::poly_from_expr("s^13 + 1"), sys::SysError);
}

TEST_CASE("sys: make_tf normalizes monic and enforces properness") {
    const RationalTF tf = sys::make_tf("2s + 2", "2s^2 + 6s + 4");
    REQUIRE(tf.den.size() == 3);
    CHECK(std::abs(tf.den[2] - 1.0) < 1e-12); // monic
    CHECK(std::abs(tf.den[1] - 3.0) < 1e-12);
    CHECK(std::abs(tf.den[0] - 2.0) < 1e-12);
    REQUIRE(tf.num.size() == 2);
    CHECK(std::abs(tf.num[1] - 1.0) < 1e-12);

    CHECK_THROWS_AS(sys::make_tf("s^3", "s^2 + 1"), sys::SysError); // improper
    CHECK_THROWS_AS(sys::make_tf("1", "0"), sys::SysError);
}

// ---------------------------------------------------------------------------
// Roots
// ---------------------------------------------------------------------------

TEST_CASE("sys: polynomial roots, real and complex") {
    const auto r1 = sorted_by_re(sys::poly_roots({2.0, 3.0, 1.0})); // (s+1)(s+2)
    REQUIRE(r1.size() == 2);
    CHECK(std::abs(r1[0] - cd{-2.0, 0.0}) < 1e-9);
    CHECK(std::abs(r1[1] - cd{-1.0, 0.0}) < 1e-9);

    const auto r2 = sorted_by_re(sys::poly_roots({5.0, 2.0, 1.0})); // -1 +- 2i
    REQUIRE(r2.size() == 2);
    CHECK(std::abs(r2[0].real() + 1.0) < 1e-9);
    CHECK(std::abs(std::abs(r2[0].imag()) - 2.0) < 1e-9);
    // Conjugate pair.
    CHECK(std::abs(r2[0].imag() + r2[1].imag()) < 1e-9);

    // Degree 5 with known roots -1..-5.
    // (s+1)(s+2)(s+3)(s+4)(s+5) = s^5 + 15s^4 + 85s^3 + 225s^2 + 274s + 120
    const auto r5 = sorted_by_re(sys::poly_roots({120, 274, 225, 85, 15, 1}));
    REQUIRE(r5.size() == 5);
    for (int k = 0; k < 5; ++k) {
        CHECK(std::abs(r5[static_cast<std::size_t>(k)] - cd{-5.0 + k, 0.0}) < 1e-7);
    }
}

// ---------------------------------------------------------------------------
// ODE -> transfer function
// ---------------------------------------------------------------------------

TEST_CASE("sys: ode_to_tf on the canonical second-order example") {
    // y'' + 3y' + 2y = u' + u  ->  H = (s + 1) / (s^2 + 3s + 2)
    const RationalTF tf = sys::ode_to_tf("y'' + 3y' + 2y = u' + u");
    REQUIRE(tf.den.size() == 3);
    CHECK(std::abs(tf.den[0] - 2.0) < 1e-12);
    CHECK(std::abs(tf.den[1] - 3.0) < 1e-12);
    CHECK(std::abs(tf.den[2] - 1.0) < 1e-12);
    REQUIRE(tf.num.size() == 2);
    CHECK(std::abs(tf.num[0] - 1.0) < 1e-12);
    CHECK(std::abs(tf.num[1] - 1.0) < 1e-12);
}

TEST_CASE("sys: ode_to_tf handles coefficients, signs, and sides") {
    // 2y' - y = 3u  ->  H = 3 / (2s - 1) -> monic: 1.5 / (s - 0.5)
    const RationalTF tf = sys::ode_to_tf("2y' - y = 3u");
    REQUIRE(tf.den.size() == 2);
    CHECK(std::abs(tf.den[0] + 0.5) < 1e-12);
    CHECK(std::abs(tf.den[1] - 1.0) < 1e-12);
    REQUIRE(tf.num.size() == 1);
    CHECK(std::abs(tf.num[0] - 1.5) < 1e-12);

    // Sides may be swapped: "u = y' + y" is y' + y = u, H = 1/(s+1) — the
    // double sign flip cancels in the monic normalization.
    const RationalTF tf2 = sys::ode_to_tf("u = y' + y");
    REQUIRE(tf2.num.size() == 1);
    CHECK(std::abs(tf2.num[0] - 1.0) < 1e-12);
    CHECK(std::abs(tf2.den[0] - 1.0) < 1e-12);
    CHECK(std::abs(sys::dc_gain(tf2) - 1.0) < 1e-12);
}

TEST_CASE("sys: ode_to_tf rejects malformed input with clear messages") {
    CHECK_THROWS_AS(sys::ode_to_tf("y'' + 3y' + 2y"), sys::SysError); // no =
    CHECK_THROWS_AS(sys::ode_to_tf("u' = u"), sys::SysError);         // no y
    CHECK_THROWS_AS(sys::ode_to_tf("y = u''"), sys::SysError);        // improper
    CHECK_THROWS_AS(sys::ode_to_tf("y' + 1 = u"), sys::SysError);     // constant
    CHECK_THROWS_AS(sys::ode_to_tf("y' + z = u"), sys::SysError);     // stray var
}

// ---------------------------------------------------------------------------
// Gain, evaluation, simulation
// ---------------------------------------------------------------------------

TEST_CASE("sys: dc gain and jw evaluation") {
    const RationalTF tf = sys::make_tf("s + 1", "s^2 + 3s + 2");
    CHECK(std::abs(sys::dc_gain(tf) - 0.5) < 1e-12);
    // H(j0) == dc gain; |H| falls off at high frequency.
    CHECK(std::abs(sys::tf_eval(tf, cd{0.0, 0.0}).real() - 0.5) < 1e-12);
    CHECK(std::abs(sys::tf_eval(tf, cd{0.0, 1000.0})) < 0.01);
}

TEST_CASE("sys: step response settles at the dc gain") {
    const RationalTF tf = sys::make_tf("s + 1", "s^2 + 3s + 2");
    const sys::TimeSim sim = sys::simulate(tf, 10.0);
    REQUIRE(sim.t.size() == sim.step.size());
    CHECK(std::abs(sim.step.back() - 0.5) < 5e-3);
    CHECK(std::abs(sim.step.front()) < 1e-9); // starts at rest
    // Impulse response decays to zero for a stable system.
    CHECK(std::abs(sim.impulse.back()) < 5e-3);
}

TEST_CASE("sys: first-order step matches the exact exponential") {
    // H = 1/(s+1): step response 1 - e^-t.
    const RationalTF tf = sys::make_tf("1", "s + 1");
    const sys::TimeSim sim = sys::simulate(tf, 5.0, 200);
    for (std::size_t k = 0; k < sim.t.size(); k += 20) {
        const double expect = 1.0 - std::exp(-sim.t[k]);
        CHECK(std::abs(sim.step[k] - expect) < 1e-6);
    }
    // Impulse response e^-t.
    for (std::size_t k = 0; k < sim.t.size(); k += 20) {
        CHECK(std::abs(sim.impulse[k] - std::exp(-sim.t[k])) < 1e-6);
    }
}

// ---------------------------------------------------------------------------
// Margins, feedback, root locus
// ---------------------------------------------------------------------------

TEST_CASE("sys: textbook margins of 1/(s(s+1)(s+2))") {
    // Classic result: phase crossover at w = sqrt(2), |L| there = 1/6
    // -> GM = 20 log10 6 = 15.56 dB; gain crossover ~0.446 -> PM = 53.4 deg.
    const RationalTF l = sys::make_tf("1", "s^3 + 3s^2 + 2s");
    const sys::Margins m = sys::compute_margins(l, 1e-3, 1e3);
    REQUIRE(!m.gain.empty());
    CHECK(std::abs(m.gain.front().db - 15.563) < 0.05);
    CHECK(std::abs(m.gain.front().freq - std::sqrt(2.0)) < 1e-3);
    REQUIRE(!m.phase.empty());
    CHECK(std::abs(m.phase.front().deg - 53.4) < 0.5);
    CHECK(std::abs(m.phase.front().freq - 0.4457) < 1e-3);
}

TEST_CASE("sys: margins report no crossing when there is none") {
    // 1/(s+1): |H| <= 1 with max phase lag 90 deg — neither crossover exists.
    const RationalTF h = sys::make_tf("1", "s + 1");
    const sys::Margins m = sys::compute_margins(h, 1e-3, 1e3);
    CHECK(m.gain.empty());
    CHECK(m.phase.empty());
}

TEST_CASE("sys: unity feedback closes the loop correctly") {
    // G = 1/(s+1), K = 1: T = 1/(s+2).
    const RationalTF g = sys::make_tf("1", "s + 1");
    const RationalTF t = sys::feedback_unity(g, 1.0);
    REQUIRE(t.den.size() == 2);
    CHECK(std::abs(t.den[0] - 2.0) < 1e-12);
    CHECK(std::abs(t.den[1] - 1.0) < 1e-12);
    REQUIRE(t.num.size() == 1);
    CHECK(std::abs(t.num[0] - 1.0) < 1e-12);
    // K = 3: T = 3/(s+4), DC gain 3/4.
    const RationalTF t3 = sys::feedback_unity(g, 3.0);
    CHECK(std::abs(sys::dc_gain(t3) - 0.75) < 1e-12);
}

TEST_CASE("sys: root locus starts at the open-loop poles and moves") {
    const RationalTF g = sys::make_tf("1", "(s+1)(s+2)");
    const auto branches = sys::root_locus(g, {1e-6, 1.0, 100.0});
    REQUIRE(branches.size() == 3);
    // K -> 0: closed poles at the open poles -1, -2.
    const auto first = sorted_by_re(branches.front());
    CHECK(std::abs(first[0] - cd{-2.0, 0.0}) < 1e-3);
    CHECK(std::abs(first[1] - cd{-1.0, 0.0}) < 1e-3);
    // Large K: this locus breaks away and goes complex at Re = -1.5.
    const auto last = branches.back();
    REQUIRE(last.size() == 2);
    CHECK(std::abs(last[0].real() + 1.5) < 1e-6);
    CHECK(std::abs(last[0].imag()) > 5.0); // far up the asymptote
}

// ---------------------------------------------------------------------------
// Command layer
// ---------------------------------------------------------------------------

TEST_CASE("sys: tf command returns the full analysis envelope") {
    const std::string out = sys_plugin().invoke("tf", {"s+1", "s^2+3s+2"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("\"Stability\",\"stable\""));
    CHECK_THAT(out, ContainsSubstring("\"DC gain\",\"0.5\""));
    CHECK_THAT(out, ContainsSubstring("Poles and zeros"));
    CHECK_THAT(out, ContainsSubstring("Pole-zero map"));
    CHECK_THAT(out, ContainsSubstring("\"points\":true"));
    CHECK_THAT(out, ContainsSubstring("Bode magnitude"));
    CHECK_THAT(out, ContainsSubstring("Bode phase"));
    CHECK_THAT(out, ContainsSubstring("Time response"));
}

TEST_CASE("sys: unstable and marginal systems are labeled") {
    CHECK_THAT(sys_plugin().invoke("tf", {"1", "s^2 - 1"}),
               ContainsSubstring("unstable"));
    CHECK_THAT(sys_plugin().invoke("tf", {"1", "s^2 + 1"}),
               ContainsSubstring("marginally stable"));
}

TEST_CASE("sys: ode command derives and analyzes H(s)") {
    const std::string out =
        sys_plugin().invoke("ode", {"y'' + 3y' + 2y = u' + u"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("s^2 + 3 s + 2"));
    CHECK_THAT(out, ContainsSubstring("\"Derived from\""));
    CHECK_THAT(out, ContainsSubstring("\"Stability\",\"stable\""));
}

TEST_CASE("sys: tf command reports margins with crossover markers") {
    const std::string out = sys_plugin().invoke("tf", {"1", "s^3 + 3s^2 + 2s"});
    CHECK_THAT(out, ContainsSubstring("\"Gain margin\",\"15.56 dB"));
    CHECK_THAT(out, ContainsSubstring("\"Phase margin\",\"53.4"));
    CHECK_THAT(out, ContainsSubstring("\\u03c9pc"));
    CHECK_THAT(out, ContainsSubstring("\\u03c9gc"));
    // No crossings -> infinity rows, no markers.
    const std::string flat = sys_plugin().invoke("tf", {"1", "s + 1"});
    CHECK_THAT(flat, ContainsSubstring("∞ (no -180° crossing)"));
    CHECK_THAT(flat, ContainsSubstring("∞ (no 0 dB crossing)"));
}

TEST_CASE("sys: feedback command analyzes the closed loop") {
    const std::string out = sys_plugin().invoke("feedback", {"1", "s + 1", "3"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("Closed loop"));
    CHECK_THAT(out, ContainsSubstring("s + 4"));
    CHECK_THAT(out, ContainsSubstring("K = 3"));
    CHECK_THAT(out, ContainsSubstring("\"DC gain\",\"0.75\""));
    CHECK_THAT(sys_plugin().invoke("feedback", {"1", "s + 1", "abc"}),
               ContainsSubstring("must be a number"));
}

TEST_CASE("sys: rlocus command renders the sweep") {
    const std::string out =
        sys_plugin().invoke("rlocus", {"1", "(s+1)(s+2)", "50"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("Root locus"));
    CHECK_THAT(out, ContainsSubstring("open-loop poles"));
    CHECK_THAT(out, ContainsSubstring("\"points\":true"));
    CHECK_THAT(out, ContainsSubstring("stable for the whole sweep"));
    // An unstable-at-high-K loop reports the critical gain: the classic
    // 1/(s(s+1)(s+2)) goes unstable at K = 6.
    const std::string crit =
        sys_plugin().invoke("rlocus", {"1", "s^3 + 3s^2 + 2s", "100"});
    CHECK_THAT(crit, ContainsSubstring("unstable near K"));
}

// ---------------------------------------------------------------------------
// Discrete-time systems (z-domain)
// ---------------------------------------------------------------------------

TEST_CASE("sys: make_tfz reads polynomials in z and stays proper") {
    const RationalTF tf = sys::make_tfz("z", "z^2 - 0.5z + 0.06");
    REQUIRE(tf.den.size() == 3);
    CHECK(std::abs(tf.den[2] - 1.0) < 1e-12); // monic in highest power
    CHECK(std::abs(tf.den[1] + 0.5) < 1e-12);
    CHECK(std::abs(tf.den[0] - 0.06) < 1e-12);
    CHECK_THROWS_AS(sys::make_tfz("z^3", "z^2 + 1"), sys::SysError); // improper
    CHECK_THROWS_AS(sys::make_tfz("s + 1", "z + 1"), sys::SysError); // wrong var
}

TEST_CASE("sys: discrete first-order impulse/step match the closed forms") {
    // H(z) = z/(z - a): impulse h[n] = a^n, step s[n] = (1 - a^{n+1})/(1 - a).
    const double a = 0.8;
    const RationalTF tf = sys::make_tfz("z", "z - 0.8");
    const sys::DiscreteSim sim = sys::simulate_discrete(tf, 20);
    for (int n = 0; n < 20; ++n) {
        CHECK(std::abs(sim.impulse[static_cast<std::size_t>(n)] -
                       std::pow(a, n)) < 1e-9);
        const double step = (1.0 - std::pow(a, n + 1)) / (1.0 - a);
        CHECK(std::abs(sim.step[static_cast<std::size_t>(n)] - step) < 1e-9);
    }
}

TEST_CASE("sys: discrete moving-average impulse is the tap set") {
    // H(z) = (z^2 + z + 1)/(3 z^2): a 3-tap averager, h = [1/3, 1/3, 1/3].
    const RationalTF tf = sys::make_tfz("z^2 + z + 1", "3z^2");
    const sys::DiscreteSim sim = sys::simulate_discrete(tf, 6);
    CHECK(std::abs(sim.impulse[0] - 1.0 / 3.0) < 1e-12);
    CHECK(std::abs(sim.impulse[1] - 1.0 / 3.0) < 1e-12);
    CHECK(std::abs(sim.impulse[2] - 1.0 / 3.0) < 1e-12);
    CHECK(std::abs(sim.impulse[3]) < 1e-12); // FIR: dies after 3 taps
    // DC gain (z = 1) is 1.
    CHECK(std::abs(sys::tfz_eval(tf, 0.0, 8000.0).real() - 1.0) < 1e-12);
}

TEST_CASE("sys: discrete stability is |pole| < 1") {
    // Poles inside the circle.
    const auto stable = sys::poly_roots(sys::make_tfz("z", "z^2 - 0.5z + 0.06").den);
    for (const cd& p : stable) CHECK(std::abs(p) < 1.0);
    // A pole at z = 1.2 is unstable.
    const auto unstable = sys::poly_roots(sys::make_tfz("1", "z - 1.2").den);
    CHECK(std::abs(unstable.front()) > 1.0);
}

TEST_CASE("sys: tfz command renders the z-plane analysis") {
    const std::string out =
        sys_plugin().invoke("tfz", {"z", "z^2 - 0.5z + 0.06", "8000"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("Pole-zero map (z-plane)"));
    CHECK_THAT(out, ContainsSubstring("unit circle"));
    CHECK_THAT(out, ContainsSubstring("\"equal\":true"));
    CHECK_THAT(out, ContainsSubstring("inside |z| = 1"));
    CHECK_THAT(out, ContainsSubstring("Max |pole|"));
    CHECK_THAT(sys_plugin().invoke("tfz", {"1", "z - 1.2", "8000"}),
               ContainsSubstring("unstable (pole outside"));
    CHECK_THAT(sys_plugin().invoke("tfz", {"z", "z + 1"}),
               ContainsSubstring("usage: sys.tfz"));
}

TEST_CASE("sys: c2d discretizes through the dsp zpk machinery") {
    const std::string out = sys_plugin().invoke("c2d", {"1", "s + 1", "100"});
    CHECK_THAT(out, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(out, ContainsSubstring("Digital biquad cascade"));
    CHECK_THAT(out, ContainsSubstring("bilinear transform"));
    CHECK_THAT(out, ContainsSubstring("digital H(z)"));
    CHECK_THAT(out, ContainsSubstring("analog H(s)"));
}

TEST_CASE("sys: command errors are reported, not thrown") {
    const auto& p = sys_plugin();
    CHECK_THAT(p.invoke("tf", {"s+1"}), ContainsSubstring("usage: sys.tf"));
    CHECK_THAT(p.invoke("tf", {"sin(s)", "s+1"}),
               ContainsSubstring("\"ok\":false"));
    CHECK_THAT(p.invoke("ode", {"y = u''"}), ContainsSubstring("improper"));
    CHECK_THAT(p.invoke("c2d", {"1", "s+1", "-5"}),
               ContainsSubstring("positive number"));
    CHECK_THAT(p.invoke("nope", {}), ContainsSubstring("no command 'nope'"));
}
