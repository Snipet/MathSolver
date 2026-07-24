#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"

using namespace mathsolver;
using Catch::Approx;

namespace {

Expr x() { return make_sym("x"); }
Expr y() { return make_sym("y"); }
Expr num(long long n) { return make_num(n); }
Expr num(long long n, long long d) { return make_num(Rational(n, d)); }

template <typename F>
EvalError capture_eval_error(F&& f) {
    try {
        std::forward<F>(f)();
    } catch (const EvalError& e) {
        return e;
    }
    FAIL("expected EvalError, but nothing was thrown");
    throw std::logic_error("unreachable");
}

bool message_contains(const EvalError& e, std::string_view needle) {
    return std::string_view(e.what()).find(needle) != std::string_view::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// evaluate: values
// ---------------------------------------------------------------------------

TEST_CASE("evaluate: numbers and constants", "[evaluator]") {
    CHECK(evaluate(num(42)) == Approx(42.0));
    CHECK(evaluate(num(-3, 2)) == Approx(-1.5));
    CHECK(evaluate(make_const(ConstantId::Pi)) == Approx(std::numbers::pi));
    CHECK(evaluate(make_const(ConstantId::E)) == Approx(std::numbers::e));
}

TEST_CASE("evaluate: binding lookups", "[evaluator]") {
    CHECK(evaluate(x(), {{"x", 2.5}}) == Approx(2.5));
    // x^2 + y with x=3, y=0.5 (DESIGN.md section 10 example)
    Expr e = make_add({make_pow(x(), num(2)), y()});
    CHECK(evaluate(e, {{"x", 3.0}, {"y", 0.5}}) == Approx(9.5));
    // Multiple uses of the same symbol.
    Expr f = make_mul({x(), make_add({x(), num(1)})});
    CHECK(evaluate(f, {{"x", 4.0}}) == Approx(20.0));
    // Subscripted and greek symbol names.
    CHECK(evaluate(make_sym("alpha"), {{"alpha", -1.25}}) == Approx(-1.25));
    CHECK(evaluate(make_sym("x_12"), {{"x_12", 7.0}}) == Approx(7.0));
}

TEST_CASE("evaluate: known values through the parser", "[evaluator]") {
    CHECK(evaluate(parse_expression("\\sin{\\pi/2}")) == Approx(1.0));
    CHECK(evaluate(parse_expression("cos(0) + sinh(1)")) == Approx(2.1752011936438014));
    CHECK(evaluate(parse_expression("e^2")) == Approx(7.38905609893065));
    CHECK(evaluate(parse_expression("log(100)")) == Approx(2.0));
    CHECK(evaluate(parse_expression("\\frac{22}{7}")) == Approx(22.0 / 7.0));
    CHECK(evaluate(parse_expression("2**3 + 1")) == Approx(9.0));
    CHECK(evaluate(parse_expression("\\sin^{-1}(0.5)")) == Approx(0.5235987755982989));
    CHECK(evaluate(parse_expression("sqrt(2)")) == Approx(std::sqrt(2.0)));
    CHECK(evaluate(parse_expression("1/x"), {{"x", 4.0}}) == Approx(0.25));
}

TEST_CASE("evaluate: every FunctionId", "[evaluator]") {
    const double u = 0.375;
    auto ev = [&](FunctionId id, double v) { return evaluate(make_fn(id, x()), {{"x", v}}); };
    CHECK(ev(FunctionId::Sin, u) == Approx(std::sin(u)));
    CHECK(ev(FunctionId::Cos, u) == Approx(std::cos(u)));
    CHECK(ev(FunctionId::Tan, u) == Approx(std::tan(u)));
    CHECK(ev(FunctionId::Asin, u) == Approx(std::asin(u)));
    CHECK(ev(FunctionId::Acos, u) == Approx(std::acos(u)));
    CHECK(ev(FunctionId::Atan, 2.5) == Approx(std::atan(2.5)));
    CHECK(ev(FunctionId::Sinh, u) == Approx(std::sinh(u)));
    CHECK(ev(FunctionId::Cosh, u) == Approx(std::cosh(u)));
    CHECK(ev(FunctionId::Tanh, u) == Approx(std::tanh(u)));
    CHECK(ev(FunctionId::Ln, u) == Approx(std::log(u)));
    CHECK(ev(FunctionId::Abs, -u) == Approx(u));
    // Boundary arguments of asin/acos are inside the domain.
    CHECK(ev(FunctionId::Asin, 1.0) == Approx(std::numbers::pi / 2));
    CHECK(ev(FunctionId::Acos, -1.0) == Approx(std::numbers::pi));
}

TEST_CASE("evaluate: integer-exponent powers allow negative bases", "[evaluator]") {
    CHECK(evaluate(make_pow(x(), num(3)), {{"x", -2.0}}) == Approx(-8.0));
    CHECK(evaluate(make_pow(x(), num(2)), {{"x", -3.0}}) == Approx(9.0));
    CHECK(evaluate(make_pow(x(), num(-2)), {{"x", -2.0}}) == Approx(0.25));
    // 0^0 == 1 by convention; 0^positive == 0.
    CHECK(evaluate(make_pow(x(), y()), {{"x", 0.0}, {"y", 0.0}}) == Approx(1.0));
    CHECK(evaluate(make_pow(x(), y()), {{"x", 0.0}, {"y", 3.0}}) == Approx(0.0));
}

// ---------------------------------------------------------------------------
// evaluate: every EvalError case
// ---------------------------------------------------------------------------

TEST_CASE("evaluate: unbound symbol names the symbol", "[evaluator][error]") {
    EvalError e = capture_eval_error([] { return evaluate(x()); });
    CHECK(message_contains(e, "x"));
    EvalError e2 = capture_eval_error(
        [] { return evaluate(make_add({make_sym("alpha"), num(1)}), {{"beta", 1.0}}); });
    CHECK(message_contains(e2, "alpha"));
    // Partially bound expressions still fail.
    REQUIRE_THROWS_AS(evaluate(make_add({x(), y()}), {{"x", 1.0}}), EvalError);
}

TEST_CASE("evaluate: division by zero", "[evaluator][error]") {
    REQUIRE_THROWS_AS(evaluate(parse_expression("1/x"), {{"x", 0.0}}), EvalError);
    REQUIRE_THROWS_AS(evaluate(make_pow(x(), num(-2)), {{"x", 0.0}}), EvalError);
    // 0^negative with a symbolic exponent.
    REQUIRE_THROWS_AS(evaluate(make_pow(x(), y()), {{"x", 0.0}, {"y", -1.0}}), EvalError);
}

TEST_CASE("evaluate: domain errors", "[evaluator][error]") {
    // ln(x <= 0)
    REQUIRE_THROWS_AS(evaluate(make_fn(FunctionId::Ln, num(0))), EvalError);
    REQUIRE_THROWS_AS(evaluate(make_fn(FunctionId::Ln, x()), {{"x", -1.0}}), EvalError);
    // asin/acos outside [-1, 1]
    REQUIRE_THROWS_AS(evaluate(make_fn(FunctionId::Asin, num(2))), EvalError);
    REQUIRE_THROWS_AS(evaluate(make_fn(FunctionId::Acos, x()), {{"x", -1.5}}), EvalError);
    // negative base with a non-integer exponent (includes even roots)
    REQUIRE_THROWS_AS(evaluate(make_sqrt(x()), {{"x", -1.0}}), EvalError);
    REQUIRE_THROWS_AS(evaluate(make_pow(x(), num(1, 3)), {{"x", -8.0}}), EvalError);
    REQUIRE_THROWS_AS(evaluate(make_pow(x(), y()), {{"x", -2.0}, {"y", 0.5}}), EvalError);
}

TEST_CASE("evaluate: non-finite results are rejected", "[evaluator][error]") {
    REQUIRE_THROWS_AS(evaluate(make_pow(num(10), x()), {{"x", 400.0}}), EvalError);
    REQUIRE_THROWS_AS(evaluate(make_fn(FunctionId::Cosh, x()), {{"x", 10000.0}}), EvalError);
    // A non-finite intermediate inside a larger expression is caught even
    // though the mathematical value of the whole sum would be 0.
    Expr f = make_add({make_pow(num(10), x()),
                       make_mul({num(-1), make_pow(num(10), x())})});
    REQUIRE(f->kind() == Kind::Add);
    REQUIRE_THROWS_AS(evaluate(f, {{"x", 400.0}}), EvalError);
}

// ---------------------------------------------------------------------------
// try_exact_numeric
// ---------------------------------------------------------------------------

TEST_CASE("try_exact_numeric: exact folds on shapes the factories keep", "[evaluator][exact]") {
    // Bare numbers pass through.
    CHECK(try_exact_numeric(num(7, 3)) == Rational(7, 3));
    // abs is exact on rationals and is not pre-folded by make_fn.
    CHECK(try_exact_numeric(make_fn(FunctionId::Abs, num(-3, 2))) == Rational(3, 2));
    CHECK(try_exact_numeric(make_fn(FunctionId::Abs, num(5))) == Rational(5));
    // Composite trees survive construction thanks to the Function subtree.
    Expr abs2 = make_fn(FunctionId::Abs, num(-2));
    CHECK(try_exact_numeric(make_add({num(1), abs2})) == Rational(3));
    CHECK(try_exact_numeric(make_mul({num(3, 4), abs2})) == Rational(3, 2));
    // Integer powers and exact rational roots.
    Expr abs4 = make_fn(FunctionId::Abs, num(-4));
    CHECK(try_exact_numeric(make_pow(abs4, num(1, 2))) == Rational(2));
    CHECK(try_exact_numeric(make_pow(abs2, num(-2))) == Rational(1, 4));
    Expr abs827 = make_fn(FunctionId::Abs, num(-8, 27));
    CHECK(try_exact_numeric(make_pow(abs827, num(1, 3))) == Rational(2, 3));
}

TEST_CASE("try_exact_numeric: symbolic and irrational inputs give nullopt",
          "[evaluator][exact]") {
    CHECK_FALSE(try_exact_numeric(x()).has_value());
    CHECK_FALSE(try_exact_numeric(make_const(ConstantId::Pi)).has_value());
    CHECK_FALSE(try_exact_numeric(make_const(ConstantId::E)).has_value());
    CHECK_FALSE(try_exact_numeric(make_add({x(), num(1)})).has_value());
    CHECK_FALSE(try_exact_numeric(make_pow(make_const(ConstantId::E), num(2))).has_value());
    // Irrational: sqrt(2) stays symbolic.
    CHECK_FALSE(try_exact_numeric(make_sqrt(num(2))).has_value());
    CHECK_FALSE(try_exact_numeric(make_pow(make_fn(FunctionId::Abs, num(2)),
                                           num(1, 2))).has_value());
    // Functions other than abs are never exact folds here (simplify's job).
    CHECK_FALSE(try_exact_numeric(make_fn(FunctionId::Sin, num(0))).has_value());
    CHECK_FALSE(try_exact_numeric(make_fn(FunctionId::Ln, num(1))).has_value());
    // A symbol buried deep in an otherwise numeric tree.
    Expr deep = make_add({num(1), make_mul({num(2), make_fn(FunctionId::Abs, x())})});
    CHECK_FALSE(try_exact_numeric(deep).has_value());
}

TEST_CASE("try_exact_numeric: large exact folds and division by zero",
          "[evaluator][exact]") {
    Expr abs1 = make_fn(FunctionId::Abs, num(1));
    Expr abs2 = make_fn(FunctionId::Abs, num(2));
    // Folds that used to overflow the 64-bit path are now exact.
    CHECK(try_exact_numeric(make_add({num(LLONG_MAX), abs1})) ==
          Rational(BigInt("9223372036854775808"))); // 2^63
    CHECK(try_exact_numeric(make_mul({num(LLONG_MAX), abs2})) ==
          Rational(BigInt("18446744073709551614"))); // 2^64 - 2
    CHECK(try_exact_numeric(make_pow(abs2, num(100))) ==
          Rational(BigInt("1267650600228229401496703205376"))); // 2^100
    CHECK(try_exact_numeric(make_fn(FunctionId::Abs, num(LLONG_MIN))) ==
          Rational(BigInt("9223372036854775808"))); // 2^63
    // 0^negative raises DivisionByZeroError inside the factory fold.
    Expr abs0 = make_fn(FunctionId::Abs, num(0));
    CHECK_FALSE(try_exact_numeric(make_pow(abs0, num(-1))).has_value());
    // Nothing above must have thrown out of try_exact_numeric (reaching this
    // line proves it), and a valid fold still works afterwards.
    CHECK(try_exact_numeric(make_add({num(1), abs1})) == Rational(2));
}

TEST_CASE("try_exact_numeric: n-ary folding avoids pairwise overflow", "[evaluator][exact]") {
    // Three thirds of LLONG_MAX sum to LLONG_MAX exactly: a binary Rational
    // chain would overflow on the 2*MAX/3 intermediate, but try_exact_numeric
    // folds through the factories' 128-bit n-ary accumulation. Two of the
    // terms are wrapped in abs so the Add survives construction unfolded.
    Expr third = num(LLONG_MAX, 3);
    Expr wrapped = make_fn(FunctionId::Abs, third);
    Expr sum = make_add({third, wrapped, make_fn(FunctionId::Abs, wrapped)});
    REQUIRE(sum->kind() == Kind::Add);
    auto folded = try_exact_numeric(sum);
    REQUIRE(folded.has_value());
    CHECK(*folded == Rational(LLONG_MAX));
}

TEST_CASE("evaluate: inverse hyperbolic values and domains") {
    const auto E = [](std::string_view s) {
        return evaluate(parse_expression(s), Bindings{});
    };
    CHECK(std::abs(E("asinh(1)") - std::asinh(1.0)) < 1e-15);
    CHECK(std::abs(E("acosh(2)") - std::acosh(2.0)) < 1e-15);
    CHECK(std::abs(E("atanh(1/2)") - std::atanh(0.5)) < 1e-15);
    CHECK_THROWS_AS(E("acosh(1/2)"), Error);
    CHECK_THROWS_AS(E("atanh(2)"), Error);
}

// ---------------------------------------------------------------------------
// evaluate_complex (complex domain, Phase 2)
// ---------------------------------------------------------------------------

TEST_CASE("evaluate_complex: arithmetic and Euler's formula", "[evaluator][complex]") {
    const auto C = [](std::string_view s) {
        return evaluate_complex(parse_expression(s));
    };
    const auto near = [](std::complex<double> got, std::complex<double> want) {
        return std::abs(got - want) < 1e-12;
    };
    CHECK(near(C("i^2"), {-1.0, 0.0}));
    CHECK(near(C("(2+3i)*(1-i)"), {5.0, 1.0}));
    CHECK(near(C("1/(1+i)"), {0.5, -0.5}));
    CHECK(near(C("(1+i)^8"), {16.0, 0.0}));
    // Euler: e^(i pi) = -1, e^(i pi/2) = i.
    CHECK(near(C("e^(i*pi)"), {-1.0, 0.0}));
    CHECK(near(C("e^(i*pi/2)"), {0.0, 1.0}));
    // abs is the modulus; cos(i) = cosh(1) is real.
    CHECK(near(C("abs(3+4i)"), {5.0, 0.0}));
    CHECK(near(C("cos(i)"), {std::cosh(1.0), 0.0}));
}

TEST_CASE("evaluate_complex: complex accessor functions", "[evaluator][complex]") {
    const auto C = [](std::string_view s) {
        return evaluate_complex(parse_expression(s));
    };
    const auto near = [](std::complex<double> got, std::complex<double> want) {
        return std::abs(got - want) < 1e-12;
    };
    CHECK(near(C("conj(2+3i)"), {2.0, -3.0}));
    CHECK(near(C("Re(2+3i)"), {2.0, 0.0}));
    CHECK(near(C("Im(2+3i)"), {3.0, 0.0}));
    CHECK(near(C("arg(i)"), {std::numbers::pi / 2.0, 0.0}));
    CHECK(near(C("arg(-1 + 0*i)"), {std::numbers::pi, 0.0}));
    // Real-domain accessors agree with the real evaluator.
    CHECK(evaluate(parse_expression("Re(x)"), {{"x", 4.0}}) == Approx(4.0));
    CHECK(evaluate(parse_expression("Im(x)"), {{"x", 4.0}}) == Approx(0.0));
    CHECK(evaluate(parse_expression("conj(x)"), {{"x", 4.0}}) == Approx(4.0));
    CHECK(evaluate(parse_expression("arg(x)"), {{"x", -2.0}}) == Approx(std::numbers::pi));
}

TEST_CASE("evaluate_complex: bindings and error surfaces", "[evaluator][complex]") {
    // A real binding participates in a complex expression.
    const Expr e = parse_expression("z*i");
    CHECK(std::abs(evaluate_complex(e, {{"z", {2.0, 0.0}}}) - std::complex<double>(0.0, 2.0)) <
          1e-12);
    // A genuinely complex binding.
    CHECK(std::abs(evaluate_complex(parse_expression("z^2"), {{"z", {0.0, 1.0}}}) -
                   std::complex<double>(-1.0, 0.0)) < 1e-12);
    // Unbound symbol still throws.
    CHECK_THROWS_AS(evaluate_complex(parse_expression("w + i")), EvalError);
    // The real evaluator is untouched: it still refuses i.
    CHECK_THROWS_AS(evaluate(parse_expression("1 + i")), Error);
}
