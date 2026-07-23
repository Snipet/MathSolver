#pragma once

// Numeric probability distributions for the `prob` plugin. Header-only so the
// native tests can check the closed forms directly. Everything is computed in
// log-space where it matters (binomial / Poisson PMFs) for numerical stability.

#include <cmath>
#include <numbers>

namespace mathsolver::plugins::prob {

// --- Normal ----------------------------------------------------------------

inline double normal_pdf(double x, double mu, double sigma) {
    const double z = (x - mu) / sigma;
    return std::exp(-0.5 * z * z) / (sigma * std::sqrt(2.0 * std::numbers::pi));
}

/// P(X <= x). Uses erfc for accuracy in the far left tail.
inline double normal_cdf(double x, double mu, double sigma) {
    return 0.5 * std::erfc(-(x - mu) / (sigma * std::numbers::sqrt2));
}

/// Inverse standard-normal CDF (Acklam's rational approximation, then one
/// Halley step against erf for ~full double accuracy). Returns the quantile of
/// N(mu, sigma^2): the x with P(X <= x) = p. NaN outside (0, 1).
inline double inv_norm(double p, double mu, double sigma) {
    if (!(p > 0.0 && p < 1.0)) return std::nan("");
    // Acklam coefficients.
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02,
                               -2.759285104469687e+02, 1.383577518672690e+02,
                               -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02,
                               -1.556989798598866e+02, 6.680131188771972e+01,
                               -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01,
                               -2.400758277161838e+00, -2.549732539343734e+00,
                               4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01,
                               2.445134137142996e+00, 3.754408661907416e+00};
    const double plow = 0.02425;
    const double phigh = 1.0 - plow;
    double z;
    if (p < plow) {
        const double q = std::sqrt(-2.0 * std::log(p));
        z = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else if (p <= phigh) {
        const double q = p - 0.5;
        const double r = q * q;
        z = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
            (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    } else {
        const double q = std::sqrt(-2.0 * std::log(1.0 - p));
        z = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
            ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    // One Halley refinement: e = Phi(z) - p, u = e * sqrt(2pi) * exp(z^2/2).
    const double e = normal_cdf(z, 0.0, 1.0) - p;
    const double u = e * std::sqrt(2.0 * std::numbers::pi) * std::exp(0.5 * z * z);
    z = z - u / (1.0 + 0.5 * z * u);
    return mu + sigma * z;
}

// --- Binomial --------------------------------------------------------------

/// P(X = k) for X ~ Binomial(n, p). Zero for k outside [0, n].
inline double binom_pmf(int n, double p, int k) {
    if (k < 0 || k > n || n < 0) return 0.0;
    if (p <= 0.0) return k == 0 ? 1.0 : 0.0;
    if (p >= 1.0) return k == n ? 1.0 : 0.0;
    const double log_c = std::lgamma(n + 1.0) - std::lgamma(k + 1.0) - std::lgamma(n - k + 1.0);
    return std::exp(log_c + k * std::log(p) + (n - k) * std::log1p(-p));
}

/// P(X <= k) for X ~ Binomial(n, p).
inline double binom_cdf(int n, double p, int k) {
    if (k < 0) return 0.0;
    if (k >= n) return 1.0;
    double acc = 0.0;
    for (int i = 0; i <= k; ++i) acc += binom_pmf(n, p, i);
    return acc > 1.0 ? 1.0 : acc;
}

// --- Poisson ---------------------------------------------------------------

/// P(X = k) for X ~ Poisson(lambda). Zero for k < 0.
inline double poisson_pmf(double lambda, int k) {
    if (k < 0 || lambda < 0.0) return 0.0;
    if (lambda == 0.0) return k == 0 ? 1.0 : 0.0;
    return std::exp(-lambda + k * std::log(lambda) - std::lgamma(k + 1.0));
}

/// P(X <= k) for X ~ Poisson(lambda).
inline double poisson_cdf(double lambda, int k) {
    if (k < 0) return 0.0;
    double acc = 0.0;
    for (int i = 0; i <= k; ++i) acc += poisson_pmf(lambda, i);
    return acc > 1.0 ? 1.0 : acc;
}

} // namespace mathsolver::plugins::prob
