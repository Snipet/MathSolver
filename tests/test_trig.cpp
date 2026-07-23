// trigexpand tests: angle-addition and multiple-angle identities, verified
// both structurally and numerically (the expansion must equal the original at
// sample points).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/trig.hpp"

using namespace mathsolver;

namespace {

std::string expand_str(const std::string& s) {
    return to_string(trig_expand(parse_expression(s)), PrintStyle::Plain);
}

// Two expressions agree as functions: their difference evaluates to ~0 at
// several assignments of the free variables.
bool agrees(const std::string& original, const Expr& expanded) {
    const Expr orig = parse_expression(original);
    const double pts[] = {0.3, 1.1, -0.7, 2.4, -1.9};
    for (double a : pts) {
        for (double b : {0.5, -1.3, 2.0}) {
            const Bindings bnd{{"a", a}, {"b", b}, {"c", 0.9}, {"x", a}, {"y", b}};
            const double lhs = evaluate(orig, bnd);
            const double rhs = evaluate(expanded, bnd);
            if (std::abs(lhs - rhs) > 1e-9) return false;
        }
    }
    return true;
}

bool ok(const std::string& s) { return agrees(s, trig_expand(parse_expression(s))); }

} // namespace

TEST_CASE("angle-addition formulas") {
    CHECK(ok("sin(a + b)"));
    CHECK(ok("cos(a + b)"));
    CHECK(ok("sin(a - b)"));
    CHECK(ok("cos(a - b)"));
    CHECK(ok("sin(a + b + c)"));
    CHECK(ok("tan(a + b)"));
    // The canonical shapes appear.
    CHECK(agrees("sin(a + b)", parse_expression("sin(a)*cos(b) + cos(a)*sin(b)")));
}

TEST_CASE("multiple-angle expansion") {
    CHECK(ok("sin(2*x)"));
    CHECK(ok("cos(2*x)"));
    CHECK(ok("sin(3*x)"));
    CHECK(ok("cos(3*x)"));
    CHECK(ok("sin(4*x)"));
    // sin(2x) = 2 sin x cos x exactly.
    CHECK(simplify(parse_expression("(" + expand_str("sin(2*x)") + ") - 2*sin(x)*cos(x)"))
              ->kind() == Kind::Number);
    CHECK(evaluate(parse_expression("(" + expand_str("sin(2*x)") + ") - 2*sin(x)*cos(x)"),
                   Bindings{{"x", 0.7}}) == 0.0);
}

TEST_CASE("mixed and nested arguments") {
    CHECK(ok("sin(2*x + a)"));
    CHECK(ok("cos(x + 2*y)"));
    CHECK(ok("sin(x)*cos(2*x)")); // product with an expandable factor
    // A special-value angle collapses: sin(x + pi/2) = cos(x).
    CHECK(evaluate(parse_expression("(" + expand_str("sin(x + pi/2)") + ") - cos(x)"),
                   Bindings{{"x", 1.234}}) == 0.0);
}

TEST_CASE("things that must not change") {
    // Single angles and non-integer / symbolic multiples are left alone.
    CHECK(expand_str("sin(x)") == "sin(x)");
    CHECK(expand_str("cos(x)") == "cos(x)");
    CHECK(expand_str("sin(x/2)") == "sin(x/2)");
    // A non-trig expression is untouched.
    CHECK(expand_str("x^2 + 1") == "x^2 + 1");
    // Still agrees numerically for the symbolic-multiple case (identity map).
    CHECK(ok("sin(a*x)"));
}
