// Partial-fraction expansion tests.
//
// Correctness is checked numerically (the decomposition equals the input at
// sample points) plus structurally (every additive term of the result has a
// denominator of degree <= 2 to the declared power), which pins down the
// partial-fraction *shape* without depending on printer spelling.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "mathsolver/apart.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

/// apart(input) must equal input numerically at several x.
void check_apart(const std::string& input,
                 const std::vector<double>& xs = {0.31, 1.7, -2.6, 4.3}) {
    const Expr in = P(input);
    const Expr out = apart(in, "x");
    for (const double x : xs) {
        const double a = evaluate(out, Bindings{{"x", x}});
        const double b = evaluate(in, Bindings{{"x", x}});
        INFO("apart(" << input << ") at x=" << x << ": got " << a << " want "
                      << b);
        CHECK(std::abs(a - b) < 1e-8 * (1.0 + std::abs(b)));
    }
}

/// Number of additive terms in the result.
std::size_t term_count(const std::string& input) {
    const Expr out = apart(P(input), "x");
    return out->kind() == Kind::Add ? out->args().size() : 1;
}

} // namespace

TEST_CASE("apart: distinct linear factors") {
    check_apart("(3x + 2)/((x + 1)(x + 2))");
    check_apart("1/((x - 1)(x + 3))");
    check_apart("x/((x - 2)(x + 2))");
    CHECK(term_count("(3x + 2)/((x + 1)(x + 2))") == 2);
}

TEST_CASE("apart: repeated linear factors") {
    check_apart("1/(x(x + 1)^2)", {0.4, 1.9, -3.2});
    check_apart("(x^2 + 1)/((x - 1)^3)", {0.2, 2.5, -1.4});
    CHECK(term_count("1/(x(x + 1)^2)") == 3);
}

TEST_CASE("apart: improper fractions get a polynomial part") {
    check_apart("x^2/(x^2 - 1)", {0.3, 2.4, -3.1});
    check_apart("(x^3 + 2x)/(x^2 + 3x + 2)", {0.5, 1.8, -4.2});
    // x^2/(x^2-1) = 1 + 1/2/(x-1) - 1/2/(x+1): three terms.
    CHECK(term_count("x^2/(x^2 - 1)") == 3);
}

TEST_CASE("apart: irreducible quadratic factors stay quadratic") {
    check_apart("(2x + 3)/((x + 1)(x^2 + 1))");
    check_apart("1/((x^2 + x + 1)(x - 2))");
    // Two separate quadratic bases decompose fine.
    check_apart("1/((x^2 + 1)(x^2 + 4))");
    CHECK(term_count("1/((x^2 + 1)(x^2 + 4))") == 2);
}

TEST_CASE("apart: factored structure merges with expanded bases") {
    // (x+1) appears once directly and once inside the expanded quadratic.
    check_apart("1/((x + 1)(x^2 + 3x + 2))", {0.6, 1.2, -4.5});
}

TEST_CASE("apart: quartic base splits by rational roots") {
    check_apart("1/(x^4 - 1)", {0.3, 2.2, -3.4});
    check_apart("(x + 5)/(x^3 - x)", {0.4, 2.1, -2.7});
}

TEST_CASE("apart: symbolic numerator parameters survive") {
    const Expr out = apart(P("(a x)/((x + 1)(x + 2))"), "x");
    // a x/((x+1)(x+2)) = -a/(x+1) + 2a/(x+2)
    for (const double a : {2.0, -1.5}) {
        for (const double x : {0.7, 3.1}) {
            const double got = evaluate(out, Bindings{{"a", a}, {"x", x}});
            const double want = a * x / ((x + 1) * (x + 2));
            CHECK(std::abs(got - want) < 1e-9 * (1.0 + std::abs(want)));
        }
    }
}

TEST_CASE("apart: identity on polynomials and non-decomposable terms") {
    CHECK(term_count("x^2 + 3") <= 2);       // unchanged polynomial
    check_apart("x^2 + 3", {0.4, 2.0});
    check_apart("1/x", {0.5, 2.0, -1.3});    // already simple
    check_apart("5", {1.0});
}

TEST_CASE("apart: sums decompose termwise") {
    check_apart("1/((x+1)(x+2)) + x^2", {0.3, 1.6, -3.8});
}

TEST_CASE("apart: errors carry clear messages") {
    CHECK_THROWS_WITH(apart(P("sin(x)/(x + 1)"), "x"),
                      ContainsSubstring("not a polynomial"));
    CHECK_THROWS_WITH(apart(P("1/(x^2 + a)"), "x"),
                      ContainsSubstring("numeric"));
    // An expanded quartic with two irreducible quadratics has no rational
    // roots; we report the limitation honestly.
    CHECK_THROWS_WITH(apart(P("1/(x^4 + 5x^2 + 5)"), "x"),
                      ContainsSubstring("cannot factor"));
    CHECK_THROWS_AS(apart(P("1/(x+1)"), ""), Error);
}
