#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/bigint.hpp"

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

/// The divisor function σ_k(n) for n >= 1, k >= 0: the sum of the k-th powers
/// of the positive divisors of n. σ_0 counts the divisors, σ_1 sums them.
/// Requires n >= 1 and k >= 0; throws OverflowError past the 64-bit range.
long long divisor_sigma(long long n, int k = 1);

/// The Möbius function μ(n) for n >= 1: 0 when n is divisible by a square > 1,
/// otherwise (-1)^(number of distinct prime factors). μ(1) = 1. Requires n>=1.
int mobius(long long n);

/// The integer partition function p(n) for n >= 0: the number of ways to write
/// n as a sum of positive integers (order-independent). p(0) = 1. Computed
/// exactly (arbitrary precision) via Euler's pentagonal-number recurrence.
/// Requires 0 <= n <= 20000 (a compute guard, not a value ceiling).
BigInt partition_count(long long n);

/// The n-th Catalan number C(n) = binomial(2n, n) / (n + 1) for n >= 0.
/// C(0) = 1. Built by an exact product so it never forms the huge factorials.
/// Arbitrary precision; requires 0 <= n <= 50000 (a compute guard).
BigInt catalan_number(long long n);

/// The Stirling number of the second kind S(n, k): the number of ways to
/// partition an n-element set into k non-empty unlabelled subsets. Requires
/// n, k >= 0 (and n <= 2000, a compute guard); S(n, k) = 0 for k > n,
/// S(0, 0) = 1. Exact (arbitrary precision) via the recurrence
/// S(n, k) = k·S(n-1, k) + S(n-1, k-1).
BigInt stirling_second(long long n, long long k);

/// The n-th Bell number B(n) = Σ_k S(n, k): the number of partitions of an
/// n-element set. B(0) = 1. Built exactly (arbitrary precision) via the Bell
/// triangle. Requires 0 <= n <= 2000 (a compute guard).
BigInt bell_number(long long n);

/// The n-th derangement (subfactorial !n) for n >= 0: the number of
/// permutations of n elements with no fixed point. !0 = 1, !1 = 0. Built
/// exactly (arbitrary precision) via !n = (n-1)·(!(n-1) + !(n-2)). Requires
/// 0 <= n <= 50000 (a compute guard).
BigInt derangement_count(long long n);

/// The n-th Lucas number L(n) for n >= 0: the companion sequence to the
/// Fibonacci numbers, L(0) = 2, L(1) = 1, L(n) = L(n-1) + L(n-2). Exact
/// (arbitrary precision); requires 0 <= n <= 200000 (a compute guard).
BigInt lucas_number(long long n);

/// The primorial n# for n >= 0: the product of all primes p <= n. 0# = 1# = 1
/// (empty product). Exact (arbitrary precision); requires 0 <= n <= 200000
/// (a compute guard).
BigInt primorial(long long n);

/// The n-th Motzkin number M(n) for n >= 0: the number of ways to draw
/// non-crossing chords between n points on a circle. M(0) = M(1) = 1, built by
/// the exact recurrence (n+2)·M(n) = (2n+1)·M(n-1) + 3(n-1)·M(n-2). Arbitrary
/// precision; requires 0 <= n <= 50000 (a compute guard).
BigInt motzkin_number(long long n);

/// The n-th Euler (secant) number E(n) for n >= 0: E(0) = 1, E(2) = -1,
/// E(4) = 5, E(6) = -61, ... with every odd-indexed value 0. Signed. Built
/// exactly (arbitrary precision) from the boustrophedon (Seidel) zigzag
/// triangle. Requires 0 <= n <= 2000 (a compute guard).
BigInt euler_number(long long n);

/// The n-th tribonacci number T(n) for n >= 0: T(0) = T(1) = 0, T(2) = 1,
/// T(n) = T(n-1) + T(n-2) + T(n-3). Exact (arbitrary precision); requires
/// 0 <= n <= 200000 (a compute guard).
BigInt tribonacci_number(long long n);

/// The n-th Pell number P(n) for n >= 0: P(0) = 0, P(1) = 1,
/// P(n) = 2*P(n-1) + P(n-2) (0, 1, 2, 5, 12, 29, 70, 169, ...). The numerators
/// of the continued-fraction convergents to sqrt(2). Exact (arbitrary
/// precision); requires 0 <= n <= 200000 (a compute guard).
BigInt pell_number(long long n);

/// Euclidean remainder: the r with 0 <= r < |m| and a == r (mod m). m = 0
/// throws DivisionByZeroError.
long long int_mod(long long a, long long m);

/// Modular exponentiation base^exponent mod m, in [0, m). exponent >= 0 and
/// m > 0 are required (throws otherwise); handles huge exponents that would
/// overflow ordinary evaluation.
long long pow_mod(long long base, long long exponent, long long modulus);

/// Modular inverse a^{-1} mod m (the b in [0, m) with a*b == 1 mod m), via the
/// extended Euclidean algorithm. Requires m > 1 and gcd(a, m) == 1; throws
/// EvalError when a is not invertible.
long long mod_inverse(long long a, long long m);

/// Solution of a system of congruences x == residues[i] (mod moduli[i]) by the
/// Chinese remainder theorem, allowing non-coprime moduli. `residue` is the
/// unique solution in [0, modulus), where `modulus` is the lcm of the moduli.
struct Crt {
    long long residue;
    long long modulus;
};

/// Solve the congruence system. The lists must be non-empty and equal length,
/// every modulus >= 1; throws EvalError on a size mismatch or an inconsistent
/// system (e.g. x == 0 mod 2 and x == 1 mod 4).
Crt crt_solve(const std::vector<long long>& residues,
              const std::vector<long long>& moduli);

/// Render a factorization as e.g. "2^3 * 3^2 * 5" using `times` between
/// factors (pass " * " for plain, " · " for pretty). An empty list is "1".
/// The caller supplies any leading sign.
std::string format_factorization(const std::vector<PrimePower>& factors,
                                 std::string_view times);

/// Render a factorization as LaTeX, e.g. "2^{3} \\cdot 3^{2} \\cdot 5". An
/// empty list is "1". The caller supplies any leading sign.
std::string format_factorization_latex(const std::vector<PrimePower>& factors);

} // namespace mathsolver
