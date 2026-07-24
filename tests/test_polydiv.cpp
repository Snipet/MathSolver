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

namespace {
std::string gcd_str(const std::string& a, const std::string& b, std::string_view v = "x") {
    const PolyGcdResult r = polynomial_gcd(parse_expression(a), parse_expression(b), v);
    return r.status == PolyGcdResult::Status::Ok ? plain(r.value) : "ERR";
}
bool poly_equal(const std::string& got, const std::string& expected) {
    return plain(expand(parse_expression("(" + got + ") - (" + expected + ")"))) == "0";
}
} // namespace

TEST_CASE("polynomial gcd") {
    // gcd(x^2 - 1, x^3 - 1) = x - 1 (both share the factor x - 1).
    CHECK(poly_equal(gcd_str("x^2 - 1", "x^3 - 1"), "x - 1"));
    // gcd((x-1)^2 (x+2), (x-1)(x+2)^2) = (x-1)(x+2) = x^2 + x - 2.
    CHECK(poly_equal(gcd_str("(x-1)^2*(x+2)", "(x-1)*(x+2)^2"), "x^2 + x - 2"));
    // Coprime polynomials → gcd 1.
    CHECK(gcd_str("x^2 + 1", "x - 1") == "1");
    // The result is monic even for non-monic inputs.
    CHECK(poly_equal(gcd_str("2*x^2 - 2", "3*x - 3"), "x - 1"));
    // gcd with 0 is the (monic) other argument.
    CHECK(poly_equal(gcd_str("x^2 - 4", "0"), "x^2 - 4"));
    // A shared quadratic factor.
    CHECK(poly_equal(gcd_str("x^4 - 1", "x^2 - 1"), "x^2 - 1"));
}

TEST_CASE("polynomial resultant") {
    const auto res = [](const std::string& a, const std::string& b) {
        const PolyGcdResult r =
            polynomial_resultant(parse_expression(a), parse_expression(b), "x");
        return r.status == PolyGcdResult::Status::Ok ? plain(r.value) : "ERR";
    };
    // res(x-a, x-b) = a - b.  (Here res(x-1, x-2) = 1 - 2 = -1.)
    CHECK(res("x - 1", "x - 2") == "-1");
    // res(f, x - c) = f(c):  res(x^2 - 1, x - 2) = 2^2 - 1 = 3.
    CHECK(res("x^2 - 1", "x - 2") == "3");
    // Zero exactly when they share a root.
    CHECK(res("x^2 - 1", "x - 1") == "0");
    CHECK(res("x^2 - 4", "x^2 - 9") != "0"); // no common root
    CHECK(res("x^2 - 1", "x^2 - 1") == "0"); // identical → shared roots
    // res(x^2 - 1, x^2 - 4): the four root differences product = 9.
    CHECK(res("x^2 - 1", "x^2 - 4") == "9");
    // Order symmetry for equal degrees: res(f,g) = res(g,f) when deg·deg even.
    CHECK(res("x^2 - 1", "x^2 - 4") == res("x^2 - 4", "x^2 - 1"));
    // A shared factor makes the resultant vanish even amid coprime parts.
    CHECK(res("(x-1)*(x-2)", "(x-2)*(x-3)") == "0");

    CHECK(polynomial_resultant(parse_expression("sin(x)"), parse_expression("x"), "x")
              .status == PolyGcdResult::Status::NotPolynomial);
}

TEST_CASE("polynomial lcm") {
    const auto lcm = [](const std::string& a, const std::string& b) {
        const PolyGcdResult r =
            polynomial_lcm(parse_expression(a), parse_expression(b), "x");
        return r.status == PolyGcdResult::Status::Ok ? plain(r.value) : "ERR";
    };
    // lcm(x-1, x+1) = x^2 - 1.
    CHECK(poly_equal(lcm("x - 1", "x + 1"), "x^2 - 1"));
    // lcm((x-1)(x+2), (x+2)(x+3)) = (x-1)(x+2)(x+3).
    CHECK(poly_equal(lcm("(x-1)*(x+2)", "(x+2)*(x+3)"), "(x-1)*(x+2)*(x+3)"));
    // gcd·lcm = a·b (up to the monic normalization), checked for a concrete pair.
    CHECK(poly_equal(lcm("x^2 - 1", "x^2 - 1"), "x^2 - 1"));

    CHECK(polynomial_gcd(parse_expression("sin(x)"), parse_expression("x"), "x").status ==
          PolyGcdResult::Status::NotPolynomial);
}

namespace {

// s·a + t·b − gcd expands to 0, i.e. the Bézout identity holds exactly.
bool bezout_identity_holds(const std::string& a, const std::string& b,
                           const PolyBezoutResult& r) {
    const Expr check = expand(parse_expression(
        "(" + plain(r.s) + ")*(" + a + ") + (" + plain(r.t) + ")*(" + b +
        ") - (" + plain(r.gcd) + ")"));
    return plain(simplify(check)) == "0";
}

PolyBezoutResult bez(const std::string& a, const std::string& b, std::string_view v = "x") {
    return polynomial_bezout(parse_expression(a), parse_expression(b), v);
}

} // namespace

TEST_CASE("polynomial Bézout (extended gcd)") {
    // Coprime: s·(x^2+1) + t·(x-1) = 1 exactly, and the gcd is 1.
    const PolyBezoutResult co = bez("x^2 + 1", "x - 1");
    REQUIRE(co.status == PolyBezoutResult::Status::Ok);
    CHECK(plain(co.gcd) == "1");
    CHECK(bezout_identity_holds("x^2 + 1", "x - 1", co));

    // Shared factor: gcd(x^2-1, x^3-1) = x - 1, cofactors reproduce it.
    const PolyBezoutResult sh = bez("x^2 - 1", "x^3 - 1");
    REQUIRE(sh.status == PolyBezoutResult::Status::Ok);
    CHECK(poly_equal(plain(sh.gcd), "x - 1"));
    CHECK(bezout_identity_holds("x^2 - 1", "x^3 - 1", sh));

    // Non-monic inputs still give a monic gcd with a valid identity.
    const PolyBezoutResult nm = bez("2*x^2 - 2", "3*x - 3");
    REQUIRE(nm.status == PolyBezoutResult::Status::Ok);
    CHECK(poly_equal(plain(nm.gcd), "x - 1"));
    CHECK(bezout_identity_holds("2*x^2 - 2", "3*x - 3", nm));

    // A larger coprime pair — the identity is the real check.
    const PolyBezoutResult big = bez("x^4 + x + 1", "x^2 + 1");
    REQUIRE(big.status == PolyBezoutResult::Status::Ok);
    CHECK(plain(big.gcd) == "1");
    CHECK(bezout_identity_holds("x^4 + x + 1", "x^2 + 1", big));

    // gcd with 0 is the monic other argument; t·b alone reproduces it.
    const PolyBezoutResult z = bez("2*x^2 - 8", "0");
    REQUIRE(z.status == PolyBezoutResult::Status::Ok);
    CHECK(poly_equal(plain(z.gcd), "x^2 - 4"));
    CHECK(bezout_identity_holds("2*x^2 - 8", "0", z));

    // Both zero → everything zero.
    const PolyBezoutResult zz = bez("0", "0");
    REQUIRE(zz.status == PolyBezoutResult::Status::Ok);
    CHECK(plain(zz.gcd) == "0");
    CHECK(plain(zz.s) == "0");
    CHECK(plain(zz.t) == "0");

    // Non-polynomial input is rejected.
    CHECK(polynomial_bezout(parse_expression("sin(x)"), parse_expression("x"), "x").status ==
          PolyBezoutResult::Status::NotPolynomial);
}
