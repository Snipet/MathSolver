// Probability-distributions plugin tests: the core numerics against closed
// forms and reference values, PMFs summing to 1, the inverse-normal round-trip,
// and the command envelope + error paths.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <string>

#include "../plugins/prob/prob_core.hpp"
#include "mathsolver/plugin.hpp"

using namespace mathsolver::plugins;
namespace pr = mathsolver::plugins::prob;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("normal pdf/cdf match known values") {
    CHECK_THAT(pr::normal_pdf(0, 0, 1), WithinRel(1.0 / std::sqrt(2 * std::numbers::pi), 1e-12));
    CHECK_THAT(pr::normal_cdf(0, 0, 1), WithinAbs(0.5, 1e-12));
    CHECK_THAT(pr::normal_cdf(1.96, 0, 1), WithinAbs(0.9750021048, 1e-9));
    // A non-standard normal: N(10, 2), one sigma up → same 0.8413.
    CHECK_THAT(pr::normal_cdf(12, 10, 2), WithinAbs(0.8413447461, 1e-9));
    // The density integrates the CDF: symmetric tails.
    CHECK_THAT(pr::normal_cdf(-1, 0, 1) + pr::normal_cdf(1, 0, 1), WithinAbs(1.0, 1e-12));
}

TEST_CASE("inverse normal inverts the CDF") {
    CHECK_THAT(pr::inv_norm(0.5, 0, 1), WithinAbs(0.0, 1e-12));
    CHECK_THAT(pr::inv_norm(0.975, 0, 1), WithinAbs(1.9599639845, 1e-7));
    for (double p : {0.001, 0.05, 0.3, 0.5, 0.84, 0.999}) {
        const double x = pr::inv_norm(p, 0, 1);
        CHECK_THAT(pr::normal_cdf(x, 0, 1), WithinAbs(p, 1e-9)); // round-trip
    }
    // Scaling: invnorm on N(mu, sigma) = mu + sigma·z.
    CHECK_THAT(pr::inv_norm(0.975, 10, 2), WithinAbs(10 + 2 * 1.9599639845, 1e-6));
}

TEST_CASE("binomial pmf/cdf are correct and normalized") {
    CHECK_THAT(pr::binom_pmf(10, 0.5, 5), WithinRel(252.0 / 1024.0, 1e-12));
    CHECK(pr::binom_pmf(10, 0.5, 11) == 0.0); // out of range
    CHECK(pr::binom_pmf(5, 0.0, 0) == 1.0);   // degenerate p=0
    CHECK(pr::binom_pmf(5, 1.0, 5) == 1.0);   // degenerate p=1
    double total = 0.0;
    for (int k = 0; k <= 10; ++k) total += pr::binom_pmf(10, 0.3, k);
    CHECK_THAT(total, WithinAbs(1.0, 1e-12)); // PMF sums to 1
    CHECK_THAT(pr::binom_cdf(10, 0.5, 10), WithinAbs(1.0, 1e-12));
    CHECK_THAT(pr::binom_cdf(10, 0.3, 3), WithinAbs(0.6496107184, 1e-9));
}

TEST_CASE("poisson pmf/cdf are correct and normalized") {
    CHECK_THAT(pr::poisson_pmf(3, 0), WithinRel(std::exp(-3.0), 1e-12));
    CHECK_THAT(pr::poisson_pmf(3, 2), WithinRel(std::exp(-3.0) * 9.0 / 2.0, 1e-12));
    CHECK(pr::poisson_pmf(3, -1) == 0.0);
    double total = 0.0;
    for (int k = 0; k <= 60; ++k) total += pr::poisson_pmf(4, k);
    CHECK_THAT(total, WithinAbs(1.0, 1e-12));
    CHECK_THAT(pr::poisson_cdf(3, 2), WithinAbs(0.4231900811, 1e-9));
}

TEST_CASE("plugin command envelopes and error paths") {
    register_builtin_plugins();
    const Plugin* p = find("prob");
    REQUIRE(p != nullptr);

    CHECK_THAT(p->invoke("normalcdf", {"1.96"}),
               ContainsSubstring("\"ok\":true") && ContainsSubstring("0.975"));
    CHECK_THAT(p->invoke("normalpdf", {"0"}), ContainsSubstring("\"type\":\"series\""));
    CHECK_THAT(p->invoke("binompdf", {"10", "0.5", "5"}), ContainsSubstring("0.246"));
    CHECK_THAT(p->invoke("poissoncdf", {"3", "2"}), ContainsSubstring("0.423"));

    // Errors: bad domain, non-integer count, unknown command — never throw.
    CHECK_THAT(p->invoke("normalcdf", {"1", "0", "-2"}),
               ContainsSubstring("\"ok\":false") && ContainsSubstring("positive"));
    CHECK_THAT(p->invoke("invnorm", {"1.5"}), ContainsSubstring("between 0 and 1"));
    CHECK_THAT(p->invoke("binompdf", {"10", "0.5", "2.5"}), ContainsSubstring("whole number"));
    CHECK_THAT(p->invoke("nope", {"1"}), ContainsSubstring("no command"));
}
