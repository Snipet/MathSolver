// logexpand / logcombine tests: the product/quotient/power log identities,
// verified numerically at positive sample points (where the identities hold)
// and structurally, plus the round-trip between the two.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <string>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/logexpand.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"

using namespace mathsolver;

namespace {

std::string plain(const Expr& e) { return to_string(e, PrintStyle::Plain); }

// Two expressions agree at several positive assignments of the free variables.
bool agrees(const Expr& a, const Expr& b) {
    for (double x : {0.3, 1.7, 2.9}) {
        for (double y : {0.5, 1.2, 3.1}) {
            const Bindings bnd{{"x", x}, {"y", y}, {"a", x}, {"b", y}, {"n", 3.0}, {"z", 0.8}};
            if (std::abs(evaluate(a, bnd) - evaluate(b, bnd)) > 1e-9) return false;
        }
    }
    return true;
}

bool exp_ok(const std::string& s) {
    return agrees(parse_expression(s), log_expand(parse_expression(s)));
}
bool comb_ok(const std::string& s) {
    return agrees(parse_expression(s), log_combine(parse_expression(s)));
}
bool structurally_equal(const Expr& got, const std::string& expected) {
    return plain(simplify(got)) == plain(simplify(parse_expression(expected)));
}

} // namespace

TEST_CASE("logexpand: products, quotients, powers") {
    CHECK(exp_ok("ln(x*y)"));
    CHECK(exp_ok("ln(x/y)"));
    CHECK(exp_ok("ln(x^3)"));
    CHECK(exp_ok("ln(x^2*y)"));
    CHECK(exp_ok("ln(2*x)"));
    CHECK(exp_ok("ln(x*y*a)"));

    CHECK(structurally_equal(log_expand(parse_expression("ln(x*y)")), "ln(x) + ln(y)"));
    CHECK(structurally_equal(log_expand(parse_expression("ln(x^3)")), "3*ln(x)"));
    CHECK(structurally_equal(log_expand(parse_expression("ln(x/y)")), "ln(x) - ln(y)"));
}

TEST_CASE("logexpand leaves atoms and non-logs alone") {
    CHECK(plain(log_expand(parse_expression("ln(x)"))) == "ln(x)");
    CHECK(plain(log_expand(parse_expression("x^2 + 1"))) == "x^2 + 1");
    // ln of a bare number is atomic (not decomposed into prime logs).
    CHECK(plain(log_expand(parse_expression("ln(5)"))) == "ln(5)");
}

TEST_CASE("logcombine: sums and multiples into one log") {
    CHECK(comb_ok("ln(x) + ln(y)"));
    CHECK(comb_ok("ln(x) - ln(y)"));
    CHECK(comb_ok("3*ln(x)"));
    CHECK(comb_ok("2*ln(x) + ln(y)"));
    CHECK(comb_ok("ln(x) + ln(y) - ln(a)"));

    CHECK(structurally_equal(log_combine(parse_expression("ln(x) + ln(y)")), "ln(x*y)"));
    CHECK(structurally_equal(log_combine(parse_expression("3*ln(x)")), "ln(x^3)"));
    CHECK(structurally_equal(log_combine(parse_expression("ln(x) - ln(y)")), "ln(x/y)"));
    // A non-log term rides along unchanged.
    CHECK(agrees(parse_expression("ln(x) + ln(y) + z"),
                 log_combine(parse_expression("ln(x) + ln(y) + z"))));
}

TEST_CASE("logexpand and logcombine round-trip") {
    for (const std::string& s : {"ln(x*y)", "ln(x^2/y)", "ln(x*y^3)"}) {
        // combine ∘ expand returns to a form equal to the original.
        const Expr back = log_combine(log_expand(parse_expression(s)));
        CHECK(agrees(parse_expression(s), back));
    }
}
