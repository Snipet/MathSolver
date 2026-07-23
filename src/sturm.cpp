// Exact real-root counting and isolation via Sturm's theorem (sturm.hpp).
//
// For a univariate polynomial p with rational coefficients, the Sturm chain
//   p_0 = p,  p_1 = p',  p_{i+1} = -rem(p_{i-1}, p_i)
// has the property that V(a) - V(b), the drop in sign variations of the chain
// evaluated at a and b, equals the number of DISTINCT real roots in (a, b] —
// independent of multiplicity. Over all of R the count is V(-inf) - V(+inf),
// read from the leading coefficients. Everything runs in exact rational
// arithmetic; each chain polynomial is reduced to a primitive integer form
// (positive scaling only, so signs — all that Sturm needs — are preserved),
// which keeps the coefficients from blowing up.
//
// Isolation subdivides a Cauchy-bounded interval, using the same variation
// count per sub-interval, until each holds exactly one root, then refines it.
// Exact rational roots are pulled out first (rational-root theorem) so they can
// be reported exactly; a midpoint-lands-on-a-root nudge keeps the subdivision
// correct even for any rational root the bounded pre-pass skips.

#include "mathsolver/sturm.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

using Poly = std::vector<Rational>; // coefficient at index k multiplies x^k

/// Drop leading (highest-degree) zero coefficients; the zero polynomial becomes
/// empty.
void trim(Poly& p) {
    while (!p.empty() && p.back().is_zero()) p.pop_back();
}

int degree(const Poly& p) { return static_cast<int>(p.size()) - 1; }

Rational eval(const Poly& p, const Rational& x) {
    Rational acc{0};
    for (int i = degree(p); i >= 0; --i) acc = acc * x + p[i];
    return acc;
}

int sign_of(const Rational& r) { return r.is_zero() ? 0 : (r.is_negative() ? -1 : 1); }

int sign_at(const Poly& p, const Rational& x) { return sign_of(eval(p, x)); }

Poly derivative(const Poly& p) {
    if (degree(p) < 1) return {};
    Poly d(p.size() - 1);
    for (int i = 1; i <= degree(p); ++i) d[i - 1] = p[i] * Rational(i);
    trim(d);
    return d;
}

/// Remainder of a divided by b (deg b >= 0), over exact rationals.
Poly poly_rem(Poly a, const Poly& b) {
    const int db = degree(b);
    const Rational lead = b[db];
    while (degree(a) >= db && !a.empty()) {
        const int da = degree(a);
        const Rational factor = a[da] / lead;
        const int shift = da - db;
        for (int i = 0; i <= db; ++i) a[i + shift] = a[i + shift] - factor * b[i];
        a[da] = Rational{0}; // exact cancellation of the leading term
        trim(a);
    }
    return a;
}

/// Scale a polynomial to a primitive integer form by a positive factor (clear
/// denominators, divide out the integer content). Sign structure is preserved.
void make_primitive(Poly& p) {
    if (p.empty()) return;
    long long l = 1; // lcm of denominators
    for (const Rational& c : p) {
        const long long d = c.den();
        l = l / std::gcd(l, d) * d;
    }
    long long g = 0; // gcd of the cleared numerators
    std::vector<long long> ints(p.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
        ints[i] = p[i].num() * (l / p[i].den());
        g = std::gcd(g, std::llabs(ints[i]));
    }
    if (g == 0) return;
    for (std::size_t i = 0; i < p.size(); ++i) p[i] = Rational(ints[i] / g);
}

std::vector<Poly> sturm_chain(const Poly& p) {
    std::vector<Poly> chain;
    chain.push_back(p);
    Poly d = derivative(p);
    if (!d.empty()) {
        make_primitive(d);
        chain.push_back(d);
    }
    while (degree(chain.back()) > 0) {
        Poly r = poly_rem(chain[chain.size() - 2], chain.back());
        trim(r);
        if (r.empty()) break;
        for (Rational& c : r) c = -c; // p_{i+1} = -rem(...)
        make_primitive(r);
        chain.push_back(std::move(r));
    }
    return chain;
}

int variations(const std::vector<int>& signs) {
    int v = 0;
    int prev = 0;
    for (int s : signs) {
        if (s == 0) continue;
        if (prev != 0 && s != prev) ++v;
        prev = s;
    }
    return v;
}

int variations_at(const std::vector<Poly>& chain, const Rational& x) {
    std::vector<int> signs;
    signs.reserve(chain.size());
    for (const Poly& p : chain) signs.push_back(sign_at(p, x));
    return variations(signs);
}

/// Sign variations at +infinity (dir = +1) or -infinity (dir = -1), read from
/// each polynomial's leading term: sign(lead) * dir^deg.
int variations_at_infinity(const std::vector<Poly>& chain, int dir) {
    std::vector<int> signs;
    signs.reserve(chain.size());
    for (const Poly& p : chain) {
        if (p.empty()) {
            signs.push_back(0);
            continue;
        }
        int s = sign_of(p.back());
        if (dir < 0 && (degree(p) % 2 != 0)) s = -s;
        signs.push_back(s);
    }
    return variations(signs);
}

/// Cauchy bound: every real root r satisfies |r| < 1 + max_{i<n} |c_i / c_n|.
Rational cauchy_bound(const Poly& p) {
    const int n = degree(p);
    const Rational lead = p[n];
    Rational m{0};
    for (int i = 0; i < n; ++i) {
        Rational ratio = p[i] / lead;
        if (ratio.is_negative()) ratio = -ratio;
        if (ratio > m) m = ratio;
    }
    return m + Rational(1);
}

/// The univariate coefficient vector of `poly` in `var`, requiring rational
/// (numeric) coefficients. Throws on a non-polynomial / symbolic coefficients /
/// the zero polynomial.
Poly to_poly(const Expr& poly, std::string_view var) {
    const auto coeffs = polynomial_coefficients(simplify(poly), var);
    if (!coeffs) {
        throw Error(std::format(
            "root isolation needs a polynomial in {} with numeric coefficients",
            var));
    }
    Poly p;
    p.reserve(coeffs->size());
    for (const Expr& c : *coeffs) {
        if (c->kind() != Kind::Number) {
            throw Error(
                "root isolation needs numeric coefficients (no free parameters)");
        }
        p.push_back(c->number());
    }
    trim(p);
    if (p.empty()) {
        throw Error("the zero polynomial has every real number as a root");
    }
    return p;
}

double horner_double(const std::vector<double>& c, double x) {
    double acc = 0.0;
    for (int i = static_cast<int>(c.size()) - 1; i >= 0; --i) acc = acc * x + c[i];
    return acc;
}

/// A precise numeric root inside a bracket [lo, hi] known to hold exactly one
/// root. Sign bisection when the endpoints straddle zero (simple roots);
/// otherwise (an even-multiplicity root, where the sign does not change) the
/// bracket midpoint — already tight from the exact rational pass.
double refine_double(const std::vector<double>& c, double lo, double hi) {
    double flo = horner_double(c, lo);
    const double fhi = horner_double(c, hi);
    if (flo == 0.0) return lo;
    if (fhi == 0.0) return hi;
    if (flo * fhi > 0.0) return (lo + hi) / 2.0; // no sign change: even multiplicity
    for (int i = 0; i < 200 && hi > lo; ++i) {
        const double m = (lo + hi) / 2.0;
        if (m == lo || m == hi) break;
        const double fm = horner_double(c, m);
        if (fm == 0.0) return m;
        if (flo * fm < 0.0) {
            hi = m;
        } else {
            lo = m;
            flo = fm;
        }
    }
    return (lo + hi) / 2.0;
}

std::vector<long long> divisors(long long n) {
    std::vector<long long> ds;
    for (long long i = 1; i * i <= n; ++i) {
        if (n % i == 0) {
            ds.push_back(i);
            if (i != n / i) ds.push_back(n / i);
        }
    }
    return ds;
}

/// Divide p by (x - r) exactly (synthetic division); caller guarantees r is a
/// root so the remainder is zero.
Poly deflate(const Poly& p, const Rational& r) {
    const int n = degree(p);
    Poly q(n); // degree n-1
    Rational carry{0};
    for (int i = n; i >= 1; --i) {
        carry = p[i] + carry * r;
        q[i - 1] = carry;
    }
    trim(q);
    return q;
}

/// Pull out the distinct rational roots (rational-root theorem), deflating each
/// fully. Best-effort: skipped when the integer bounds are large; any rational
/// root left behind is still handled correctly by the isolation nudge.
void extract_rational_roots(Poly& p, std::vector<Rational>& exact) {
    // Factor out x = 0 to its full multiplicity.
    if (!p.empty() && p[0].is_zero()) {
        exact.push_back(Rational(0));
        int k = 0;
        while (k < static_cast<int>(p.size()) && p[k].is_zero()) ++k;
        p.erase(p.begin(), p.begin() + k);
        trim(p);
    }
    if (degree(p) < 1) return;

    // Clear denominators to an integer polynomial for the p|a0, q|an test.
    long long l = 1;
    for (const Rational& c : p) l = l / std::gcd(l, c.den()) * c.den();
    const long long a0 = std::llabs(p[0].num() * (l / p[0].den()));
    const long long an = std::llabs(p[degree(p)].num() * (l / p[degree(p)].den()));
    constexpr long long k_cap = 2'000'000; // keep divisor enumeration cheap
    if (a0 == 0 || an == 0 || a0 > k_cap || an > k_cap) return;

    for (long long num : divisors(a0)) {
        for (long long den : divisors(an)) {
            for (int s : {1, -1}) {
                const Rational cand(s * num, den);
                bool changed = true;
                while (changed && degree(p) >= 1 && eval(p, cand).is_zero()) {
                    if (std::find(exact.begin(), exact.end(), cand) == exact.end())
                        exact.push_back(cand);
                    p = deflate(p, cand);
                    changed = true;
                }
            }
        }
    }
}

} // namespace

int sturm_root_count(const Expr& poly, std::string_view var,
                     const std::optional<Rational>& lo,
                     const std::optional<Rational>& hi) {
    const Poly p = to_poly(poly, var);
    if (degree(p) == 0) return 0; // nonzero constant: no roots
    const std::vector<Poly> chain = sturm_chain(p);
    if (lo && hi) {
        if (!(*lo < *hi)) throw Error("root count needs lo < hi");
        return variations_at(chain, *lo) - variations_at(chain, *hi);
    }
    return variations_at_infinity(chain, -1) - variations_at_infinity(chain, +1);
}

std::vector<RootInterval> sturm_isolate_roots(const Expr& poly,
                                              std::string_view var) {
    Poly p = to_poly(poly, var);
    std::vector<RootInterval> out;
    if (degree(p) == 0) return out;

    // Exact rational roots first (reported exactly), deflating them away.
    std::vector<Rational> exact;
    extract_rational_roots(p, exact);
    for (const Rational& r : exact) {
        out.push_back({r, r, true, r.to_double()});
    }

    // Isolate the remaining (irrational) roots with Sturm subdivision. Because
    // the pre-pass removed the rational roots, chosen dyadic endpoints are not
    // roots of `p`; the nudge below is a safety net for any skipped ones. All
    // exact-rational work is guarded against 64-bit overflow (deep dyadic
    // bisection grows denominators); on overflow we stop tightening and finish
    // the numeric approximation in double precision.
    if (degree(p) >= 1) {
        std::vector<double> pc; // coefficients as doubles for numeric refinement
        pc.reserve(p.size());
        for (const Rational& c : p) pc.push_back(c.to_double());

        try {
            const std::vector<Poly> chain = sturm_chain(p);
            const Rational bound = cauchy_bound(p);
            const int total =
                variations_at(chain, -bound) - variations_at(chain, bound);
            if (total > 0) {
                struct Seg {
                    Rational a, b;
                    int count;
                };
                std::vector<Seg> stack{{-bound, bound, total}};
                std::vector<std::pair<Rational, Rational>> brackets;
                int guard = 0;
                while (!stack.empty() && guard++ < 100000) {
                    Seg seg = stack.back();
                    stack.pop_back();
                    if (seg.count <= 0) continue;
                    if (seg.count == 1) {
                        brackets.emplace_back(seg.a, seg.b);
                        continue;
                    }
                    Rational m = (seg.a + seg.b) / Rational(2);
                    // Never split on a root: nudge toward b until m is a non-root.
                    int nudge = 0;
                    while (eval(p, m).is_zero() && nudge++ < 64) {
                        m = (m + seg.b) / Rational(2);
                    }
                    const int vm = variations_at(chain, m);
                    stack.push_back({seg.a, m, variations_at(chain, seg.a) - vm});
                    stack.push_back({m, seg.b, vm - variations_at(chain, seg.b)});
                }
                // Tighten each single-root bracket, choosing the half by the
                // Sturm count (not a sign change) so even-multiplicity roots —
                // e.g. the double root of (x^2 - 2)^2, where p keeps its sign —
                // are bracketed correctly too. Best-effort: stop on overflow.
                for (auto& [a, b] : brackets) {
                    try {
                        int va = variations_at(chain, a);
                        for (int it = 0;
                             it < 40 && (b - a) > Rational(1, 100'000'000); ++it) {
                            const Rational m = (a + b) / Rational(2);
                            if (eval(p, m).is_zero()) {
                                a = m;
                                b = m;
                                break;
                            }
                            const int vm = variations_at(chain, m);
                            if (va - vm == 1) {
                                b = m;
                            } else {
                                a = m;
                                va = vm;
                            }
                        }
                    } catch (const OverflowError&) {
                        // Keep the current (coarser) rational bracket.
                    }
                    const bool exactHit = (a == b);
                    const double approx =
                        exactHit ? a.to_double()
                                 : refine_double(pc, a.to_double(), b.to_double());
                    out.push_back({a, b, exactHit, approx});
                }
            }
        } catch (const OverflowError&) {
            throw Error(
                "root isolation: the coefficients or root spacing exceed the "
                "engine's exact 64-bit precision");
        }
    }

    std::sort(out.begin(), out.end(),
              [](const RootInterval& x, const RootInterval& y) {
                  return x.approx < y.approx;
              });
    return out;
}

} // namespace mathsolver
