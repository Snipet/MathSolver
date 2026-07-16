#include "mathsolver/rational.hpp"

#include <cctype>
#include <climits>
#include <numeric>

namespace mathsolver {

namespace {

unsigned long long magnitude(long long x) noexcept {
    return x < 0 ? 0ULL - static_cast<unsigned long long>(x)
                 : static_cast<unsigned long long>(x);
}

long long checked_mul(long long a, long long b) {
    long long r = 0;
    if (__builtin_mul_overflow(a, b, &r)) {
        throw OverflowError("rational arithmetic overflow in multiplication");
    }
    return r;
}

/// Exact division of a long long by a positive value known to divide it.
/// Never overflows (the divisor is positive, so the magnitude shrinks).
long long exact_div(long long a, unsigned long long positive_divisor) noexcept {
    const bool neg = a < 0;
    const unsigned long long q = magnitude(a) / positive_divisor;
    return neg ? static_cast<long long>(0ULL - q) : static_cast<long long>(q);
}

unsigned __int128 magnitude128(__int128 x) noexcept {
    return x < 0 ? static_cast<unsigned __int128>(0) - static_cast<unsigned __int128>(x)
                 : static_cast<unsigned __int128>(x);
}

unsigned __int128 gcd128(unsigned __int128 a, unsigned __int128 b) noexcept {
    while (b != 0) {
        const unsigned __int128 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/// Combine a +/- b exactly in 128 bits, reduce by gcd, and range-check the
/// reduced result once at the end. Any a, b with 64-bit components combine
/// without overflowing __int128, so a representable result is never rejected.
Rational add_or_sub(const Rational& a, const Rational& b, bool subtract) {
    const __int128 left = static_cast<__int128>(a.num()) * b.den();
    const __int128 right = static_cast<__int128>(b.num()) * a.den();
    const __int128 num = subtract ? left - right : left + right;
    if (num == 0) {
        return Rational(0);
    }
    const __int128 den = static_cast<__int128>(a.den()) * b.den(); // > 0
    const unsigned __int128 g = gcd128(magnitude128(num), static_cast<unsigned __int128>(den));
    const unsigned __int128 un = magnitude128(num) / g;
    const unsigned __int128 ud = static_cast<unsigned __int128>(den) / g;
    const bool negative = num < 0;
    if (ud > static_cast<unsigned __int128>(LLONG_MAX)) {
        throw OverflowError(subtract ? "rational arithmetic overflow in subtraction"
                                     : "rational arithmetic overflow in addition");
    }
    const unsigned __int128 num_limit =
        negative ? static_cast<unsigned __int128>(magnitude(LLONG_MIN))
                 : static_cast<unsigned __int128>(LLONG_MAX);
    if (un > num_limit) {
        throw OverflowError(subtract ? "rational arithmetic overflow in subtraction"
                                     : "rational arithmetic overflow in addition");
    }
    const long long num_ll =
        negative ? static_cast<long long>(0ULL - static_cast<unsigned long long>(un))
                 : static_cast<long long>(un);
    return Rational(num_ll, static_cast<long long>(ud));
}

/// base^exponent by checked square-and-multiply.
long long checked_pow_ll(long long base, unsigned long long exponent) {
    long long result = 1;
    while (exponent > 0) {
        if (exponent & 1ULL) {
            result = checked_mul(result, base);
        }
        exponent >>= 1;
        if (exponent > 0) {
            base = checked_mul(base, base);
        }
    }
    return result;
}

} // namespace

Rational::Rational(long long n, long long d) : num_(0), den_(1) {
    if (d == 0) {
        throw DivisionByZeroError("rational with zero denominator");
    }
    if (n == 0) {
        return; // 0/1
    }
    const unsigned long long un_raw = magnitude(n);
    const unsigned long long ud_raw = magnitude(d);
    const unsigned long long g = std::gcd(un_raw, ud_raw);
    const unsigned long long un = un_raw / g;
    const unsigned long long ud = ud_raw / g;
    const bool negative = (n < 0) != (d < 0);
    if (ud > static_cast<unsigned long long>(LLONG_MAX)) {
        throw OverflowError("rational denominator overflow");
    }
    if (negative) {
        if (un > magnitude(LLONG_MIN)) {
            throw OverflowError("rational numerator overflow");
        }
        num_ = static_cast<long long>(0ULL - un);
    } else {
        if (un > static_cast<unsigned long long>(LLONG_MAX)) {
            throw OverflowError("rational numerator overflow");
        }
        num_ = static_cast<long long>(un);
    }
    den_ = static_cast<long long>(ud);
}

Rational Rational::from_decimal_string(std::string_view s) {
    const auto malformed = [&]() -> Rational {
        throw ParseError("malformed decimal literal '" + std::string(s) + "'", 0, s.size());
    };
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && s[i] == '-') {
        negative = true;
        ++i;
    }
    const std::size_t int_begin = i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
        ++i;
    }
    if (i == int_begin) {
        return malformed(); // no digits before optional '.'
    }
    std::string_view int_digits = s.substr(int_begin, i - int_begin);
    std::string_view frac_digits;
    if (i < s.size() && s[i] == '.') {
        ++i;
        const std::size_t frac_begin = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
            ++i;
        }
        if (i == frac_begin) {
            return malformed(); // '.' must be followed by digits
        }
        frac_digits = s.substr(frac_begin, i - frac_begin);
    }
    if (i != s.size()) {
        return malformed(); // trailing garbage
    }

    // Trailing fractional zeros carry no value; dropping them avoids
    // spurious overflow ("0.50000000000000000000" is still 1/2).
    while (!frac_digits.empty() && frac_digits.back() == '0') {
        frac_digits.remove_suffix(1);
    }

    const unsigned long long limit = negative
        ? magnitude(LLONG_MIN)
        : static_cast<unsigned long long>(LLONG_MAX);
    unsigned long long num = 0;
    const auto push_digit = [&](char c) {
        const auto digit = static_cast<unsigned long long>(c - '0');
        if (num > (limit - digit) / 10ULL) {
            throw OverflowError("decimal literal does not fit in a 64-bit rational");
        }
        num = num * 10ULL + digit;
    };
    for (char c : int_digits) {
        push_digit(c);
    }
    for (char c : frac_digits) {
        push_digit(c);
    }
    const long long den = checked_pow_ll(10, frac_digits.size());
    const long long signed_num = negative ? static_cast<long long>(0ULL - num)
                                          : static_cast<long long>(num);
    return Rational(signed_num, den);
}

double Rational::to_double() const noexcept {
    return static_cast<double>(num_) / static_cast<double>(den_);
}

std::string Rational::to_string() const {
    if (den_ == 1) {
        return std::to_string(num_);
    }
    return std::to_string(num_) + "/" + std::to_string(den_);
}

Rational Rational::pow(long long exponent) const {
    if (exponent == 0) {
        return Rational(1);
    }
    if (num_ == 0) {
        if (exponent < 0) {
            throw DivisionByZeroError("zero raised to a negative power");
        }
        return Rational(0);
    }
    const unsigned long long e = magnitude(exponent);
    const long long pn = checked_pow_ll(num_, e);
    const long long pd = checked_pow_ll(den_, e);
    if (exponent < 0) {
        return Rational(pd, pn); // constructor fixes the sign
    }
    return Rational(pn, pd);
}

Rational Rational::operator-() const {
    if (num_ == LLONG_MIN) {
        throw OverflowError("rational arithmetic overflow in negation");
    }
    return Rational(-num_, den_);
}

Rational operator+(const Rational& a, const Rational& b) {
    // Combine exactly in 128 bits, reduce, and range-check the final reduced
    // value only — a result that fits in 64 bits never spuriously overflows
    // (e.g. LLONG_MAX/2 + LLONG_MAX/2 == LLONG_MAX is fine).
    return add_or_sub(a, b, /*subtract=*/false);
}

Rational operator-(const Rational& a, const Rational& b) {
    return add_or_sub(a, b, /*subtract=*/true);
}

Rational operator*(const Rational& a, const Rational& b) {
    // Cross-cancel before multiplying to avoid spurious overflow.
    const unsigned long long g1 =
        std::gcd(magnitude(a.num_), static_cast<unsigned long long>(b.den_));
    const unsigned long long g2 =
        std::gcd(magnitude(b.num_), static_cast<unsigned long long>(a.den_));
    const long long num = checked_mul(exact_div(a.num_, g1), exact_div(b.num_, g2));
    const long long den = checked_mul(exact_div(a.den_, g2), exact_div(b.den_, g1));
    return Rational(num, den);
}

Rational operator/(const Rational& a, const Rational& b) {
    if (b.is_zero()) {
        throw DivisionByZeroError("rational division by zero");
    }
    // a/b = (a.num * b.den) / (a.den * b.num). Cross-cancel with unsigned
    // magnitudes before assembling (mirroring operator*), so a divisor whose
    // numerator is LLONG_MIN never forces an unrepresentable intermediate
    // reciprocal: LLONG_MIN / LLONG_MIN == 1, 2 / LLONG_MIN == -1/2^62.
    unsigned long long an = magnitude(a.num_);
    auto ad = static_cast<unsigned long long>(a.den_);
    unsigned long long bn = magnitude(b.num_);
    auto bd = static_cast<unsigned long long>(b.den_);
    const unsigned long long g1 = std::gcd(an, bn); // g1 > 0: b.num != 0
    an /= g1;
    bn /= g1;
    if (an != 0) { // a == 0 short-circuits to 0/1 below
        const unsigned long long g2 = std::gcd(ad, bd);
        ad /= g2;
        bd /= g2;
    }
    unsigned long long num_mag = 0;
    unsigned long long den_mag = 0;
    if (__builtin_mul_overflow(an, bd, &num_mag) || __builtin_mul_overflow(ad, bn, &den_mag)) {
        throw OverflowError("rational arithmetic overflow in division");
    }
    if (den_mag > static_cast<unsigned long long>(LLONG_MAX)) {
        throw OverflowError("rational arithmetic overflow in division");
    }
    const bool negative = (a.num_ < 0) != (b.num_ < 0);
    if (negative) {
        if (num_mag > magnitude(LLONG_MIN)) {
            throw OverflowError("rational arithmetic overflow in division");
        }
        return Rational(static_cast<long long>(0ULL - num_mag), static_cast<long long>(den_mag));
    }
    if (num_mag > static_cast<unsigned long long>(LLONG_MAX)) {
        throw OverflowError("rational arithmetic overflow in division");
    }
    return Rational(static_cast<long long>(num_mag), static_cast<long long>(den_mag));
}

std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept {
    // Cross-multiply in 128 bits: cannot overflow for any 64-bit operands.
    const __int128 lhs = static_cast<__int128>(a.num_) * b.den_;
    const __int128 rhs = static_cast<__int128>(b.num_) * a.den_;
    if (lhs < rhs) {
        return std::strong_ordering::less;
    }
    if (lhs > rhs) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

} // namespace mathsolver
