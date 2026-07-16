#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Expr parse(std::string_view s) {
    return parse_expression(s);
}

/// differentiate(parse(input), var) must be structurally equal to
/// simplify(parse(expected)) — the derivative is returned pre-simplified,
/// so the simplified form of the expected string is the right target.
void expect_diff(std::string_view input, std::string_view var, std::string_view expected) {
    const Expr got = differentiate(parse(input), var);
    const Expr want = simplify(parse(expected));
    INFO("d/d" << var << " of " << input);
    INFO("got:      " << debug_string(got));
    INFO("expected: " << debug_string(want));
    CHECK(structurally_equal(got, want));
}

constexpr double kStep = 1e-6;

/// evaluate(df, x=v) must match the central difference of f at v.
void check_central_difference(const Expr& f, const Expr& df, double v) {
    const double sym = evaluate(df, Bindings{{"x", v}});
    const double num = (evaluate(f, Bindings{{"x", v + kStep}}) -
                        evaluate(f, Bindings{{"x", v - kStep}})) /
                       (2 * kStep);
    INFO("f  = " << debug_string(f));
    INFO("df = " << debug_string(df));
    INFO("v = " << v << "  symbolic = " << sym << "  numeric = " << num);
    CHECK_THAT(sym, WithinRel(num, 1e-5) || WithinAbs(num, 1e-7));
}

} // namespace

// ---------------------------------------------------------------------------
// Base cases
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: numbers, constants and other symbols give 0") {
    const Expr zero = make_num(0);
    for (const char* input :
         {"5", "-3/2", "pi", "e", "y", "y^2 + pi", "sin(y)*z", "sqrt(2)*pi + 2^y"}) {
        const Expr got = differentiate(parse(input), "x");
        INFO("input: " << input << " -> " << debug_string(got));
        CHECK(structurally_equal(got, zero));
    }
}

TEST_CASE("differentiate: the variable itself and simple linear shapes") {
    expect_diff("x", "x", "1");
    expect_diff("x + y + 1", "x", "1");
    expect_diff("-x", "x", "-1");
    expect_diff("2*x", "x", "2");
    expect_diff("x/3", "x", "1/3");
    expect_diff("a*x", "x", "a");
}

TEST_CASE("differentiate: linearity") {
    expect_diff("3*sin(x) + 2*x + 7", "x", "3*cos(x) + 2");
    expect_diff("x^3 - 2*x + 5", "x", "3*x^2 - 2");
    expect_diff("a*x^2 + b*x + c", "x", "2*a*x + b");
    expect_diff("sin(x) - cos(x)", "x", "cos(x) + sin(x)");
}

// ---------------------------------------------------------------------------
// Chain-rule table (DESIGN.md §8) — every FunctionId
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: chain-rule table entries") {
    expect_diff("sin(x)", "x", "cos(x)");
    expect_diff("cos(x)", "x", "-sin(x)");
    expect_diff("tan(x)", "x", "1 + tan(x)^2");
    expect_diff("asin(x)", "x", "(1 - x^2)^(-1/2)");
    expect_diff("acos(x)", "x", "-(1 - x^2)^(-1/2)");
    expect_diff("atan(x)", "x", "1/(1 + x^2)");
    expect_diff("sinh(x)", "x", "cosh(x)");
    expect_diff("cosh(x)", "x", "sinh(x)");
    expect_diff("tanh(x)", "x", "1 - tanh(x)^2");
    expect_diff("ln(x)", "x", "1/x");
    expect_diff("abs(x)", "x", "x/abs(x)");
}

TEST_CASE("differentiate: chain-rule table entries (factory expectations)") {
    const Expr x = make_sym("x");
    const Expr one_minus_x2 = make_sub(make_num(1), make_pow(x, make_num(2)));

    CHECK(structurally_equal(differentiate(make_fn(FunctionId::Sin, x), "x"),
                             make_fn(FunctionId::Cos, x)));
    CHECK(structurally_equal(differentiate(make_fn(FunctionId::Cos, x), "x"),
                             make_neg(make_fn(FunctionId::Sin, x))));
    CHECK(structurally_equal(differentiate(make_fn(FunctionId::Asin, x), "x"),
                             make_pow(one_minus_x2, make_num(Rational(-1, 2)))));
    CHECK(structurally_equal(differentiate(make_fn(FunctionId::Acos, x), "x"),
                             make_neg(make_pow(one_minus_x2, make_num(Rational(-1, 2))))));
    CHECK(structurally_equal(differentiate(make_fn(FunctionId::Ln, x), "x"),
                             make_pow(x, make_num(-1))));
    CHECK(structurally_equal(differentiate(make_fn(FunctionId::Abs, x), "x"),
                             make_mul({x, make_pow(make_fn(FunctionId::Abs, x), make_num(-1))})));
}

// ---------------------------------------------------------------------------
// Product rule (n-ary)
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: product rule") {
    expect_diff("x*sin(x)", "x", "sin(x) + x*cos(x)");
    expect_diff("x*y*z", "x", "y*z");
    expect_diff("x^2*ln(x)", "x", "2*x*ln(x) + x");
    expect_diff("x*sin(x)*cos(x)", "x",
                "sin(x)*cos(x) + x*cos(x)^2 - x*sin(x)^2");
}

// ---------------------------------------------------------------------------
// Power rule
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: constant-exponent power rule") {
    expect_diff("x^2", "x", "2*x");
    expect_diff("x^5", "x", "5*x^4");
    expect_diff("x^(-1)", "x", "-1/x^2");
    expect_diff("sqrt(x)", "x", "1/(2*sqrt(x))");
    expect_diff("x^(1/3)", "x", "(1/3)*x^(-2/3)");
    expect_diff("x^(-1/2)", "x", "(-1/2)*x^(-3/2)");
    expect_diff("x^y", "x", "y*x^(y - 1)"); // symbolic but x-free exponent
    expect_diff("(x + 1)^3", "x", "3*(x + 1)^2");
}

TEST_CASE("differentiate: general power rule") {
    expect_diff("x^x", "x", "x^x*(ln(x) + 1)"); // DESIGN §8 example
    expect_diff("2^x", "x", "2^x*ln(2)");
    expect_diff("x^y", "y", "x^y*ln(x)");
    expect_diff("x^ln(x)", "x", "2*ln(x)*x^ln(x)/x");
}

TEST_CASE("differentiate: exponential special case (base e)") {
    expect_diff("e^x", "x", "e^x");
    expect_diff("exp(x)", "x", "e^x");
    expect_diff("e^(2*x)", "x", "2*e^(2*x)");
    expect_diff("e^(x^2)", "x", "2*x*e^(x^2)");
    expect_diff("exp(sin(x))", "x", "cos(x)*e^(sin(x))");
}

// ---------------------------------------------------------------------------
// Quotient shapes (negative powers)
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: quotient shapes via negative powers") {
    expect_diff("1/x", "x", "-1/x^2");
    expect_diff("sin(x)/x", "x", "cos(x)/x - sin(x)/x^2");
    expect_diff("x/(x + 1)", "x", "1/(x + 1) - x/(x + 1)^2");
    expect_diff("(x^2 + 1)/x", "x", "2 - (x^2 + 1)/x^2");
}

// ---------------------------------------------------------------------------
// Chain rule through composites
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: chain rule composites") {
    expect_diff("sin(x^2)", "x", "2*x*cos(x^2)"); // DESIGN §8 example
    expect_diff("cos(x^3)", "x", "-3*x^2*sin(x^3)");
    expect_diff("tan(x^2)", "x", "2*x*(1 + tan(x^2)^2)");
    expect_diff("ln(sin(x))", "x", "cos(x)/sin(x)");
    expect_diff("sin(cos(x))", "x", "-sin(x)*cos(cos(x))");
    expect_diff("sin(x)^2", "x", "2*sin(x)*cos(x)");
    expect_diff("sinh(2*x)", "x", "2*cosh(2*x)");
    expect_diff("tanh(x^2)", "x", "2*x*(1 - tanh(x^2)^2)");
    expect_diff("abs(sin(x))", "x", "sin(x)*cos(x)/abs(sin(x))");
    expect_diff("asin(x/2)", "x", "(1/2)*(1 - x^2/4)^(-1/2)");
}

TEST_CASE("differentiate: second derivatives") {
    const Expr d2_sin = differentiate(differentiate(parse("sin(x)"), "x"), "x");
    INFO(debug_string(d2_sin));
    CHECK(structurally_equal(d2_sin, simplify(parse("-sin(x)"))));

    const Expr d2_x4 = differentiate(differentiate(parse("x^4"), "x"), "x");
    INFO(debug_string(d2_x4));
    CHECK(structurally_equal(d2_x4, simplify(parse("12*x^2"))));

    const Expr d2_ln = differentiate(differentiate(parse("ln(x)"), "x"), "x");
    INFO(debug_string(d2_ln));
    CHECK(structurally_equal(d2_ln, simplify(parse("-x^(-2)"))));
}

// ---------------------------------------------------------------------------
// Partial derivatives / other variable names
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: other symbols are treated as constants") {
    expect_diff("x*y^2", "y", "2*x*y");
    expect_diff("x*y", "x", "y");
    expect_diff("x^2 + y^2", "y", "2*y");
    expect_diff("alpha^2", "alpha", "2*alpha"); // greek symbol name
    expect_diff("x_1^3", "x_1", "3*x_1^2");     // subscripted symbol name
    expect_diff("sin(x*y)", "y", "x*cos(x*y)");
}

// ---------------------------------------------------------------------------
// Result hygiene
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: result is already simplified") {
    for (const char* input :
         {"sin(x^2)", "x^x", "x*sin(x)*cos(x)", "sin(x)/x", "abs(sin(x))", "e^(2*x)",
          "x/(x + 1)", "asin(x/2)"}) {
        const Expr d = differentiate(parse(input), "x");
        INFO("input: " << input << " -> " << debug_string(d));
        CHECK(structurally_equal(simplify(d), d));
    }
}

TEST_CASE("differentiate: degenerate shapes do not throw") {
    // 0^x: derivative construction must not attempt make_pow(0, -1).
    const Expr zero_pow = parse("0^x");
    REQUIRE_NOTHROW(differentiate(zero_pow, "x"));

    // A base whose derivative is literally 0 (factories do not collect
    // like terms, so Add(x, -x) is a valid node) under a symbolic exponent.
    const Expr x = make_sym("x");
    const Expr degenerate = make_pow(make_add({x, make_neg(x)}), make_sym("y"));
    const Expr d = differentiate(degenerate, "x");
    INFO(debug_string(d));
    CHECK(structurally_equal(d, make_num(0)));
}

TEST_CASE("differentiate: derivative domain errors surface via evaluate") {
    // d(abs(x)) = x/abs(x) is undefined at 0.
    const Expr dabs = differentiate(parse("abs(x)"), "x");
    REQUIRE_THROWS_AS(evaluate(dabs, Bindings{{"x", 0.0}}), EvalError);

    // d(ln(x)) = 1/x is undefined at 0.
    const Expr dln = differentiate(parse("ln(x)"), "x");
    REQUIRE_THROWS_AS(evaluate(dln, Bindings{{"x", 0.0}}), EvalError);
}

// ---------------------------------------------------------------------------
// Property tests: symbolic derivative vs central difference
// ---------------------------------------------------------------------------

TEST_CASE("differentiate: matches central difference for every FunctionId") {
    const std::vector<std::pair<FunctionId, std::vector<double>>> probes = {
        {FunctionId::Sin, {-0.7, 0.3, 1.1}},
        {FunctionId::Cos, {-0.7, 0.3, 1.1}},
        {FunctionId::Tan, {-0.7, 0.3, 1.1}},   // away from odd multiples of pi/2
        {FunctionId::Asin, {-0.6, 0.1, 0.5}},  // inside (-1, 1)
        {FunctionId::Acos, {-0.6, 0.1, 0.5}},
        {FunctionId::Atan, {-2.0, 0.4, 3.0}},
        {FunctionId::Sinh, {-1.2, 0.4, 2.0}},
        {FunctionId::Cosh, {-1.2, 0.4, 2.0}},
        {FunctionId::Tanh, {-1.2, 0.4, 2.0}},
        {FunctionId::Ln, {0.5, 1.7, 4.2}},     // positive, away from 0
        {FunctionId::Abs, {-2.3, 0.9, 1.4}},   // away from the kink at 0
    };
    const Expr x = make_sym("x");
    const Expr half_x = parse("x/2");
    for (const auto& [id, points] : probes) {
        // Direct application f(x) and a composite f(x/2) (chain-rule check;
        // halving the argument keeps every point inside f's domain).
        for (const Expr& arg : {x, half_x}) {
            const Expr f = make_fn(id, arg);
            const Expr df = differentiate(f, "x");
            for (double v : points) {
                check_central_difference(f, df, v);
            }
        }
    }
}

TEST_CASE("differentiate: matches central difference on composite expressions") {
    const std::vector<std::pair<const char*, std::vector<double>>> cases = {
        {"sin(x^2)", {-1.1, 0.4, 1.3}},
        {"x^x", {0.5, 1.3, 2.2}},              // positive base only
        {"x^3 - 2*x + 5", {-2.0, 0.3, 1.7}},
        {"ln(x^2 + 1)", {-1.5, 0.2, 2.0}},
        {"sqrt(x^2 + 1)", {-1.0, 0.5, 2.0}},
        {"exp(sin(x))", {-1.0, 0.5, 2.0}},
        {"sin(x)*cos(x)", {-0.9, 0.2, 1.1}},
        {"x^2*ln(x)", {0.4, 1.2, 3.0}},        // positive for ln
        {"2^x", {-1.5, 0.0, 2.5}},
        {"1/(x^2 + 1)", {-1.4, 0.3, 2.1}},
        {"atan(x^2)", {-1.2, 0.6, 1.8}},
        {"x*tanh(x)", {-1.5, 0.4, 2.0}},
        {"abs(x^3 - 2)", {-1.0, 0.5, 2.0}},    // kink at cbrt(2) ~ 1.26, avoided
        {"asin(x/2)", {-1.2, 0.3, 1.5}},       // |x/2| < 1
        {"x/(x^2 + 1)", {-1.7, 0.2, 2.4}},
        {"tan(x/3)", {-1.2, 0.5, 1.4}},        // |x/3| well inside (-pi/2, pi/2)
    };
    for (const auto& [input, points] : cases) {
        const Expr f = parse(input);
        const Expr df = differentiate(f, "x");
        for (double v : points) {
            check_central_difference(f, df, v);
        }
    }
}
