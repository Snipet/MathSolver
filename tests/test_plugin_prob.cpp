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

TEST_CASE("student t: symmetry, tail values, normal limit") {
    CHECK_THAT(pr::t_cdf(0, 10), WithinAbs(0.5, 1e-12));
    CHECK_THAT(pr::t_cdf(1.5, 7) + pr::t_cdf(-1.5, 7), WithinAbs(1.0, 1e-12)); // symmetry
    // The 97.5th percentile of t(10) is 2.228.
    CHECK_THAT(pr::t_cdf(2.228138852, 10), WithinAbs(0.975, 1e-6));
    // As nu -> inf, t -> standard normal.
    CHECK_THAT(pr::t_cdf(1.959963985, 1e7), WithinAbs(0.975, 1e-5));
    CHECK_THAT(pr::t_pdf(0, 1e7), WithinAbs(pr::normal_pdf(0, 0, 1), 1e-5));
}

TEST_CASE("chi-squared: known percentiles and the k=2 exponential identity") {
    // chi^2(2) is Exponential(1/2): CDF = 1 - e^{-x/2}.
    CHECK_THAT(pr::chi2_cdf(2, 2), WithinAbs(1.0 - std::exp(-1.0), 1e-10));
    // The 95th percentile of chi^2(3) is 7.815.
    CHECK_THAT(pr::chi2_cdf(7.814727903, 3), WithinAbs(0.95, 1e-6));
    CHECK(pr::chi2_cdf(0, 3) == 0.0);
    CHECK(pr::chi2_pdf(-1, 3) == 0.0);
}

TEST_CASE("exponential and uniform closed forms") {
    CHECK_THAT(pr::exp_pdf(0, 0.5), WithinRel(0.5, 1e-12));
    CHECK_THAT(pr::exp_cdf(2, 0.5), WithinAbs(1.0 - std::exp(-1.0), 1e-12));
    CHECK(pr::exp_cdf(-1, 1) == 0.0);
    CHECK_THAT(pr::unif_pdf(3, 0, 10), WithinRel(0.1, 1e-12));
    CHECK(pr::unif_pdf(-1, 0, 10) == 0.0);
    CHECK_THAT(pr::unif_cdf(3, 0, 10), WithinRel(0.3, 1e-12));
    CHECK(pr::unif_cdf(-5, 0, 10) == 0.0);
    CHECK(pr::unif_cdf(15, 0, 10) == 1.0);
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
    CHECK_THAT(p->invoke("tcdf", {"2.228", "10"}),
               ContainsSubstring("\"ok\":true") && ContainsSubstring("0.9749"));
    CHECK_THAT(p->invoke("chi2cdf", {"7.815", "3"}), ContainsSubstring("0.95"));
    CHECK_THAT(p->invoke("expcdf", {"2", "0.5"}), ContainsSubstring("0.632"));
    CHECK_THAT(p->invoke("unifcdf", {"3", "0", "10"}), ContainsSubstring("0.3"));

    // Errors: bad domain, non-integer count, unknown command — never throw.
    CHECK_THAT(p->invoke("normalcdf", {"1", "0", "-2"}),
               ContainsSubstring("\"ok\":false") && ContainsSubstring("positive"));
    CHECK_THAT(p->invoke("invnorm", {"1.5"}), ContainsSubstring("between 0 and 1"));
    CHECK_THAT(p->invoke("binompdf", {"10", "0.5", "2.5"}), ContainsSubstring("whole number"));
    CHECK_THAT(p->invoke("nope", {"1"}), ContainsSubstring("no command"));
}
