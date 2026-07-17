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
