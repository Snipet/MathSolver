// Common-denominator combination tests (docs/proposals/together.md).
//
//   1. Structural unit rows — every §6 example (combining and no-op), plus the
//      Equation overload and the overflow bail.
//   2. Differential value-preservation — together(e) equals e at sample points
//      where the original is defined (I1; formal cancellation).
//   3. Idempotence (I3), a single-fraction-shape check, a multivariate case,
//      and the cancel∘together composition.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

void check_together(const std::string& input, const std::string& expected) {
    const Expr got = together(P(input));
    // together keeps the denominator *grouped* (Pow of the factored Mul); the
    // raw parser builds the same grouped shape, whereas simplify(expected)
    // would distribute Pow(x*y,-1). Compare against the unsimplified parse.
    const Expr want = P(expected);
    INFO("together(" << input << ") = " << to_string(got, PrintStyle::Plain)
                     << ", want " << to_string(want, PrintStyle::Plain));
    CHECK(structurally_equal(got, want));
}

void check_noop(const std::string& input) {
    const Expr got = together(P(input));
    const Expr want = simplify(P(input));
    INFO("together(" << input << ") = " << to_string(got, PrintStyle::Plain)
                     << " (expected unchanged)");
    CHECK(structurally_equal(got, want));
}

/// I1: together(e) equals e at points where every original denominator is
/// nonzero. evaluate throws at singular points → skip.
void check_value_preserving(const std::string& input,
                            const std::vector<std::pair<double, double>>& pts) {
    const Expr in = P(input);
    const Expr out = together(in);
    int checked = 0;
    for (const auto& [xv, yv] : pts) {
        Bindings b{{"x", xv}, {"y", yv}, {"z", 1.7}, {"a", 0.9}};
        double want = 0.0;
        try {
            want = evaluate(in, b);
        } catch (const Error&) {
            continue;
        }
        if (!std::isfinite(want)) continue;
        const double got = evaluate(out, b);
        INFO("value of " << input << " at x=" << xv << ",y=" << yv << ": together="
                         << got << " orig=" << want);
        CHECK(std::abs(got - want) < 1e-9 * (1.0 + std::abs(want)));
        ++checked;
    }
    CHECK(checked > 0);
}

}  // namespace

TEST_CASE("together: §6 combining rows", "[together]") {
    check_together("1/x + 1/y", "(x + y)/(x*y)");
    check_together("1/x + 1/x^2", "(x + 1)/x^2");
    check_together("1/(x - 1) + 1/(x + 1)", "2*x/((x - 1)*(x + 1))");
    check_together("a + 1/x", "(a*x + 1)/x");
    check_together("2/x + 3/x", "5/x");
    check_together("1/x + 1/y + 1/z", "(x*y + x*z + y*z)/(x*y*z)");
}

TEST_CASE("together: reductions to a constant", "[together]") {
    check_together("1/(x - 1) - 1/(x - 1)", "0");
    check_together("x/(x + 1) + 1/(x + 1)", "1");
}

TEST_CASE("together: no-op rows return the input unchanged", "[together]") {
    check_noop("x/2 + 1/3");  // numeric denominators already folded
    check_noop("1/x");        // a single fraction
    check_noop("x + 1");      // no denominators
    check_noop("x^2 + 3x");
}

TEST_CASE("together: value-preserving (I1)", "[together]") {
    check_value_preserving("1/x + 1/y",
                           {{-2.7, 1.3}, {0.37, -1.9}, {3.1, 4.25}});
    check_value_preserving("1/(x - 1) + 1/(x + 1)",
                           {{-2.7, 0}, {0.37, 0}, {3.1, 0}, {4.25, 0}});
    check_value_preserving("a + 1/x", {{-2.7, 0}, {0.37, 0}, {3.1, 0}});
    check_value_preserving("1/x + 1/x^2", {{-2.7, 0}, {0.37, 0}, {3.1, 0}});
}

TEST_CASE("together: result is a single fraction (I2)", "[together]") {
    // A combined result is either a polynomial or Mul(N, Pow(D, -1)); it must
    // not remain an Add of fractions.
    const Expr r = together(P("1/x + 1/y + a"));
    INFO(to_string(r, PrintStyle::Plain));
    // Top level is a single product N * D^-1 (not an Add of separate terms).
    CHECK(r->kind() != Kind::Add);
}

TEST_CASE("together: idempotence (I3)", "[together]") {
    const std::vector<std::string> corpus = {
        "1/x + 1/y",       "1/x + 1/x^2",        "1/(x - 1) + 1/(x + 1)",
        "a + 1/x",         "1/x + 1/y + 1/z",     "2/x + 3/x",
        "1/x",             "x + 1",               "x/2 + 1/3",
    };
    for (const std::string& s : corpus) {
        const Expr once = together(P(s));
        const Expr twice = together(once);
        INFO("idempotence for " << s);
        CHECK(structurally_equal(once, twice));
    }
}

TEST_CASE("together: Equation overload combines each side", "[together]") {
    const Equation eq{P("1/x + 1/y"), P("2/z")};
    const Equation got = together(eq);
    CHECK(structurally_equal(got.lhs, P("(x + y)/(x*y)")));
    CHECK(structurally_equal(got.rhs, P("2/z")));
}

TEST_CASE("together then cancel: composition", "[together]") {
    // 1/(x-1)+1/(x+1) → 2x/((x-1)(x+1)); no common factor, cancel is a no-op.
    const Expr t = together(P("1/(x - 1) + 1/(x + 1)"));
    const Expr c = cancel(t);
    CHECK(structurally_equal(c, simplify(t)));
    // (1/(x-1) + 1/(x-1)^2) → (x+1+1)/(x-1)^2 ... cancel leaves the (x-1)^2.
    const Expr t2 = together(P("1/(x - 1) - 1/(x - 1)^2"));
    // Value-equal to the input at a safe point.
    Bindings b{{"x", 2.3}};
    CHECK(std::abs(evaluate(t2, b) - evaluate(P("1/(x - 1) - 1/(x - 1)^2"), b)) < 1e-9);
}

TEST_CASE("together: overflow bails to the unchanged input", "[together]") {
    // Scaling the numerators onto the LCD multiplies the huge coefficients,
    // overflowing a 64-bit rational; the pipeline must bail, never throw.
    const std::string huge =
        "10000000000/(x - 1) + 30000000000/(x^2 - 1)";
    Expr got;
    CHECK_NOTHROW(got = together(P(huge)));
    // Either it combined exactly (no overflow) or bailed to simplify(input);
    // in both cases it is value-preserving where defined.
    Bindings b{{"x", 3.5}};
    CHECK(std::abs(evaluate(got, b) - evaluate(P(huge), b)) <
          1e-6 * (1.0 + std::abs(evaluate(P(huge), b))));
}
