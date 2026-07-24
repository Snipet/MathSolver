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
    // Arbitrary precision: p(1000) is exact, far beyond int64.
    CHECK(partition_count(1000) == BigInt("24061467864032622473692149727991"));
    CHECK_THROWS_AS(partition_count(20001), EvalError); // compute guard
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
    // Arbitrary precision: S(200, 100) is exact, far beyond int64.
    CHECK(stirling_second(200, 100) ==
          BigInt("2283943596473854926494186023981050257599257601238577334618926128114625773649859560873689656807578061558677832404485591735394159623463212267910289858070088234410209458409430330345615635450585241866631706977579545278456032333350189907556"));
    CHECK_THROWS_AS(stirling_second(2001, 1), EvalError); // compute guard
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
    BigInt s;
    for (long long k = 0; k <= 7; ++k) s += stirling_second(7, k);
    CHECK(s == bell_number(7)); // 877
    CHECK_THROWS_AS(bell_number(-1), EvalError);
    // Arbitrary precision: B(100) is exact, far beyond int64.
    CHECK(bell_number(100) ==
          BigInt("47585391276764833658790768841387207826363669686825611466616334637559114497892442622672724044217756306953557882560751"));
    CHECK_THROWS_AS(bell_number(2001), EvalError); // compute guard
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
    // Arbitrary precision: !30 is exact, far beyond int64.
    CHECK(derangement_count(30) == BigInt("97581073836835777732377428235481"));
    CHECK_THROWS_AS(derangement_count(-1), EvalError);
    CHECK_THROWS_AS(derangement_count(50001), EvalError); // compute guard
}

TEST_CASE("Lucas numbers") {
    CHECK(lucas_number(0) == 2);
    CHECK(lucas_number(1) == 1);
    CHECK(lucas_number(2) == 3);
    CHECK(lucas_number(3) == 4);
    CHECK(lucas_number(4) == 7);
    CHECK(lucas_number(5) == 11);
    CHECK(lucas_number(10) == 123);
    CHECK(lucas_number(20) == 15127);
    // Each term is the sum of the previous two.
    for (long long n = 2; n <= 80; ++n)
        CHECK(lucas_number(n) == lucas_number(n - 1) + lucas_number(n - 2));
    // L(90) is the largest Lucas number fitting in a signed 64-bit int.
    CHECK(lucas_number(90) == 6440026026380244498LL);
    // Arbitrary precision: L(100) is exact, far beyond int64.
    CHECK(lucas_number(100) == BigInt("792070839848372253127"));
    CHECK_THROWS_AS(lucas_number(-1), EvalError);
    CHECK_THROWS_AS(lucas_number(200001), EvalError); // compute guard
}

TEST_CASE("Primorial") {
    CHECK(primorial(0) == 1);
    CHECK(primorial(1) == 1);
    CHECK(primorial(2) == 2);
    CHECK(primorial(3) == 6);
    CHECK(primorial(5) == 30);
    CHECK(primorial(7) == 210);
    CHECK(primorial(10) == 210);  // no prime in (7, 10]
    CHECK(primorial(11) == 2310);
    CHECK(primorial(13) == 30030);
    // 47# = 52# (there is no prime in (47, 52]) is the last that fit int64.
    CHECK(primorial(47) == 614889782588491410LL);
    CHECK(primorial(52) == 614889782588491410LL);
    // Arbitrary precision: 100# is exact, far beyond int64.
    CHECK(primorial(100) == BigInt("2305567963945518424753102147331756070"));
    CHECK_THROWS_AS(primorial(-1), EvalError);
    CHECK_THROWS_AS(primorial(200001), EvalError); // compute guard
}

TEST_CASE("Motzkin numbers") {
    CHECK(motzkin_number(0) == 1);
    CHECK(motzkin_number(1) == 1);
    CHECK(motzkin_number(2) == 2);
    CHECK(motzkin_number(3) == 4);
    CHECK(motzkin_number(4) == 9);
    CHECK(motzkin_number(5) == 21);
    CHECK(motzkin_number(6) == 51);
    CHECK(motzkin_number(10) == 2188);
    CHECK(motzkin_number(15) == 310572);
    CHECK(motzkin_number(20) == 50852019);
    // Cross-check against the convolution definition M(n+1) = M(n) +
    // Σ_{k=0}^{n-1} M(k)·M(n-1-k) for small n.
    std::vector<long long> m(21);
    m[0] = 1;
    for (int nn = 0; nn < 20; ++nn) {
        long long s = m[nn];
        for (int k = 0; k < nn; ++k) s += m[k] * m[nn - 1 - k];
        m[nn + 1] = s;
    }
    for (int nn = 0; nn <= 20; ++nn) CHECK(motzkin_number(nn) == m[nn]);
    // M(44) is the largest Motzkin number fitting in a signed 64-bit int.
    CHECK(motzkin_number(44) == 4684478925507420069LL);
    // Arbitrary precision: M(50) is exact, far beyond int64.
    CHECK(motzkin_number(50) == BigInt("2837208756709314025578"));
    CHECK_THROWS_AS(motzkin_number(-1), EvalError);
    CHECK_THROWS_AS(motzkin_number(50001), EvalError); // compute guard
}

TEST_CASE("Euler numbers") {
    CHECK(euler_number(0) == 1);
    CHECK(euler_number(2) == -1);
    CHECK(euler_number(4) == 5);
    CHECK(euler_number(6) == -61);
    CHECK(euler_number(8) == 1385);
    CHECK(euler_number(10) == -50521);
    CHECK(euler_number(12) == 2702765);
    CHECK(euler_number(14) == -199360981);
    // Every odd-indexed Euler number is zero.
    for (long long n = 1; n <= 21; n += 2) CHECK(euler_number(n) == 0);
    // E(22) is the largest-magnitude Euler number fitting a signed 64-bit int.
    CHECK(euler_number(22) == -69348874393137901LL);
    // Arbitrary precision: E(30) is exact, far beyond int64 (and signed).
    CHECK(euler_number(30) == BigInt("-441543893249023104553682821"));
    CHECK_THROWS_AS(euler_number(-1), EvalError);
    CHECK_THROWS_AS(euler_number(2001), EvalError); // compute guard
}

TEST_CASE("Tribonacci numbers") {
    const long long expected[] = {0, 0, 1, 1, 2, 4, 7, 13, 24, 44, 81, 149, 274};
    for (long long n = 0; n <= 12; ++n) CHECK(tribonacci_number(n) == expected[n]);
    // Each term is the sum of the previous three.
    for (long long n = 3; n <= 70; ++n)
        CHECK(tribonacci_number(n) == tribonacci_number(n - 1) +
                                          tribonacci_number(n - 2) +
                                          tribonacci_number(n - 3));
    // T(74) is the largest tribonacci number fitting in a signed 64-bit int.
    CHECK(tribonacci_number(74) == 7015254043203144209LL);
    // Arbitrary precision: T(100) is exact, far beyond int64.
    CHECK(tribonacci_number(100) == BigInt("53324762928098149064722658"));
    CHECK_THROWS_AS(tribonacci_number(-1), EvalError);
    CHECK_THROWS_AS(tribonacci_number(200001), EvalError); // compute guard
}

TEST_CASE("Pell numbers") {
    const long long expected[] = {0, 1, 2, 5, 12, 29, 70, 169, 408, 985, 2378};
    for (long long n = 0; n <= 10; ++n) CHECK(pell_number(n) == expected[n]);
    // Each term is twice the previous plus the one before.
    for (long long n = 2; n <= 48; ++n)
        CHECK(pell_number(n) == 2 * pell_number(n - 1) + pell_number(n - 2));
    // P(50) is the largest Pell number fitting in a signed 64-bit int.
    CHECK(pell_number(50) == 4866752642924153522LL);
    // Arbitrary precision: P(100) is exact, far beyond int64.
    CHECK(pell_number(100) == BigInt("66992092050551637663438906713182313772"));
    CHECK_THROWS_AS(pell_number(-1), EvalError);
    CHECK_THROWS_AS(pell_number(200001), EvalError); // compute guard
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
    // Arbitrary precision: C(50) is exact, far beyond int64.
    CHECK(catalan_number(50) == BigInt("1978261657756160653623774456"));
    CHECK_THROWS_AS(catalan_number(-1), EvalError);
    CHECK_THROWS_AS(catalan_number(50001), EvalError); // compute guard
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
