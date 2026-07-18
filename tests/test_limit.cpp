// Limit tests: classic exact limits, infinity reductions, divergence, and
// one-sided disagreement.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <string>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/limit.hpp"
#include "mathsolver/parser.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

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

// ---------------------------------------------------------------------------
// Regressions from the adversarial review (each was a confirmed wrong answer)
// ---------------------------------------------------------------------------

TEST_CASE("limit review: cancellation plateaus must not fake convergence") {
    // (1-cos x)/x^4 ~ 1/(2x^2) -> +inf; the numeric tail collapses to
    // exact 0 in doubles and previously returned Numeric 0.
    const LimitResult r = limit(P("(1 - cos(x))/x^4"), "x", P("0"), 0);
    CHECK(r.status == LimitResult::Status::Diverges);
    CHECK(r.sign == +1);
    const LimitResult r2 = limit(P("(x - sin(x))/x^5"), "x", P("0"), +1);
    CHECK(r2.status == LimitResult::Status::Diverges);
    CHECK(r2.sign == +1);
}

TEST_CASE("limit review: parameters must not disable the guards") {
    // a·abs(x)/x at 0 previously returned Exact 0 for every direction.
    const LimitResult two = limit(P("a*abs(x)/x"), "x", P("0"), 0);
    CHECK(two.status == LimitResult::Status::DoesNotExist);
    // sin(a x)/x -> a must still work with the parameter present.
    const LimitResult ok = limit(P("sin(a*x)/x"), "x", P("0"), 0);
    REQUIRE(ok.status == LimitResult::Status::Exact);
    CHECK(std::abs(evaluate(ok.value, Bindings{{"a", 2.5}}) - 2.5) < 1e-12);
}

TEST_CASE("limit review: functions defined at the point trust substitution") {
    // Boundary-layer rational: continuous at 0 with value 0; the probe used
    // to veto the correct answer and numerics locked onto the mesa at ~1.
    const LimitResult r = limit(P("x/(x + 10^(-15))"), "x", P("0"), 0);
    REQUIRE(r.status == LimitResult::Status::Exact);
    CHECK(std::abs(evaluate(r.value, Bindings{})) < 1e-30);
    // Steep but continuous polynomial: exactly 10, not 10.000001.
    const LimitResult p = limit(P("10 + 1000000*x"), "x", P("0"), 0);
    REQUIRE(p.status == LimitResult::Status::Exact);
    CHECK(evaluate(p.value, Bindings{}) == 10.0);
    // Pole just past the deepest sample: f(0) = -1e13 exactly.
    const LimitResult q = limit(P("1/(x - 10^(-13))"), "x", P("0"), 0);
    REQUIRE(q.status == LimitResult::Status::Exact);
    CHECK(std::abs(evaluate(q.value, Bindings{}) + 1e13) < 1.0);
}

TEST_CASE("limit review: small-magnitude jumps still veto substitution") {
    // abs(x)/(10000 x) at 0+ is 1/10000; the absolute-tolerance probe used
    // to accept the swallowed 0.
    const LimitResult r = limit(P("abs(x)/(10000*x)"), "x", P("0"), +1);
    REQUIRE((r.status == LimitResult::Status::Exact ||
             r.status == LimitResult::Status::Numeric));
    CHECK(std::abs(evaluate(r.value, Bindings{}) - 1e-4) < 1e-9);
}

TEST_CASE("limit review: unsigned growth is not reported as infinity") {
    // x sin x is unbounded but oscillates through 0: no limit, and
    // definitely not "inf (unsigned)". Unsolved or DoesNotExist are honest.
    const LimitResult r = limit_at_infinity(P("x*sin(x)"), "x", true);
    CHECK(r.status != LimitResult::Status::Diverges);
    CHECK(r.status != LimitResult::Status::Exact);
    CHECK(r.status != LimitResult::Status::Numeric);
}

TEST_CASE("limit review: undefined substitution values are rejected") {
    // ln(x) at 0+ previously returned Exact "ln(0)". -inf (Diverges) or
    // Unsolved are acceptable; a definite finite/Exact answer is not.
    const LimitResult r = limit(P("ln(x)"), "x", P("0"), +1);
    CHECK(r.status != LimitResult::Status::Exact);
    CHECK(r.status != LimitResult::Status::Numeric);
}

TEST_CASE("limit: errors") {
    CHECK_THROWS_AS(limit(P("x"), "", P("0"), 0), Error);
    CHECK_THROWS_AS(limit(P("x"), "x", P("x + 1"), 0), Error);
}

// ---------------------------------------------------------------------------
// Multivariate limits (path sampling)
// ---------------------------------------------------------------------------

TEST_CASE("mlimit: path disagreement proves nonexistence") {
    // xy/(x^2+y^2): 0 along the axes, 1/2 along y = x.
    const LimitResult r =
        limit_multi(P("x*y/(x^2 + y^2)"), "x", P("0"), "y", P("0"));
    CHECK(r.status == LimitResult::Status::DoesNotExist);
    REQUIRE(r.warnings.size() == 2);
    // (x^2 - y^2)/(x^2 + y^2): +1 and -1 along the two axes.
    CHECK(limit_multi(P("(x^2 - y^2)/(x^2 + y^2)"), "x", P("0"), "y", P("0"))
              .status == LimitResult::Status::DoesNotExist);
}

TEST_CASE("mlimit: the parabolic path catches xy^2/(x^2+y^4)") {
    // Every straight line gives 0, but x = y^2 gives 1/2 — the classic.
    const LimitResult r =
        limit_multi(P("x*y^2/(x^2 + y^4)"), "x", P("0"), "y", P("0"));
    CHECK(r.status == LimitResult::Status::DoesNotExist);
}

TEST_CASE("mlimit: agreement returns the value with a caveat") {
    const LimitResult r =
        limit_multi(P("x^2*y/(x^2 + y^2)"), "x", P("0"), "y", P("0"));
    REQUIRE((r.status == LimitResult::Status::Exact ||
             r.status == LimitResult::Status::Numeric));
    CHECK(std::abs(evaluate(r.value, Bindings{})) < 1e-9);
    CHECK(!r.warnings.empty()); // the not-a-proof caveat
    // Continuous point: exact.
    const LimitResult c =
        limit_multi(P("x + 2*y"), "x", P("1"), "y", P("2"));
    REQUIRE(c.status == LimitResult::Status::Exact);
    CHECK(evaluate(c.value, Bindings{}) == 5.0);
}

TEST_CASE("mlimit: errors") {
    CHECK_THROWS_AS(limit_multi(P("x"), "x", P("0"), "x", P("0")), Error);
    CHECK_THROWS_WITH(limit_multi(P("x*t"), "x", P("0"), "y", P("0")),
                      ContainsSubstring("path parameter"));
}
