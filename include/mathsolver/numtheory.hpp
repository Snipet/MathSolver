#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mathsolver {

/// A prime power p^e appearing in an integer factorization (e >= 1).
struct PrimePower {
    long long prime;
    int exponent;
};

/// Greatest common divisor of |a| and |b|, non-negative. gcd(0, 0) = 0,
/// gcd(n, 0) = |n|. Throws OverflowError only in the |LLONG_MIN| corner.
long long int_gcd(long long a, long long b);

/// Least common multiple of |a| and |b|, non-negative. lcm(_, 0) = 0.
/// Overflow-checked: throws OverflowError beyond the 64-bit range.
long long int_lcm(long long a, long long b);

/// Deterministic primality test valid for every 64-bit integer (Miller-Rabin
/// with a fixed witness set). n < 2 is not prime.
bool is_prime(long long n);

/// The smallest prime strictly greater than n. Throws OverflowError if the
/// next prime would exceed the 64-bit range.
long long next_prime(long long n);

/// The largest prime strictly less than n, or 0 when there is none (n <= 2).
long long prev_prime(long long n);

/// Prime factorization of |n| in ascending prime order. |n| <= 1 yields an
/// empty list. Trial division plus Pollard's rho, so it handles all int64
/// magnitudes. Requires n != 0 (the caller checks; 0 returns empty).
std::vector<PrimePower> factorize(long long n);

/// All positive divisors of |n| in ascending order. |n| = 1 yields {1}.
/// Requires n != 0.
std::vector<long long> divisors(long long n);

/// Euler's totient phi(n) for n >= 1: the count of 1 <= k <= n coprime to n.
/// phi(1) = 1. Requires n >= 1.
long long euler_totient(long long n);

/// Euclidean remainder: the r with 0 <= r < |m| and a == r (mod m). m = 0
/// throws DivisionByZeroError.
long long int_mod(long long a, long long m);

/// Render a factorization as e.g. "2^3 * 3^2 * 5" using `times` between
/// factors (pass " * " for plain, " · " for pretty). An empty list is "1".
/// The caller supplies any leading sign.
std::string format_factorization(const std::vector<PrimePower>& factors,
                                 std::string_view times);

/// Render a factorization as LaTeX, e.g. "2^{3} \\cdot 3^{2} \\cdot 5". An
/// empty list is "1". The caller supplies any leading sign.
std::string format_factorization_latex(const std::vector<PrimePower>& factors);

} // namespace mathsolver
