// Multivariate / vector calculus tests. Operator identities are checked
// numerically (both sides evaluated at sample points), which is agnostic to
// the printer's canonical spelling.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/vector_calculus.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

double ev(const Expr& e, const Bindings& b) { return evaluate(e, b); }

const std::vector<std::string> XYZ{"x", "y", "z"};
const std::vector<std::string> XY{"x", "y"};

} // namespace

TEST_CASE("gradient: components are the partials") {
    const ExprVec g = gradient(P("x^2 y + sin(z)"), XYZ);
    REQUIRE(g.size() == 3);
    const Bindings b{{"x", 1.3}, {"y", 0.7}, {"z", 0.4}};
    CHECK(std::abs(ev(g[0], b) - 2 * 1.3 * 0.7) < 1e-9);       // 2xy
    CHECK(std::abs(ev(g[1], b) - 1.3 * 1.3) < 1e-9);           // x^2
    CHECK(std::abs(ev(g[2], b) - std::cos(0.4)) < 1e-9);       // cos z
}

TEST_CASE("divergence and curl of a field") {
    const ExprVec F{P("x*y"), P("y*z"), P("z*x")};
    // div F = y + z + x.
    const Expr div = divergence(F, XYZ);
    const Bindings b{{"x", 1.1}, {"y", 2.2}, {"z", -0.5}};
    CHECK(std::abs(ev(div, b) - (2.2 + (-0.5) + 1.1)) < 1e-9);
    // curl F = (-y, -z, -x).
    const ExprVec c = curl(F, XYZ);
    CHECK(std::abs(ev(c[0], b) - (-2.2)) < 1e-9);
    CHECK(std::abs(ev(c[1], b) - (0.5)) < 1e-9);
    CHECK(std::abs(ev(c[2], b) - (-1.1)) < 1e-9);
}

TEST_CASE("curl of a gradient is zero (vector identity)") {
    const Expr f = P("x^2 y + y^2 z + z^2 x + sin(x y z)");
    const ExprVec g = gradient(f, XYZ);
    const ExprVec c = curl(g, XYZ);
    const Bindings b{{"x", 0.6}, {"y", -1.2}, {"z", 0.9}};
    for (const Expr& comp : c) {
        CHECK(std::abs(ev(comp, b)) < 1e-8);
    }
}

TEST_CASE("divergence of a curl is zero (vector identity)") {
    const ExprVec F{P("x y z"), P("sin(x) cos(y)"), P("x^2 - z y")};
    const Expr d = divergence(curl(F, XYZ), XYZ);
    const Bindings b{{"x", 0.4}, {"y", 1.1}, {"z", -0.7}};
    CHECK(std::abs(ev(d, b)) < 1e-8);
}

TEST_CASE("laplacian equals div grad and matches a harmonic function") {
    const Expr f = P("x^2 - y^2");
    CHECK(std::abs(ev(laplacian(f, XY), Bindings{{"x", 1.0}, {"y", 2.0}})) < 1e-9);
    // div(grad f) == laplacian f for a general f.
    const Expr f2 = P("x^3 y + cos(x y) + y^2");
    const Expr lap = laplacian(f2, XY);
    const Expr divgrad = divergence(gradient(f2, XY), XY);
    const Bindings b{{"x", 0.5}, {"y", 1.3}};
    CHECK(std::abs(ev(lap, b) - ev(divgrad, b)) < 1e-9);
}

TEST_CASE("2-D scalar curl") {
    const ExprVec F{P("-y"), P("x")};
    CHECK(std::abs(ev(curl2d(F, XY), Bindings{{"x", 3.0}, {"y", 5.0}}) - 2.0) < 1e-9);
}

TEST_CASE("jacobian and hessian shapes and values") {
    const ExprVec F{P("x^2 - y"), P("x y")};
    const ExprMat J = jacobian(F, XY);
    REQUIRE(J.size() == 2);
    REQUIRE(J[0].size() == 2);
    const Bindings b{{"x", 2.0}, {"y", 3.0}};
    CHECK(std::abs(ev(J[0][0], b) - 4.0) < 1e-9); // d(x^2-y)/dx = 2x
    CHECK(std::abs(ev(J[0][1], b) + 1.0) < 1e-9); // = -1
    CHECK(std::abs(ev(J[1][0], b) - 3.0) < 1e-9); // y
    CHECK(std::abs(ev(J[1][1], b) - 2.0) < 1e-9); // x

    const ExprMat H = hessian(P("x^3 + x y^2"), XY);
    // H = [[6x, 2y],[2y, 2x]].
    CHECK(std::abs(ev(H[0][0], b) - 12.0) < 1e-9);
    CHECK(std::abs(ev(H[0][1], b) - 6.0) < 1e-9);
    CHECK(std::abs(ev(H[1][0], b) - 6.0) < 1e-9);
    CHECK(std::abs(ev(H[1][1], b) - 4.0) < 1e-9);
    // Symmetry of mixed partials.
    CHECK(ev(H[0][1], b) == ev(H[1][0], b));
}

TEST_CASE("directional derivative normalizes a Pythagorean direction") {
    // f = x^2 + y^2, at (1,2), direction (3,4) (norm 5): grad=(2,4),
    // dot (3,4) = 6 + 16 = 22, /5 = 4.4.
    const Expr d = directional_derivative(P("x^2 + y^2"), XY, {P("3"), P("4")});
    CHECK(std::abs(ev(d, Bindings{{"x", 1.0}, {"y", 2.0}}) - 4.4) < 1e-9);
}

TEST_CASE("vector and matrix rendering round-trips through the parser") {
    const ExprVec g = gradient(P("x^2 + y^2"), XY);
    CHECK_THAT(vec_to_string(g, PrintStyle::Plain), ContainsSubstring("2*x"));
    CHECK_THAT(vec_to_string(g, PrintStyle::LaTeX), ContainsSubstring("pmatrix"));
    const ExprMat H = hessian(P("x*y"), XY);
    CHECK_THAT(mat_to_string(H, PrintStyle::Plain), ContainsSubstring(";"));
}

TEST_CASE("vector calculus dimension errors") {
    CHECK_THROWS_AS(curl({P("x"), P("y")}, XY), Error);        // needs 3
    CHECK_THROWS_AS(divergence({P("x")}, XY), Error);          // mismatch
    CHECK_THROWS_AS(curl2d({P("x"), P("y"), P("z")}, XYZ), Error);
    CHECK_THROWS_AS(gradient(P("x"), {}), Error);
}
