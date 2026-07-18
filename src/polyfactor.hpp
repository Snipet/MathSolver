#pragma once

// Internal shared machinery: exact factoring of rational-coefficient
// polynomials into linear factors (rational roots with multiplicity) and
// monic irreducible-over-Q quadratics, by rational-root deflation.
// Used by apart() and rsolve(); not part of the public API.

#include <cmath>
#include <map>
#include <numeric>
#include <utility>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/rational.hpp"

namespace mathsolver::internal {

inline constexpr long long k_divisor_cap = 1'000'000'000'000LL; // 1e12

inline std::vector<long long> divisors_of(long long v) {
    if (v < 0) {
        v = -v;
    }
    if (v == 0 || v > k_divisor_cap) {
        throw Error("polynomial coefficients are too large to factor");
    }
    std::vector<long long> out;
    for (long long d = 1; d * d <= v; ++d) {
        if (v % d == 0) {
            out.push_back(d);
            if (d != v / d) {
                out.push_back(v / d);
            }
        }
    }
    return out;
}

/// Horner evaluation of an ascending-coefficient polynomial.
inline Rational eval_poly(const std::vector<Rational>& c, const Rational& x) {
    Rational acc{0};
    for (std::size_t i = c.size(); i-- > 0;) {
        acc = acc * x + c[i];
    }
    return acc;
}

/// Divide by (x - r), assuming r is a root. Ascending coefficients in/out.
inline std::vector<Rational> deflate(const std::vector<Rational>& c,
                                     const Rational& r) {
    std::vector<Rational> q(c.size() - 1, Rational{0});
    Rational carry = c.back();
    for (std::size_t i = c.size() - 1; i-- > 0;) {
        q[i] = carry;
        carry = c[i] + carry * r;
    }
    return q; // remainder (carry) is zero by assumption
}

/// Factored form: monic linear factors (x - root)^m, monic irreducible
/// quadratics (x^2 + bx + c)^m, and the extracted leading constant.
struct FactoredPoly {
    std::map<Rational, int> roots;                      // root -> multiplicity
    std::map<std::pair<Rational, Rational>, int> quads; // (b, c) -> mult
    Rational lead{1};
};

/// Factor one polynomial (ascending coefficients, degree >= 1), merging
/// `outer_mult` copies of each factor into `out`. Throws when a remainder
/// of degree >= 3 has no rational roots.
inline void factor_rational_poly(std::vector<Rational> c, int outer_mult,
                                 FactoredPoly& out) {
    const Rational lead = c.back();
    for (int m = 0; m < outer_mult; ++m) {
        out.lead = out.lead * lead;
    }
    for (Rational& v : c) {
        v = v / lead;
    }

    while (c.size() > 1 && c.front().is_zero()) {
        out.roots[Rational{0}] += outer_mult;
        c.erase(c.begin());
    }

    while (c.size() > 2) {
        long long lcm = 1;
        for (const Rational& v : c) {
            lcm = std::lcm(lcm, v.den());
            if (lcm > k_divisor_cap) {
                throw Error("polynomial coefficients are too large to factor");
            }
        }
        std::vector<long long> ic(c.size());
        for (std::size_t i = 0; i < c.size(); ++i) {
            ic[i] = c[i].num() * (lcm / c[i].den());
        }
        bool found = false;
        for (const long long p : divisors_of(ic.front())) {
            for (const long long q : divisors_of(ic.back())) {
                for (const int sign : {1, -1}) {
                    const Rational r{sign * p, q};
                    if (eval_poly(c, r).is_zero()) {
                        out.roots[r] += outer_mult;
                        c = deflate(c, r);
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (found) break;
        }
        if (!found) {
            break;
        }
    }

    if (c.size() == 1) {
        return;
    }
    if (c.size() == 2) {
        out.roots[-c[0]] += outer_mult; // monic linear: root -c0
        return;
    }
    if (c.size() == 3) {
        out.quads[{c[1], c[0]}] += outer_mult; // x^2 + c1 x + c0
        return;
    }
    throw Error(
        "cannot factor the polynomial into linear and quadratic factors over "
        "the rationals");
}

} // namespace mathsolver::internal
