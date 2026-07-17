// Regression tests for the SIMPLIFY-FIXES review stage.
// Each test uses the exact reproducer from its finding.

#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;
using Catch::Matchers::WithinRel;

namespace {

Expr parse(std::string_view s) {
    return parse_expression(s);
}

// Does `e` contain a Function node with the given id anywhere?
bool contains_function(const Expr& e, FunctionId id) {
    if (e->kind() == Kind::Function && e->function() == id) {
        return true;
    }
    for (const Expr& a : e->args()) {
        if (contains_function(a, id)) {
            return true;
        }
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// FINDING 3: unchecked degree accumulation in polynomial_term overflowed
// signed int64 and wrapped negative, misclassifying a nonzero polynomial as
// the zero polynomial ({0}) and letting collect rewrite it.
// ---------------------------------------------------------------------------

TEST_CASE("polynomial_coefficients: huge x-power does not overflow to {0}") {
    // x^2 * x^9223372036854775807 -- the second exponent is INT64_MAX, so the
    // degree sum wraps signed int64 without the pre-add guard.
    const Expr in = parse("x^2 * x^9223372036854775807");
    const auto pc = polynomial_coefficients(in, "x");
    // The exact degree is unrepresentable, so the honest answer is nullopt --
    // NOT the reserved zero-polynomial value {0}.
    CHECK_FALSE(pc.has_value());
}

TEST_CASE("collect: huge x-power is left as an unclassifiable remainder") {
    const Expr in = parse("x^2 * x^9223372036854775807");
    // collect must not rewrite it to a different expression; it stays the
    // simplified input.
    CHECK(structurally_equal(collect(in, "x"), simplify(in)));
}

TEST_CASE("polynomial_coefficients: degrees at/above the cap") {
    // A degree exactly at the cap is still a valid polynomial position; one
    // past the cap bails to nullopt (and never overflows).
    const auto ok = polynomial_coefficients(parse("x^1000000"), "x");
    REQUIRE(ok.has_value());
    CHECK(ok->size() == 1000001);
    CHECK_FALSE(polynomial_coefficients(parse("x^1000001"), "x").has_value());
}

TEST_CASE("polynomial_coefficients: ordinary polynomials still work") {
    const auto pc = polynomial_coefficients(parse("(x+1)^3"), "x");
    REQUIRE(pc.has_value());
    REQUIRE(pc->size() == 4);
    CHECK(structurally_equal((*pc)[0], parse("1")));
    CHECK(structurally_equal((*pc)[3], parse("1")));
}

// ---------------------------------------------------------------------------
// FINDING 4: apply_pythagorean registered only the FIRST sin^2/cos^2 factor
// per term, so k*sin^2 + k*cos^2 -> k was missed order-dependently when k
// itself carried a sin^2 factor.
// ---------------------------------------------------------------------------

TEST_CASE("pythagorean: coefficient carrying a sin^2 factor still folds") {
    // sin(y)^2 * (sin(x)^2 + cos(x)^2) = sin(y)^2.
    const Expr out =
        simplify(parse("sin(y)^2*sin(x)^2 + sin(y)^2*cos(x)^2"));
    INFO(debug_string(out));
    CHECK(structurally_equal(out, parse("sin(y)^2")));
}

TEST_CASE("pythagorean: cos-coefficient mirror also folds") {
    const Expr out =
        simplify(parse("cos(y)^2*sin(x)^2 + cos(y)^2*cos(x)^2"));
    INFO(debug_string(out));
    CHECK(structurally_equal(out, parse("cos(y)^2")));
}

TEST_CASE("pythagorean: plain identity is unaffected") {
    CHECK(structurally_equal(simplify(parse("sin(x)^2 + cos(x)^2")), parse("1")));
}

// ---------------------------------------------------------------------------
// FINDING A: guarded() rolled the ENTIRE node back on an overflowing number
// fold, discarding the children's completed simplifications -- so ln(e) never
// folded and the node was stuck non-confluent.
// ---------------------------------------------------------------------------

TEST_CASE("overflow fallback: inner ln(e) still folds under a bignum product") {
    const Expr out = simplify(parse("4000000000*(3000000000+ln(e))"));
    INFO(debug_string(out));
    // ln(e) -> 1 must have fired (absorbed into 3000000001); the product
    // itself is unrepresentable so it stays a factored product.
    CHECK_FALSE(contains_function(out, FunctionId::Ln));
    // idempotent: a second pass reproduces the same node.
    CHECK(structurally_equal(simplify(out), out));
}

TEST_CASE("overflow fallback: differing association folds identically") {
    const Expr a = simplify(parse("4000000000*(3000000000+ln(e))"));
    const Expr b = simplify(parse("(ln(e)+3000000000)*4000000000"));
    INFO(debug_string(a));
    INFO(debug_string(b));
    CHECK_FALSE(contains_function(b, FunctionId::Ln));
    CHECK(structurally_equal(a, b));
}

TEST_CASE("overflow fallback: control case folds fully") {
    // Same shape, no overflow -> a plain number.
    CHECK(structurally_equal(simplify(parse("3*(3000000000+ln(e))")),
                             parse("9000000003")));
}

// ---------------------------------------------------------------------------
// FINDING B: expand() was O(raw*distinct); (a+..+j)^7 took ~448 s. Collect
// during distribution / hash-bucketed grouping makes it near-linear. Results
// must be unchanged; the wall-clock bounds are deliberately generous for CI.
// ---------------------------------------------------------------------------

TEST_CASE("expand perf: (x+y)^20 completes, is correct") {
    const Expr in = parse("(x+y)^20");
    const auto t0 = std::chrono::steady_clock::now();
    const Expr out = expand(in);
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE(out->kind() == Kind::Add);
    CHECK(out->args().size() == 21); // 21 distinct monomials
    // Value preservation: expansion agrees with the unexpanded form.
    const Bindings b{{"x", 2.0}, {"y", 3.0}};
    CHECK_THAT(evaluate(out, b), WithinRel(evaluate(in, b), 1e-9));
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 30);
}

TEST_CASE("expand perf: (a+..+j)^7 has exactly 11440 terms, completes fast") {
    // NB: the `e` here parses as Euler's constant, not a free symbol -- still
    // 10 distinct atoms, so the monomial count is unchanged. (`i` is the
    // imaginary unit as of v0.6, so `k` stands in for it.)
    const Expr in = parse("(a+b+c+d+e+f+g+h+k+j)^7");
    const auto t0 = std::chrono::steady_clock::now();
    const Expr out = expand(in);
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE(out->kind() == Kind::Add);
    CHECK(out->args().size() == 11440); // C(16, 9)
    // Value preservation vs the unexpanded form.
    const Bindings b{{"a", 1.5}, {"b", 0.5}, {"c", 2.0}, {"d", 1.0}, {"f", 0.25},
                     {"g", 3.0}, {"h", 0.75}, {"k", 1.25}, {"j", 2.5}};
    CHECK_THAT(evaluate(out, b), WithinRel(evaluate(in, b), 1e-9));
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 30);
}

// ---------------------------------------------------------------------------
// AMENDED DESIGN.md section 7: generalized (u^a)^b power rule for rational
// Number exponents (non-integer outer b; integer b is a factory fold).
//   - a non-integer or an odd integer      -> u^(a*b)
//   - a even integer, a*b an even integer  -> u^(a*b)
//   - a even integer, a*b an odd integer   -> abs(u)^(a*b)
//   - a even integer, a*b not an integer   -> unchanged (domain restriction)
// On Rational overflow of a*b the node is left unchanged.
// ---------------------------------------------------------------------------

#include "mathsolver/errors.hpp"

namespace {

void expect_folds(std::string_view input, std::string_view expected) {
    const Expr out = simplify(parse(input));
    INFO(input << " simplified to " << debug_string(out));
    CHECK(structurally_equal(out, parse(expected)));
    // Idempotence on the new output.
    CHECK(structurally_equal(simplify(out), out));
}

void expect_unchanged_shape(std::string_view input) {
    const Expr in = parse(input);
    const Expr out = simplify(in);
    INFO(input << " simplified to " << debug_string(out));
    CHECK(structurally_equal(out, in));
}

struct EvalOutcome {
    bool defined;
    double value;
};

EvalOutcome try_eval(const Expr& e, double x) {
    try {
        return {true, evaluate(e, Bindings{{"x", x}})};
    } catch (const EvalError&) {
        return {false, 0.0};
    }
}

// Section 7 doctrine: the fold may EXTEND the real domain (EvalError on the
// original only) but must never restrict it (EvalError on the simplified
// form where the original evaluates is a failure), and values must agree
// wherever both sides are defined.
void expect_value_preserving(std::string_view input) {
    const Expr in = parse(input);
    const Expr out = simplify(in);
    REQUIRE_FALSE(structurally_equal(out, in)); // the rule must have fired
    for (const double x : {2.3, -1.7, 0.5}) {
        const EvalOutcome orig = try_eval(in, x);
        const EvalOutcome simp = try_eval(out, x);
        INFO(input << " -> " << debug_string(out) << " at x = " << x);
        if (orig.defined) {
            REQUIRE(simp.defined); // domain restriction: forbidden
            CHECK_THAT(simp.value, WithinRel(orig.value, 1e-9));
        }
        // orig undefined: a defined simplified value is an acceptable
        // domain extension; both undefined is trivially fine.
    }
}

} // namespace

TEST_CASE("generalized power rule: a non-integer or odd integer folds") {
    expect_folds("(x^(1/3))^(1/5)", "x^(1/15)");
    expect_folds("(x^3)^(1/2)", "x^(3/2)");
    expect_folds("(x^(2/3))^(3/2)", "x");
    expect_folds("(x^(1/2))^(1/3)", "x^(1/6)"); // even q: now folds
    expect_folds("(x^(2/3))^(1/2)", "x^(1/3)"); // even p, q != 1: now folds
}

TEST_CASE("generalized power rule: even a with even integer a*b folds plain") {
    expect_folds("(x^6)^(1/3)", "x^2");
    expect_folds("(x^4)^(1/2)", "x^2");
}

TEST_CASE("generalized power rule: even a with odd integer a*b folds to abs") {
    expect_folds("(x^2)^(1/2)", "abs(x)");
    expect_folds("(x^6)^(1/2)", "abs(x)^3");
    expect_folds("(x^(-2))^(1/2)", "abs(x)^(-1)");
}

TEST_CASE("generalized power rule: old odd/odd parity cases still fold") {
    expect_folds("(x^(1/3))^(5/3)", "x^(5/9)");
    expect_folds("(x^3)^(1/3)", "x");
    expect_folds("(x^(3/5))^(1/3)", "x^(1/5)");
    expect_folds("(x^(-1/3))^(1/3)", "x^(-1/9)");
}

TEST_CASE("generalized power rule: sqrt(x^2) -> abs(x) still holds") {
    expect_folds("sqrt(x^2)", "abs(x)");
    // and never plain x
    CHECK_FALSE(structurally_equal(simplify(parse("sqrt(x^2)")), parse("x")));
}

TEST_CASE("generalized power rule: guards") {
    // even a, a*b not an integer: folding would restrict the real domain.
    expect_unchanged_shape("(x^2)^(1/3)");
    expect_unchanged_shape("(x^2)^(1/4)");
    expect_unchanged_shape("(x^4)^(1/8)");
}

TEST_CASE("generalized power rule: a*b overflow leaves the node unchanged") {
    // a = 1/INT64_MAX, b = 1/7: the product's denominator does not fit
    // 64 bits, so the checked Rational multiply throws and the rule bails.
    expect_unchanged_shape("(x^(1/9223372036854775807))^(1/7)");
}

TEST_CASE("generalized power rule: value preservation at sample points") {
    for (const std::string_view input :
         {"(x^(1/3))^(1/5)", "(x^3)^(1/2)", "(x^(2/3))^(3/2)",
          "(x^(1/2))^(1/3)", "(x^(2/3))^(1/2)", "(x^6)^(1/3)", "(x^4)^(1/2)",
          "(x^2)^(1/2)", "(x^6)^(1/2)", "(x^(1/3))^(5/3)", "(x^3)^(1/3)",
          "sqrt(x^2)"}) {
        expect_value_preserving(input);
    }
}

// ---------------------------------------------------------------------------
// abs(u) -> u for structurally provably nonnegative u (DESIGN §7).
// ---------------------------------------------------------------------------

TEST_CASE("simplify: abs of provably nonnegative expressions unwraps") {
    CHECK(structurally_equal(simplify(parse_expression("abs(e^2)")),
                             parse_expression("e^2")));
    CHECK(structurally_equal(simplify(parse_expression("abs(e^x)")),
                             parse_expression("e^x")));
    CHECK(structurally_equal(simplify(parse_expression("abs(x^2)")),
                             parse_expression("x^2")));
    CHECK(structurally_equal(simplify(parse_expression("abs(sqrt(x))")),
                             parse_expression("sqrt(x)")));
    CHECK(structurally_equal(simplify(parse_expression("abs(x^2 + 1)")),
                             parse_expression("x^2 + 1")));
    CHECK(structurally_equal(simplify(parse_expression("abs(cosh(x))")),
                             parse_expression("cosh(x)")));
    // The motivating end-to-end case: ln(abs(e^2)) -> ln(e^2) -> 2.
    CHECK(structurally_equal(simplify(parse_expression("ln(abs(e^2))")),
                             parse_expression("2")));
}

TEST_CASE("simplify: abs stays when nonnegativity is not provable") {
    CHECK(structurally_equal(simplify(parse_expression("abs(x)")),
                             parse_expression("abs(x)")));
    CHECK(structurally_equal(simplify(parse_expression("abs(sin(x))")),
                             parse_expression("abs(sin(x))")));
    CHECK(structurally_equal(simplify(parse_expression("abs(x^3)")),
                             parse_expression("abs(x^3)")));
    CHECK(structurally_equal(simplify(parse_expression("abs(x - 1)")),
                             parse_expression("abs(x - 1)")));
}

// ---------------------------------------------------------------------------
// v0.4: ln(a)/ln(b) folds to the exact rational log when one exists.
// ---------------------------------------------------------------------------

TEST_CASE("simplify: exact rational logarithm folding") {
    CHECK(structurally_equal(simplify(parse_expression("ln(8)/ln(2)")),
                             parse_expression("3")));
    CHECK(structurally_equal(simplify(parse_expression("\\log_2(8)")),
                             parse_expression("3")));
    CHECK(structurally_equal(simplify(parse_expression("\\log_4(8)")),
                             parse_expression("3/2")));
    CHECK(structurally_equal(simplify(parse_expression("\\log_9(27)")),
                             parse_expression("3/2")));
    CHECK(structurally_equal(simplify(parse_expression("ln(1/8)/ln(2)")),
                             parse_expression("-3")));
    CHECK(structurally_equal(simplify(parse_expression("ln(8/27)/ln(2/3)")),
                             parse_expression("3")));
    // With a symbolic cofactor the fold still applies to the ln pair.
    CHECK(structurally_equal(simplify(parse_expression("x*ln(8)/ln(2)")),
                             parse_expression("3*x")));
    // No exact log: unchanged (and idempotent).
    const Expr stays = simplify(parse_expression("ln(5)/ln(3)"));
    CHECK(structurally_equal(stays, parse_expression("ln(5)/ln(3)")));
    CHECK(structurally_equal(simplify(stays), stays));
}

// ---------------------------------------------------------------------------
// v0.4: radical normal form (coefficient * b^f, f in (0,1); square-free
// radicands with rationalized denominators for square roots).
// ---------------------------------------------------------------------------

TEST_CASE("simplify: radical normal form") {
    CHECK(structurally_equal(simplify(parse_expression("sqrt(8)")),
                             parse_expression("2*sqrt(2)")));
    CHECK(structurally_equal(simplify(parse_expression("sqrt(12)")),
                             parse_expression("2*sqrt(3)")));
    CHECK(structurally_equal(simplify(parse_expression("1/sqrt(2)")),
                             parse_expression("sqrt(2)/2")));
    CHECK(structurally_equal(simplify(parse_expression("sqrt(9/2)")),
                             parse_expression("(3/2)*sqrt(2)")));
    CHECK(structurally_equal(simplify(parse_expression("2^(3/2)")),
                             parse_expression("2*sqrt(2)")));
    CHECK(structurally_equal(simplify(parse_expression("8^(5/2)")),
                             parse_expression("128*sqrt(2)")));
    CHECK(structurally_equal(simplify(parse_expression("sqrt(45)")),
                             parse_expression("3*sqrt(5)")));
    // Guards: already-normal forms stay put; non-sqrt fractional exponents
    // only get integer-part extraction; negative bases untouched.
    const Expr sqrt2 = simplify(parse_expression("sqrt(2)"));
    CHECK(structurally_equal(sqrt2, parse_expression("sqrt(2)")));
    CHECK(structurally_equal(simplify(parse_expression("2^(7/6)")),
                             parse_expression("2*2^(1/6)")));
    const Expr cbrt16 = simplify(parse_expression("16^(1/3)"));
    CHECK(structurally_equal(cbrt16, parse_expression("16^(1/3)")));
}

TEST_CASE("simplify: radical normal form is confluent with like-base combining") {
    // 2*sqrt(2) must NOT re-merge to 2^(3/2) (the roadmap's confluence trap):
    // idempotence proves the fixpoint is stable.
    const Expr once = simplify(parse_expression("sqrt(8)"));
    CHECK(structurally_equal(simplify(once), once));
    // Radical parts still combine among themselves...
    CHECK(structurally_equal(simplify(parse_expression("2^(1/2) * 2^(1/3)")),
                             parse_expression("2^(5/6)")));
    // ...and sqrt(2)*sqrt(2) collapses fully.
    CHECK(structurally_equal(simplify(parse_expression("sqrt(2)*sqrt(2)")),
                             parse_expression("2")));
    // The roadmap probe: nested radical input keeps its 2*sqrt(2) inside.
    CHECK(structurally_equal(simplify(parse_expression("sqrt(3 + 2*sqrt(2))")),
                             parse_expression("sqrt(2*sqrt(2) + 3)")));
    // Value preservation spot checks.
    CHECK(evaluate(simplify(parse_expression("sqrt(8)"))) ==
          Catch::Approx(std::sqrt(8.0)).epsilon(1e-12));
    CHECK(evaluate(simplify(parse_expression("1/sqrt(2)"))) ==
          Catch::Approx(1.0 / std::sqrt(2.0)).epsilon(1e-12));
}
