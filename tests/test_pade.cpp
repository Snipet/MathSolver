// Padé approximant tests. The defining contract is verified directly: the
// Maclaurin series of the approximant P(x)/Q(x) must agree with the series of
// f through order m + n. Canonical closed forms and the normalization Q(0) = 1
// are checked too, plus the error paths.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/pade.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/series.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

/// Two polynomials are equal iff they agree at more points than their degree.
void same_poly(const Expr& a, const Expr& b, int degree) {
    for (int i = 0; i <= degree + 2; ++i) {
        const double x = -1.1 + 0.37 * i;
        const double va = evaluate(a, Bindings{{"x", x}});
        const double vb = evaluate(b, Bindings{{"x", x}});
        INFO("compare at x=" << x << "  a=" << to_string(a, PrintStyle::Plain)
                             << "  b=" << to_string(b, PrintStyle::Plain));
        CHECK(std::abs(va - vb) < 1e-9 * (1.0 + std::abs(vb)));
    }
}

/// The Padé contract: series of P/Q matches series of f through order m + n.
void check_pade_contract(const std::string& f, int m, int n) {
    const PadeResult r = pade(P(f), "x", m, n);
    const int order = m + n;
    const Expr sf = series(P(f), "x", make_num(0), order);
    const Expr sr = series(r.approximant, "x", make_num(0), order);
    same_poly(sf, sr, order);
    // Q(0) = 1 by normalization.
    CHECK(std::abs(evaluate(r.denominator, Bindings{{"x", 0.0}}) - 1.0) < 1e-12);
}

} // namespace

TEST_CASE("pade: matches the Maclaurin series through order m+n") {
    check_pade_contract("exp(x)", 1, 1);
    check_pade_contract("exp(x)", 2, 2);
    check_pade_contract("exp(x)", 3, 2);
    check_pade_contract("sin(x)", 3, 2);
    check_pade_contract("cos(x)", 2, 2);
    check_pade_contract("cos(x)", 4, 2);
    check_pade_contract("ln(1 + x)", 2, 2);
    check_pade_contract("1/(1 + x)", 2, 2);
    check_pade_contract("atan(x)", 3, 2);
}

TEST_CASE("pade: canonical closed forms") {
    // exp(x) [1/1] = (2 + x)/(2 - x).
    {
        const PadeResult r = pade(P("exp(x)"), "x", 1, 1);
        same_poly(r.approximant, P("(2 + x)/(2 - x)"), 3);
    }
    // exp(x) [2/2] = (12 + 6x + x^2)/(12 - 6x + x^2).
    {
        const PadeResult r = pade(P("exp(x)"), "x", 2, 2);
        same_poly(r.approximant, P("(12 + 6*x + x^2)/(12 - 6*x + x^2)"), 4);
    }
    // sin(x) [3/2] = (x - 7x^3/60)/(1 + x^2/20).
    {
        const PadeResult r = pade(P("sin(x)"), "x", 3, 2);
        same_poly(r.approximant, P("(x - 7*x^3/60)/(1 + x^2/20)"), 5);
    }
}

TEST_CASE("pade: recovers rational functions exactly") {
    // A rational function whose [m/n] bounds cover it is reproduced exactly
    // (away from its pole). Here [2/2] is a defective (non-normal) entry — the
    // exact function is [1/1] — so the minimal-denominator branch is exercised.
    const PadeResult r = pade(P("1/(1 - x)"), "x", 2, 2);
    for (double x : {-2.0, -0.5, 0.3, 0.75, 2.5}) {
        const double got = evaluate(r.approximant, Bindings{{"x", x}});
        CHECK(std::abs(got - 1.0 / (1.0 - x)) < 1e-9);
    }
    // A polynomial recovered at an over-specified [4/4] (denominator collapses).
    const PadeResult p = pade(P("x^2 + 1"), "x", 4, 4);
    same_poly(p.approximant, P("x^2 + 1"), 4);
}

TEST_CASE("pade: n = 0 is the Taylor polynomial") {
    const PadeResult r = pade(P("exp(x)"), "x", 4, 0);
    same_poly(r.approximant, P("1 + x + x^2/2 + x^3/6 + x^4/24"), 4);
    CHECK(to_string(r.denominator, PrintStyle::Plain) == "1");
}

TEST_CASE("pade: symbolic variable is honored") {
    // Padé in t rather than x.
    const PadeResult r = pade(P("exp(t)"), "t", 1, 1);
    for (double t : {-0.6, 0.2, 0.9}) {
        const double got = evaluate(r.approximant, Bindings{{"t", t}});
        CHECK(std::abs(got - (2.0 + t) / (2.0 - t)) < 1e-9);
    }
}

TEST_CASE("pade: error paths") {
    // Negative orders.
    CHECK_THROWS_AS(pade(P("exp(x)"), "x", -1, 2), Error);
    // Order cap m + n <= 20.
    CHECK_THROWS_AS(pade(P("exp(x)"), "x", 15, 10), Error);
    // A non-analytic input has no Maclaurin polynomial series.
    CHECK_THROWS_AS(pade(P("1/x"), "x", 2, 2), Error);
    // A genuinely non-existent entry: [1/1] of 1 + x^2 forces 0 = -1.
    CHECK_THROWS_AS(pade(P("1 + x^2"), "x", 1, 1), Error);
}
