#include "mathsolver/numtheory.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "mathsolver/bigint.hpp"
#include "mathsolver/errors.hpp"

namespace mathsolver {

namespace {

/// Magnitude of a signed 64-bit value as an unsigned, correct even for
/// LLONG_MIN (whose negation overflows a signed long long).
std::uint64_t abs_u64(long long v) {
    return v < 0 ? 0u - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}

long long checked_from_u64(std::uint64_t v) {
    if (v > static_cast<std::uint64_t>(LLONG_MAX)) {
        throw OverflowError{"number-theory result exceeds the 64-bit range"};
    }
    return static_cast<long long>(v);
}

std::uint64_t gcd_u64(std::uint64_t a, std::uint64_t b) {
    while (b != 0) {
        std::uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/// (a * b) mod m without overflow, via 128-bit intermediate.
std::uint64_t mulmod(std::uint64_t a, std::uint64_t b, std::uint64_t m) {
    return static_cast<std::uint64_t>((static_cast<__uint128_t>(a) * b) % m);
}

/// (base ^ exp) mod m.
std::uint64_t powmod(std::uint64_t base, std::uint64_t exp, std::uint64_t m) {
    std::uint64_t result = 1 % m;
    base %= m;
    while (exp > 0) {
        if (exp & 1u) result = mulmod(result, base, m);
        base = mulmod(base, base, m);
        exp >>= 1;
    }
    return result;
}

/// Deterministic Miller-Rabin for the full 64-bit range: the first twelve
/// primes as witnesses cover every n < 3.3 * 10^24.
bool is_prime_u64(std::uint64_t n) {
    if (n < 2) return false;
    for (std::uint64_t p : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        if (n % p == 0) return n == p;
    }
    // n - 1 = d * 2^s with d odd.
    std::uint64_t d = n - 1;
    int s = 0;
    while ((d & 1u) == 0) {
        d >>= 1;
        ++s;
    }
    for (std::uint64_t a : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        std::uint64_t x = powmod(a, d, n);
        if (x == 1 || x == n - 1) continue;
        bool composite = true;
        for (int r = 1; r < s; ++r) {
            x = mulmod(x, x, n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

/// Pollard's rho (Brent's variant) returning one non-trivial factor of the
/// composite n. n must be odd and composite.
std::uint64_t pollard_rho(std::uint64_t n) {
    if (n % 2 == 0) return 2;
    // Fixed, deterministic parameter sweep (no RNG, so tests are stable).
    for (std::uint64_t c = 1;; ++c) {
        std::uint64_t x = 2, y = 2, d = 1;
        auto f = [&](std::uint64_t v) { return (mulmod(v, v, n) + c) % n; };
        while (d == 1) {
            x = f(x);
            y = f(f(y));
            std::uint64_t diff = x > y ? x - y : y - x;
            d = diff == 0 ? n : gcd_u64(diff, n);
        }
        if (d != n) return d;
        // Cycle collapsed onto n itself — retry with the next c.
    }
}

void factor_recurse(std::uint64_t n, std::vector<std::uint64_t>& out) {
    if (n == 1) return;
    if (is_prime_u64(n)) {
        out.push_back(n);
        return;
    }
    std::uint64_t d = pollard_rho(n);
    factor_recurse(d, out);
    factor_recurse(n / d, out);
}

} // namespace

long long int_gcd(long long a, long long b) {
    return checked_from_u64(gcd_u64(abs_u64(a), abs_u64(b)));
}

long long int_lcm(long long a, long long b) {
    if (a == 0 || b == 0) return 0;
    const std::uint64_t ua = abs_u64(a);
    const std::uint64_t ub = abs_u64(b);
    const std::uint64_t g = gcd_u64(ua, ub);
    const std::uint64_t q = ua / g;
    // q * ub must fit in 64 bits, then also within LLONG_MAX.
    const __uint128_t prod = static_cast<__uint128_t>(q) * ub;
    if (prod > static_cast<__uint128_t>(LLONG_MAX)) {
        throw OverflowError{"lcm exceeds the 64-bit range"};
    }
    return static_cast<long long>(static_cast<std::uint64_t>(prod));
}

bool is_prime(long long n) {
    return n >= 2 && is_prime_u64(static_cast<std::uint64_t>(n));
}

long long next_prime(long long n) {
    if (n < 2) return 2;
    for (long long c = n + 1;; ++c) {
        if (c <= 0) throw OverflowError{"next_prime exceeds the 64-bit range"};
        if (is_prime(c)) return c;
    }
}

long long prev_prime(long long n) {
    for (long long c = n - 1; c >= 2; --c) {
        if (is_prime(c)) return c;
    }
    return 0;
}

std::vector<PrimePower> factorize(long long n) {
    std::vector<PrimePower> result;
    std::uint64_t m = abs_u64(n);
    if (m <= 1) return result;
    std::vector<std::uint64_t> primes;
    factor_recurse(m, primes);
    std::sort(primes.begin(), primes.end());
    for (std::uint64_t p : primes) {
        if (!result.empty() && static_cast<std::uint64_t>(result.back().prime) == p) {
            ++result.back().exponent;
        } else {
            result.push_back({static_cast<long long>(p), 1});
        }
    }
    return result;
}

std::vector<long long> divisors(long long n) {
    std::vector<long long> result{1};
    for (const PrimePower& pp : factorize(n)) {
        const std::size_t base = result.size();
        long long power = 1;
        for (int e = 1; e <= pp.exponent; ++e) {
            power *= pp.prime;
            for (std::size_t i = 0; i < base; ++i) result.push_back(result[i] * power);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

long long euler_totient(long long n) {
    if (n < 1) throw EvalError{"totient is defined for positive integers"};
    if (n == 1) return 1;
    long long phi = 1;
    for (const PrimePower& pp : factorize(n)) {
        long long term = pp.prime - 1; // p^(e-1) * (p - 1)
        for (int e = 1; e < pp.exponent; ++e) term *= pp.prime;
        phi *= term;
    }
    return phi;
}

long long divisor_sigma(long long n, int k) {
    if (n < 1) throw EvalError{"sigma is defined for positive integers"};
    if (k < 0) throw EvalError{"sigma exponent must be non-negative"};
    long long sum = 0;
    for (long long d : divisors(n)) {
        long long term = 1;
        for (int e = 0; e < k; ++e)
            if (__builtin_mul_overflow(term, d, &term))
                throw OverflowError{"sigma overflows the 64-bit range"};
        if (__builtin_add_overflow(sum, term, &sum))
            throw OverflowError{"sigma overflows the 64-bit range"};
    }
    return sum;
}

int mobius(long long n) {
    if (n < 1) throw EvalError{"mobius is defined for positive integers"};
    if (n == 1) return 1;
    const std::vector<PrimePower> f = factorize(n);
    for (const PrimePower& pp : f)
        if (pp.exponent > 1) return 0; // a squared factor → μ = 0
    return (f.size() % 2 == 0) ? 1 : -1;
}

BigInt partition_count(long long n) {
    if (n < 0) throw EvalError{"partitions is defined for n >= 0"};
    // Compute guard: p(n) is exact (arbitrary precision), but a giant n would
    // allocate/iterate for a value nobody typed intentionally.
    if (n > 20000) throw EvalError{"partitions argument too large (max 20000)"};
    // Euler's pentagonal-number recurrence:
    //   p(n) = Σ_{k>=1} (-1)^(k-1) [ p(n - g_k) + p(n - g'_k) ],
    // with generalized pentagonal numbers g_k = k(3k-1)/2, g'_k = k(3k+1)/2.
    std::vector<BigInt> p(static_cast<std::size_t>(n) + 1);
    p[0] = BigInt(1);
    for (long long m = 1; m <= n; ++m) {
        BigInt sum;
        for (long long k = 1;; ++k) {
            const long long g1 = k * (3 * k - 1) / 2;
            if (g1 > m) break;
            const long long g2 = k * (3 * k + 1) / 2;
            BigInt term = p[static_cast<std::size_t>(m - g1)];
            if (g2 <= m) term += p[static_cast<std::size_t>(m - g2)];
            if (k % 2 == 1) sum += term;
            else sum -= term;
        }
        p[static_cast<std::size_t>(m)] = std::move(sum);
    }
    return p[static_cast<std::size_t>(n)];
}

BigInt stirling_second(long long n, long long k) {
    if (n < 0 || k < 0) throw EvalError{"stirling2 is defined for n, k >= 0"};
    if (k > n) return BigInt(0);
    if (n > 2000) throw EvalError{"stirling2 argument too large (max n = 2000)"};
    // dp[j] = S(i, j), rolled forward row by row. Update j high→low in place.
    std::vector<BigInt> dp(static_cast<std::size_t>(k) + 1);
    dp[0] = BigInt(1); // S(0, 0)
    for (long long i = 1; i <= n; ++i) {
        const long long upper = std::min(i, k);
        for (long long j = upper; j >= 1; --j) {
            dp[static_cast<std::size_t>(j)] =
                BigInt(j) * dp[static_cast<std::size_t>(j)] + dp[static_cast<std::size_t>(j - 1)];
        }
        dp[0] = BigInt(0); // S(i, 0) = 0 for i >= 1
    }
    return dp[static_cast<std::size_t>(k)];
}

BigInt bell_number(long long n) {
    if (n < 0) throw EvalError{"bell is defined for n >= 0"};
    if (n > 2000) throw EvalError{"bell argument too large (max 2000)"};
    // Bell triangle (Aitken's array): a(i,0) = a(i-1, i-1);
    // a(i,j) = a(i,j-1) + a(i-1,j-1). B(n) = a(n, 0) = row[0] after n rows.
    std::vector<BigInt> row{BigInt(1)}; // row 0 = {a(0,0)} = {1}
    for (long long i = 1; i <= n; ++i) {
        std::vector<BigInt> next(static_cast<std::size_t>(i) + 1);
        next[0] = row.back();
        for (long long j = 1; j <= i; ++j) {
            next[static_cast<std::size_t>(j)] =
                next[static_cast<std::size_t>(j - 1)] + row[static_cast<std::size_t>(j - 1)];
        }
        row = std::move(next);
    }
    return row[0];
}

BigInt derangement_count(long long n) {
    if (n < 0) throw EvalError{"derangement is defined for n >= 0"};
    if (n > 50000) throw EvalError{"derangement argument too large (max 50000)"};
    // !n = (n-1)·(!(n-1) + !(n-2)), rolled forward with two running values.
    // !0 = 1, !1 = 0.
    if (n == 0) return BigInt(1);
    BigInt prev2(1); // !(k-2), starting at !0
    BigInt prev1(0); // !(k-1), starting at !1
    for (long long k = 2; k <= n; ++k) {
        BigInt term = BigInt(k - 1) * (prev1 + prev2);
        prev2 = std::move(prev1);
        prev1 = std::move(term);
    }
    return prev1;
}

BigInt lucas_number(long long n) {
    if (n < 0) throw EvalError{"lucas is defined for n >= 0"};
    if (n > 200000) throw EvalError{"lucas argument too large (max 200000)"};
    // L(0) = 2, L(1) = 1, L(n) = L(n-1) + L(n-2).
    BigInt prev2(2); // L(0)
    BigInt prev1(1); // L(1)
    if (n == 0) return prev2;
    for (long long k = 2; k <= n; ++k) {
        BigInt next = prev1 + prev2;
        prev2 = std::move(prev1);
        prev1 = std::move(next);
    }
    return prev1;
}

BigInt primorial(long long n) {
    if (n < 0) throw EvalError{"primorial is defined for n >= 0"};
    if (n > 200000) throw EvalError{"primorial argument too large (max 200000)"};
    // Product of all primes p <= n.
    BigInt acc(1);
    for (long long p = 2; p <= n; ++p) {
        if (!is_prime(p)) continue;
        acc *= BigInt(p);
    }
    return acc;
}

BigInt motzkin_number(long long n) {
    if (n < 0) throw EvalError{"motzkin is defined for n >= 0"};
    if (n > 50000) throw EvalError{"motzkin argument too large (max 50000)"};
    if (n < 2) return BigInt(1); // M(0) = M(1) = 1
    // (k+2)·M(k) = (2k+1)·M(k-1) + 3(k-1)·M(k-2), an exact division at each step.
    BigInt prev2(1); // M(k-2), starting at M(0)
    BigInt prev1(1); // M(k-1), starting at M(1)
    for (long long k = 2; k <= n; ++k) {
        const BigInt num = BigInt(2 * k + 1) * prev1 + BigInt(3 * (k - 1)) * prev2;
        BigInt m = num / BigInt(k + 2); // exact
        prev2 = std::move(prev1);
        prev1 = std::move(m);
    }
    return prev1;
}

BigInt euler_number(long long n) {
    if (n < 0) throw EvalError{"euler is defined for n >= 0"};
    if (n > 2000) throw EvalError{"euler argument too large (max 2000)"};
    if (n % 2 == 1) return BigInt(0); // odd-indexed Euler numbers vanish
    // Boustrophedon (Seidel) triangle for the zigzag numbers A(n): each row is
    // read in the opposite direction to the last, accumulating prefix sums.
    // |E(2m)| = A(2m); the sign is (-1)^m.
    std::vector<BigInt> row{BigInt(1)}; // n = 0 row
    BigInt an(1);                       // A(0)
    for (long long i = 1; i <= n; ++i) {
        std::vector<BigInt> next(static_cast<std::size_t>(i) + 1);
        if (i % 2 == 1) { // left to right
            for (long long k = 1; k <= i; ++k) {
                next[static_cast<std::size_t>(k)] =
                    next[static_cast<std::size_t>(k - 1)] + row[static_cast<std::size_t>(k - 1)];
            }
            an = next[static_cast<std::size_t>(i)];
        } else { // right to left, then reverse into place
            std::vector<BigInt> r(static_cast<std::size_t>(i) + 1);
            for (long long k = 1; k <= i; ++k) {
                r[static_cast<std::size_t>(k)] =
                    r[static_cast<std::size_t>(k - 1)] + row[static_cast<std::size_t>(i - k)];
            }
            for (long long k = 0; k <= i; ++k)
                next[static_cast<std::size_t>(k)] = r[static_cast<std::size_t>(i - k)];
            an = next[0];
        }
        row = std::move(next);
    }
    return (n / 2) % 2 == 0 ? an : -an;
}

BigInt tribonacci_number(long long n) {
    if (n < 0) throw EvalError{"tribonacci is defined for n >= 0"};
    if (n > 200000) throw EvalError{"tribonacci argument too large (max 200000)"};
    // T(0) = T(1) = 0, T(2) = 1, T(n) = T(n-1) + T(n-2) + T(n-3).
    if (n < 2) return BigInt(0);
    if (n == 2) return BigInt(1);
    BigInt a(0); // T(k-3)
    BigInt b(0); // T(k-2)
    BigInt c(1); // T(k-1), seeded at T(2)
    for (long long k = 3; k <= n; ++k) {
        BigInt t = a + b + c;
        a = std::move(b);
        b = std::move(c);
        c = std::move(t);
    }
    return c;
}

BigInt pell_number(long long n) {
    if (n < 0) throw EvalError{"pell is defined for n >= 0"};
    if (n > 200000) throw EvalError{"pell argument too large (max 200000)"};
    // P(0) = 0, P(1) = 1, P(n) = 2*P(n-1) + P(n-2).
    if (n == 0) return BigInt(0);
    BigInt a(0); // P(k-1)
    BigInt b(1); // P(k)
    for (long long k = 1; k < n; ++k) {
        BigInt t = b * BigInt(2) + a;
        a = std::move(b);
        b = std::move(t);
    }
    return b;
}

BigInt catalan_number(long long n) {
    if (n < 0) throw EvalError{"catalan is defined for n >= 0"};
    if (n > 50000) throw EvalError{"catalan argument too large (max 50000)"};
    // Iterate C(k+1) = C(k)·2(2k+1)/(k+2) — an exact division at every step
    // (each Catalan number is integral).
    BigInt c(1);
    for (long long k = 0; k < n; ++k) {
        c = c * BigInt(2 * (2 * k + 1)) / BigInt(k + 2); // exact
    }
    return c;
}

long long int_mod(long long a, long long m) {
    if (m == 0) throw DivisionByZeroError{"mod by zero"};
    long long r = a % m;
    if (r < 0) r += m < 0 ? -m : m;
    return r;
}

long long pow_mod(long long base, long long exponent, long long modulus) {
    if (modulus <= 0) throw EvalError{"powmod requires a positive modulus"};
    if (exponent < 0) {
        throw EvalError{"powmod requires a non-negative exponent (use modinv for "
                        "negative powers)"};
    }
    if (modulus == 1) return 0;
    // Reduce the base into [0, m) first (handles negatives), then square-and-
    // multiply with 128-bit products so nothing overflows.
    std::uint64_t b = static_cast<std::uint64_t>(int_mod(base, modulus));
    std::uint64_t e = static_cast<std::uint64_t>(exponent);
    const std::uint64_t m = static_cast<std::uint64_t>(modulus);
    return static_cast<long long>(powmod(b, e, m));
}

namespace {

/// Extended Euclid: returns g = gcd(|a|, |b|) and sets x, y so a*x + b*y = g.
long long ext_gcd(long long a, long long b, long long& x, long long& y) {
    if (b == 0) {
        x = a < 0 ? -1 : 1; // sign so that a*x = |a|
        y = 0;
        return a < 0 ? -a : a;
    }
    long long x1 = 0, y1 = 0;
    const long long g = ext_gcd(b, a % b, x1, y1);
    x = y1;
    y = x1 - (a / b) * y1;
    return g;
}

} // namespace

long long mod_inverse(long long a, long long m) {
    if (m <= 1) throw EvalError{"modinv requires a modulus > 1"};
    long long x = 0, y = 0;
    const long long g = ext_gcd(int_mod(a, m), m, x, y);
    if (g != 1) {
        throw EvalError{std::format("{} is not invertible mod {} (gcd is {})",
                                    a, m, g)};
    }
    return int_mod(x, m);
}

Crt crt_solve(const std::vector<long long>& residues,
              const std::vector<long long>& moduli) {
    if (residues.empty() || residues.size() != moduli.size()) {
        throw EvalError{"crt needs an equal, non-empty count of residues and moduli"};
    }
    long long r = 0;    // running solution
    long long mod = 1;  // running modulus (lcm so far)
    for (std::size_t i = 0; i < moduli.size(); ++i) {
        const long long mi = moduli[i];
        if (mi < 1) throw EvalError{"crt moduli must be positive"};
        const long long ri = int_mod(residues[i], mi);
        // Combine x == r (mod mod) with x == ri (mod mi).
        long long p = 0, q = 0;
        const long long g = ext_gcd(mod, mi, p, q);
        const long long diff = ri - r;
        if (diff % g != 0) {
            throw EvalError{std::format(
                "crt system is inconsistent at x == {} (mod {})", ri, mi)};
        }
        // new modulus = lcm(mod, mi); step = mod/g * ((diff/g) mod (mi/g)).
        const long long lcm = int_lcm(mod, mi); // overflow-checked
        const long long mi_g = mi / g;
        // r += mod * (((diff/g) * p) mod mi_g), kept in 128-bit then reduced.
        const long long t = int_mod(static_cast<long long>(
                                        (static_cast<__int128_t>(diff / g) * p) % mi_g),
                                    mi_g);
        r = static_cast<long long>(
            (static_cast<__int128_t>(r) + static_cast<__int128_t>(mod) * t) % lcm);
        if (r < 0) r += lcm;
        mod = lcm;
    }
    return {r, mod};
}

std::string format_factorization(const std::vector<PrimePower>& factors,
                                 std::string_view times) {
    if (factors.empty()) return "1";
    std::string out;
    for (std::size_t i = 0; i < factors.size(); ++i) {
        if (i > 0) out += times;
        out += std::to_string(factors[i].prime);
        if (factors[i].exponent > 1) {
            out += '^';
            out += std::to_string(factors[i].exponent);
        }
    }
    return out;
}

std::string format_factorization_latex(const std::vector<PrimePower>& factors) {
    if (factors.empty()) return "1";
    std::string out;
    for (std::size_t i = 0; i < factors.size(); ++i) {
        if (i > 0) out += " \\cdot ";
        out += std::to_string(factors[i].prime);
        if (factors[i].exponent > 1) {
            out += "^{";
            out += std::to_string(factors[i].exponent);
            out += '}';
        }
    }
    return out;
}

} // namespace mathsolver
