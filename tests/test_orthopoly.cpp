// Orthogonal polynomial generators (chebyshev/legendre/hermite/laguerre).
//
// Each family is checked against its known closed forms for the low degrees,
// against exact special values (T_n(1)=1, U_n(1)=n+1, P_n(1)=1, …), and on the
// recurrence's structural properties (variable naming, degree, error paths).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/orthopoly.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;

namespace {

Expr P(const std::string& s) { return simplify(parse_expression(s)); }

Expr gen(OrthoFamily fam, int n, std::string_view var = "x") {
    const OrthoPolyResult r = ortho_poly(fam, n, var);
    REQUIRE(r.status == OrthoPolyResult::Status::Ok);
    return r.expr;
}

// Structural equality up to representation: two polynomials are equal iff their
// difference expands to zero. `expand` distributes the products/fractions that
// plain `simplify` leaves intact (e.g. a `-(2x^2 - 1)` term, or a numerator
// kept over a common denominator), so this is robust to how each side is
// rendered.
bool equals(const Expr& got, const std::string& want) {
    return structurally_equal(expand(got - P(want)), make_num(0));
}

double at(const Expr& e, double x) { return evaluate(e, {{"x", x}}); }

} // namespace

TEST_CASE("Chebyshev first kind T_n matches the known closed forms") {
    CHECK(equals(gen(OrthoFamily::ChebyshevT, 0), "1"));
    CHECK(equals(gen(OrthoFamily::ChebyshevT, 1), "x"));
    CHECK(equals(gen(OrthoFamily::ChebyshevT, 2), "2*x^2 - 1"));
    CHECK(equals(gen(OrthoFamily::ChebyshevT, 3), "4*x^3 - 3*x"));
    CHECK(equals(gen(OrthoFamily::ChebyshevT, 4), "8*x^4 - 8*x^2 + 1"));
    CHECK(equals(gen(OrthoFamily::ChebyshevT, 5), "16*x^5 - 20*x^3 + 5*x"));
}

TEST_CASE("Chebyshev second kind U_n matches the known closed forms") {
    CHECK(equals(gen(OrthoFamily::ChebyshevU, 0), "1"));
    CHECK(equals(gen(OrthoFamily::ChebyshevU, 1), "2*x"));
    CHECK(equals(gen(OrthoFamily::ChebyshevU, 2), "4*x^2 - 1"));
    CHECK(equals(gen(OrthoFamily::ChebyshevU, 3), "8*x^3 - 4*x"));
    CHECK(equals(gen(OrthoFamily::ChebyshevU, 4), "16*x^4 - 12*x^2 + 1"));
}

TEST_CASE("Legendre P_n matches the known closed forms (exact fractions)") {
    CHECK(equals(gen(OrthoFamily::Legendre, 0), "1"));
    CHECK(equals(gen(OrthoFamily::Legendre, 1), "x"));
    CHECK(equals(gen(OrthoFamily::Legendre, 2), "(3*x^2 - 1)/2"));
    CHECK(equals(gen(OrthoFamily::Legendre, 3), "(5*x^3 - 3*x)/2"));
    CHECK(equals(gen(OrthoFamily::Legendre, 4), "(35*x^4 - 30*x^2 + 3)/8"));
}

TEST_CASE("Hermite H_n (physicists') matches the known closed forms") {
    CHECK(equals(gen(OrthoFamily::Hermite, 0), "1"));
    CHECK(equals(gen(OrthoFamily::Hermite, 1), "2*x"));
    CHECK(equals(gen(OrthoFamily::Hermite, 2), "4*x^2 - 2"));
    CHECK(equals(gen(OrthoFamily::Hermite, 3), "8*x^3 - 12*x"));
    CHECK(equals(gen(OrthoFamily::Hermite, 4), "16*x^4 - 48*x^2 + 12"));
}

TEST_CASE("Laguerre L_n matches the known closed forms (exact fractions)") {
    CHECK(equals(gen(OrthoFamily::Laguerre, 0), "1"));
    CHECK(equals(gen(OrthoFamily::Laguerre, 1), "1 - x"));
    CHECK(equals(gen(OrthoFamily::Laguerre, 2), "(x^2 - 4*x + 2)/2"));
    CHECK(equals(gen(OrthoFamily::Laguerre, 3), "(-x^3 + 9*x^2 - 18*x + 6)/6"));
}

TEST_CASE("orthogonal polynomials satisfy their exact special values") {
    // T_n(1) = 1, T_n(-1) = (-1)^n.
    for (int n = 0; n <= 8; ++n) {
        CHECK(at(gen(OrthoFamily::ChebyshevT, n), 1.0) == Catch::Approx(1.0));
        CHECK(at(gen(OrthoFamily::ChebyshevT, n), -1.0) == Catch::Approx(n % 2 == 0 ? 1.0 : -1.0));
    }
    // U_n(1) = n + 1.
    for (int n = 0; n <= 8; ++n)
        CHECK(at(gen(OrthoFamily::ChebyshevU, n), 1.0) == Catch::Approx(n + 1.0));
    // P_n(1) = 1, P_n(-1) = (-1)^n.
    for (int n = 0; n <= 8; ++n) {
        CHECK(at(gen(OrthoFamily::Legendre, n), 1.0) == Catch::Approx(1.0));
        CHECK(at(gen(OrthoFamily::Legendre, n), -1.0) == Catch::Approx(n % 2 == 0 ? 1.0 : -1.0));
    }
    // L_n(0) = 1 for every n.
    for (int n = 0; n <= 8; ++n)
        CHECK(at(gen(OrthoFamily::Laguerre, n), 0.0) == Catch::Approx(1.0));
}

TEST_CASE("degree, variable naming, and error paths") {
    const OrthoPolyResult t = ortho_poly(OrthoFamily::ChebyshevT, 3, "t");
    REQUIRE(t.status == OrthoPolyResult::Status::Ok);
    CHECK(t.degree == 3);
    CHECK(t.family == "Chebyshev T");
    CHECK(t.variable == "t");
    CHECK(structurally_equal(t.expr, P("4*t^3 - 3*t")));

    // Negative degree is rejected.
    const OrthoPolyResult bad = ortho_poly(OrthoFamily::Legendre, -1);
    CHECK(bad.status == OrthoPolyResult::Status::Error);

    // A degree that overflows the exact int64 range fails cleanly (Chebyshev
    // leading coefficient is 2^(n-1)).
    const OrthoPolyResult huge = ortho_poly(OrthoFamily::ChebyshevT, 200);
    CHECK(huge.status == OrthoPolyResult::Status::Error);
}

TEST_CASE("family-name parsing accepts the documented aliases") {
    CHECK(parse_ortho_family("chebyshev") == OrthoFamily::ChebyshevT);
    CHECK(parse_ortho_family("Chebyshev") == OrthoFamily::ChebyshevT);
    CHECK(parse_ortho_family("chebyt") == OrthoFamily::ChebyshevT);
    CHECK(parse_ortho_family("t") == OrthoFamily::ChebyshevT);
    CHECK(parse_ortho_family("chebyshevu") == OrthoFamily::ChebyshevU);
    CHECK(parse_ortho_family("u") == OrthoFamily::ChebyshevU);
    CHECK(parse_ortho_family("legendre") == OrthoFamily::Legendre);
    CHECK(parse_ortho_family("hermite") == OrthoFamily::Hermite);
    CHECK(parse_ortho_family("laguerre") == OrthoFamily::Laguerre);
    CHECK_FALSE(parse_ortho_family("nope").has_value());
}
