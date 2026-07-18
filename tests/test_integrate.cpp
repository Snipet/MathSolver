// Tests for DESIGN.md §8b: linearity, the full table with linear inner
// arguments, polynomial/expansion, derivative-divides u-substitution,
// pattern-bounded integration by parts, rational functions via partial
// fractions, trig powers, Unsolved honesty, and definite integration
// (FTC + grid check + quadrature cross-check + adaptive Simpson). Every
// Integrated result is re-verified in-test by differentiating the returned
// antiderivative and comparing numerically against the integrand.

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mathsolver/derivative.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/integrate.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Expr parse(std::string_view s) {
    return parse_expression(s);
}

/// d/dx of the returned antiderivative must match the integrand numerically
/// at every sample point (points chosen away from domain edges).
void verify_at(const Expr& integrand, const IntegrateResult& res,
               std::initializer_list<double> points, const Bindings& params = {}) {
    REQUIRE(res.status == IntegrateResult::Status::Integrated);
    const Expr d = differentiate(res.antiderivative, "x");
    for (const double v : points) {
        Bindings b = params;
        b["x"] = v;
        const double lhs = evaluate(d, b);
        const double rhs = evaluate(integrand, b);
        INFO("F  = " << debug_string(res.antiderivative));
        INFO("F' = " << debug_string(d));
        INFO("x = " << v << "  F'(x) = " << lhs << "  f(x) = " << rhs);
        CHECK_THAT(lhs, WithinRel(rhs, 1e-9) || WithinAbs(rhs, 1e-12));
    }
}

IntegrateResult check_integral(std::string_view src,
                               std::initializer_list<double> points = {0.4, 1.2,
                                                                       2.3}) {
    const Expr f = parse(src);
    const IntegrateResult res = integrate(f, "x");
    INFO("integrand: " << src);
    for (const auto& w : res.warnings) INFO("warning: " << w);
    verify_at(f, res, points);
    return res;
}

bool method_contains(const IntegrateResult& res, std::string_view label) {
    return res.method.find(label) != std::string::npos;
}

bool has_warning(const std::vector<std::string>& warnings, std::string_view w) {
    for (const auto& s : warnings)
        if (s == w) return true;
    return false;
}

void expect_antiderivative(std::string_view src, std::string_view expected) {
    const Expr f = parse(src);
    const IntegrateResult res = integrate(f, "x");
    REQUIRE(res.status == IntegrateResult::Status::Integrated);
    const Expr want = simplify(parse(expected));
    INFO("integrand: " << src);
    INFO("got:      " << debug_string(res.antiderivative));
    INFO("expected: " << debug_string(want));
    CHECK(structurally_equal(res.antiderivative, want));
}

} // namespace

// ---------------------------------------------------------------------------
// Stage 2: every table entry (§8b) integrates and differentiates back.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: table battery — every entry differentiates back") {
    // u^r, r != -1 (power rule), incl. linear u and fractional/negative r.
    check_integral("x^3");
    check_integral("(5*x+2)^7", {-0.3, 0.2, 0.5});
    check_integral("sqrt(x)", {0.4, 1.2, 2.3});
    check_integral("x^(-2)", {0.4, 1.2, -1.7});
    // u^(-1) -> ln(abs(u))/a, on both sides of the singularity.
    check_integral("1/x", {0.4, 1.2, -1.7});
    check_integral("1/(2*x+3)", {0.4, 1.2, 2.3});
    // Trig.
    check_integral("sin(x)");
    check_integral("sin(3*x+1)");
    check_integral("cos(2*x)");
    check_integral("tan(x)", {0.4, 1.2, -0.6});
    // Exponentials.
    check_integral("exp(2*x)", {0.4, 1.2, -0.6});
    check_integral("2^x");
    check_integral("2^(3*x+1)", {0.3, 0.9, -0.5});
    // Hyperbolics.
    check_integral("sinh(x)");
    check_integral("cosh(2*x)");
    check_integral("tanh(x)", {0.4, 1.2, -0.6});
    // Logs and inverse trig.
    check_integral("ln(x)", {0.4, 1.2, 2.3});
    check_integral("ln(2*x+1)", {0.4, 1.2, 2.3});
    check_integral("asin(x)", {0.2, -0.5, 0.7});
    check_integral("acos(x)", {0.2, -0.5, 0.7});
    check_integral("atan(x)");
    check_integral("atan(2*x)", {0.3, -0.8, 1.4});
    // sec^2 / csc^2 shapes.
    check_integral("1/cos(x)^2", {0.4, 1.2, -0.6});
    check_integral("1/sin(x)^2", {0.4, 1.2, 2.3});
    // 1/(u^2 + c) -> atan, incl. a completed square.
    check_integral("1/(x^2+1)");
    check_integral("1/(x^2+2*x+5)", {0.4, -1.0, 2.3});
    // 1/sqrt(c - u^2) -> asin.
    check_integral("1/sqrt(1-x^2)", {0.2, -0.5, 0.7});
    check_integral("1/sqrt(4-x^2)", {0.3, -1.1, 1.7});
}

TEST_CASE("integrate: method labels for pure table hits") {
    CHECK(integrate(parse("sin(x)"), "x").method == "table");
    CHECK(integrate(parse("x^3"), "x").method == "power rule");
    CHECK(integrate(parse("ln(x)"), "x").method == "table");
    CHECK(integrate(parse("atan(x)"), "x").method == "table");
}

// ---------------------------------------------------------------------------
// Stage 1: linearity, including parameter coefficients.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: linearity with parameter coefficients") {
    const Expr f = parse("a*sin(x) + b");
    const IntegrateResult res = integrate(f, "x");
    REQUIRE(res.status == IntegrateResult::Status::Integrated);
    CHECK(method_contains(res, "linearity"));
    // Parameters are unbound at the verifier's sample points, so every point
    // is skipped: the specced behavior is to keep the result with a warning.
    CHECK(has_warning(res.warnings, "could not verify numerically"));
    verify_at(f, res, {0.4, 1.2, -0.7}, Bindings{{"a", 1.5}, {"b", -0.5}});
}

TEST_CASE("integrate: linearity over plain sums and constant factors") {
    const auto res = check_integral("3*x^2 - 4*x + 5", {0.4, 1.2, -1.7});
    CHECK(method_contains(res, "power rule"));
    check_integral("2*sin(x) + 3*cos(x) - 1/2", {0.4, 1.2, -0.7});
}

// ---------------------------------------------------------------------------
// Stage 2: linear-argument scaling comes out exactly.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: linear inner arguments scale by 1/a") {
    check_integral("sin(3*x+1)");
    check_integral("exp(2*x)");
    check_integral("(5*x+2)^7", {-0.3, 0.2, 0.5});
    expect_antiderivative("1/(2*x+3)", "ln(abs(2*x+3))/2");
}

// ---------------------------------------------------------------------------
// Stage 3: polynomial expansion.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: polynomial expansion") {
    check_integral("(x+1)^3", {0.4, 1.2, -1.7});
    check_integral("x*(x-2)*(x+3)", {0.4, 1.2, -1.7});
    // Non-polynomial product that only splits after expansion (retry-once).
    check_integral("(x+1)*sin(x)", {0.4, 1.2, -0.7});
}

// ---------------------------------------------------------------------------
// Stage 4: derivative-divides u-substitution.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: u-substitution wins") {
    const auto res = check_integral("x*exp(x^2)", {0.4, 1.2, -0.7});
    CHECK(method_contains(res, "u-substitution"));
    check_integral("2*x*cos(x^2)", {0.4, 1.2, -0.7});
    check_integral("ln(x)/x", {0.4, 1.2, 2.3});
    check_integral("x/(x^2+1)", {0.4, 1.2, -1.7});
    expect_antiderivative("x/(x^2+1)", "ln(abs(x^2+1))/2");
    check_integral("exp(x)/(1+exp(x))", {0.4, 1.2, -0.7});
}

// ---------------------------------------------------------------------------
// Stage 5: pattern-bounded integration by parts.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: integration by parts") {
    expect_antiderivative("x*sin(x)", "sin(x) - x*cos(x)");
    const auto poly_exp = check_integral("x^2*exp(x)", {0.4, 1.2, -0.7});
    CHECK(method_contains(poly_exp, "integration by parts"));
    check_integral("x*ln(x)", {0.4, 1.2, 2.3});
    // Repeated parts: degree-3 polynomial needs the full depth-3 budget.
    const auto rep = check_integral("x^3*cos(x)", {0.4, 1.2, -0.7});
    CHECK(method_contains(rep, "integration by parts"));
}

TEST_CASE("integrate: cyclic e^(ax)*sin/cos(bx) closed forms") {
    expect_antiderivative("exp(x)*sin(x)", "exp(x)*(sin(x) - cos(x))/2");
    const auto res = integrate(parse("exp(x)*sin(x)"), "x");
    REQUIRE(res.status == IntegrateResult::Status::Integrated);
    CHECK(method_contains(res, "integration by parts"));
    check_integral("exp(2*x)*cos(3*x)", {0.4, 1.2, -0.7});
    check_integral("exp(-x)*sin(2*x)", {0.4, 1.2, -0.7});
}

// ---------------------------------------------------------------------------
// Unsolved honesty: never a wrong answer.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: unsolvable integrands return Unsolved with the warning") {
    for (const char* src : {"exp(x^2)", "sin(x)/x", "abs(x)", "x^x"}) {
        const IntegrateResult res = integrate(parse(src), "x");
        INFO("integrand: " << src);
        for (const auto& w : res.warnings) INFO("warning: " << w);
        CHECK(res.status == IntegrateResult::Status::Unsolved);
        CHECK(has_warning(res.warnings, "no applicable integration rule"));
    }
}

// ---------------------------------------------------------------------------
// Stage 6: rational functions via partial fractions.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: partial fractions — distinct linear factors") {
    const auto res = check_integral("1/(x^2-1)", {0.4, 2.3, -3.1});
    CHECK(method_contains(res, "partial fractions"));
    expect_antiderivative("1/(x^2-1)", "ln(abs(x-1))/2 - ln(abs(x+1))/2");
}

TEST_CASE("integrate: partial fractions — polynomial division first") {
    const auto res = check_integral("(x^2+1)/(x-1)", {0.4, 2.3, -1.7});
    CHECK(method_contains(res, "partial fractions"));
    expect_antiderivative("(x^2+1)/(x-1)", "x^2/2 + x + 2*ln(abs(x-1))");
}

TEST_CASE("integrate: partial fractions — repeated linear factor") {
    // 1/(x^2+2x+1) == 1/(x+1)^2.
    const auto res = check_integral("1/(x^2+2*x+1)", {0.4, 1.2, -2.6});
    CHECK(method_contains(res, "partial fractions"));
    expect_antiderivative("1/(x^2+2*x+1)", "-1/(x+1)");
    check_integral("1/((x-1)^2*(x+2))", {0.4, 2.3, -3.1});
}

TEST_CASE("integrate: partial fractions — linear factors + one irreducible quadratic") {
    const auto res = check_integral("x/(x^4-1)", {0.4, 2.3, -3.1});
    CHECK(method_contains(res, "partial fractions"));
}

TEST_CASE("integrate: irreducible quadratic denominators (atan forms)") {
    // 1/(x^2+x+1) completes the square (a stage-2 table hit is fine too —
    // what matters is a verified antiderivative).
    check_integral("1/(x^2+x+1)", {0.4, 1.2, -1.7});
    check_integral("(3*x+2)/(x^2+4)", {0.4, 1.2, -1.7});
}

TEST_CASE("integrate: rational functions out of stage-6 scope stay Unsolved") {
    // Denominator degree 7 > 6.
    const auto res = integrate(parse("1/((x-1)^7)"), "x");
    // (x-1)^(-7) is a table power-rule hit, so use a truly deg-7 polynomial.
    const auto res2 = integrate(parse("1/(x^7-x+1)"), "x");
    CHECK(res.status == IntegrateResult::Status::Integrated);  // table handles it
    CHECK(res2.status == IntegrateResult::Status::Unsolved);
    // Symbolic-parameter coefficients -> Unsolved (§8b stage 6).
    const auto res3 = integrate(parse("1/(x^2 + a*x - 1)"), "x");
    CHECK(res3.status == IntegrateResult::Status::Unsolved);
}

// ---------------------------------------------------------------------------
// Stage 7: trig powers.
// ---------------------------------------------------------------------------

TEST_CASE("integrate: trig powers") {
    const auto sq = check_integral("sin(x)^2", {0.4, 1.2, -0.7});
    CHECK(method_contains(sq, "trig identity"));
    check_integral("cos(x)^2", {0.4, 1.2, -0.7});
    check_integral("cos(2*x+1)^2", {0.4, 1.2, -0.7});
    const auto cube = check_integral("cos(x)^3", {0.4, 1.2, -0.7});
    CHECK(method_contains(cube, "trig identity"));
    expect_antiderivative("cos(x)^3", "sin(x) - sin(x)^3/3");
    check_integral("sin(x)^3", {0.4, 1.2, -0.7});
    check_integral("sin(x)^5", {0.4, 1.2, -0.7});
    check_integral("cos(3*x)^5", {0.4, 1.2, -0.7});
}

// ---------------------------------------------------------------------------
// Definite integration (§8b): FTC path, cross-check, numeric fallback.
// ---------------------------------------------------------------------------

TEST_CASE("integrate_definite: FTC exact values") {
    const auto sin_res = integrate_definite(parse("sin(x)"), "x", make_num(0),
                                            make_const(ConstantId::Pi));
    REQUIRE(sin_res.status == DefiniteIntegralResult::Status::Exact);
    CHECK(sin_res.method == "FTC");
    CHECK(structurally_equal(sin_res.value, make_num(2)));

    const auto ln_res = integrate_definite(parse("1/x"), "x", make_num(1),
                                           make_const(ConstantId::E));
    REQUIRE(ln_res.status == DefiniteIntegralResult::Status::Exact);
    CHECK(structurally_equal(ln_res.value, make_num(1)));

    const auto third = integrate_definite(parse("x^2"), "x", make_num(0), make_num(1));
    REQUIRE(third.status == DefiniteIntegralResult::Status::Exact);
    CHECK(structurally_equal(third.value, make_num(Rational(1, 3))));
}

TEST_CASE("integrate_definite: numeric fallback when the indefinite is Unsolved") {
    // sin(x)/x has no elementary antiderivative; Si is not implemented.
    const auto res =
        integrate_definite(parse("sin(x)/x"), "x", make_num(1), make_num(2));
    REQUIRE(res.status == DefiniteIntegralResult::Status::Numeric);
    CHECK(res.method == "numeric (adaptive Simpson)");
    REQUIRE(res.value->kind() == Kind::Number);
    // Si(2) - Si(1) = 0.65932977...
    CHECK_THAT(res.value->number().to_double(), WithinAbs(0.6593299064, 1e-8));
}

TEST_CASE("integrate_definite: the gaussian is now exact through erf") {
    const auto res =
        integrate_definite(parse("e^(-x^2)"), "x", make_num(0), make_num(1));
    REQUIRE(res.status == DefiniteIntegralResult::Status::Exact);
    CHECK_THAT(evaluate(res.value, Bindings{}), WithinAbs(0.7468241328, 1e-9));
}

TEST_CASE("integrate_definite: 1/x over [-1,1] is never a bogus 0") {
    // The integrand is not evaluable at x = 0: the grid check makes FTC
    // unsafe, and the numeric path hits the same gap -> Unsolved (§8b).
    const auto res = integrate_definite(parse("1/x"), "x", make_num(-1), make_num(1));
    CHECK(res.status == DefiniteIntegralResult::Status::Unsolved);
    CHECK_FALSE(res.warnings.empty());
}

TEST_CASE("integrate_definite: tan over [0,3] never reports a silent FTC lie") {
    // tan has a pole at pi/2 that the 65-point grid can miss; the quadrature
    // cross-check must reject the FTC value (here the quadrature does not
    // converge, so the amended §8b answers Unsolved, never a bogus number).
    const auto res = integrate_definite(parse("tan(x)"), "x", make_num(0), make_num(3));
    CHECK(res.status != DefiniteIntegralResult::Status::Exact);
    CHECK_FALSE(res.warnings.empty());
}

// ---------------------------------------------------------------------------
// Review fixes (adversarial review of §8b): divergence honesty, endpoint
// singularities, symbolic quadratic coefficients, explicit assumptions.
// ---------------------------------------------------------------------------

TEST_CASE("integrate_definite: non-converged quadrature is Unsolved, never a value") {
    // A pole *between* grid points used to leak a huge meaningless Numeric
    // (e.g. 1/(x-1)^2 over [0, 2.5] "≈ 1.7e13").
    for (const char* src : {"1/(x-1)^2", "exp(x)/(x-1)", "cos(x)^(-2)"}) {
        const auto res = integrate_definite(parse(src), "x", make_num(0),
                                            make_num(Rational(5, 2)));
        INFO("integrand: " << src);
        for (const auto& w : res.warnings) INFO("warning: " << w);
        CHECK(res.status == DefiniteIntegralResult::Status::Unsolved);
        CHECK(has_warning(res.warnings,
                          "numeric quadrature failed to converge; the integral "
                          "may be divergent"));
    }
    // Divergent although FTC produces a finite-looking number (pole at pi/2
    // sits between grid points): must be Unsolved, not "preferring numeric".
    const auto tan_res =
        integrate_definite(parse("tan(x)"), "x", make_num(0), make_num(3));
    CHECK(tan_res.status == DefiniteIntegralResult::Status::Unsolved);
    // 1/x over [-1, 2]: FTC would claim ln(2); the integral diverges at 0.
    const auto rec =
        integrate_definite(parse("1/x"), "x", make_num(-1), make_num(2));
    CHECK(rec.status == DefiniteIntegralResult::Status::Unsolved);
}

TEST_CASE("integrate_definite: endpoint-only singularities use an open interval") {
    // sin(x)/x is integrable on [0, 1]; the removable singularity at the
    // endpoint used to make the whole integral Unsolved.
    const auto res =
        integrate_definite(parse("sin(x)/x"), "x", make_num(0), make_num(1));
    for (const auto& w : res.warnings) INFO("warning: " << w);
    REQUIRE(res.status == DefiniteIntegralResult::Status::Numeric);
    REQUIRE(res.value->kind() == Kind::Number);
    CHECK_THAT(res.value->number().to_double(), WithinAbs(0.9460830704, 1e-6));
    CHECK(has_warning(res.warnings,
                      "integrand is not evaluable at an integration endpoint; "
                      "integrating over a slightly smaller open interval"));

    const auto lnr =
        integrate_definite(parse("ln(x)"), "x", make_num(0), make_num(1));
    REQUIRE(lnr.status == DefiniteIntegralResult::Status::Numeric);
    CHECK_THAT(lnr.value->number().to_double(), WithinAbs(-1.0, 1e-6));

    // An *interior* gap is still Unsolved: 1/x^2 over [-1, 1].
    const auto interior =
        integrate_definite(parse("1/x^2"), "x", make_num(-1), make_num(1));
    CHECK(interior.status == DefiniteIntegralResult::Status::Unsolved);
}

TEST_CASE("integrate: atan table entry accepts symbolic quadratic coefficients") {
    // 1/(a^2*x^2 + 1) is a §8b stage-2 shape (u = a*x); it used to be
    // rejected because the x^2 coefficient was not a literal Number.
    const Expr f = parse("1/(a^2*x^2+1)");
    const IntegrateResult res = integrate(f, "x");
    REQUIRE(res.status == IntegrateResult::Status::Integrated);
    CHECK(method_contains(res, "table"));
    verify_at(f, res, {0.4, 1.2, -1.7}, Bindings{{"a", 1.3}});
    verify_at(f, res, {0.4, 1.2, -1.7}, Bindings{{"a", -0.7}});

    // A provably impossible positivity (k = -1 - a^2/4 < 0 for every a) is
    // never assumed: stays Unsolved.
    const auto impossible = integrate(parse("1/(x^2+a*x-1)"), "x");
    CHECK(impossible.status == IntegrateResult::Status::Unsolved);
}

TEST_CASE("integrate: the c > 0 assumption for 1/(x^2+c) is warned explicitly") {
    const auto param = integrate(parse("1/(x^2+c)"), "x");
    REQUIRE(param.status == IntegrateResult::Status::Integrated);
    CHECK(has_warning(param.warnings, "result assumes c > 0"));
    // Numeric constants need no assumption: no such warning.
    const auto numeric = integrate(parse("1/(x^2+4)"), "x");
    REQUIRE(numeric.status == IntegrateResult::Status::Integrated);
    for (const auto& w : numeric.warnings) CHECK(w.find("assumes") == std::string::npos);
}

TEST_CASE("integrate_definite: bad bounds are Unsolved with a warning") {
    const auto symbolic =
        integrate_definite(parse("x^2"), "x", parse("a"), make_num(1));
    CHECK(symbolic.status == DefiniteIntegralResult::Status::Unsolved);
    CHECK(has_warning(symbolic.warnings, "integration bounds must be symbol-free"));

    const auto empty = integrate_definite(parse("sin(x)"), "x", make_num(2), make_num(2));
    REQUIRE(empty.status == DefiniteIntegralResult::Status::Exact);
    CHECK(structurally_equal(empty.value, make_num(0)));
}
