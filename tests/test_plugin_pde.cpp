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
    CHECK(p->commands().size() == 3);

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

TEST_CASE("pde.simulate: linear reaction-diffusion matches the exact rate") {
    // u_t = u_xx + 2u with u0 = sin(pi x) on [0, 1]:
    // u(x, t) = e^{(2 - pi^2) t} sin(pi x) exactly.
    const pd::SimulateResult r = pd::simulate_reaction_diffusion(
        1.0, 1.0, parse_expression("2u"), parse_expression("sin(pi*x)"), 0.2);
    REQUIRE(r.times.size() == 5);
    REQUIRE(r.profiles.back().size() == r.x.size());
    const double rate = 2.0 - std::numbers::pi * std::numbers::pi;
    for (std::size_t k = 1; k < r.times.size(); ++k) {
        const double t = r.times[k];
        for (std::size_t i = 0; i < r.x.size(); i += 10) {
            const double want =
                std::exp(rate * t) * std::sin(std::numbers::pi * r.x[i]);
            CHECK(std::abs(r.profiles[k][i] - want) < 2e-4);
        }
    }
    CHECK_FALSE(r.stopped_early);
    CHECK(r.newton_total > 0);
}

TEST_CASE("pde.simulate: pure diffusion agrees with the separated solution") {
    const pd::SimulateResult r = pd::simulate_reaction_diffusion(
        1.0, 1.0, parse_expression("0"), parse_expression("sin(pi*x)"), 0.1);
    const double t = r.times.back();
    for (std::size_t i = 0; i < r.x.size(); i += 12) {
        const double want =
            std::exp(-std::numbers::pi * std::numbers::pi * t) *
            std::sin(std::numbers::pi * r.x[i]);
        CHECK(std::abs(r.profiles.back()[i] - want) < 1e-4);
    }
}

TEST_CASE("pde.simulate: Fisher-KPP grows toward the carrying capacity") {
    // On L = 10 the domain is supercritical (L > pi sqrt(alpha)): a small
    // bump grows toward u = 1 in the interior instead of dying out.
    const pd::SimulateResult r = pd::simulate_reaction_diffusion(
        10.0, 1.0, parse_expression("u*(1-u)"),
        parse_expression("0.5*sin(pi*x/10)"), 8.0);
    const std::size_t mid = r.x.size() / 2;
    CHECK(r.profiles.back()[mid] > 0.9);
    for (const auto& row : r.profiles) {
        for (const double v : row) {
            CHECK(v > -1e-8);
            CHECK(v < 1.001);
        }
    }
    // Monotone growth at the center across snapshots.
    for (std::size_t k = 1; k < r.profiles.size(); ++k) {
        CHECK(r.profiles[k][mid] >= r.profiles[k - 1][mid] - 1e-9);
    }
}

TEST_CASE("pde.simulate: reaction blow-up stops with a note") {
    // u_t = u_xx + u^2 with large data escapes in finite time.
    const pd::SimulateResult r = pd::simulate_reaction_diffusion(
        1.0, 1.0, parse_expression("u^2"), parse_expression("50*sin(pi*x)"),
        1.0);
    CHECK(r.stopped_early);
    CHECK_THAT(r.note, ContainsSubstring("stopped"));
    CHECK(r.times.back() < 1.0);
}

TEST_CASE("pde.simulate: a stiff runaway stops gracefully at an honest time") {
    // 10 u^2 on [0,1] with a moderate bump runs away in finite time; the
    // implicit step eventually fails to converge. That must be a graceful
    // early stop (a real answer), never an exception, and the recorded time
    // must be the one actually reached — strictly inside the horizon and
    // after t = 0 (Fix: advance() returns the achieved time).
    const pd::SimulateResult r = pd::simulate_reaction_diffusion(
        1.0, 1.0, parse_expression("10*u^2"), parse_expression("3*sin(pi*x)"),
        3.0);
    CHECK(r.stopped_early);
    CHECK_THAT(r.note, ContainsSubstring("stopped"));
    CHECK(r.times.back() > 0.0);
    CHECK(r.times.back() < 3.0);
}

TEST_CASE("pde.simulate: a reaction that overflows is caught, never thrown") {
    // e^(5u) is a thermal-runaway reaction: as u grows, f(u) = e^(5u)
    // overflows to non-finite. The evaluation failure must be caught inside
    // the step and surface as an early stop, not escape and discard the run
    // (Fix: the reaction eval is wrapped in the step's try/catch). We assert
    // by *not* letting an exception escape and by getting a stopped result.
    const pd::SimulateResult r = pd::simulate_reaction_diffusion(
        1.0, 1.0, parse_expression("e^(5u)"), parse_expression("3*sin(pi*x)"),
        3.0);
    CHECK(r.stopped_early);
    CHECK_THAT(r.note, ContainsSubstring("stopped"));
    CHECK(r.times.back() < 3.0);
}

TEST_CASE("pde.simulate: the command rejects infinite arguments") {
    register_builtin_plugins();
    const Plugin* p = find("pde");
    REQUIRE(p != nullptr);
    CHECK_THAT(p->invoke("simulate", {"inf", "1", "u", "sin(pi*x)", "1"}),
               ContainsSubstring("positive"));
    CHECK_THAT(p->invoke("simulate", {"1", "1", "u", "sin(pi*x)", "inf"}),
               ContainsSubstring("positive"));
    // And still solves the ordinary case.
    CHECK_THAT(p->invoke("simulate", {"1", "1", "0", "sin(pi*x)", "0.1"}),
               ContainsSubstring("\"ok\":true"));
}

TEST_CASE("pde.simulate: errors are specific") {
    CHECK_THROWS_WITH(
        pd::simulate_reaction_diffusion(1.0, 1.0, parse_expression("u + y"),
                                        parse_expression("sin(pi*x)"), 1.0),
        ContainsSubstring("found 'y'"));
    CHECK_THROWS_WITH(
        pd::simulate_reaction_diffusion(1.0, 1.0, parse_expression("0"),
                                        parse_expression("u"), 1.0),
        ContainsSubstring("found 'u'"));
    CHECK_THROWS_AS(
        pd::simulate_reaction_diffusion(1.0, -1.0, parse_expression("0"),
                                        parse_expression("x"), 1.0),
        Error);
    CHECK_THROWS_AS(
        pd::simulate_reaction_diffusion(1.0, 1.0, parse_expression("0"),
                                        parse_expression("x"), -1.0),
        Error);
}

TEST_CASE("pde plugin: simulate command envelope") {
    register_builtin_plugins();
    const Plugin* p = find("pde");
    REQUIRE(p != nullptr);
    const std::string sim = p->invoke(
        "simulate", {"10", "1", "u*(1-u)", "0.5*sin(pi*x/10)", "4"});
    CHECK_THAT(sim, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(sim, ContainsSubstring("Concentration profiles"));
    CHECK_THAT(sim, ContainsSubstring("Newton iterations"));
    CHECK_THAT(p->invoke("simulate", {"1", "1", "u*y", "x", "1"}),
               ContainsSubstring("found 'y'"));
    CHECK_THAT(p->invoke("simulate", {"1", "1", "u", "sin(pi*x)"}),
               ContainsSubstring("usage"));
}
