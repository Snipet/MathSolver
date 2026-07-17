// Limit tests: classic exact limits, infinity reductions, divergence, and
// one-sided disagreement.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/limit.hpp"
#include "mathsolver/parser.hpp"

using namespace mathsolver;

namespace {

Expr P(const std::string& s) { return parse_expression(s); }

double val(const LimitResult& r) { return evaluate(r.value, Bindings{}); }

/// The limit exists (exactly or numerically) with the given value.
void check_limit(const std::string& f, const std::string& point, double want,
                 int dir = 0) {
    const LimitResult r = limit(P(f), "x", P(point), dir);
    INFO("limit of " << f << " at " << point << " (method " << r.method << ")");
    REQUIRE((r.status == LimitResult::Status::Exact ||
             r.status == LimitResult::Status::Numeric));
    CHECK(std::abs(val(r) - want) < 1e-5 * (1.0 + std::abs(want)));
}

void check_limit_inf(const std::string& f, bool positive, double want) {
    const LimitResult r = limit_at_infinity(P(f), "x", positive);
    INFO("limit of " << f << " at " << (positive ? "+inf" : "-inf")
                     << " (method " << r.method << ")");
    REQUIRE((r.status == LimitResult::Status::Exact ||
             r.status == LimitResult::Status::Numeric));
    CHECK(std::abs(val(r) - want) < 1e-5 * (1.0 + std::abs(want)));
}

} // namespace

TEST_CASE("limit: classic 0/0 forms are exact via L'Hopital") {
    struct Case {
        const char* f;
        const char* point;
        double want;
    };
    for (const Case& c : {Case{"sin(x)/x", "0", 1.0},
                          Case{"(1 - cos(x))/x^2", "0", 0.5},
                          Case{"(e^x - 1)/x", "0", 1.0},
                          Case{"(x^2 - 1)/(x - 1)", "1", 2.0},
                          Case{"tan(x)/x", "0", 1.0},
                          Case{"(x^3 - 8)/(x - 2)", "2", 12.0}}) {
        const LimitResult r = limit(P(c.f), "x", P(c.point), 0);
        INFO("limit of " << c.f << " at " << c.point);
        REQUIRE(r.status == LimitResult::Status::Exact);
        CHECK(std::abs(val(r) - c.want) < 1e-12);
    }
}

TEST_CASE("limit: continuous points are direct substitution") {
    const LimitResult r = limit(P("x^2 + 3x"), "x", P("2"), 0);
    REQUIRE(r.status == LimitResult::Status::Exact);
    CHECK(val(r) == 10.0);
    CHECK(r.method == "substitution");
}

TEST_CASE("limit: rational functions at infinity are exact") {
    const LimitResult r =
        limit_at_infinity(P("(3x^2 + x + 1)/(x^2 - 5)"), "x", true);
    REQUIRE(r.status == LimitResult::Status::Exact);
    CHECK(std::abs(val(r) - 3.0) < 1e-12);
    check_limit_inf("(2x + 1)/(x^3 + 1)", true, 0.0);
}

TEST_CASE("limit: transcendental limits at infinity") {
    check_limit_inf("ln(x)/x", true, 0.0);
    check_limit_inf("(1 + 1/x)^x", true, std::exp(1.0));
    check_limit_inf("e^(-x)", true, 0.0);
}

TEST_CASE("limit: one-sided limits and blow-ups") {
    check_limit("x*ln(x)", "0", 0.0, +1);
    const LimitResult up = limit(P("1/x"), "x", P("0"), +1);
    CHECK(up.status == LimitResult::Status::Diverges);
    CHECK(up.sign == +1);
    const LimitResult down = limit(P("1/x"), "x", P("0"), -1);
    CHECK(down.status == LimitResult::Status::Diverges);
    CHECK(down.sign == -1);
}

TEST_CASE("limit: two-sided disagreement is reported, not averaged") {
    const LimitResult r = limit(P("1/x"), "x", P("0"), 0);
    CHECK(r.status == LimitResult::Status::DoesNotExist);
    REQUIRE(r.warnings.size() == 2);
    const LimitResult r2 = limit(P("abs(x)/x"), "x", P("0"), 0);
    CHECK(r2.status == LimitResult::Status::DoesNotExist);
}

TEST_CASE("limit: polynomial divergence at infinity keeps its sign") {
    const LimitResult up = limit_at_infinity(P("x^2 + 1"), "x", true);
    CHECK(up.status == LimitResult::Status::Diverges);
    CHECK(up.sign == +1);
    const LimitResult down = limit_at_infinity(P("x^3"), "x", false);
    CHECK(down.status == LimitResult::Status::Diverges);
    CHECK(down.sign == -1);
}

TEST_CASE("limit: errors") {
    CHECK_THROWS_AS(limit(P("x"), "", P("0"), 0), Error);
    CHECK_THROWS_AS(limit(P("x"), "x", P("x + 1"), 0), Error);
}
