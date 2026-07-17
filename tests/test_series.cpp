// Taylor series expansion tests: known expansions checked by polynomial
// identity (numeric equality at more points than the degree).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
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
