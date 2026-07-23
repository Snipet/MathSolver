// Summary-statistics (stats) tests.
//
// Exactness is the point: rational data yields exact fractions and radicals
// (mean 7/3, stdev sqrt(14)/3), checked structurally. Numeric fallback and the
// error paths are checked too.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <map>
#include <string>
#include <vector>

#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/stats.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

Expr P(const std::string& s) { return simplify(parse_expression(s)); }

std::map<std::string, Expr> byLabel(const StatsResult& r) {
    std::map<std::string, Expr> m;
    for (const StatItem& s : r.items) m[s.label] = s.value;
    return m;
}

StatsResult stats(const std::string& data) { return compute_stats(parse_stat_data(data)); }

} // namespace

TEST_CASE("integer data gives exact statistics") {
    const StatsResult r = stats("1, 2, 3, 4, 5");
    REQUIRE(r.status == StatsResult::Status::Ok);
    CHECK(r.exact);
    CHECK(r.n == 5);
    const auto m = byLabel(r);
    CHECK(structurally_equal(m.at("mean"), P("3")));
    CHECK(structurally_equal(m.at("median"), P("3")));
    CHECK(structurally_equal(m.at("Q1"), P("3/2")));
    CHECK(structurally_equal(m.at("Q3"), P("9/2")));
    CHECK(structurally_equal(m.at("variance (pop)"), P("2")));
    CHECK(structurally_equal(m.at("stdev (pop)"), P("sqrt(2)")));
    CHECK(structurally_equal(m.at("stdev (sample)"), P("sqrt(10)/2")));
}

TEST_CASE("mean and stdev stay exact fractions/radicals") {
    const StatsResult r = stats("1, 2, 4");
    REQUIRE(r.status == StatsResult::Status::Ok);
    CHECK(r.exact);
    const auto m = byLabel(r);
    CHECK(structurally_equal(m.at("mean"), P("7/3")));
    CHECK(structurally_equal(m.at("variance (pop)"), P("14/9")));
    CHECK(structurally_equal(m.at("stdev (pop)"), P("sqrt(14)/3")));
}

TEST_CASE("even count median is the exact midpoint") {
    const auto m = byLabel(stats("1, 2, 3, 4"));
    CHECK(structurally_equal(m.at("median"), P("5/2")));
    CHECK(structurally_equal(m.at("IQR"), P("2")));
}

TEST_CASE("fractional inputs are exact") {
    const StatsResult r = stats("1/2, 1/2, 1");
    CHECK(r.exact);
    const auto m = byLabel(r);
    CHECK(structurally_equal(m.at("mean"), P("2/3")));
    CHECK(structurally_equal(m.at("stdev (pop)"), P("sqrt(2)/6")));
}

TEST_CASE("a single value has no spread statistics") {
    const StatsResult r = stats("42");
    REQUIRE(r.status == StatsResult::Status::Ok);
    CHECK(r.n == 1);
    const auto m = byLabel(r);
    CHECK(structurally_equal(m.at("mean"), P("42")));
    CHECK(m.count("Q1") == 0);
    CHECK(m.count("variance (sample)") == 0);
}

TEST_CASE("non-rational data falls back to numeric") {
    const StatsResult r = stats("1, pi, 2");
    REQUIRE(r.status == StatsResult::Status::Ok);
    CHECK_FALSE(r.exact);
    const auto m = byLabel(r);
    // mean ≈ (3 + pi)/3 ≈ 2.0472
    CHECK(std::abs(evaluate(m.at("mean")) - (3.0 + 3.14159265358979) / 3.0) < 1e-3);
}

TEST_CASE("error paths") {
    CHECK(compute_stats({}).status == StatsResult::Status::Error);
    const StatsResult bad = stats("1, oops!, 3");
    CHECK(bad.status == StatsResult::Status::Error);
    CHECK_THAT(bad.message, ContainsSubstring("could not read"));
}

TEST_CASE("data parsing splits on commas, semicolons, and spaces") {
    CHECK(parse_stat_data("1, 2;3 4").size() == 4);
    CHECK(parse_stat_data("  ").empty());
    CHECK(parse_stat_data("5")[0] == "5");
}
