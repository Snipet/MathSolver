#include "mathsolver/orthopoly.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

// A dense polynomial as coefficients over the rationals: coeff[i] multiplies
// x^i. All recurrences below operate on these vectors so every step stays exact.
using Poly = std::vector<Rational>;

// x · p — shift every coefficient up one power.
Poly shift(const Poly& p) {
    Poly r(p.size() + 1, Rational(0));
    for (std::size_t i = 0; i < p.size(); ++i) r[i + 1] = p[i];
    return r;
}

Poly scale(const Poly& p, const Rational& c) {
    Poly r = p;
    for (Rational& v : r) v = v * c;
    return r;
}

// a − b (either may be shorter).
Poly sub(const Poly& a, const Poly& b) {
    Poly r(std::max(a.size(), b.size()), Rational(0));
    for (std::size_t i = 0; i < a.size(); ++i) r[i] = r[i] + a[i];
    for (std::size_t i = 0; i < b.size(); ++i) r[i] = r[i] - b[i];
    return r;
}

// The base cases p0, p1 and the (k → k+1) step for each family. Returning the
// coefficient vector for degree n keeps the whole computation in exact Rational
// arithmetic; the caller turns it into an Expr once at the end.
Poly generate(OrthoFamily fam, int n) {
    const Poly x{Rational(0), Rational(1)}; // the polynomial "x"
    Poly p0{Rational(1)};
    Poly p1;
    switch (fam) {
        case OrthoFamily::ChebyshevT: p1 = x; break;
        case OrthoFamily::ChebyshevU: p1 = scale(x, Rational(2)); break;
        case OrthoFamily::Legendre:   p1 = x; break;
        case OrthoFamily::Hermite:    p1 = scale(x, Rational(2)); break;
        case OrthoFamily::Laguerre:   p1 = sub(Poly{Rational(1)}, x); break;
    }
    if (n == 0) return p0;
    if (n == 1) return p1;

    Poly prev = p0, cur = p1;
    for (int k = 1; k < n; ++k) {
        Poly next;
        switch (fam) {
            case OrthoFamily::ChebyshevT:
            case OrthoFamily::ChebyshevU:
                // next = 2x·cur − prev
                next = sub(scale(shift(cur), Rational(2)), prev);
                break;
            case OrthoFamily::Legendre:
                // next = ((2k+1)x·cur − k·prev) / (k+1)
                next = scale(sub(scale(shift(cur), Rational(2 * k + 1)),
                                 scale(prev, Rational(k))),
                             Rational(1, k + 1));
                break;
            case OrthoFamily::Hermite:
                // next = 2x·cur − 2k·prev
                next = sub(scale(shift(cur), Rational(2)), scale(prev, Rational(2 * k)));
                break;
            case OrthoFamily::Laguerre:
                // next = ((2k+1)·cur − x·cur − k·prev) / (k+1)
                next = scale(sub(sub(scale(cur, Rational(2 * k + 1)), shift(cur)),
                                 scale(prev, Rational(k))),
                             Rational(1, k + 1));
                break;
        }
        prev = std::move(cur);
        cur = std::move(next);
    }
    return cur;
}

// coeff vector → simplified Expr in `variable`.
Expr to_expr(const Poly& p, std::string_view variable) {
    const Expr x = make_sym(std::string(variable));
    std::vector<Expr> terms;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].is_zero()) continue;
        Expr coeff = make_num(p[i]);
        if (i == 0) terms.push_back(coeff);
        else if (i == 1) terms.push_back(coeff * x);
        else terms.push_back(coeff * make_pow(x, make_num(static_cast<long long>(i))));
    }
    if (terms.empty()) return make_num(0);
    return simplify(make_add(std::move(terms)));
}

std::string label(OrthoFamily fam) {
    switch (fam) {
        case OrthoFamily::ChebyshevT: return "Chebyshev T";
        case OrthoFamily::ChebyshevU: return "Chebyshev U";
        case OrthoFamily::Legendre:   return "Legendre";
        case OrthoFamily::Hermite:    return "Hermite";
        case OrthoFamily::Laguerre:   return "Laguerre";
    }
    return "orthogonal";
}

std::string lower(std::string_view s) {
    std::string r(s);
    std::ranges::transform(r, r.begin(), [](unsigned char c) { return std::tolower(c); });
    return r;
}

} // namespace

OrthoPolyResult ortho_poly(OrthoFamily family, int n, std::string_view variable) {
    OrthoPolyResult r;
    r.family = label(family);
    r.variable = std::string(variable);
    r.degree = n;
    if (n < 0) {
        r.message = "degree must be a non-negative integer";
        return r;
    }
    try {
        r.expr = to_expr(generate(family, n), variable);
    } catch (const OverflowError&) {
        r.message = "degree too large — the exact coefficients overflow the rational range";
        return r;
    }
    r.status = OrthoPolyResult::Status::Ok;
    return r;
}

std::optional<OrthoFamily> parse_ortho_family(std::string_view name) {
    const std::string s = lower(name);
    if (s == "chebyshev" || s == "chebyshevt" || s == "chebyt" || s == "cheby" || s == "t")
        return OrthoFamily::ChebyshevT;
    if (s == "chebyshevu" || s == "chebyu" || s == "u") return OrthoFamily::ChebyshevU;
    if (s == "legendre" || s == "p") return OrthoFamily::Legendre;
    if (s == "hermite" || s == "h") return OrthoFamily::Hermite;
    if (s == "laguerre" || s == "l") return OrthoFamily::Laguerre;
    return std::nullopt;
}

} // namespace mathsolver
