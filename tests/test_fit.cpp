// Least-squares regression (fit) tests.
//
// Polynomial fits are checked for EXACT structure (perfectly fittable data
// returns the generating polynomial, imperfect data returns exact rational
// coefficients); the transcendental models are checked numerically (the fitted
// curve reproduces the data). Error paths assert Status::Error with a message.

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

TEST_CASE("huge inputs overflow the exact path and fall back to numeric") {
    // Σ x^4 with x ~ 1e6 overflows long long; the double path still fits x^2/1e12.
    const FitResult r = fit({"1000000", "2000000", "3000000", "4000000"},
                            {"1", "4", "9", "16"}, FitModel::Poly, 2, "x");
    REQUIRE(r.status == FitResult::Status::Ok);
    CHECK_FALSE(r.exact); // numeric fallback
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
