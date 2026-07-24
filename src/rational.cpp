#include "mathsolver/rational.hpp"

#include <cctype>

namespace mathsolver {

Rational::Rational(BigInt n, BigInt d) : num_(0), den_(1) {
    if (d.is_zero()) {
        throw DivisionByZeroError("rational with zero denominator");
    }
    if (n.is_zero()) {
        return; // 0/1
    }
    // Normalize sign onto the numerator, then reduce by the gcd.
    if (d.is_negative()) {
        n = -n;
        d = -d;
    }
    const BigInt g = BigInt::gcd(n, d); // > 0
    num_ = n / g;
    den_ = d / g;
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
    std::string digits(s.substr(int_begin, i - int_begin));
    std::size_t frac_len = 0;
    if (i < s.size() && s[i] == '.') {
        ++i;
        const std::size_t frac_begin = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
            ++i;
        }
        if (i == frac_begin) {
            return malformed(); // '.' must be followed by digits
        }
        std::string_view frac = s.substr(frac_begin, i - frac_begin);
        // Trailing fractional zeros carry no value ("0.50" is still 1/2).
        while (!frac.empty() && frac.back() == '0') {
            frac.remove_suffix(1);
        }
        digits.append(frac);
        frac_len = frac.size();
    }
    if (i != s.size()) {
        return malformed(); // trailing garbage
    }
    BigInt num(digits.empty() ? "0" : digits);
    if (negative) num = -num;
    return Rational(num, BigInt(10).pow(static_cast<unsigned long long>(frac_len)));
}

double Rational::to_double() const noexcept {
    return num_.to_double() / den_.to_double();
}

std::string Rational::to_string() const {
    if (den_.is_one()) {
        return num_.to_string();
    }
    return num_.to_string() + "/" + den_.to_string();
}

Rational Rational::pow(long long exponent) const {
    if (exponent == 0) {
        return Rational(1);
    }
    if (num_.is_zero()) {
        if (exponent < 0) {
            throw DivisionByZeroError("zero raised to a negative power");
        }
        return Rational(0);
    }
    const unsigned long long e =
        exponent < 0 ? 0ULL - static_cast<unsigned long long>(exponent)
                     : static_cast<unsigned long long>(exponent);
    const BigInt pn = num_.pow(e);
    const BigInt pd = den_.pow(e);
    if (exponent < 0) {
        return Rational(pd, pn); // constructor fixes the sign
    }
    return Rational(pn, pd);
}

Rational Rational::operator-() const {
    Rational r;
    r.num_ = -num_;
    r.den_ = den_;
    return r;
}

Rational operator+(const Rational& a, const Rational& b) {
    // a/b + c/d = (a*d + c*b) / (b*d), reduced by the constructor.
    return Rational(a.num_ * b.den_ + b.num_ * a.den_, a.den_ * b.den_);
}

Rational operator-(const Rational& a, const Rational& b) {
    return Rational(a.num_ * b.den_ - b.num_ * a.den_, a.den_ * b.den_);
}

Rational operator*(const Rational& a, const Rational& b) {
    return Rational(a.num_ * b.num_, a.den_ * b.den_);
}

Rational operator/(const Rational& a, const Rational& b) {
    if (b.is_zero()) {
        throw DivisionByZeroError("rational division by zero");
    }
    return Rational(a.num_ * b.den_, a.den_ * b.num_);
}

std::strong_ordering operator<=>(const Rational& a, const Rational& b) noexcept {
    // Cross-multiply with positive denominators: a/b <=> c/d  ==  a*d <=> c*b.
    return (a.num_ * b.den_) <=> (b.num_ * a.den_);
}

} // namespace mathsolver
