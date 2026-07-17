// FEM plugin tests: manufactured solutions with known exact answers, the
// observed convergence order, boundary-condition handling, string
// eigenvalues, and the command envelopes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <string>

#include "../plugins/fem/fem_core.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver;
using namespace mathsolver::plugins;
namespace fe = mathsolver::plugins::fem;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

double max_err_against(const fe::FemSolution& s, double a, double b,
                       double (*exact)(double)) {
    double err = 0.0;
    for (int i = 0; i <= 200; ++i) {
        const double x = a + (b - a) * i / 200.0;
        err = std::max(err, std::abs(fe::fem_eval(s, x) - exact(x)));
    }
    return err;
}

} // namespace

TEST_CASE("fem: -u'' = pi^2 sin(pi x) reproduces sin(pi x)") {
    const fe::BvpResult p1 = fe::solve_bvp(
        P("1"), P("0"), P("pi^2*sin(pi*x)"), 0.0, 1.0, {true, 0.0},
        {true, 0.0}, 1, 16);
    CHECK(max_err_against(p1.solution, 0.0, 1.0,
                          [](double x) { return std::sin(std::numbers::pi * x); }) <
          5e-4); // P1 at 64 elements: O(h^2), ~3e-4 in practice
    // The observed order must sit near the P1 theory value of 2.
    CHECK(p1.observed_order > 1.7);
    CHECK(p1.observed_order < 2.4);
    CHECK(p1.error_estimate > 0.0);
    CHECK(p1.error_estimate < 2e-3);

    const fe::BvpResult p2 = fe::solve_bvp(
        P("1"), P("0"), P("pi^2*sin(pi*x)"), 0.0, 1.0, {true, 0.0},
        {true, 0.0}, 2, 16);
    CHECK(max_err_against(p2.solution, 0.0, 1.0,
                          [](double x) { return std::sin(std::numbers::pi * x); }) <
          3e-6); // P2 at 64 elements: O(h^3), ~1e-6 in practice
    CHECK(p2.observed_order > 2.5);
}

TEST_CASE("fem: variable coefficients with a manufactured solution") {
    // u = sin(x) on [1, 2] with p = x, q = 1:
    // f = -(x cos x)' + sin x = -cos x + x sin x + sin x.
    const fe::BvpResult r = fe::solve_bvp(
        P("x"), P("1"), P("-cos(x) + x*sin(x) + sin(x)"), 1.0, 2.0,
        {true, std::sin(1.0)}, {true, std::sin(2.0)}, 2, 16);
    CHECK(max_err_against(r.solution, 1.0, 2.0,
                          [](double x) { return std::sin(x); }) < 1e-6);
}

TEST_CASE("fem: a natural flux condition and an exactly-linear solution") {
    // -u'' = 0, u(0) = 1, u'(1) = 2 (p = 1): u = 1 + 2x, exact in P1.
    const fe::BvpResult r = fe::solve_bvp(
        P("1"), P("0"), P("0"), 0.0, 1.0, {true, 1.0}, {false, 2.0}, 1, 8);
    CHECK(max_err_against(r.solution, 0.0, 1.0,
                          [](double x) { return 1.0 + 2.0 * x; }) < 1e-11);
    // Exactness means the refinement differences are roundoff: no order.
    CHECK_FALSE(r.warnings.empty());

    // Left flux too: -u'' = 0, p u'(0) = 1 (so u' = 1), u(1) = 3: u = 2 + x.
    const fe::BvpResult l = fe::solve_bvp(
        P("1"), P("0"), P("0"), 0.0, 1.0, {false, 1.0}, {true, 3.0}, 1, 8);
    CHECK(max_err_against(l.solution, 0.0, 1.0,
                          [](double x) { return 2.0 + x; }) < 1e-11);
}

TEST_CASE("fem: reaction term and Helmholtz sign") {
    // -u'' + u = (1 + pi^2) sin(pi x): u = sin(pi x).
    const fe::BvpResult r = fe::solve_bvp(
        P("1"), P("1"), P("(1 + pi^2)*sin(pi*x)"), 0.0, 1.0, {true, 0.0},
        {true, 0.0}, 2, 16);
    CHECK(max_err_against(r.solution, 0.0, 1.0,
                          [](double x) { return std::sin(std::numbers::pi * x); }) <
          1e-6);
    // -u'' - u = f is indefinite but nonresonant on [0, 1]; still solvable.
    // u = sin(pi x): f = (pi^2 - 1) sin(pi x).
    const fe::BvpResult h = fe::solve_bvp(
        P("1"), P("-1"), P("(pi^2 - 1)*sin(pi*x)"), 0.0, 1.0, {true, 0.0},
        {true, 0.0}, 2, 16);
    CHECK(max_err_against(h.solution, 0.0, 1.0,
                          [](double x) { return std::sin(std::numbers::pi * x); }) <
          1e-6);
}

TEST_CASE("fem: solver errors are specific") {
    CHECK_THROWS_WITH(
        fe::solve_bvp(P("y"), P("0"), P("1"), 0.0, 1.0, {true, 0.0},
                      {true, 0.0}, 1, 8),
        ContainsSubstring("found 'y'"));
    CHECK_THROWS_WITH(
        fe::solve_bvp(P("x - 1"), P("0"), P("1"), 0.0, 2.0, {true, 0.0},
                      {true, 0.0}, 1, 8),
        ContainsSubstring("positive"));
    CHECK_THROWS_WITH(
        fe::solve_bvp(P("1"), P("0"), P("1"), 1.0, 0.0, {true, 0.0},
                      {true, 0.0}, 1, 8),
        ContainsSubstring("a < b"));
    // Pure Neumann with q = 0 is singular up to a constant.
    CHECK_THROWS_WITH(
        fe::solve_bvp(P("1"), P("0"), P("1"), 0.0, 1.0, {false, 0.0},
                      {false, 0.0}, 1, 8),
        ContainsSubstring("singular"));
    CHECK_THROWS_AS(
        fe::solve_bvp(P("1"), P("0"), P("1"), 0.0, 1.0, {true, 0.0},
                      {true, 0.0}, 3, 8),
        Error);
    CHECK_THROWS_AS(
        fe::solve_bvp(P("1"), P("0"), P("1"), 0.0, 1.0, {true, 0.0},
                      {true, 0.0}, 1, 2),
        Error);
}

TEST_CASE("fem: string eigenvalues lambda_n = n^2 on [0, pi]") {
    const fe::ModesResult r = fe::solve_modes(
        P("1"), P("0"), P("1"), 0.0, std::numbers::pi, 3, 1, 128);
    REQUIRE(r.lambdas.size() == 3);
    CHECK(std::abs(r.lambdas[0] - 1.0) < 5e-3);
    CHECK(std::abs(r.lambdas[1] - 4.0) < 2e-2);
    CHECK(std::abs(r.lambdas[2] - 9.0) < 1e-1);
    // Mode 1 is the half sine: positive peak at the middle, zero at ends.
    const fe::FemSolution& m1 = r.modes[0];
    CHECK(fe::fem_eval(m1, std::numbers::pi / 2) > 0.0);
    CHECK(std::abs(fe::fem_eval(m1, 0.0)) < 1e-12);
    CHECK(std::abs(fe::fem_eval(m1, std::numbers::pi)) < 1e-12);
    // Mode 2 changes sign across the midpoint.
    const fe::FemSolution& m2 = r.modes[1];
    CHECK(fe::fem_eval(m2, std::numbers::pi / 4) *
              fe::fem_eval(m2, 3 * std::numbers::pi / 4) <
          0.0);

    // A constant shift in q shifts every eigenvalue: q = 2 -> n^2 + 2.
    const fe::ModesResult s = fe::solve_modes(
        P("1"), P("2"), P("1"), 0.0, std::numbers::pi, 2, 2, 64);
    CHECK(std::abs(s.lambdas[0] - 3.0) < 1e-3);
    CHECK(std::abs(s.lambdas[1] - 6.0) < 1e-2);
}

TEST_CASE("fem: P2 eigenvalues converge much faster than P1") {
    const fe::ModesResult p1 = fe::solve_modes(
        P("1"), P("0"), P("1"), 0.0, std::numbers::pi, 1, 1, 32);
    const fe::ModesResult p2 = fe::solve_modes(
        P("1"), P("0"), P("1"), 0.0, std::numbers::pi, 1, 2, 32);
    const double e1 = std::abs(p1.lambdas[0] - 1.0);
    const double e2 = std::abs(p2.lambdas[0] - 1.0);
    CHECK(e2 < e1 / 20.0);
}

TEST_CASE("fem: modes errors are specific") {
    CHECK_THROWS_WITH(
        fe::solve_modes(P("1"), P("0"), P("x - 1"), 0.0, 2.0, 2, 1, 16),
        ContainsSubstring("w(x) must be positive"));
    CHECK_THROWS_AS(
        fe::solve_modes(P("1"), P("0"), P("1"), 0.0, 1.0, 9, 1, 16), Error);
}

TEST_CASE("fem plugin: command envelopes") {
    register_builtin_plugins();
    const Plugin* p = find("fem");
    REQUIRE(p != nullptr);
    CHECK(p->commands().size() == 2);

    const std::string bvp = p->invoke(
        "bvp", {"1", "0", "pi^2*sin(pi*x)", "0", "1", "u=0", "u=0"});
    CHECK_THAT(bvp, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(bvp, ContainsSubstring("Observed convergence order"));
    CHECK_THAT(bvp, ContainsSubstring("Solution u(x)"));

    const std::string flux = p->invoke(
        "bvp", {"1", "0", "0", "0", "1", "u=1", "u'=2"});
    CHECK_THAT(flux, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(flux, ContainsSubstring("p u' = 2"));

    const std::string modes =
        p->invoke("modes", {"1", "0", "1", "0", "pi", "3"});
    CHECK_THAT(modes, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(modes, ContainsSubstring("Eigenvalues"));
    CHECK_THAT(modes, ContainsSubstring("mode 3"));

    CHECK_THAT(p->invoke("bvp", {"1", "0", "1", "0", "1", "q=0", "u=0"}),
               ContainsSubstring("u=<value>"));
    CHECK_THAT(p->invoke("bvp", {"1", "0", "1", "0", "1", "u=0"}),
               ContainsSubstring("usage"));
    CHECK_THAT(p->invoke("modes", {"1", "0", "y", "0", "pi"}),
               ContainsSubstring("found 'y'"));
}
