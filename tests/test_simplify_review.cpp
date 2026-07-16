// Regression tests for the SIMPLIFY-FIXES review stage.
// Each test uses the exact reproducer from its finding.

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

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
    // 10 distinct atoms, so the monomial count is unchanged.
    const Expr in = parse("(a+b+c+d+e+f+g+h+i+j)^7");
    const auto t0 = std::chrono::steady_clock::now();
    const Expr out = expand(in);
    const auto elapsed = std::chrono::steady_clock::now() - t0;
    REQUIRE(out->kind() == Kind::Add);
    CHECK(out->args().size() == 11440); // C(16, 9)
    // Value preservation vs the unexpanded form.
    const Bindings b{{"a", 1.5}, {"b", 0.5}, {"c", 2.0}, {"d", 1.0}, {"f", 0.25},
                     {"g", 3.0}, {"h", 0.75}, {"i", 1.25}, {"j", 2.5}};
    CHECK_THAT(evaluate(out, b), WithinRel(evaluate(in, b), 1e-9));
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 30);
}
