// Integral-equation plugin tests: both solvers against equations with known
// closed-form solutions, the honesty of the half-resolution error estimate,
// the failure modes, and the command envelope.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>

#include "../plugins/ie/ie_core.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver;
using namespace mathsolver::plugins;
namespace ei = mathsolver::plugins::ie;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("ie: separable Fredholm kernel matches u = 3x/(3 - lambda)") {
    // u(x) = x + lambda ∫_0^1 x t u(t) dt has the exact solution
    // u = 3x/(3 - lambda); the ansatz u = c x gives c = 3/(3 - lambda).
    const Expr k = parse_expression("x*t");
    const Expr f = parse_expression("x");
    for (const double lambda : {1.0, -2.0, 0.5}) {
        const ei::IeSolution s = ei::solve_fredholm(k, f, lambda, 0.0, 1.0);
        const double c = 3.0 / (3.0 - lambda);
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            CHECK(std::abs(s.u[i] - c * s.x[i]) < 1e-9);
        }
        // Nyström interpolation between nodes is just as exact here.
        CHECK(std::abs(ei::fredholm_eval(s, k, f, lambda, 0.37) - c * 0.37) <
              1e-9);
        CHECK(s.error_estimate < 1e-9);
    }
}

TEST_CASE("ie: smooth non-separable Fredholm kernel converges") {
    // u(x) = e^x + lambda ∫_0^1 sin(x t) u(t) dt has no elementary closed
    // form; Simpson at 31 vs 15 nodes must agree to quadrature accuracy and
    // the reported estimate must bound nothing wild.
    const ei::IeSolution s = ei::solve_fredholm(
        parse_expression("sin(x*t)"), parse_expression("exp(x)"), 0.5, 0.0,
        1.0);
    CHECK(s.error_estimate < 1e-6);
    // Sanity: plugging the solution back into the equation must close.
    const Expr k = parse_expression("sin(x*t)");
    const Expr f = parse_expression("exp(x)");
    for (const double x : {0.1, 0.5, 0.9}) {
        double integral = 0.0;
        for (std::size_t j = 0; j < s.x.size(); ++j) {
            integral += s.w[j] * std::sin(x * s.x[j]) * s.u[j];
        }
        const double residual =
            ei::fredholm_eval(s, k, f, 0.5, x) - (std::exp(x) + 0.5 * integral);
        CHECK(std::abs(residual) < 1e-12);
    }
}

TEST_CASE("ie: characteristic value is reported, not returned as garbage") {
    // For K = x t on [0,1] the only characteristic value is lambda = 3.
    CHECK_THROWS_WITH(
        ei::solve_fredholm(parse_expression("x*t"), parse_expression("x"),
                           3.0, 0.0, 1.0),
        ContainsSubstring("characteristic value"));
}

TEST_CASE("ie: Volterra u' = u marching reproduces e^x") {
    // u(x) = 1 + ∫_0^x u(t) dt  =>  u = e^x.
    const ei::IeSolution s = ei::solve_volterra(
        parse_expression("1"), parse_expression("1"), 1.0, 0.0, 1.0);
    REQUIRE(s.x.size() == 201);
    for (std::size_t i = 0; i < s.x.size(); i += 20) {
        CHECK(std::abs(s.u[i] - std::exp(s.x[i])) < 1e-4);
    }
    // Trapezoid is O(h^2): the half-resolution estimate is small but honest.
    CHECK(s.error_estimate > 0.0);
    CHECK(s.error_estimate < 1e-3);
}

TEST_CASE("ie: Volterra convolution kernel reproduces sin x") {
    // u(x) = x - ∫_0^x (x - t) u(t) dt  =>  u = sin x  (lambda = -1).
    const ei::IeSolution s = ei::solve_volterra(
        parse_expression("x - t"), parse_expression("x"), -1.0, 0.0, 3.0);
    for (std::size_t i = 0; i < s.x.size(); i += 25) {
        CHECK(std::abs(s.u[i] - std::sin(s.x[i])) < 5e-4);
    }
}

TEST_CASE("ie: solver errors are specific") {
    const Expr k = parse_expression("x*t");
    const Expr f = parse_expression("x");
    CHECK_THROWS_WITH(
        ei::solve_fredholm(parse_expression("x*y"), f, 1.0, 0.0, 1.0),
        ContainsSubstring("found 'y'"));
    CHECK_THROWS_WITH(
        ei::solve_fredholm(k, parse_expression("x + t"), 1.0, 0.0, 1.0),
        ContainsSubstring("found 't'"));
    CHECK_THROWS_WITH(ei::solve_fredholm(k, f, 1.0, 2.0, 1.0),
                      ContainsSubstring("a < b"));
    CHECK_THROWS_AS(ei::solve_fredholm(k, f, 1.0, 0.0, 1.0, 8), Error);
    CHECK_THROWS_WITH(ei::solve_volterra(k, f, 1.0, 1.0, 1.0),
                      ContainsSubstring("a < b"));
    CHECK_THROWS_AS(ei::solve_volterra(k, f, 1.0, 0.0, 1.0, 7), Error);
    // 1/t is singular at the quadrature node t = 0 (the evaluator raises
    // its own domain error before the finiteness guard can).
    CHECK_THROWS_WITH(
        ei::solve_fredholm(parse_expression("1/t"), f, 1.0, 0.0, 1.0),
        ContainsSubstring("division by zero"));
}

TEST_CASE("ie plugin: command envelopes") {
    register_builtin_plugins();
    const Plugin* p = find("ie");
    REQUIRE(p != nullptr);
    CHECK(p->commands().size() == 2);

    const std::string fred =
        p->invoke("fredholm", {"x*t", "x", "1", "0", "1"});
    CHECK_THAT(fred, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(fred, ContainsSubstring("Nyström"));
    CHECK_THAT(fred, ContainsSubstring("Solution u(x)"));
    // u(1) = 1.5 exactly for lambda = 1.
    CHECK_THAT(fred, ContainsSubstring("\"1.5\""));

    const std::string vol =
        p->invoke("volterra", {"1", "1", "1", "0", "1"});
    CHECK_THAT(vol, ContainsSubstring("\"ok\":true"));
    CHECK_THAT(vol, ContainsSubstring("trapezoidal marching"));

    // Numeric arguments go through the CAS: pi is a valid bound.
    const std::string pib =
        p->invoke("volterra", {"1", "1", "1", "0", "pi"});
    CHECK_THAT(pib, ContainsSubstring("\"ok\":true"));

    CHECK_THAT(p->invoke("fredholm", {"x*t", "x", "1", "0"}),
               ContainsSubstring("usage"));
    CHECK_THAT(p->invoke("fredholm", {"x*t", "x", "nope+", "0", "1"}),
               ContainsSubstring("\"ok\":false"));
    CHECK_THAT(p->invoke("fredholm", {"x*y", "x", "1", "0", "1"}),
               ContainsSubstring("found 'y'"));
}
