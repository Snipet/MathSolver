// PDE plugin tests: Fourier coefficients against classic closed forms, the
// series solutions against known exact PDE solutions, and the command
// envelope.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <string>

#include "../plugins/pde/pde_core.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver;
using namespace mathsolver::plugins;
namespace pd = mathsolver::plugins::pde;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("pde: sine coefficients of a pure mode") {
    // f = sin(pi x): b_1 = 1, everything else 0 (L = 1).
    const pd::SineSeries s =
        pd::sine_coefficients(parse_expression("sin(pi*x)"), "x", 1.0, 8);
    CHECK(std::abs(s.b[0] - 1.0) < 1e-8);
    for (std::size_t k = 1; k < s.b.size(); ++k) {
        CHECK(std::abs(s.b[k]) < 1e-8);
    }
}

TEST_CASE("pde: parabolic profile gives the classic 8/(n^3 pi^3) series") {
    // f = x(1-x) on [0,1]: b_n = 8/(n^3 pi^3) for odd n, 0 for even.
    const pd::SineSeries s =
        pd::sine_coefficients(parse_expression("x*(1-x)"), "x", 1.0, 6);
    for (int n = 1; n <= 6; ++n) {
        const double want =
            n % 2 == 1 ? 8.0 / (std::pow(n, 3) * std::pow(std::numbers::pi, 3))
                       : 0.0;
        CHECK(std::abs(s.b[static_cast<std::size_t>(n - 1)] - want) < 1e-8);
    }
}

TEST_CASE("pde: heat solution matches the single-mode exact solution") {
    // u(x,0) = sin(pi x), alpha = 1: u = sin(pi x) e^{-pi^2 t} exactly.
    const pd::SineSeries s =
        pd::sine_coefficients(parse_expression("sin(pi*x)"), "x", 1.0, 8);
    for (const double t : {0.0, 0.05, 0.2}) {
        for (const double x : {0.25, 0.5, 0.8}) {
            const double want = std::sin(std::numbers::pi * x) *
                                std::exp(-std::numbers::pi * std::numbers::pi * t);
            CHECK(std::abs(pd::heat_eval(s, 1.0, 1.0, x, t) - want) < 1e-7);
        }
    }
}

TEST_CASE("pde: wave solution matches the single-mode standing wave") {
    // f = sin(pi x), g = 0, c = 2: u = sin(pi x) cos(2 pi t).
    const pd::SineSeries a =
        pd::sine_coefficients(parse_expression("sin(pi*x)"), "x", 1.0, 8);
    const pd::SineSeries g; // zero velocity
    for (const double t : {0.0, 0.1, 0.3}) {
        for (const double x : {0.3, 0.6}) {
            const double want = std::sin(std::numbers::pi * x) *
                                std::cos(2.0 * std::numbers::pi * t);
            CHECK(std::abs(pd::wave_eval(a, g, 1.0, 2.0, x, t) - want) < 1e-7);
        }
    }
}

TEST_CASE("pde: dirichlet boundaries are honored by the series") {
    const pd::SineSeries s =
        pd::sine_coefficients(parse_expression("x*(2-x)"), "x", 2.0, 24);
    for (const double t : {0.0, 0.1}) {
        CHECK(std::abs(pd::heat_eval(s, 2.0, 1.0, 0.0, t)) < 1e-9);
        CHECK(std::abs(pd::heat_eval(s, 2.0, 1.0, 2.0, t)) < 1e-9);
    }
}

TEST_CASE("pde: profile errors are specific") {
    CHECK_THROWS_WITH(
        pd::sine_coefficients(parse_expression("a*x"), "x", 1.0, 4),
        ContainsSubstring("found 'a'"));
    CHECK_THROWS_AS(
        pd::sine_coefficients(parse_expression("x"), "x", -1.0, 4), Error);
}

TEST_CASE("pde plugin: command envelopes") {
    register_builtin_plugins();
    const Plugin* p = find("pde");
    REQUIRE(p != nullptr);
    CHECK(p->commands().size() == 2);

    const std::string heat = p->invoke("heat", {"1", "1", "x*(1-x)"});
    CHECK_THAT(heat, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(heat, ContainsSubstring("Temperature profiles"));
    CHECK_THAT(heat, ContainsSubstring("Sine coefficients"));

    const std::string wave = p->invoke("wave", {"1", "2", "sin(pi*x)"});
    CHECK_THAT(wave, ContainsSubstring("Displacement profiles"));
    CHECK_THAT(wave, ContainsSubstring("Fundamental period\",\"1\""));

    const std::string err = p->invoke("heat", {"-1", "1", "x"});
    CHECK_THAT(err, ContainsSubstring("positive"));
    const std::string err2 = p->invoke("heat", {"1", "1", "x*y"});
    CHECK_THAT(err2, ContainsSubstring("found"));
}
