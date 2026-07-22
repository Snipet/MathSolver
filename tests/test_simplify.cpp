#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

Expr parse(std::string_view s) {
    return parse_expression(s);
}

/// simplify(parse(input)) must be structurally equal to parse(expected).
void expect_simplifies(std::string_view input, std::string_view expected) {
    const Expr out = simplify(parse(input));
    const Expr want = parse(expected);
    INFO("input:    " << input);
    INFO("got:      " << debug_string(out));
    INFO("expected: " << debug_string(want));
    CHECK(structurally_equal(out, want));
}

/// Guard: simplify must leave parse(input) unchanged.
void expect_unchanged(std::string_view input) {
    const Expr in = parse(input);
    const Expr out = simplify(in);
    INFO("input: " << input << " parsed " << debug_string(in));
    INFO("got:   " << debug_string(out));
    CHECK(structurally_equal(out, in));
}

bool has_function(const Expr& e) {
    if (e->kind() == Kind::Function) {
        return true;
    }
    for (const Expr& a : e->args()) {
        if (has_function(a)) {
            return true;
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Numeric folding
// ---------------------------------------------------------------------------

TEST_CASE("simplify: numeric folding through factories") {
    expect_simplifies("2^(1/2) * 2^(1/2)", "2");
    expect_simplifies("sqrt(2)*sqrt(2) + 1", "3");
    expect_simplifies("abs(-3/2)", "3/2");
    expect_simplifies("abs(sin(0))", "0");
    expect_simplifies("2 + 3*4", "14");
    expect_unchanged("2^(1/2)"); // irrational: stays symbolic
}

// ---------------------------------------------------------------------------
// Like terms
// ---------------------------------------------------------------------------

TEST_CASE("simplify: like-term collection") {
    expect_simplifies("2*x + 3*x", "5*x");
    expect_simplifies("x + x", "2*x");
    expect_simplifies("x*y + 2*x*y", "3*x*y");
    expect_simplifies("x - x", "0");
    expect_simplifies("(x+1) + (x+1)", "2*x + 2"); // Add flattens on parse
    expect_simplifies("2*sin(x) + 3*sin(x)", "5*sin(x)");
    expect_simplifies("x/2 + x/2", "x");
    expect_simplifies("3*x - 2*x + y", "x + y");
}

TEST_CASE("simplify: like-term guards") {
    expect_unchanged("x + y");
    expect_unchanged("2*x + 3*y");
    expect_unchanged("x + x^2");           // different powers are not terms alike
    expect_unchanged("sin(x) + sin(y)");
}

// ---------------------------------------------------------------------------
// Like factors
// ---------------------------------------------------------------------------

TEST_CASE("simplify: like-factor collection") {
    expect_simplifies("x * x^2", "x^3");
    expect_simplifies("x * x", "x^2");
    expect_simplifies("x / x", "1");
    expect_simplifies("x^2 / x", "x");
    expect_simplifies("sqrt(x)*sqrt(x)", "x");
    expect_simplifies("x^(1/3) * x^(1/3) * x^(1/3)", "x");
    expect_simplifies("y * x^2 * x^(-2)", "y");
    expect_simplifies("sin(x)*sin(x)", "sin(x)^2");
}

TEST_CASE("simplify: like-factor guards") {
    expect_unchanged("x^a * y^a");     // NOT combined (DESIGN §7 non-rule)
    expect_unchanged("x^a * x^2");     // symbolic exponent does not combine
    expect_unchanged("x * y");
    expect_unchanged("x^2 * y^3");
}

// ---------------------------------------------------------------------------
// Power rules
// ---------------------------------------------------------------------------

TEST_CASE("simplify: generalized power rule (amended)") {
    // integer outer exponent is a factory fold already
    expect_simplifies("(x^2)^3", "x^6");
    expect_simplifies("(x^(1/2))^2", "x"); // domain extension, documented
    // inner exponent a non-integer or an odd integer -> u^(a*b)
    expect_simplifies("(x^(1/3))^(5/3)", "x^(5/9)");
    expect_simplifies("(x^3)^(1/3)", "x");
    expect_simplifies("(x^(3/5))^(1/3)", "x^(1/5)");
    expect_simplifies("(x^(-1/3))^(1/3)", "x^(-1/9)");
    expect_simplifies("(x^(1/2))^(1/3)", "x^(1/6)");
    expect_simplifies("(x^(2/3))^(1/2)", "x^(1/3)");
    expect_simplifies("(x^3)^(1/2)", "x^(3/2)");
    // inner exponent even integer, a*b an even integer -> u^(a*b)
    expect_simplifies("(x^6)^(1/3)", "x^2");
}

TEST_CASE("simplify: generalized power rule guards") {
    // even inner exponent with non-integer a*b would restrict the domain
    expect_unchanged("(x^2)^(1/3)");
    expect_unchanged("(x^2)^(1/4)");
    expect_unchanged("x^(2/3)");
}

TEST_CASE("simplify: pow distribution over Mul for integer exponents") {
    expect_simplifies("(x*y)^2", "x^2 * y^2");
    expect_simplifies("(x*y*z)^3", "x^3 * y^3 * z^3");
    expect_simplifies("(x*y)^(-1)", "x^(-1) * y^(-1)");
    expect_unchanged("(x*y)^(1/2)"); // non-integer exponent: not distributed
    expect_unchanged("(x*y)^a");
}

TEST_CASE("simplify: sqrt of even powers respects abs") {
    expect_simplifies("sqrt(x^2)", "abs(x)");
    expect_simplifies("sqrt(x^4)", "x^2");      // abs(x)^2 -> x^2
    expect_simplifies("sqrt(x^6)", "abs(x)^3");
    expect_simplifies("(x^(-2))^(1/2)", "abs(x)^(-1)");
    // Odd inner exponent folds without abs (left side undefined for x < 0)
    expect_simplifies("sqrt(x^3)", "x^(3/2)");
    // Guards
    expect_unchanged("sqrt(x)");
    const Expr out = simplify(parse("sqrt(x^2)"));
    INFO(debug_string(out));
    CHECK_FALSE(structurally_equal(out, parse("x"))); // never plain x
}

TEST_CASE("simplify: abs even powers") {
    expect_simplifies("abs(x)^2", "x^2");
    expect_simplifies("abs(x)^4", "x^4");
    expect_simplifies("abs(x)^(-2)", "x^(-2)");
    expect_unchanged("abs(x)^3");
    expect_unchanged("abs(x)^(1/2)");
}

// ---------------------------------------------------------------------------
// exp / ln
// ---------------------------------------------------------------------------

TEST_CASE("simplify: exp/ln inverses") {
    expect_simplifies("e^(ln(x))", "x");
    expect_simplifies("ln(e^x)", "x");
    expect_simplifies("ln(1)", "0");
    expect_simplifies("ln(e)", "1");
    expect_simplifies("ln(e^2)", "2");
    expect_simplifies("ln(exp(x+y))", "x + y");
}

TEST_CASE("simplify: ln non-rules (no log expansion)") {
    expect_unchanged("ln(x*y)");
    expect_unchanged("ln(x^3)");
    expect_unchanged("ln(2)");
    expect_unchanged("ln(x)");
}

// ---------------------------------------------------------------------------
// Trig special values
// ---------------------------------------------------------------------------

TEST_CASE("simplify: trig special values, spot checks") {
    expect_simplifies("sin(0)", "0");
    expect_simplifies("sin(pi/6)", "1/2");
    expect_simplifies("sin(pi/4)", "sqrt(2)/2");
    expect_simplifies("sin(pi/3)", "sqrt(3)/2");
    expect_simplifies("sin(pi/2)", "1");
    expect_simplifies("sin(pi)", "0");
    expect_simplifies("sin(7*pi/6)", "-1/2");
    expect_simplifies("sin(-pi/3)", "-sqrt(3)/2");
    expect_simplifies("sin(13*pi/6)", "1/2"); // reduced mod 2*pi
    expect_simplifies("sin(3*pi/2)", "-1");

    expect_simplifies("cos(0)", "1");
    expect_simplifies("cos(pi/6)", "sqrt(3)/2");
    expect_simplifies("cos(pi/4)", "sqrt(2)/2");
    expect_simplifies("cos(pi/3)", "1/2");
    expect_simplifies("cos(pi/2)", "0");
    expect_simplifies("cos(3*pi/4)", "-sqrt(2)/2");
    expect_simplifies("cos(pi)", "-1");
    expect_simplifies("cos(-pi/6)", "sqrt(3)/2");
    expect_simplifies("cos(2*pi)", "1");

    expect_simplifies("tan(0)", "0");
    expect_simplifies("tan(pi/6)", "sqrt(3)/3");
    expect_simplifies("tan(pi/4)", "1");
    expect_simplifies("tan(pi/3)", "sqrt(3)");
    expect_simplifies("tan(2*pi/3)", "-sqrt(3)");
    expect_simplifies("tan(5*pi/4)", "1");
    expect_simplifies("tan(pi)", "0");
    expect_simplifies("tan(-pi/4)", "-1");
}

TEST_CASE("simplify: tan undefined at odd multiples of pi/2 stays put") {
    expect_unchanged("tan(pi/2)");
    expect_unchanged("tan(3*pi/2)");
    expect_unchanged("tan(-pi/2)");
    expect_unchanged("tan(5*pi/2)");
}

TEST_CASE("simplify: trig special value guards") {
    expect_unchanged("sin(pi/5)"); // denominator not in {1,2,3,4,6}
    expect_unchanged("sin(2*pi/7)");
    expect_unchanged("sin(1)");    // 1 radian, not a pi multiple
    expect_unchanged("cos(x)");
    expect_unchanged("sin(x*pi)"); // symbolic multiple
}

TEST_CASE("simplify: trig special values, exhaustive sweep") {
    const std::vector<long long> denominators{1, 2, 3, 4, 6};
    for (const long long q : denominators) {
        for (long long p = -12; p <= 12; ++p) {
            const Rational r(p, q);
            const Expr arg = make_mul({make_num(r), make_const(ConstantId::Pi)});
            const double angle = r.to_double() * std::numbers::pi;
            for (const FunctionId id :
                 {FunctionId::Sin, FunctionId::Cos, FunctionId::Tan}) {
                const Expr call = make_fn(id, arg);
                const Expr out = simplify(call);
                INFO("fn=" << function_name(id) << " p=" << p << " q=" << q
                           << " -> " << debug_string(out));
                if (id == FunctionId::Tan && r.den() == 2) {
                    CHECK(structurally_equal(out, call)); // undefined: unchanged
                    continue;
                }
                CHECK_FALSE(has_function(out)); // fully evaluated
                const double expected = id == FunctionId::Sin ? std::sin(angle)
                                        : id == FunctionId::Cos ? std::cos(angle)
                                                                : std::tan(angle);
                REQUIRE_THAT(evaluate(out), WithinAbs(expected, 1e-9));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Trig parity and compositions
// ---------------------------------------------------------------------------

TEST_CASE("simplify: trig parity (sign pulled from Mul)") {
    expect_simplifies("sin(-x)", "-sin(x)");
    expect_simplifies("cos(-x)", "cos(x)");
    expect_simplifies("tan(-2*x)", "-tan(2*x)");
    expect_simplifies("sin(-x*y)", "-sin(x*y)");
    expect_simplifies("cos(-3*x)", "cos(3*x)");
    expect_simplifies("sin(-x) + sin(x)", "0");
    // Guards: no sign extraction from Adds or positive arguments
    expect_unchanged("sin(x)");
    expect_unchanged("sin(y - x)");
    expect_unchanged("tan(2*x)");
}

TEST_CASE("simplify: pythagorean identity") {
    expect_simplifies("sin(x)^2 + cos(x)^2", "1");
    expect_simplifies("3*sin(x)^2 + 3*cos(x)^2", "3");
    expect_simplifies("y*sin(x)^2 + y*cos(x)^2", "y");
    expect_simplifies("sin(x)^2 + cos(x)^2 + 5", "6");
    expect_simplifies("sin(2*x)^2 + cos(2*x)^2", "1");
    expect_simplifies("sin(x)^2 + cos(x)^2 + sin(y)^2 + cos(y)^2", "2");
}

TEST_CASE("simplify: pythagorean guards") {
    expect_unchanged("sin(x)^2 + cos(y)^2");     // different arguments
    expect_unchanged("2*sin(x)^2 + 3*cos(x)^2"); // different coefficients
    expect_unchanged("sin(x)^2 + cos(x)^3");     // wrong power
    expect_unchanged("sin(x) + cos(x)");
    expect_unchanged("y*sin(x)^2 + z*cos(x)^2");
}

TEST_CASE("simplify: function/inverse compositions") {
    expect_simplifies("sin(asin(x))", "x");
    expect_simplifies("cos(acos(x))", "x");
    expect_simplifies("tan(atan(x))", "x");
    // NOT value-preserving, must stay (DESIGN §7 non-rule):
    expect_unchanged("asin(sin(x))");
    expect_unchanged("acos(cos(x))");
    expect_unchanged("atan(tan(x))");
}

// ---------------------------------------------------------------------------
// Inverse trig special values (amended §7)
// ---------------------------------------------------------------------------

TEST_CASE("simplify: asin special values") {
    expect_simplifies("asin(0)", "0");
    expect_simplifies("asin(1/2)", "pi/6"); // the solver depends on this one
    expect_simplifies("asin(-1/2)", "-pi/6");
    expect_simplifies("asin(sqrt(2)/2)", "pi/4");
    expect_simplifies("asin(-sqrt(2)/2)", "-pi/4");
    expect_simplifies("asin(sqrt(3)/2)", "pi/3");
    expect_simplifies("asin(-sqrt(3)/2)", "-pi/3");
    expect_simplifies("asin(1)", "pi/2");
    expect_simplifies("asin(-1)", "-pi/2");
    expect_simplifies("asin(1/sqrt(2))", "pi/4"); // Pow(2,-1/2) shape
}

TEST_CASE("simplify: acos special values") {
    expect_simplifies("acos(0)", "pi/2");
    expect_simplifies("acos(1/2)", "pi/3");
    expect_simplifies("acos(-1/2)", "2*pi/3");
    expect_simplifies("acos(sqrt(2)/2)", "pi/4");
    expect_simplifies("acos(-sqrt(2)/2)", "3*pi/4");
    expect_simplifies("acos(sqrt(3)/2)", "pi/6");
    expect_simplifies("acos(-sqrt(3)/2)", "5*pi/6");
    expect_simplifies("acos(1)", "0");
    expect_simplifies("acos(-1)", "pi");
}

TEST_CASE("simplify: atan special values") {
    expect_simplifies("atan(0)", "0");
    expect_simplifies("atan(1)", "pi/4");
    expect_simplifies("atan(-1)", "-pi/4");
    expect_simplifies("atan(sqrt(3))", "pi/3");
    expect_simplifies("atan(-sqrt(3))", "-pi/3");
    expect_simplifies("atan(sqrt(3)/3)", "pi/6");
    expect_simplifies("atan(-sqrt(3)/3)", "-pi/6");
}

TEST_CASE("simplify: inverse trig guards") {
    expect_unchanged("asin(1/3)");
    expect_unchanged("asin(2)");    // outside the table: never invent values
    expect_unchanged("atan(sqrt(2))");
    expect_unchanged("acos(x)");
    expect_unchanged("atan(2)");
}

// ---------------------------------------------------------------------------
// abs and hyperbolic
// ---------------------------------------------------------------------------

TEST_CASE("simplify: abs rules") {
    expect_simplifies("abs(-x)", "abs(x)");
    expect_simplifies("abs(-3*x)", "abs(3*x)");
    expect_simplifies("abs(abs(x))", "abs(x)");
    expect_simplifies("abs(-5/2)", "5/2");
    expect_simplifies("abs(pi)", "pi");
    expect_simplifies("abs(e)", "e");
    expect_simplifies("abs(-pi)", "pi");
    expect_unchanged("abs(x)");
    expect_unchanged("abs(x + 1)");
}

TEST_CASE("simplify: hyperbolic rules") {
    expect_simplifies("sinh(0)", "0");
    expect_simplifies("cosh(0)", "1");
    expect_simplifies("tanh(0)", "0");
    expect_simplifies("sinh(-x)", "-sinh(x)");
    expect_simplifies("cosh(-x)", "cosh(x)");
    expect_simplifies("tanh(-3*x)", "-tanh(3*x)");
    expect_unchanged("sinh(x)");
    expect_unchanged("cosh(1)");
}

TEST_CASE("simplify: equations simplify both sides") {
    const Equation eq = simplify(parse_equation("2*x + 3*x = sin(0) + 7"));
    INFO(debug_string(eq.lhs) << " = " << debug_string(eq.rhs));
    CHECK(structurally_equal(eq.lhs, parse("5*x")));
    CHECK(structurally_equal(eq.rhs, parse("7")));
}

// ---------------------------------------------------------------------------
// Robustness: factory throws are contained (DESIGN §2 notes)
// ---------------------------------------------------------------------------

TEST_CASE("simplify: overflow during a rule leaves the node unchanged") {
    // x^HUGE * x^HUGE: the exponent sum overflows; the factors stay separate.
    const Expr huge = make_pow(make_sym("x"), make_num(Rational(9223372036854775807LL)));
    const Expr e = make_mul({huge, huge});
    Expr out;
    REQUIRE_NOTHROW(out = simplify(e));
    INFO(debug_string(out));
    CHECK(structurally_equal(out, e));
}

TEST_CASE("simplify: division-by-zero fold is contained") {
    // (x - x)^-1: children simplify to 0, make_pow(0,-1) would throw; the
    // guarded rebuild keeps the original node instead of throwing.
    const Expr denom = make_add({make_sym("x"), make_neg(make_sym("x"))});
    const Expr e = make_pow(denom, make_num(-1));
    REQUIRE(e->kind() == Kind::Pow);
    Expr out;
    REQUIRE_NOTHROW(out = simplify(e));
    INFO(debug_string(out));
    CHECK(structurally_equal(out, e));
}

// ---------------------------------------------------------------------------
// expand
// ---------------------------------------------------------------------------

TEST_CASE("expand: binomials and distribution") {
    CHECK(structurally_equal(expand(parse("(x+1)^2")), parse("x^2 + 2*x + 1")));
    CHECK(structurally_equal(expand(parse("(x+1)^3")),
                             parse("x^3 + 3*x^2 + 3*x + 1")));
    CHECK(structurally_equal(expand(parse("(x+1)*(x-2)")), parse("x^2 - x - 2")));
    CHECK(structurally_equal(expand(parse("x*(y+z)")), parse("x*y + x*z")));
    CHECK(structurally_equal(expand(parse("2*(x+3)")), parse("2*x + 6")));
    CHECK(structurally_equal(expand(parse("(x+y)^2*(x-y)")),
                             parse("x^3 + x^2*y - x*y^2 - y^3")));
    CHECK(structurally_equal(expand(parse("(x+1)^2 - x^2 - 2*x")), parse("1")));
    CHECK(structurally_equal(expand(parse("(x+2)*(x-2)")), parse("x^2 - 4")));
}

TEST_CASE("expand: reaches inside functions and leaves non-expandables") {
    CHECK(structurally_equal(expand(parse("sin((x+1)^2)")),
                             parse("sin(x^2 + 2*x + 1)")));
    CHECK(structurally_equal(expand(parse("x + 1")), parse("x + 1")));
    CHECK(structurally_equal(expand(parse("(x+1)^(1/2)")), parse("(x+1)^(1/2)")));
    CHECK(structurally_equal(expand(parse("(x+1)^(-2)")), parse("(x+1)^(-2)")));
    CHECK(structurally_equal(expand(parse("(x+1)^y")), parse("(x+1)^y")));
}

// ---------------------------------------------------------------------------
// collect
// ---------------------------------------------------------------------------

TEST_CASE("collect: groups powers of the symbol") {
    const Expr out = collect(parse("x*y + x*z + 1"), "x");
    INFO(debug_string(out));
    CHECK(structurally_equal(out, parse("(y+z)*x + 1")));
}

TEST_CASE("collect: multiple degrees with simplified coefficients") {
    const Expr out = collect(parse("x^2*y + x^2*z + x + 2*x + 5"), "x");
    INFO(debug_string(out));
    CHECK(structurally_equal(out, parse("(y+z)*x^2 + 3*x + 5")));
}

TEST_CASE("collect: non-polynomial parts become an additive remainder") {
    const Expr out = collect(parse("sin(x) + x*y + x*z"), "x");
    INFO(debug_string(out));
    CHECK(structurally_equal(out, parse("sin(x) + (y+z)*x")));
}

TEST_CASE("collect: no-op when there is nothing to group") {
    const Expr in = parse("x + y");
    CHECK(structurally_equal(collect(in, "x"), in));
    // collecting in an absent symbol keeps the (simplified) value
    CHECK(structurally_equal(collect(parse("2*x + 3*x"), "q"), parse("5*x")));
}

// ---------------------------------------------------------------------------
// polynomial_coefficients
// ---------------------------------------------------------------------------

TEST_CASE("polynomial_coefficients: (x+1)^3 -> {1,3,3,1}") {
    const auto pc = polynomial_coefficients(parse("(x+1)^3"), "x");
    REQUIRE(pc.has_value());
    REQUIRE(pc->size() == 4);
    CHECK(structurally_equal((*pc)[0], parse("1")));
    CHECK(structurally_equal((*pc)[1], parse("3")));
    CHECK(structurally_equal((*pc)[2], parse("3")));
    CHECK(structurally_equal((*pc)[3], parse("1")));
}

TEST_CASE("polynomial_coefficients: symbolic coefficients") {
    const auto pc = polynomial_coefficients(parse("x*y + x*z + 1"), "x");
    REQUIRE(pc.has_value());
    REQUIRE(pc->size() == 2);
    CHECK(structurally_equal((*pc)[0], parse("1")));
    CHECK(structurally_equal((*pc)[1], parse("y + z")));
}

TEST_CASE("polynomial_coefficients: gaps, constants, zero polynomial") {
    const auto cubic = polynomial_coefficients(parse("x^3 + 1"), "x");
    REQUIRE(cubic.has_value());
    REQUIRE(cubic->size() == 4);
    CHECK(structurally_equal((*cubic)[1], parse("0")));
    CHECK(structurally_equal((*cubic)[2], parse("0")));

    const auto constant = polynomial_coefficients(parse("5"), "x");
    REQUIRE(constant.has_value());
    REQUIRE(constant->size() == 1);
    CHECK(structurally_equal((*constant)[0], parse("5")));

    const auto zero = polynomial_coefficients(parse("x - x"), "x");
    REQUIRE(zero.has_value());
    REQUIRE(zero->size() == 1);
    CHECK(structurally_equal((*zero)[0], parse("0")));

    const auto folded = polynomial_coefficients(parse("(x+1)^2 - x^2 - 2*x"), "x");
    REQUIRE(folded.has_value());
    REQUIRE(folded->size() == 1);
    CHECK(structurally_equal((*folded)[0], parse("1")));

    const auto diff2 = polynomial_coefficients(parse("(x+2)*(x-2)"), "x");
    REQUIRE(diff2.has_value());
    REQUIRE(diff2->size() == 3);
    CHECK(structurally_equal((*diff2)[0], parse("-4")));
    CHECK(structurally_equal((*diff2)[1], parse("0")));
    CHECK(structurally_equal((*diff2)[2], parse("1")));
}

TEST_CASE("polynomial_coefficients: nullopt for non-polynomials") {
    CHECK_FALSE(polynomial_coefficients(parse("sin(x)"), "x").has_value());
    CHECK_FALSE(polynomial_coefficients(parse("x^(1/2)"), "x").has_value());
    CHECK_FALSE(polynomial_coefficients(parse("x^y"), "x").has_value());
    CHECK_FALSE(polynomial_coefficients(parse("1/x"), "x").has_value());
    CHECK_FALSE(polynomial_coefficients(parse("2^x"), "x").has_value());
    CHECK_FALSE(polynomial_coefficients(parse("x + ln(x)"), "x").has_value());
    // ... but fine when the symbol only appears in coefficients
    const auto pc = polynomial_coefficients(parse("y^2 + sin(y)"), "x");
    REQUIRE(pc.has_value());
    REQUIRE(pc->size() == 1);
}

// ---------------------------------------------------------------------------
// factor
// ---------------------------------------------------------------------------

TEST_CASE("factor: quadratics with rational roots") {
    CHECK(structurally_equal(factor(parse("x^2 - 5*x + 6")),
                             parse("(x-2)*(x-3)")));
    CHECK(structurally_equal(factor(parse("4*x^2 - 9")),
                             parse("4*(x - 3/2)*(x + 3/2)")));
    CHECK(structurally_equal(factor(parse("x^2 - 2*x + 1")), parse("(x-1)^2")));
    CHECK(structurally_equal(factor(parse("3*x^2 + 3*x")), parse("3*x*(x+1)")));
    CHECK(structurally_equal(factor(parse("x^2 - 4")), parse("(x-2)*(x+2)")));
}

TEST_CASE("factor: common numeric and symbolic factors") {
    CHECK(structurally_equal(factor(parse("2*x + 2*y")), parse("2*(x+y)")));
    CHECK(structurally_equal(factor(parse("x*y + x*z")), parse("x*(y+z)")));
    CHECK(structurally_equal(factor(parse("x^3 + x^2")), parse("x^2*(x+1)")));
    CHECK(structurally_equal(factor(parse("6*x^2*y + 9*x*y^2")),
                             parse("3*x*y*(2*x + 3*y)")));
}

TEST_CASE("factor: leaves unfactorable input unchanged, never throws") {
    CHECK(structurally_equal(factor(parse("x^2 + x + 1")), parse("x^2 + x + 1")));
    CHECK(structurally_equal(factor(parse("x^2 - 2")), parse("x^2 - 2")));
    CHECK(structurally_equal(factor(parse("x + y")), parse("x + y")));
    CHECK(structurally_equal(factor(parse("sin(x) + 1")), parse("sin(x) + 1")));
    CHECK(structurally_equal(factor(parse("7")), parse("7")));
    CHECK(structurally_equal(factor(parse("x")), parse("x")));
    REQUIRE_NOTHROW(factor(parse("x^2 + 2*x + 2"))); // negative discriminant
}

// ---------------------------------------------------------------------------
// Properties: idempotence and value preservation
// ---------------------------------------------------------------------------

namespace {

const std::vector<std::string>& evaluable_corpus() {
    static const std::vector<std::string> kCorpus = {
        "2*x + 3*x",
        "x*y + 2*x*y - z",
        "x*x^2 + x^4/x",
        "sqrt(x^2)",
        "sqrt(x^4)",
        "(x*y)^2",
        "(x^2)^(1/3)",
        "sin(-x) + sin(x)",
        "cos(-x)*cos(x)",
        "sin(x)^2 + cos(x)^2",
        "3*sin(y)^2 + 3*cos(y)^2 + x",
        "tan(-2*x)",
        "sin(pi/6) + cos(pi/3) + x",
        "tan(pi/4)*x",
        "e^(ln(x)) + ln(e^y)",
        "ln(1) + ln(e)",
        "abs(-x) + abs(abs(y))",
        "abs(x)^2",
        "sinh(0) + cosh(0) + tanh(0) + x",
        "sinh(-x) + sinh(x)",
        "asin(1/2) + acos(1/2)",
        "atan(1) + atan(-1) + z",
        "(x+1)^2 - x^2",
        "x/x + y/y",
        "1/2*x + 0.5*x",
        "2^(1/2)*2^(1/2) + x",
        "sin(asin(x/3))",
        "x^(1/2)*x^(1/2)",
        "(x+y)*(x-y)",
        "x - x + y",
    };
    return kCorpus;
}

std::vector<std::string> idempotence_corpus() {
    std::vector<std::string> corpus = evaluable_corpus();
    corpus.insert(corpus.end(), {
        "tan(pi/2)",
        "asin(2)",
        "ln(x*y)",
        "asin(sin(x))",
        "x^a*y^a",
        "(x^(1/3))^(5/3)",
        "abs(x)^3",
        "sqrt(x^6)",
        "y*sin(x)^2 + y*cos(x)^2",
        "(x*y*z)^3 / (x*y*z)",
    });
    return corpus;
}

} // namespace

TEST_CASE("simplify: idempotence property over corpus") {
    for (const std::string& src : idempotence_corpus()) {
        const Expr e = parse(src);
        const Expr once = simplify(e);
        const Expr twice = simplify(once);
        INFO("input: " << src);
        INFO("once:  " << debug_string(once));
        INFO("twice: " << debug_string(twice));
        CHECK(structurally_equal(once, twice));
    }
}

TEST_CASE("simplify: value preservation property over corpus") {
    const std::vector<Bindings> binding_sets = {
        {{"x", 0.7}, {"y", 1.3}, {"z", 0.5}},
        {{"x", 1.9}, {"y", 0.4}, {"z", 2.2}},
        {{"x", 0.3}, {"y", 2.7}, {"z", 1.6}},
    };
    for (const std::string& src : evaluable_corpus()) {
        const Expr e = parse(src);
        const Expr s = simplify(e);
        for (const Bindings& b : binding_sets) {
            const double before = evaluate(e, b);
            const double after = evaluate(s, b);
            INFO("input: " << src << " simplified: " << debug_string(s));
            INFO("x=" << b.at("x") << " y=" << b.at("y") << " z=" << b.at("z"));
            REQUIRE_THAT(after, WithinRel(before, 1e-9) || WithinAbs(before, 1e-12));
        }
    }
}

TEST_CASE("simplify: nested expressions simplify recursively") {
    expect_simplifies("sin(2*x + 3*x)", "sin(5*x)");
    expect_simplifies("(2*x + 3*x)^2", "25*x^2");
    expect_simplifies("ln(e^(x + x))", "2*x");
    expect_simplifies("abs(-sin(-x))", "abs(sin(x))");
}

TEST_CASE("simplify: inverse hyperbolic special values and round-trips") {
    const auto S = [](std::string_view s) {
        return to_string(simplify(parse_expression(s)), PrintStyle::Plain);
    };
    CHECK(S("asinh(0)") == "0");
    CHECK(S("atanh(0)") == "0");
    CHECK(S("acosh(1)") == "0");
    CHECK(S("sinh(asinh(x))") == "x");
    CHECK(S("tanh(atanh(x))") == "x");
    CHECK(S("cosh(acosh(x))") == "x");
    CHECK(S("asinh(sinh(x))") == "x");
    CHECK(S("atanh(tanh(x))") == "x");
    CHECK(S("asinh(-x)") == "-asinh(x)");
    CHECK(S("atanh(-x)") == "-atanh(x)");
    // acosh(cosh(x)) is NOT x for negative x; must stay put.
    CHECK(S("acosh(cosh(x))") == "acosh(cosh(x))");
}

TEST_CASE("simplify: exact complex (Gaussian) folding to a + b*i") {
    // Products and powers of complex constants collapse to canonical a + b*i,
    // matching what expand() already produces.
    expect_simplifies("(1+i)*(1-i)", "2");
    expect_simplifies("(2+3i)*(1-i)", "5 + i");
    expect_simplifies("(1+i)^2", "2*i");
    expect_simplifies("(1+i)^3", "-2 + 2*i");
    expect_simplifies("i*i", "-1");
    // Integer powers of i cycle; large powers do not spin (square-and-multiply).
    expect_simplifies("i^7", "-i");
    expect_simplifies("i^100", "1");
    expect_simplifies("i^(-1)", "-i");
    // Rationalized complex denominators — the Phase 1 headline.
    expect_simplifies("1/i", "-i");
    expect_simplifies("1/(1+i)", "1/2 - i/2");
    expect_simplifies("1/(3+4i)", "3/25 - 4*i/25");
    expect_simplifies("(3+i)/(1-i)", "1 + 2*i");
    expect_simplifies("(1+i)^(-2)", "-i/2");
}

TEST_CASE("simplify: complex folding is scoped — symbolic and real untouched") {
    // A symbol anywhere blocks the Gaussian fold; simplify never distributes.
    expect_unchanged("x + i");
    expect_unchanged("i + x"); // canonical order already
    expect_unchanged("2*(i + x)");
    expect_unchanged("(i + x)*(x - i)"); // no conjugate expansion for symbolic x
    expect_unchanged("1/(i + x)");       // no rationalizing over an unknown-sign x
    // The real-arithmetic path is entirely unaffected.
    expect_simplifies("2*3 + 4", "10");
    expect_simplifies("sqrt(8)", "2*sqrt(2)");
    expect_unchanged("1/(x + 1)");
}

TEST_CASE("simplify: complex accessors on numeric arguments") {
    const auto S = [](std::string_view s) {
        return to_string(simplify(parse_expression(s)), PrintStyle::Plain);
    };
    // conj/Re/Im fold on Gaussian arguments; abs is the modulus.
    CHECK(S("conj(2+3i)") == "-3*i + 2");
    CHECK(S("Re(2+3i)") == "2");
    CHECK(S("Im(2+3i)") == "3");
    CHECK(S("conj(i)") == "-i");
    CHECK(S("conj(conj(2+3i))") == "3*i + 2");
    CHECK(S("abs(3+4i)") == "5");    // sqrt(25) normalized
    CHECK(S("abs(1+i)") == "sqrt(2)");
    // A symbolic argument stays unevaluated (a symbol's realness is not assumed).
    CHECK(S("conj(x)") == "conj(x)");
    CHECK(S("Re(x + i)") == "Re(i + x)");
    CHECK(S("Im(x)") == "Im(x)");
}

TEST_CASE("simplify: complex folding is idempotent") {
    const auto S = [](std::string_view s) {
        return simplify(parse_expression(s));
    };
    for (const auto* s : {"1/(1+i)", "(3+i)/(1-i)", "(1+i)^5", "1/(3+4i)",
                          "(2+3i)*(1-i)", "i^100"}) {
        const Expr once = S(s);
        const Expr twice = simplify(once);
        INFO("input: " << s << " once " << debug_string(once));
        CHECK(structurally_equal(once, twice));
    }
}
