// Laplace / inverse-Laplace transform tests.
//
// Transforms are checked by NUMERIC equality (evaluate both sides at several
// points), which is robust against simplify's choice of canonical form — the
// structural spelling of e.g. 2/((s+1)^2+4) vs 2/(s^2+2s+5) is unimportant.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/transform.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

/// Assert L{f(t)} equals the expression F(s) at several s (via evaluation).
void check_laplace(const std::string& f, const std::string& F) {
    const Expr got = laplace(P(f), "t");
    const Expr want = P(F);
    for (const double s : {1.5, 2.7, 4.0, 6.3}) {
        const double a = evaluate(got, Bindings{{"s", s}});
        const double b = evaluate(want, Bindings{{"s", s}});
        INFO("L{" << f << "} at s=" << s << ": got " << a << " want " << b);
        CHECK(std::abs(a - b) < 1e-7 * (1.0 + std::abs(b)));
    }
}

/// Assert L^{-1}{F(s)} equals f(t) at several t.
void check_ilaplace(const std::string& F, const std::string& f) {
    const Expr got = inverse_laplace(P(F), "s");
    const Expr want = P(f);
    for (const double t : {0.1, 0.6, 1.3, 2.2}) {
        const double a = evaluate(got, Bindings{{"t", t}});
        const double b = evaluate(want, Bindings{{"t", t}});
        INFO("L^-1{" << F << "} at t=" << t << ": got " << a << " want " << b);
        CHECK(std::abs(a - b) < 1e-7 * (1.0 + std::abs(b)));
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Forward transform
// ---------------------------------------------------------------------------

TEST_CASE("laplace: base table") {
    check_laplace("1", "1/s");
    check_laplace("t", "1/s^2");
    check_laplace("t^2", "2/s^3");
    check_laplace("t^4", "24/s^5");
    check_laplace("e^(2t)", "1/(s - 2)");
    check_laplace("e^(-3t)", "1/(s + 3)");
    check_laplace("sin(3t)", "3/(s^2 + 9)");
    check_laplace("cos(3t)", "s/(s^2 + 9)");
    check_laplace("sinh(2t)", "2/(s^2 - 4)");
    check_laplace("cosh(2t)", "s/(s^2 - 4)");
}

TEST_CASE("laplace: linearity") {
    check_laplace("3t + 2", "3/s^2 + 2/s");
    check_laplace("5 sin(2t) - 4 cos(2t)", "(10 - 4s)/(s^2 + 4)");
    check_laplace("2 + 3t + 4t^2", "2/s + 3/s^2 + 8/s^3");
}

TEST_CASE("laplace: s-shift (e^{at} factor) composes with the table") {
    check_laplace("e^(-t) sin(2t)", "2/((s + 1)^2 + 4)");
    check_laplace("e^(2t) cos(3t)", "(s - 2)/((s - 2)^2 + 9)");
    check_laplace("t e^(-3t)", "1/(s + 3)^2");
    check_laplace("t^2 e^(-3t)", "2/(s + 3)^3");
}

TEST_CASE("laplace: frequency differentiation (t^n factor)") {
    check_laplace("t sin(2t)", "4s/(s^2 + 4)^2");
    check_laplace("t cos(2t)", "(s^2 - 4)/(s^2 + 4)^2");
}

TEST_CASE("laplace: symbolic rates pass through") {
    // L{e^{a t}} = 1/(s - a) with a symbolic.
    const Expr got = laplace(P("e^(a*t)"), "t");
    for (const double s : {3.0, 5.0}) {
        for (const double a : {0.5, -1.2}) {
            const double lhs = evaluate(got, Bindings{{"s", s}, {"a", a}});
            CHECK(std::abs(lhs - 1.0 / (s - a)) < 1e-9);
        }
    }
}

TEST_CASE("laplace: errors on non-transformable input") {
    CHECK_THROWS_AS(laplace(P("ln(t)"), "t"), Error);
    CHECK_THROWS_AS(laplace(P("1/t"), "t"), Error);
    CHECK_THROWS_AS(laplace(P("t"), "s"), Error); // time var can't be s
}

// ---------------------------------------------------------------------------
// Inverse transform
// ---------------------------------------------------------------------------

TEST_CASE("ilaplace: linear-factor table") {
    check_ilaplace("1/s", "1");
    check_ilaplace("1/s^2", "t");
    check_ilaplace("6/s^4", "t^3");
    check_ilaplace("1/(s - 2)", "e^(2t)");
    check_ilaplace("1/(s + 1)^2", "t e^(-t)");
    check_ilaplace("2/(s - 3)^3", "t^2 e^(3t)");
}

TEST_CASE("ilaplace: irreducible quadratics give sin/cos") {
    check_ilaplace("3/(s^2 + 9)", "sin(3t)");
    check_ilaplace("s/(s^2 + 9)", "cos(3t)");
    check_ilaplace("(2s + 3)/(s^2 + 4)", "2 cos(2t) + 3/2 sin(2t)");
    // Completed square -> damped oscillation.
    check_ilaplace("1/(s^2 + 2s + 5)", "e^(-t) sin(2t)/2");
    check_ilaplace("(s + 1)/(s^2 + 2s + 5)", "e^(-t) cos(2t)");
}

TEST_CASE("ilaplace: negative-discriminant quadratics give sinh/cosh") {
    check_ilaplace("1/(s^2 - 2)", "sinh(sqrt(2) t)/sqrt(2)");
    check_ilaplace("s/(s^2 - 4)", "cosh(2t)");
    // Shifted: (s+1)^2 - 1 = s^2 + 2s.
    check_ilaplace("1/(s^2 + 2s)", "e^(-t) sinh(t)");
}

TEST_CASE("ilaplace: zero-discriminant quadratics are repeated real roots") {
    check_ilaplace("(s + 2)/(s^2 + 2s + 1)", "e^(-t) (1 + t)");
    check_ilaplace("1/(s^2 - 4s + 4)", "t e^(2t)");
}

TEST_CASE("ilaplace: squared quadratics give the resonance pair") {
    check_ilaplace("1/(s^2 + 1)^2", "(sin(t) - t cos(t))/2");
    check_ilaplace("s/(s^2 + 4)^2", "t sin(2t)/4");
    // Shifted resonance.
    check_ilaplace("1/((s + 1)^2 + 1)^2", "e^(-t) (sin(t) - t cos(t))/2");
}

TEST_CASE("ilaplace: partial-fraction sums") {
    check_ilaplace("1/(s + 1) + 2/(s + 3)", "e^(-t) + 2 e^(-3t)");
    check_ilaplace("3/s - 2/(s - 4)", "3 - 2 e^(4t)");
}

TEST_CASE("ilaplace: errors") {
    CHECK_THROWS_AS(inverse_laplace(P("s"), "s"), Error);       // improper
    CHECK_THROWS_AS(inverse_laplace(P("s^2 + 1"), "s"), Error); // polynomial
    CHECK_THROWS_AS(inverse_laplace(P("1/s"), "t"), Error);     // svar can't be t
}

// ---------------------------------------------------------------------------
// Round trips
// ---------------------------------------------------------------------------

TEST_CASE("laplace: round trips L^-1(L(f)) = f") {
    for (const std::string& f :
         {"1", "t", "e^(-2t)", "sin(3t)", "cos(2t)", "t e^(-t)",
          "e^(-t) sin(2t)", "3 + 2 e^(-4t)"}) {
        const Expr F = laplace(P(f), "t");
        const Expr back = inverse_laplace(F, "s");
        const Expr want = P(f);
        for (const double t : {0.2, 0.9, 1.7}) {
            const double a = evaluate(back, Bindings{{"t", t}});
            const double b = evaluate(want, Bindings{{"t", t}});
            INFO("round trip " << f << " at t=" << t);
            CHECK(std::abs(a - b) < 1e-6 * (1.0 + std::abs(b)));
        }
    }
}
