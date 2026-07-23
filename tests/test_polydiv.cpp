// Polynomial-division tests: exact quotient/remainder, verified against the
// division identity  dividend = quotient * divisor + remainder,  with symbolic
// coefficients and the degenerate/error cases.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "mathsolver/parser.hpp"
#include "mathsolver/polydiv.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

std::string plain(const Expr& e) { return to_string(e, PrintStyle::Plain); }

// dividend - (quotient*divisor + remainder) expands to 0.
bool identity_holds(const std::string& dividend, const std::string& divisor,
                    const PolyDivResult& r) {
    const Expr check = expand(parse_expression(
        "(" + dividend + ") - ((" + plain(r.quotient) + ")*(" + divisor +
        ") + (" + plain(r.remainder) + "))"));
    return plain(simplify(check)) == "0";
}

PolyDivResult div(const std::string& a, const std::string& b, std::string_view v = "x") {
    return polynomial_divide(parse_expression(a), parse_expression(b), v);
}

} // namespace

TEST_CASE("exact polynomial division") {
    // x^3 - 1 = (x - 1)(x^2 + x + 1), remainder 0.
    const PolyDivResult a = div("x^3 - 1", "x - 1");
    REQUIRE(a.status == PolyDivResult::Status::Ok);
    CHECK(plain(a.remainder) == "0");
    CHECK(identity_holds("x^3 - 1", "x - 1", a));
    // Quotient is x^2 + x + 1.
    const std::string qcheck =
        plain(expand(parse_expression("(" + plain(a.quotient) + ") - (x^2 + x + 1)")));
    CHECK(qcheck == "0");
}

TEST_CASE("division with a non-zero remainder") {
    // x^3 + 2x + 1 = x·(x^2 + 1) + (x + 1).
    const PolyDivResult a = div("x^3 + 2*x + 1", "x^2 + 1");
    REQUIRE(a.status == PolyDivResult::Status::Ok);
    CHECK(identity_holds("x^3 + 2*x + 1", "x^2 + 1", a));
    CHECK(plain(a.quotient) == "x");
    CHECK(plain(a.remainder) == "x + 1");

    // 2x^2 + 3x + 1 divided by x + 1 → 2x + 1, remainder 0.
    const PolyDivResult b = div("2*x^2 + 3*x + 1", "x + 1");
    CHECK(identity_holds("2*x^2 + 3*x + 1", "x + 1", b));
    CHECK(plain(b.remainder) == "0");
}

TEST_CASE("fractional and constant divisors") {
    // Non-monic divisor introduces fractions in the quotient.
    const PolyDivResult a = div("x^2 + 1", "2*x");
    REQUIRE(a.status == PolyDivResult::Status::Ok);
    CHECK(identity_holds("x^2 + 1", "2*x", a));

    // Dividing by a constant scales the polynomial, remainder 0.
    const PolyDivResult b = div("2*x^2 + 4", "2");
    CHECK(identity_holds("2*x^2 + 4", "2", b));
    CHECK(plain(b.remainder) == "0");

    // Divisor of higher degree than dividend: quotient 0, remainder = dividend.
    const PolyDivResult c = div("x + 1", "x^2");
    CHECK(plain(c.quotient) == "0");
    CHECK(identity_holds("x + 1", "x^2", c));
}

TEST_CASE("symbolic coefficients") {
    // (a x^2 + b x + c) / (x) → quotient a x + b, remainder c.
    const PolyDivResult a = div("a*x^2 + b*x + c", "x");
    REQUIRE(a.status == PolyDivResult::Status::Ok);
    CHECK(identity_holds("a*x^2 + b*x + c", "x", a));
    CHECK(plain(a.remainder) == "c");

    // Division in a different variable treats x as a coefficient.
    const PolyDivResult b = div("y^2 - x^2", "y - x", "y");
    CHECK(identity_holds("y^2 - x^2", "y - x", b));
}

TEST_CASE("polydiv error paths") {
    CHECK(div("x^2", "0").status == PolyDivResult::Status::DivByZero);
    CHECK(div("sin(x)", "x").status == PolyDivResult::Status::NotPolynomial);
}
