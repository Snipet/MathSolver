// Least-squares regression (fit) tests.
//
// Polynomial fits are checked for EXACT structure (perfectly fittable data
// returns the generating polynomial, imperfect data returns exact rational
// coefficients); the transcendental models are checked numerically (the fitted
// curve reproduces the data). Error paths assert Status::Error with a message.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/fit.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return simplify(parse_expression(s)); }

double at(const Expr& e, double x) { return evaluate(e, {{"x", x}}); }

} // namespace

TEST_CASE("polynomial fits are exact for fittable data") {
    // Perfectly collinear -> 2x + 1, exactly.
    const FitResult line = fit({"0", "1", "2"}, {"1", "3", "5"}, FitModel::Poly, 1, "x");
    REQUIRE(line.status == FitResult::Status::Ok);
    CHECK(line.exact);
    CHECK(structurally_equal(line.expr, P("2*x + 1")));
    CHECK(line.r2 == 1.0);

    // y = x^2 exactly.
    const FitResult quad =
        fit({"0", "1", "2", "3"}, {"0", "1", "4", "9"}, FitModel::Poly, 2, "x");
    REQUIRE(quad.status == FitResult::Status::Ok);
    CHECK(quad.exact);
    CHECK(structurally_equal(quad.expr, P("x^2")));

    // y = x^3 exactly.
    const FitResult cubic =
        fit({"0", "1", "2", "3"}, {"0", "1", "8", "27"}, FitModel::Poly, 3, "x");
    REQUIRE(cubic.status == FitResult::Status::Ok);
    CHECK(structurally_equal(cubic.expr, P("x^3")));
}

TEST_CASE("imperfect data yields exact rational coefficients") {
    // Least-squares line through (0,1),(1,2),(2,2),(3,4): slope 9/10, intercept 9/10.
    const FitResult r =
        fit({"0", "1", "2", "3"}, {"1", "2", "2", "4"}, FitModel::Poly, 1, "x");
    REQUIRE(r.status == FitResult::Status::Ok);
    CHECK(r.exact);
    CHECK(structurally_equal(r.expr, P("9*x/10 + 9/10")));
    CHECK(r.r2 > 0.8);
    CHECK(r.r2 < 1.0);
}

TEST_CASE("rational (fractional) inputs stay exact") {
    // (1/2,1),(1,2),(3/2,3) are collinear on y = 2x.
    const FitResult r = fit({"1/2", "1", "3/2"}, {"1", "2", "3"}, FitModel::Poly, 1, "x");
    REQUIRE(r.status == FitResult::Status::Ok);
    CHECK(r.exact);
    CHECK(structurally_equal(r.expr, P("2*x")));
}

TEST_CASE("huge inputs stay on the exact path (arbitrary precision)") {
    // Σ x^4 with x ~ 1e6 used to overflow long long; the exact Rational path now
    // handles it, so the fit stays exact.
    const FitResult r = fit({"1000000", "2000000", "3000000", "4000000"},
                            {"1", "4", "9", "16"}, FitModel::Poly, 2, "x");
    REQUIRE(r.status == FitResult::Status::Ok);
    CHECK(r.exact); // exact, no numeric fallback
    CHECK(std::abs(at(r.expr, 2500000.0) - 6.25) < 1e-6);
}

TEST_CASE("exponential / power / logarithmic fits reproduce the data") {
    // y = e^x.
    const FitResult e = fit({"0", "1", "2"}, {"1", "2.718281828", "7.389056099"},
                            FitModel::Exp, 0, "x");
    REQUIRE(e.status == FitResult::Status::Ok);
    CHECK(std::abs(at(e.expr, 1.5) - std::exp(1.5)) < 1e-2);

    // y = x^2 as a power law.
    const FitResult p = fit({"1", "2", "3", "4"}, {"1", "4", "9", "16"}, FitModel::Power, 0, "x");
    REQUIRE(p.status == FitResult::Status::Ok);
    CHECK(std::abs(at(p.expr, 5.0) - 25.0) < 1e-3);

    // y = 2 + 3 ln x.
    const FitResult l = fit({"1", "2", "3", "4"},
                            {"2", "4.0794", "5.2958", "6.1589"}, FitModel::Log, 0, "x");
    REQUIRE(l.status == FitResult::Status::Ok);
    CHECK(std::abs(at(l.expr, 2.0) - (2.0 + 3.0 * std::log(2.0))) < 1e-2);
}

TEST_CASE("degenerate and out-of-domain inputs error cleanly") {
    // Fewer distinct x than the degree needs.
    const FitResult few = fit({"0", "0"}, {"1", "2"}, FitModel::Poly, 2, "x");
    CHECK(few.status == FitResult::Status::Error);
    CHECK_THAT(few.message, ContainsSubstring("distinct"));

    // Too few points.
    CHECK(fit({"1"}, {"2"}, FitModel::Poly, 1, "x").status == FitResult::Status::Error);

    // Mismatched lengths.
    CHECK(fit({"1", "2"}, {"2"}, FitModel::Poly, 1, "x").status == FitResult::Status::Error);

    // Exponential needs positive y.
    CHECK(fit({"0", "1"}, {"1", "-2"}, FitModel::Exp, 0, "x").status ==
          FitResult::Status::Error);

    // Power/log need positive x.
    CHECK(fit({"0", "1"}, {"1", "2"}, FitModel::Power, 0, "x").status ==
          FitResult::Status::Error);
}

TEST_CASE("model and data parsing helpers") {
    CHECK(parse_fit_model("linear")->first == FitModel::Poly);
    CHECK(parse_fit_model("linear")->second == 1);
    CHECK(parse_fit_model("quadratic")->second == 2);
    CHECK(parse_fit_model("cubic")->second == 3);
    CHECK(parse_fit_model("poly")->second == -1); // caller supplies the degree
    CHECK(parse_fit_model("exp")->first == FitModel::Exp);
    CHECK(parse_fit_model("power")->first == FitModel::Power);
    CHECK(parse_fit_model("log")->first == FitModel::Log);
    CHECK_FALSE(parse_fit_model("nope").has_value());

    const auto [xs, ys] = parse_point_data("0,1; 2, 3 ;4,5");
    REQUIRE(xs.size() == 3);
    REQUIRE(ys.size() == 3);
    CHECK(xs[1] == "2");
    CHECK(ys[2] == "5");
}

TEST_CASE("interp is the exact polynomial through the points") {
    // Through (1,1),(2,4),(3,9) → x^2, exactly.
    const InterpResult sq = interp({"1", "2", "3"}, {"1", "4", "9"}, "x");
    REQUIRE(sq.status == InterpResult::Status::Ok);
    CHECK(sq.exact);
    CHECK(sq.degree == 2);
    CHECK(structurally_equal(sq.expr, P("x^2")));

    // Collinear data collapses to a line (degree < n-1 detected).
    const InterpResult line = interp({"0", "1", "2"}, {"1", "3", "5"}, "x");
    REQUIRE(line.status == InterpResult::Status::Ok);
    CHECK(line.degree == 1);
    CHECK(structurally_equal(line.expr, P("2*x + 1")));

    // Exact rational coefficients: (0,0),(1,1),(2,1) → -x^2/2 + 3x/2.
    const InterpResult frac = interp({"0", "1", "2"}, {"0", "1", "1"}, "x");
    REQUIRE(frac.status == InterpResult::Status::Ok);
    CHECK(frac.exact);
    CHECK(structurally_equal(frac.expr, P("-x^2/2 + 3*x/2")));

    // The polynomial passes through every data point exactly.
    const std::vector<std::string> xs{"-2", "0", "1", "3"};
    const std::vector<std::string> ys{"5", "1", "0", "10"};
    const InterpResult r = interp(xs, ys, "x");
    REQUIRE(r.status == InterpResult::Status::Ok);
    CHECK(r.exact);
    for (std::size_t i = 0; i < xs.size(); ++i)
        CHECK(at(r.expr, std::stod(xs[i])) == Catch::Approx(std::stod(ys[i])).margin(1e-9));

    // A single point is the constant polynomial.
    const InterpResult one = interp({"5"}, {"7"}, "x");
    REQUIRE(one.status == InterpResult::Status::Ok);
    CHECK(one.degree == 0);
    CHECK(structurally_equal(one.expr, P("7")));

    // A named variable other than x.
    const InterpResult t = interp({"0", "1"}, {"1", "4"}, "t");
    REQUIRE(t.status == InterpResult::Status::Ok);
    CHECK(structurally_equal(t.expr, P("3*t + 1")));
}

TEST_CASE("interp numeric fallback for non-rational data still passes through") {
    // sqrt(2) is not a rational number → the double path; the line still hits
    // both points to numeric precision.
    const InterpResult r = interp({"0", "1"}, {"0", "sqrt(2)"}, "x");
    REQUIRE(r.status == InterpResult::Status::Ok);
    CHECK_FALSE(r.exact);
    CHECK(at(r.expr, 0.0) == Catch::Approx(0.0));
    CHECK(at(r.expr, 1.0) == Catch::Approx(std::sqrt(2.0)));
}

TEST_CASE("interp error paths") {
    CHECK(interp({"1", "1"}, {"2", "3"}, "x").status == InterpResult::Status::Error); // dup x
    CHECK(interp({"1", "2"}, {"3"}, "x").status == InterpResult::Status::Error);       // length
    CHECK(interp({}, {}, "x").status == InterpResult::Status::Error);                   // empty
    CHECK(interp({"1", "2"}, {"x", "3"}, "x").status == InterpResult::Status::Error);   // non-const y
}

TEST_CASE("newton and lagrange forms expand to the interpolating polynomial") {
    // Both forms are alternative presentations of the SAME polynomial interp
    // finds: expanding them must reproduce it, and they must pass through the
    // data — but they stay factored, so their text differs from the expansion.
    const std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> cases = {
        {{"1", "2", "3"}, {"1", "4", "9"}},   // → x^2
        {{"0", "1", "2"}, {"0", "1", "1"}},   // → -x^2/2 + 3x/2 (fractions)
        {{"-1", "0", "2"}, {"3", "1", "7"}},  // arbitrary
    };
    for (const auto& [xs, ys] : cases) {
        const InterpResult ip = interp(xs, ys, "x");
        REQUIRE(ip.status == InterpResult::Status::Ok);
        for (InterpForm f : {InterpForm::Newton, InterpForm::Lagrange}) {
            const InterpFormResult r = interp_form(xs, ys, "x", f);
            REQUIRE(r.status == InterpFormResult::Status::Ok);
            CHECK(r.exact);
            CHECK(static_cast<int>(r.notes.size()) == static_cast<int>(xs.size()));
            // Expands to interp's polynomial.
            CHECK(structurally_equal(simplify(expand(make_sub(r.expr, ip.expr))), make_num(0)));
            // Passes through every data point.
            for (std::size_t i = 0; i < xs.size(); ++i) {
                const double xi = std::stod(xs[i]);
                const double yi = std::stod(ys[i]);
                CHECK(std::abs(at(r.expr, xi) - yi) < 1e-9);
            }
            // Stays factored: its canonical form differs from the expansion.
            CHECK(!structurally_equal(simplify(r.expr), simplify(ip.expr)));
        }
    }
}

TEST_CASE("newton form leads with c0 = y0; single point is the constant") {
    const InterpFormResult nw = interp_form({"2", "5"}, {"3", "12"}, "x", InterpForm::Newton);
    REQUIRE(nw.status == InterpFormResult::Status::Ok);
    CHECK(nw.notes.front() == "c0 = 3");   // c0 is the first y

    const InterpFormResult one = interp_form({"4"}, {"7"}, "x", InterpForm::Lagrange);
    REQUIRE(one.status == InterpFormResult::Status::Ok);
    CHECK(structurally_equal(simplify(one.expr), make_num(7)));
}

TEST_CASE("interp form error paths mirror interp") {
    CHECK(interp_form({"1", "1"}, {"2", "3"}, "x", InterpForm::Newton).status ==
          InterpFormResult::Status::Error); // dup x
    CHECK(interp_form({"1", "2"}, {"3"}, "x", InterpForm::Lagrange).status ==
          InterpFormResult::Status::Error); // length
    CHECK(interp_form({}, {}, "x", InterpForm::Newton).status ==
          InterpFormResult::Status::Error); // empty
}

namespace {

std::vector<Expr> nodes(std::initializer_list<const char*> ss) {
    std::vector<Expr> out;
    for (const char* s : ss) out.push_back(parse_expression(s));
    return out;
}

} // namespace

TEST_CASE("vandermonde matrix — numeric nodes") {
    // Nodes 1, 2, 3 → rows (1, x, x^2).
    const VandermondeResult v = vandermonde_matrix(nodes({"1", "2", "3"}));
    REQUIRE(v.status == VandermondeResult::Status::Ok);
    REQUIRE(v.matrix.size() == 3);
    REQUIRE(v.matrix[0].size() == 3);
    // Row 0: node 1 → (1, 1, 1).
    CHECK(structurally_equal(simplify(v.matrix[0][0]), P("1")));
    CHECK(structurally_equal(simplify(v.matrix[0][1]), P("1")));
    CHECK(structurally_equal(simplify(v.matrix[0][2]), P("1")));
    // Row 1: node 2 → (1, 2, 4).
    CHECK(structurally_equal(simplify(v.matrix[1][0]), P("1")));
    CHECK(structurally_equal(simplify(v.matrix[1][1]), P("2")));
    CHECK(structurally_equal(simplify(v.matrix[1][2]), P("4")));
    // Row 2: node 3 → (1, 3, 9).
    CHECK(structurally_equal(simplify(v.matrix[2][1]), P("3")));
    CHECK(structurally_equal(simplify(v.matrix[2][2]), P("9")));
}

TEST_CASE("vandermonde matrix — zero node keeps x^0 = 1") {
    // Node 0 → (1, 0, 0); a 2-node list stays 2×2.
    const VandermondeResult v = vandermonde_matrix(nodes({"0", "5"}));
    REQUIRE(v.status == VandermondeResult::Status::Ok);
    REQUIRE(v.matrix.size() == 2);
    CHECK(structurally_equal(simplify(v.matrix[0][0]), P("1")));
    CHECK(structurally_equal(simplify(v.matrix[0][1]), P("0")));
    CHECK(structurally_equal(simplify(v.matrix[1][0]), P("1")));
    CHECK(structurally_equal(simplify(v.matrix[1][1]), P("5")));
}

TEST_CASE("vandermonde matrix — symbolic nodes stay symbolic") {
    // Nodes a, b → [[1, a], [1, b]].
    const VandermondeResult v = vandermonde_matrix(nodes({"a", "b"}));
    REQUIRE(v.status == VandermondeResult::Status::Ok);
    CHECK(structurally_equal(simplify(v.matrix[0][1]), P("a")));
    CHECK(structurally_equal(simplify(v.matrix[1][1]), P("b")));
}

TEST_CASE("vandermonde matrix — single node and empty") {
    const VandermondeResult one = vandermonde_matrix(nodes({"7"}));
    REQUIRE(one.status == VandermondeResult::Status::Ok);
    REQUIRE(one.matrix.size() == 1);
    CHECK(structurally_equal(simplify(one.matrix[0][0]), P("1")));

    CHECK(vandermonde_matrix({}).status == VandermondeResult::Status::Empty);
}
