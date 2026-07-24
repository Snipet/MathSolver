// Integer number-theory tests: gcd/lcm, deterministic primality, factorization
// (trial division + Pollard's rho), divisors, Euler's totient, and Euclidean
// mod — checked against closed forms and known values across the int64 range.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <climits>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/numtheory.hpp"

using namespace mathsolver;
using Catch::Matchers::ContainsSubstring;

namespace {

// Reconstruct |n| from its factorization, and confirm every base is prime and
// listed once in ascending order.
long long product_of(const std::vector<PrimePower>& f) {
    long long p = 1;
    long long last = 0;
    for (const PrimePower& pp : f) {
        CHECK(pp.prime > last); // strictly ascending, deduplicated
        CHECK(pp.exponent >= 1);
        CHECK(is_prime(pp.prime));
        last = pp.prime;
        for (int e = 0; e < pp.exponent; ++e) p *= pp.prime;
    }
    return p;
}

} // namespace

TEST_CASE("gcd and lcm over the integers") {
    CHECK(int_gcd(48, 36) == 12);
    CHECK(int_gcd(1071, 462) == 21);
    CHECK(int_gcd(-48, 36) == 12); // magnitude only
    CHECK(int_gcd(17, 0) == 17);
    CHECK(int_gcd(0, 0) == 0);
    CHECK(int_gcd(13, 7) == 1); // coprime

    CHECK(int_lcm(4, 6) == 12);
    CHECK(int_lcm(21, 6) == 42);
    CHECK(int_lcm(-4, 6) == 12);
    CHECK(int_lcm(5, 0) == 0);
    // gcd * lcm == |a * b|.
    CHECK(int_gcd(84, 30) * int_lcm(84, 30) == 84 * 30);
    // Overflow is reported, not wrapped.
    CHECK_THROWS_AS(int_lcm(3037000500LL, 3037000501LL), OverflowError);
}

TEST_CASE("deterministic primality across the range") {
    CHECK_FALSE(is_prime(0));
    CHECK_FALSE(is_prime(1));
    CHECK(is_prime(2));
    CHECK(is_prime(3));
    CHECK_FALSE(is_prime(4));
    CHECK(is_prime(97));
    CHECK_FALSE(is_prime(91)); // 7 * 13
    CHECK_FALSE(is_prime(-7)); // negatives are not prime
    // Carmichael numbers (fool naive Fermat tests) are correctly composite.
    CHECK_FALSE(is_prime(561));
    CHECK_FALSE(is_prime(41041));
    // Large primes and their neighbours.
    CHECK(is_prime(1000003));
    CHECK_FALSE(is_prime(1000005));
    CHECK(is_prime(2147483647));          // 2^31 - 1 (Mersenne)
    CHECK(is_prime(9223372036854775783LL)); // largest prime < 2^63
    CHECK_FALSE(is_prime(9223372036854775807LL)); // 2^63 - 1 is composite
}

TEST_CASE("prime factorization reconstructs the input") {
    CHECK(factorize(1).empty());
    CHECK(factorize(-1).empty());

    auto f360 = factorize(360);
    CHECK(product_of(f360) == 360);
    REQUIRE(f360.size() == 3);
    CHECK(f360[0].prime == 2);
    CHECK(f360[0].exponent == 3);
    CHECK(f360[1].prime == 3);
    CHECK(f360[1].exponent == 2);
    CHECK(f360[2].prime == 5);
    CHECK(f360[2].exponent == 1);

    // A prime factors to itself^1; negatives use the magnitude.
    auto f97 = factorize(97);
    REQUIRE(f97.size() == 1);
    CHECK(f97[0].prime == 97);
    CHECK(f97[0].exponent == 1);
    CHECK(product_of(factorize(-84)) == 84);

    // A large semiprime exercises Pollard's rho (beyond trial division).
    const long long semiprime = 1000003LL * 1000033LL;
    auto fs = factorize(semiprime);
    CHECK(product_of(fs) == semiprime);
    REQUIRE(fs.size() == 2);
    CHECK(fs[0].prime == 1000003);
    CHECK(fs[1].prime == 1000033);

    // A perfect prime power.
    auto f1024 = factorize(1024);
    REQUIRE(f1024.size() == 1);
    CHECK(f1024[0].prime == 2);
    CHECK(f1024[0].exponent == 10);
}

TEST_CASE("format_factorization renders exponents and separators") {
    CHECK(format_factorization(factorize(360), " * ") == "2^3 * 3^2 * 5");
    CHECK(format_factorization(factorize(97), " * ") == "97");
    CHECK(format_factorization({}, " * ") == "1");
    CHECK(format_factorization(factorize(12), " · ") == "2^2 · 3");
}

TEST_CASE("divisors are complete, sorted, and multiplicative in count") {
    CHECK(divisors(1) == std::vector<long long>{1});
    CHECK(divisors(28) == std::vector<long long>{1, 2, 4, 7, 14, 28}); // perfect number
    CHECK(divisors(-12) == std::vector<long long>{1, 2, 3, 4, 6, 12});
    CHECK(divisors(97) == std::vector<long long>{1, 97});
    // d(360) = (3+1)(2+1)(1+1) = 24.
    CHECK(divisors(360).size() == 24);
}

TEST_CASE("Euler's totient") {
    CHECK(euler_totient(1) == 1);
    CHECK(euler_totient(9) == 6);   // 1,2,4,5,7,8
    CHECK(euler_totient(10) == 4);  // 1,3,7,9
    CHECK(euler_totient(97) == 96); // prime p → p-1
    CHECK(euler_totient(36) == 12);
    // Sum of totients over divisors of n equals n (Gauss).
    long long s = 0;
    for (long long d : divisors(36)) s += euler_totient(d);
    CHECK(s == 36);
    CHECK_THROWS_AS(euler_totient(0), EvalError);
}

TEST_CASE("divisor function sigma_k") {
    // σ_1 (sum of divisors).
    CHECK(divisor_sigma(1) == 1);
    CHECK(divisor_sigma(6) == 12);   // 1+2+3+6 (perfect number)
    CHECK(divisor_sigma(28) == 56);  // 1+2+4+7+14+28
    CHECK(divisor_sigma(12) == 28);  // 1+2+3+4+6+12
    CHECK(divisor_sigma(97) == 98);  // prime p → p+1
    // σ_0 counts divisors; σ_2 sums squares.
    CHECK(divisor_sigma(12, 0) == 6);            // d(12) = 6
    CHECK(divisor_sigma(6, 2) == 1 + 4 + 9 + 36); // 50
    // sigma is multiplicative: σ(3·4) = σ(3)·σ(4) since gcd(3,4)=1.
    CHECK(divisor_sigma(12) == divisor_sigma(3) * divisor_sigma(4));
    CHECK_THROWS_AS(divisor_sigma(0), EvalError);
    CHECK_THROWS_AS(divisor_sigma(6, -1), EvalError);
}

TEST_CASE("Mobius function") {
    CHECK(mobius(1) == 1);
    CHECK(mobius(2) == -1);  // one prime
    CHECK(mobius(6) == 1);   // 2·3, two primes
    CHECK(mobius(30) == -1); // 2·3·5, three primes
    CHECK(mobius(4) == 0);   // 2^2 — squared factor
    CHECK(mobius(12) == 0);  // 2^2·3
    CHECK(mobius(97) == -1); // prime
    // Σ_{d | n} μ(d) = [n == 1] (the Möbius identity).
    long long acc = 0;
    for (long long d : divisors(30)) acc += mobius(d);
    CHECK(acc == 0);
    CHECK_THROWS_AS(mobius(0), EvalError);
}

TEST_CASE("integer partition function p(n)") {
    CHECK(partition_count(0) == 1);
    CHECK(partition_count(1) == 1);
    CHECK(partition_count(2) == 2);   // 2, 1+1
    CHECK(partition_count(4) == 5);   // 4, 3+1, 2+2, 2+1+1, 1+1+1+1
    CHECK(partition_count(5) == 7);
    CHECK(partition_count(10) == 42);
    CHECK(partition_count(100) == 190569292);
    CHECK_THROWS_AS(partition_count(-1), EvalError);
    CHECK_THROWS_AS(partition_count(1000000), OverflowError); // beyond int64
}

TEST_CASE("Stirling numbers of the second kind") {
    CHECK(stirling_second(0, 0) == 1);
    CHECK(stirling_second(4, 0) == 0);
    CHECK(stirling_second(4, 1) == 1);   // one subset
    CHECK(stirling_second(4, 4) == 1);   // singletons
    CHECK(stirling_second(4, 2) == 7);
    CHECK(stirling_second(5, 3) == 25);
    CHECK(stirling_second(5, 2) == 15);  // 2^(n-1) - 1
    CHECK(stirling_second(6, 3) == 90);
    CHECK(stirling_second(3, 5) == 0);   // k > n
    CHECK_THROWS_AS(stirling_second(-1, 0), EvalError);
    CHECK_THROWS_AS(stirling_second(200, 100), OverflowError);
}

TEST_CASE("Bell numbers") {
    CHECK(bell_number(0) == 1);
    CHECK(bell_number(1) == 1);
    CHECK(bell_number(2) == 2);
    CHECK(bell_number(3) == 5);
    CHECK(bell_number(4) == 15);
    CHECK(bell_number(5) == 52);
    CHECK(bell_number(6) == 203);
    CHECK(bell_number(10) == 115975);
    // B(n) = Σ_k S(n, k).
    long long s = 0;
    for (long long k = 0; k <= 7; ++k) s += stirling_second(7, k);
    CHECK(s == bell_number(7)); // 877
    CHECK_THROWS_AS(bell_number(-1), EvalError);
    CHECK_THROWS_AS(bell_number(100), OverflowError);
}

TEST_CASE("Derangements (subfactorial)") {
    CHECK(derangement_count(0) == 1);
    CHECK(derangement_count(1) == 0);
    CHECK(derangement_count(2) == 1);
    CHECK(derangement_count(3) == 2);
    CHECK(derangement_count(4) == 9);
    CHECK(derangement_count(5) == 44);
    CHECK(derangement_count(6) == 265);
    CHECK(derangement_count(10) == 1334961);
    // !n = n·!(n-1) + (-1)^n, cross-checked against the two-term recurrence.
    for (long long n = 2; n <= 18; ++n) {
        const long long sign = (n % 2 == 0) ? 1 : -1;
        CHECK(derangement_count(n) == n * derangement_count(n - 1) + sign);
    }
    // !20 is the largest derangement number fitting in a signed 64-bit int.
    CHECK(derangement_count(20) == 895014631192902121LL);
    CHECK_THROWS_AS(derangement_count(-1), EvalError);
    CHECK_THROWS_AS(derangement_count(21), OverflowError);
}

TEST_CASE("Catalan numbers") {
    CHECK(catalan_number(0) == 1);
    CHECK(catalan_number(1) == 1);
    CHECK(catalan_number(2) == 2);
    CHECK(catalan_number(3) == 5);
    CHECK(catalan_number(4) == 14);
    CHECK(catalan_number(5) == 42);
    CHECK(catalan_number(10) == 16796);
    // C(35) is the largest Catalan number fitting in a signed 64-bit integer.
    CHECK(catalan_number(35) == 3116285494907301262LL);
    CHECK_THROWS_AS(catalan_number(-1), EvalError);
    CHECK_THROWS_AS(catalan_number(40), OverflowError);
}

TEST_CASE("next/prev prime and Euclidean mod") {
    CHECK(next_prime(0) == 2);
    CHECK(next_prime(7) == 11);
    CHECK(next_prime(100) == 101);
    CHECK(prev_prime(100) == 97);
    CHECK(prev_prime(2) == 0); // none below 2

    CHECK(int_mod(17, 5) == 2);
    CHECK(int_mod(-1, 5) == 4);   // result stays in [0, |m|)
    CHECK(int_mod(-7, 3) == 2);
    CHECK(int_mod(7, -3) == 1);   // sign of m does not affect the residue range
    CHECK_THROWS_AS(int_mod(1, 0), DivisionByZeroError);
}

TEST_CASE("modular exponentiation") {
    CHECK(pow_mod(7, 100, 13) == 9);
    CHECK(pow_mod(2, 10, 1000) == 24);       // 1024 mod 1000
    CHECK(pow_mod(2, 0, 7) == 1);            // b^0 = 1
    CHECK(pow_mod(0, 5, 7) == 0);
    CHECK(pow_mod(-3, 3, 7) == int_mod(-27, 7)); // negative base reduced first
    CHECK(pow_mod(5, 3, 1) == 0);            // everything is 0 mod 1
    // Fermat: a^(p-1) == 1 (mod p) for prime p, gcd(a,p)=1.
    CHECK(pow_mod(3, 10, 11) == 1);
    // Huge exponent that would overflow ordinary evaluation.
    CHECK(pow_mod(7, 1000000, 13) == pow_mod(7, 1000000 % 12, 13));
    CHECK_THROWS_AS(pow_mod(2, -1, 7), EvalError);
    CHECK_THROWS_AS(pow_mod(2, 3, 0), EvalError);
}

TEST_CASE("modular inverse") {
    CHECK(mod_inverse(3, 11) == 4);    // 3*4 = 12 == 1 (mod 11)
    CHECK(mod_inverse(10, 17) == 12);  // 10*12 = 120 == 1 (mod 17)
    CHECK(int_mod(mod_inverse(7, 100) * 7, 100) == 1);
    CHECK(mod_inverse(-3, 11) == mod_inverse(8, 11)); // -3 == 8 (mod 11)
    // Not invertible when gcd(a, m) != 1.
    CHECK_THROWS_AS(mod_inverse(6, 9), EvalError);
    CHECK_THROWS_AS(mod_inverse(2, 1), EvalError);
}

TEST_CASE("Chinese remainder theorem") {
    // The classic Sun-tzu problem: x == 2 (3), 3 (5), 2 (7) -> 23 (mod 105).
    const Crt s = crt_solve({2, 3, 2}, {3, 5, 7});
    CHECK(s.residue == 23);
    CHECK(s.modulus == 105);

    // Two congruences with coprime moduli.
    const Crt t = crt_solve({1, 2}, {4, 5});
    CHECK(t.modulus == 20);
    CHECK(int_mod(t.residue, 4) == 1);
    CHECK(int_mod(t.residue, 5) == 2);

    // Non-coprime but consistent moduli: lcm(6, 4) = 12.
    const Crt u = crt_solve({2, 2}, {6, 4});
    CHECK(u.modulus == 12);
    CHECK(int_mod(u.residue, 6) == 2);
    CHECK(int_mod(u.residue, 4) == 2);

    // Inconsistent system throws.
    CHECK_THROWS_AS(crt_solve({0, 1}, {2, 4}), EvalError);
    CHECK_THROWS_AS(crt_solve({1}, {2, 3}), EvalError); // size mismatch
}
