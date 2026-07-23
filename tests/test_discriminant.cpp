// Discriminant tests: the closed-form formulas for degree 2–4, symbolic and
// numeric, with the root-nature classification and the error paths.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "mathsolver/discriminant.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

std::string disc_str(const std::string& poly, std::string_view var = "x") {
    const DiscriminantResult r = discriminant(parse_expression(poly), var);
    return r.status == DiscriminantResult::Status::Ok
               ? to_string(r.value, PrintStyle::Plain)
               : "ERR";
}

// Equal as expressions: their difference expands to 0 (order-independent).
bool same(const std::string& got_poly, const std::string& expected) {
    const Expr diff =
        expand(parse_expression("(" + got_poly + ") - (" + expected + ")"));
    return to_string(simplify(diff), PrintStyle::Plain) == "0";
}

} // namespace

TEST_CASE("quadratic discriminant, symbolic and numeric") {
    const DiscriminantResult sym =
        discriminant(parse_expression("a*x^2 + b*x + c"), "x");
    REQUIRE(sym.status == DiscriminantResult::Status::Ok);
    CHECK(sym.degree == 2);
    CHECK(same(to_string(sym.value, PrintStyle::Plain), "b^2 - 4*a*c"));
    CHECK(sym.root_nature.empty()); // symbolic: sign unknown

    // x^2 - 5x + 6: disc = 1 > 0, two distinct real roots.
    const DiscriminantResult n1 =
        discriminant(parse_expression("x^2 - 5*x + 6"), "x");
    CHECK(same(to_string(n1.value, PrintStyle::Plain), "1"));
    CHECK_THAT(n1.root_nature, ContainsSubstring("two distinct real"));

    // x^2 + 1: disc = -4 < 0, complex conjugate roots.
    const DiscriminantResult n2 = discriminant(parse_expression("x^2 + 1"), "x");
    CHECK(same(to_string(n2.value, PrintStyle::Plain), "-4"));
    CHECK_THAT(n2.root_nature, ContainsSubstring("complex"));

    // x^2 - 2x + 1 = (x-1)^2: disc = 0, repeated root.
    const DiscriminantResult n3 =
        discriminant(parse_expression("x^2 - 2*x + 1"), "x");
    CHECK(same(to_string(n3.value, PrintStyle::Plain), "0"));
    CHECK_THAT(n3.root_nature, ContainsSubstring("repeated"));
}

TEST_CASE("cubic discriminant") {
    // x^3 - 3x + 2 = (x-1)^2 (x+2): repeated root, disc = 0.
    CHECK(same(disc_str("x^3 - 3*x + 2"), "0"));
    // x^3 - 6x^2 + 11x - 6 = (x-1)(x-2)(x-3): three distinct real, disc = 4.
    const DiscriminantResult r =
        discriminant(parse_expression("x^3 - 6*x^2 + 11*x - 6"), "x");
    CHECK(same(to_string(r.value, PrintStyle::Plain), "4"));
    CHECK_THAT(r.root_nature, ContainsSubstring("three distinct real"));
    // x^3 + x + 1: one real root, disc = -31 < 0.
    const DiscriminantResult c = discriminant(parse_expression("x^3 + x + 1"), "x");
    CHECK(same(to_string(c.value, PrintStyle::Plain), "-31"));
    CHECK_THAT(c.root_nature, ContainsSubstring("one real"));
    // Depressed-cubic form matches -4p^3 - 27q^2 for x^3 + p x + q.
    CHECK(same(disc_str("x^3 + p*x + q"), "-4*p^3 - 27*q^2"));
}

TEST_CASE("quartic discriminant") {
    // x^4 - 1 = (x-1)(x+1)(x^2+1): two real + a conjugate pair, disc = -256.
    const DiscriminantResult r = discriminant(parse_expression("x^4 - 1"), "x");
    CHECK(same(to_string(r.value, PrintStyle::Plain), "-256"));
    // (x^2-1)^2 = x^4 - 2x^2 + 1 has repeated roots, disc = 0.
    CHECK(same(disc_str("x^4 - 2*x^2 + 1"), "0"));
    // Biquadratic x^4 + 1 (four complex roots), disc = 256 > 0.
    CHECK(same(disc_str("x^4 + 1"), "256"));
}

TEST_CASE("degree edge cases and errors") {
    // Linear: discriminant is 1 by convention.
    CHECK(disc_str("2*x + 3") == "1");
    // Constant: degree too low.
    CHECK(discriminant(parse_expression("7"), "x").status ==
          DiscriminantResult::Status::DegreeTooLow);
    // Degree 5 is unsupported.
    CHECK(discriminant(parse_expression("x^5 + x + 1"), "x").status ==
          DiscriminantResult::Status::DegreeUnsupported);
    // Not a polynomial in x.
    CHECK(discriminant(parse_expression("sin(x)"), "x").status ==
          DiscriminantResult::Status::NotPolynomial);
    // Choosing the wrong variable still works: a*x^2+b*x+c in `a` is linear.
    CHECK(discriminant(parse_expression("a*x^2 + b*x + c"), "a").status ==
          DiscriminantResult::Status::Ok);
}
