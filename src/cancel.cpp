// Rational-expression cancellation via univariate polynomial GCD over Q[x]
// (docs/proposals/cancel-poly-gcd.md). Splits a rational expression into
// numerator N and denominator D (by the sign of integer Pow exponents,
// exactly as the integrator's partial-fractions stage does), and — when both
// are single-variable polynomials with rational Number coefficients — divides
// both by their polynomial GCD, returning the normalized quotient.
//
// The whole pipeline runs over exact overflow-checked Rational arithmetic and
// obeys the 64-bit doctrine (DESIGN.md §3, §12): any OverflowError anywhere
// inside is caught and the (simplified) input is returned unchanged. Formal
// cancellation only, same domain caveat as §7's x/x -> 1.
//
// v1 refuses (returns the input unchanged) on: no denominator, non-polynomial
// parts, symbolic coefficients, more than one symbol, degree over the cap, or
// any overflow. The helper polynomial routines (rational_poly_coeffs,
// poly_divide, poly_from_coeffs, checked_mul_ll) are file-local copies of the
// ones duplicated in src/integrate.cpp and src/solver.cpp (proposal §10).

#include <cstdlib>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

constexpr std::size_t kMaxCancelDegree = 32;  // per side, after expansion (§4.6)

bool checked_mul_ll(long long a, long long b, long long& out) {
    return !__builtin_mul_overflow(a, b, &out);
}

/// All-Number coefficients of a polynomial in `sym`, indexed by degree
/// (c[0] constant term, back() nonzero); nullopt for non-polynomial shapes
/// and symbolic-parameter coefficients. The zero polynomial returns {0}.
std::optional<std::vector<Rational>> rational_poly_coeffs(const Expr& e,
                                                          const std::string& sym) {
    const auto coeffs = polynomial_coefficients(e, sym);
    if (!coeffs) return std::nullopt;
    std::vector<Rational> out;
    out.reserve(coeffs->size());
    for (const Expr& c : *coeffs) {
        if (c->kind() != Kind::Number) return std::nullopt;
        out.push_back(c->number());
    }
    return out;
}

bool is_zero_poly(const std::vector<Rational>& p) {
    return p.size() == 1 && p[0].is_zero();
}

/// Content of a nonzero coefficient vector: a positive rational,
/// gcd(|num|) / lcm(den). Exact (arbitrary precision).
Rational content(const std::vector<Rational>& p) {
    BigInt ng(0); // gcd of |numerators|
    BigInt dl(1); // lcm of denominators
    for (const Rational& c : p) {
        ng = BigInt::gcd(ng, c.num().abs());
        const BigInt g = BigInt::gcd(dl, c.den());
        dl = dl / g * c.den();
    }
    if (ng.is_zero()) ng = BigInt(1); // all-zero: content 1 (only reached for {0})
    return Rational(ng, dl);
}

/// Primitive part: p / content(p), with positive leading coefficient. The
/// result has integer coefficients whose collective gcd is 1.
std::vector<Rational> primitive_part(const std::vector<Rational>& p) {
    const Rational c = content(p);
    std::vector<Rational> out;
    out.reserve(p.size());
    for (const Rational& v : p) out.push_back(v / c);
    if (!out.empty() && out.back().is_negative())
        for (Rational& v : out) v = -v;
    return out;
}

/// gcd of two positive rationals: gcd(num) / lcm(den). Exact (arbitrary
/// precision).
Rational gcd_Q(const Rational& a, const Rational& b) {
    const BigInt n = BigInt::gcd(a.num(), b.num());
    const BigInt g = BigInt::gcd(a.den(), b.den());
    const BigInt d = a.den() / g * b.den();
    return Rational(n, d);
}

/// Exact polynomial long division: num = quot*den + rem, deg rem < deg den.
/// Requires den.back() != 0. Handles a degree-0 (constant nonzero) divisor,
/// for which the remainder is exactly {0}.
void poly_divide(std::vector<Rational> num, const std::vector<Rational>& den,
                 std::vector<Rational>& quot, std::vector<Rational>& rem) {
    const std::size_t dd = den.size() - 1;
    if (num.size() <= dd) {
        quot = {Rational(0)};
        rem = std::move(num);
        return;
    }
    quot.assign(num.size() - dd, Rational(0));
    for (std::size_t k = num.size() - 1;; --k) {
        const Rational f = num[k] / den[dd];
        quot[k - dd] = f;
        if (!f.is_zero())
            for (std::size_t i = 0; i <= dd; ++i)
                num[k - dd + i] = num[k - dd + i] - f * den[i];
        if (k == dd) break;
    }
    num.resize(dd);
    while (num.size() > 1 && num.back().is_zero()) num.pop_back();
    if (num.empty()) num.push_back(Rational(0));  // dd == 0: remainder is zero
    rem = std::move(num);
}

std::vector<Rational> poly_mul(const std::vector<Rational>& a,
                               const std::vector<Rational>& b) {
    std::vector<Rational> out(a.size() + b.size() - 1, Rational(0));
    for (std::size_t i = 0; i < a.size(); ++i)
        for (std::size_t j = 0; j < b.size(); ++j)
            out[i + j] = out[i + j] + a[i] * b[j];
    return out;
}

/// Structural equality of coefficient vectors after trailing-zero trim.
bool poly_equal(std::vector<Rational> a, std::vector<Rational> b) {
    while (a.size() > 1 && a.back().is_zero()) a.pop_back();
    while (b.size() > 1 && b.back().is_zero()) b.pop_back();
    return a == b;
}

/// Euclidean GCD of two nonzero primitive polynomials, primitive-part
/// normalized at every step; returns a primitive polynomial. A nonzero
/// constant divisor means the two are coprime (gcd 1).
std::vector<Rational> poly_gcd(std::vector<Rational> a, std::vector<Rational> b) {
    if (a.size() < b.size()) std::swap(a, b);
    while (!is_zero_poly(b)) {
        if (b.size() == 1) return {Rational(1)};  // nonzero constant -> coprime
        std::vector<Rational> quot, rem;
        poly_divide(a, b, quot, rem);
        a = std::move(b);
        b = is_zero_poly(rem) ? std::vector<Rational>{Rational(0)} : primitive_part(rem);
    }
    return primitive_part(a);
}

/// Polynomial Expr from ascending Rational coefficients (0 -> Number 0).
Expr poly_from_coeffs(const std::vector<Rational>& coeffs, const Expr& x) {
    std::vector<Expr> terms;
    for (std::size_t k = 0; k < coeffs.size(); ++k) {
        if (coeffs[k].is_zero()) continue;
        terms.push_back(make_mul({make_num(coeffs[k]),
                                  make_pow(x, make_num(static_cast<long long>(k)))}));
    }
    return make_add(std::move(terms));
}

/// The §4 pipeline over an already-simplified expression `s`. Returns the
/// cancelled Expr, or `s` unchanged on any refuse/bail path. May throw
/// OverflowError, which the public wrapper contains.
Expr cancel_simplified(const Expr& s) {
    // §4.1 numerator/denominator split by the sign of integer Pow exponents.
    std::vector<Expr> num_factors;
    std::vector<Expr> den_factors;
    const auto classify = [&](const Expr& f) {
        if (f->kind() == Kind::Pow && f->arg(1)->kind() == Kind::Number) {
            const Rational& r = f->arg(1)->number();
            if (r.is_integer() && r.is_negative()) {
                den_factors.push_back(make_pow(f->arg(0), make_num(-r)));
                return;
            }
        }
        num_factors.push_back(f);
    };
    if (s->kind() == Kind::Mul) {
        for (const Expr& f : s->args()) classify(f);
    } else {
        classify(s);
    }
    if (den_factors.empty()) return s;  // nothing to cancel

    const Expr N = make_mul(std::move(num_factors));  // empty list -> 1
    const Expr D = make_mul(std::move(den_factors));

    // §4.2 single-symbol requirement.
    std::set<std::string> vars = free_symbols(N);
    for (const std::string& v : free_symbols(D)) vars.insert(v);
    if (vars.size() != 1) return s;
    const std::string x_name = *vars.begin();
    const Expr x = make_sym(x_name);

    const auto Nc = rational_poly_coeffs(N, x_name);
    const auto Dc = rational_poly_coeffs(D, x_name);
    if (!Nc || !Dc) return s;                 // non-polynomial / symbolic coeffs
    if (Dc->size() < 2) return s;             // denominator has no symbol
    // §4.6 degree cap (deg = size - 1).
    if (Nc->size() - 1 > kMaxCancelDegree || Dc->size() - 1 > kMaxCancelDegree)
        return s;
    if (is_zero_poly(*Nc)) return s;          // 0/D already folded to 0 upstream

    // §4.3-4.4 GCD including content.
    const Rational cN = content(*Nc);
    const Rational cD = content(*Dc);
    std::vector<Rational> g = poly_gcd(primitive_part(*Nc), primitive_part(*Dc));
    const Rational cg = gcd_Q(cN, cD);
    for (Rational& v : g) v = v * cg;

    // deg g == 0 && g == 1: nothing cancels.
    if (g.size() == 1 && g[0].is_one()) return s;

    // §4.5 quotients + mandatory internal verification.
    std::vector<Rational> Nq, Nr, Dq, Dr;
    poly_divide(*Nc, g, Nq, Nr);
    poly_divide(*Dc, g, Dq, Dr);
    if (!is_zero_poly(Nr) || !is_zero_poly(Dr)) return s;      // must divide exactly
    if (!poly_equal(poly_mul(Nq, g), *Nc)) return s;          // belt & braces
    if (!poly_equal(poly_mul(Dq, g), *Dc)) return s;

    // Sign normal form: make the denominator's leading coefficient positive.
    if (Dq.back().is_negative()) {
        for (Rational& v : Nq) v = -v;
        for (Rational& v : Dq) v = -v;
    }

    // §4.5 rebuild.
    const Expr N_expr = poly_from_coeffs(Nq, x);
    Expr result;
    if (Dq.size() == 1) {
        // Plain polynomial quotient; Dq[0] > 0 by the sign rule, possibly != 1.
        const Rational inv = Rational(1) / Dq[0];
        result = make_mul({make_num(inv), N_expr});
    } else {
        result = make_div(N_expr, poly_from_coeffs(Dq, x));
    }
    return simplify(result);
}

}  // namespace

Expr cancel(const Expr& e) {
    const Expr s = simplify(e);
    try {
        return cancel_simplified(s);
    } catch (const OverflowError&) {
        return s;  // §4.7 bail-to-unchanged
    }
}

Equation cancel(const Equation& eq) {
    return Equation{cancel(eq.lhs), cancel(eq.rhs)};
}

}  // namespace mathsolver
