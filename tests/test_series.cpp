// Taylor series expansion tests: known expansions checked by polynomial
// identity (numeric equality at more points than the degree).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/series.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

/// The series result must equal the reference polynomial identically
/// (numeric equality at order+2 sample points pins a polynomial down).
void check_series(const std::string& f, const std::string& center, int order,
                  const std::string& want) {
    const Expr got = series(P(f), "x", P(center), order);
    const Expr ref = P(want);
    for (int i = 0; i <= order + 1; ++i) {
        const double x = -1.3 + 0.51 * i;
        const double a = evaluate(got, Bindings{{"x", x}});
        const double b = evaluate(ref, Bindings{{"x", x}});
        INFO("series(" << f << ", " << center << ", " << order << ") at x=" << x);
        CHECK(std::abs(a - b) < 1e-9 * (1.0 + std::abs(b)));
    }
}

} // namespace

TEST_CASE("series: classic Maclaurin expansions") {
    check_series("e^x", "0", 4, "1 + x + x^2/2 + x^3/6 + x^4/24");
    check_series("sin(x)", "0", 5, "x - x^3/6 + x^5/120");
    check_series("cos(x)", "0", 6, "1 - x^2/2 + x^4/24 - x^6/720");
    check_series("1/(1 - x)", "0", 4, "1 + x + x^2 + x^3 + x^4");
    check_series("ln(1 + x)", "0", 4, "x - x^2/2 + x^3/3 - x^4/4");
}

TEST_CASE("series: expansion about a nonzero center") {
    check_series("ln(x)", "1", 3, "(x-1) - (x-1)^2/2 + (x-1)^3/3");
    check_series("e^x", "1", 2, "e + e (x-1) + e/2 (x-1)^2");
}

TEST_CASE("series: polynomials reproduce exactly at full order") {
    check_series("x^3 - 2x + 1", "2", 3, "x^3 - 2x + 1");
    check_series("(x+1)^2", "5", 2, "x^2 + 2x + 1");
}

TEST_CASE("series: order zero is the value at the center") {
    check_series("cos(x)", "0", 0, "1");
    check_series("x^2 + 3", "2", 0, "7");
}

TEST_CASE("series: hyperbolic and composite functions") {
    check_series("sinh(x)", "0", 5, "x + x^3/6 + x^5/120");
    check_series("e^(2x)", "0", 3, "1 + 2x + 2x^2 + 4x^3/3");
}

TEST_CASE("series: errors") {
    CHECK_THROWS_WITH(series(P("ln(x)"), "x", P("0"), 3),
                      ContainsSubstring("singular"));
    CHECK_THROWS_AS(series(P("e^x"), "x", P("0"), -1), Error);
    CHECK_THROWS_AS(series(P("e^x"), "x", P("0"), 21), Error);
    CHECK_THROWS_AS(series(P("e^x"), "", P("0"), 3), Error);
    CHECK_THROWS_WITH(series(P("e^x"), "x", P("x + 1"), 3),
                      ContainsSubstring("must not depend"));
}

// ---------------------------------------------------------------------------
// Asymptotic expansions at infinity
// ---------------------------------------------------------------------------

TEST_CASE("series at infinity: rational functions") {
    // (x+1)/(x-1) = 1 + 2/x + 2/x^2 + ... — compare numerically at large x.
    const Expr s = series_at_infinity(P("(x+1)/(x-1)"), "x", 4);
    for (const double x : {50.0, 200.0}) {
        const double want = (x + 1) / (x - 1);
        CHECK(std::abs(evaluate(s, Bindings{{"x", x}}) - want) <
              1e-8 * std::abs(want));
    }
    // Improper: the polynomial part is exact.
    const Expr q = series_at_infinity(P("x^3/(x-1)"), "x", 2);
    for (const double x : {60.0, 150.0}) {
        const double want = x * x * x / (x - 1);
        CHECK(std::abs(evaluate(q, Bindings{{"x", x}}) - want) <
              1e-6 * std::abs(want));
    }
}

TEST_CASE("series at infinity: transcendental reductions") {
    // ln(1 + 1/x) = 1/x - 1/(2x^2) + 1/(3x^3) - ...
    const Expr l = series_at_infinity(P("ln(1 + 1/x)"), "x", 3);
    CHECK(std::abs(evaluate(l, Bindings{{"x", 40.0}}) -
                   std::log(1.0 + 1.0 / 40.0)) < 2e-7);
    // e^(1/x) = 1 + 1/x + 1/(2x^2) + ...
    const Expr e = series_at_infinity(P("e^(1/x)"), "x", 4);
    CHECK(std::abs(evaluate(e, Bindings{{"x", 25.0}}) -
                   std::exp(1.0 / 25.0)) < 1e-8);
}

TEST_CASE("series at infinity: polynomials pass through, e^x errors") {
    const Expr p = series_at_infinity(P("x^2 + 3x"), "x", 2);
    CHECK(evaluate(p, Bindings{{"x", 7.0}}) == 70.0);
    CHECK_THROWS_WITH(series_at_infinity(P("e^x"), "x", 3),
                      ContainsSubstring("no expansion"));
}

TEST_CASE("bernoulli numbers are the classic exact rationals") {
    const auto b = bernoulli_numbers(12);
    CHECK(b[0] == Rational(1));
    CHECK(b[1] == Rational(-1, 2));
    CHECK(b[2] == Rational(1, 6));
    CHECK(b[3] == Rational(0));
    CHECK(b[4] == Rational(-1, 30));
    CHECK(b[6] == Rational(1, 42));
    CHECK(b[8] == Rational(-1, 30));
    CHECK(b[10] == Rational(5, 66));
    CHECK(b[12] == Rational(-691, 2730));
    CHECK_THROWS_AS(bernoulli_numbers(21), Error);
}

TEST_CASE("stirling series carries the classic correction terms") {
    const StirlingResult r = stirling_series("x", 3);
    const std::string plain = to_string(r.series, PrintStyle::Plain);
    CHECK_THAT(plain, ContainsSubstring("1/(12*x)"));
    CHECK_THAT(plain, ContainsSubstring("1/(360*x^3)"));
    CHECK_THAT(plain, ContainsSubstring("1/(1260*x^5)"));
    CHECK_THAT(plain, ContainsSubstring("ln(2*pi)/2"));
    // Accuracy: at x = 10 the 3-term truncation is good to ~1e-10.
    const double approx = evaluate(r.series, Bindings{{"x", 10.0}});
    CHECK(std::abs(approx - std::lgamma(10.0)) < 1e-9);
    // ln 9! through ln Gamma(10).
    CHECK(std::abs(approx - std::log(362880.0)) < 1e-9);
    CHECK(r.checks.size() == 3);
    CHECK_THROWS_AS(stirling_series("x", 9), Error);
    CHECK_THROWS_AS(stirling_series("x", -1), Error);
}

TEST_CASE("stirling truncations improve then stall (asymptotic, not convergent)") {
    // At fixed x = 5, more terms help until the optimal index, and the
    // 0-term (pure leading) truncation is clearly worse than 3 terms.
    const double exact = std::lgamma(5.0);
    const double e0 = std::abs(
        evaluate(stirling_series("x", 0).series, Bindings{{"x", 5.0}}) - exact);
    const double e3 = std::abs(
        evaluate(stirling_series("x", 3).series, Bindings{{"x", 5.0}}) - exact);
    CHECK(e0 > 1e-3);
    CHECK(e3 < 1e-7);
}
