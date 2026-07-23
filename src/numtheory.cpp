#include "mathsolver/numtheory.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <format>
#include <string>

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
