#include "mathsolver/cfrac.hpp"

#include <cmath>
#include <cstdint>
#include <string>

#include "mathsolver/errors.hpp"

namespace mathsolver {

namespace {

/// Floor of p/q for q > 0 (C++ integer division truncates toward zero).
long long floor_div(long long p, long long q) {
    long long d = p / q;
    if ((p % q != 0) && ((p < 0) != (q < 0))) --d;
    return d;
}

/// Integer square root of n >= 0 (largest r with r*r <= n).
long long isqrt(long long n) {
    if (n < 0) return 0;
    long long r = static_cast<long long>(std::sqrt(static_cast<double>(n)));
    while (r > 0 && r * r > n) --r;
    while ((r + 1) * (r + 1) <= n) ++r;
    return r;
}

/// Build convergents from a quotient sequence, stopping before any product or
/// sum would overflow 64 bits (so the list may be shorter than the input).
void fill_convergents(CFrac& cf, const std::vector<long long>& quotients) {
    long long hPrev = 1, hPrev2 = 0; // numerators   p_{-1}=1, p_{-2}=0
    long long kPrev = 0, kPrev2 = 1; // denominators q_{-1}=0, q_{-2}=1
    for (long long a : quotients) {
        const __int128_t h = static_cast<__int128_t>(a) * hPrev + hPrev2;
        const __int128_t k = static_cast<__int128_t>(a) * kPrev + kPrev2;
        if (h > INT64_MAX || h < INT64_MIN || k > INT64_MAX || k < INT64_MIN) {
            break; // convergent no longer fits — stop, keeping what we have
        }
        const long long hi = static_cast<long long>(h);
        const long long ki = static_cast<long long>(k);
        cf.convergents.emplace_back(hi, ki);
        hPrev2 = hPrev;
        hPrev = hi;
        kPrev2 = kPrev;
        kPrev = ki;
    }
}

} // namespace

CFrac cf_rational(long long p, long long q) {
    if (q == 0) throw DivisionByZeroError{"continued fraction of x/0"};
    if (q < 0) {
        p = -p;
        q = -q;
    }
    CFrac cf;
    cf.kind = CFrac::Kind::Rational;
    cf.exact = true;
    // Euclidean expansion: a_i = floor(p/q), then (p, q) <- (q, p - a*q).
    while (q != 0) {
        const long long a = floor_div(p, q);
        cf.terms.push_back(a);
        const long long r = p - a * q; // 0 <= r < q
        p = q;
        q = r;
    }
    fill_convergents(cf, cf.terms);
    return cf;
}

CFrac cf_sqrt(long long n) {
    if (n < 1) throw EvalError{"continued fraction of sqrt requires n >= 1"};
    const long long a0 = isqrt(n);
    if (a0 * a0 == n) return cf_rational(a0, 1); // perfect square

    CFrac cf;
    cf.kind = CFrac::Kind::Surd;
    cf.exact = true;
    cf.terms.push_back(a0);
    // Classic (m, d, a) recurrence; the period ends when a == 2*a0. __int128_t
    // intermediates keep d*a and m*m from overflowing for large n.
    long long m = 0, d = 1, a = a0;
    do {
        m = static_cast<long long>(static_cast<__int128_t>(d) * a - m);
        d = static_cast<long long>((static_cast<__int128_t>(n) - static_cast<__int128_t>(m) * m) / d);
        a = static_cast<long long>((static_cast<__int128_t>(a0) + m) / d);
        cf.terms.push_back(a);
    } while (a != 2 * a0);
    cf.period_start = 1; // a0 is aperiodic; the rest repeats
    // Convergents are best-rational-approximations, so unroll the period a few
    // times (fill_convergents stops on overflow) — [1; 2] alone would only give
    // 1 and 3/2, but sqrt(2)'s useful approximations are 7/5, 17/12, 41/29, ….
    std::vector<long long> unrolled = cf.terms;
    const std::size_t plen = cf.terms.size() - 1; // period length (a0 excluded)
    for (std::size_t pi = 0; plen > 0 && unrolled.size() < 14; pi = (pi + 1) % plen) {
        unrolled.push_back(cf.terms[1 + pi]);
    }
    fill_convergents(cf, unrolled);
    return cf;
}

CFrac cf_numeric(double x, int max_terms) {
    CFrac cf;
    cf.kind = CFrac::Kind::Numeric;
    cf.exact = false;
    if (!std::isfinite(x)) return cf;
    double y = x;
    for (int i = 0; i < max_terms; ++i) {
        const double fl = std::floor(y);
        if (fl > 9.0e15 || fl < -9.0e15) break; // past exact double integers
        cf.terms.push_back(static_cast<long long>(fl));
        const double frac = y - fl;
        if (frac < 1e-9) break; // remainder negligible — effectively terminated
        y = 1.0 / frac;
    }
    fill_convergents(cf, cf.terms);
    return cf;
}

std::string format_cfrac(const CFrac& cf) {
    if (cf.terms.empty()) return "[]";
    std::string out = "[";
    for (std::size_t i = 0; i < cf.terms.size(); ++i) {
        if (i == 0) {
            out += std::to_string(cf.terms[i]);
            out += cf.terms.size() > 1 ? "; " : "";
        } else {
            if (i == static_cast<std::size_t>(cf.period_start)) out += "(";
            out += std::to_string(cf.terms[i]);
            if (i + 1 < cf.terms.size()) out += ", ";
        }
    }
    if (cf.period_start > 0) out += ")";
    else if (cf.kind == CFrac::Kind::Numeric) out += ", …";
    out += "]";
    return out;
}

std::string format_cfrac_latex(const CFrac& cf) {
    if (cf.terms.empty()) return "[\\,]";
    std::string out = "[";
    for (std::size_t i = 0; i < cf.terms.size(); ++i) {
        if (i == 0) {
            out += std::to_string(cf.terms[i]);
            out += cf.terms.size() > 1 ? "; " : "";
        } else {
            if (i == static_cast<std::size_t>(cf.period_start)) out += "\\overline{";
            out += std::to_string(cf.terms[i]);
            if (i + 1 < cf.terms.size()) out += ", ";
        }
    }
    if (cf.period_start > 0) out += "}";
    else if (cf.kind == CFrac::Kind::Numeric) out += ", \\dots";
    out += "]";
    return out;
}

} // namespace mathsolver
