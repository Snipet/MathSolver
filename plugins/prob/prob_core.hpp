#pragma once

// Numeric probability distributions for the `prob` plugin. Header-only so the
// native tests can check the closed forms directly. Everything is computed in
// log-space where it matters (binomial / Poisson PMFs) for numerical stability.

#include <cmath>
#include <limits>
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

// --- Special functions (for the t and chi-squared CDFs) --------------------

/// Regularized lower incomplete gamma P(a, x) = γ(a, x) / Γ(a). Series below the
/// crossover, continued fraction (for Q = 1 − P) above (Numerical Recipes).
inline double reg_lower_gamma(double a, double x) {
    if (x <= 0.0 || a <= 0.0) return x <= 0.0 ? 0.0 : std::nan("");
    const double gln = std::lgamma(a);
    if (x < a + 1.0) {
        double ap = a;
        double sum = 1.0 / a;
        double del = sum;
        for (int n = 0; n < 300; ++n) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (std::fabs(del) < std::fabs(sum) * 1e-16) break;
        }
        return sum * std::exp(-x + a * std::log(x) - gln);
    }
    const double tiny = 1e-300;
    double b = x + 1.0 - a;
    double c = 1.0 / tiny;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i <= 300; ++i) {
        const double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < tiny) d = tiny;
        c = b + an / c;
        if (std::fabs(c) < tiny) c = tiny;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-16) break;
    }
    const double q = std::exp(-x + a * std::log(x) - gln) * h;
    return 1.0 - q;
}

/// Continued fraction for the incomplete beta (Lentz).
inline double betacf(double a, double b, double x) {
    const double tiny = 1e-300;
    const double qab = a + b;
    const double qap = a + 1.0;
    const double qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::fabs(d) < tiny) d = tiny;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= 400; ++m) {
        const double m2 = 2.0 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < tiny) d = tiny;
        c = 1.0 + aa / c;
        if (std::fabs(c) < tiny) c = tiny;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < tiny) d = tiny;
        c = 1.0 + aa / c;
        if (std::fabs(c) < tiny) c = tiny;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < 1e-16) break;
    }
    return h;
}

/// Regularized incomplete beta I_x(a, b).
inline double reg_inc_beta(double a, double b, double x) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    const double bt = std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) +
                               a * std::log(x) + b * std::log1p(-x));
    if (x < (a + 1.0) / (a + b + 2.0)) return bt * betacf(a, b, x) / a;
    return 1.0 - bt * betacf(b, a, 1.0 - x) / b;
}

// --- Student's t -----------------------------------------------------------

inline double t_pdf(double t, double nu) {
    const double c = std::exp(std::lgamma((nu + 1.0) / 2.0) - std::lgamma(nu / 2.0)) /
                     std::sqrt(nu * std::numbers::pi);
    return c * std::pow(1.0 + t * t / nu, -(nu + 1.0) / 2.0);
}

inline double t_cdf(double t, double nu) {
    const double ib = reg_inc_beta(nu / 2.0, 0.5, nu / (nu + t * t));
    return t <= 0.0 ? 0.5 * ib : 1.0 - 0.5 * ib;
}

// --- Chi-squared -----------------------------------------------------------

inline double chi2_pdf(double x, double k) {
    if (x < 0.0) return 0.0;
    if (x == 0.0) return k < 2.0 ? std::numeric_limits<double>::infinity() : (k == 2.0 ? 0.5 : 0.0);
    return std::exp((k / 2.0 - 1.0) * std::log(x) - x / 2.0 - (k / 2.0) * std::log(2.0) -
                    std::lgamma(k / 2.0));
}

inline double chi2_cdf(double x, double k) { return x <= 0.0 ? 0.0 : reg_lower_gamma(k / 2.0, x / 2.0); }

// --- Exponential -----------------------------------------------------------

inline double exp_pdf(double x, double lambda) { return x < 0.0 ? 0.0 : lambda * std::exp(-lambda * x); }
inline double exp_cdf(double x, double lambda) { return x < 0.0 ? 0.0 : -std::expm1(-lambda * x); }

// --- Uniform on [a, b] -----------------------------------------------------

inline double unif_pdf(double x, double a, double b) { return (x < a || x > b) ? 0.0 : 1.0 / (b - a); }
inline double unif_cdf(double x, double a, double b) {
    if (x <= a) return 0.0;
    if (x >= b) return 1.0;
    return (x - a) / (b - a);
}

} // namespace mathsolver::plugins::prob
