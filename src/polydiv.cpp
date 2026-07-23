#include "mathsolver/polydiv.hpp"

#include <string>
#include <vector>

#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

bool is_zero(const Expr& e) {
    const Expr s = simplify(e);
    return s->kind() == Kind::Number && s->number().is_zero();
}

/// Highest index of a non-zero coefficient, or -1 if all zero.
int effective_degree(const std::vector<Expr>& c) {
    for (int i = static_cast<int>(c.size()) - 1; i >= 0; --i) {
        if (!is_zero(c[i])) return i;
    }
    return -1;
}

bool is_zero_poly(const Expr& e, std::string_view var) {
    const auto c = polynomial_coefficients(simplify(e), var);
    return c && effective_degree(*c) < 0;
}

/// Rebuild Σ c[i]·var^i, simplified.
Expr from_coeffs(const std::vector<Expr>& c, std::string_view var) {
    const Expr x = make_sym(std::string(var));
    std::vector<Expr> terms;
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (is_zero(c[i])) continue;
        if (i == 0) terms.push_back(c[i]);
        else if (i == 1) terms.push_back(make_mul({c[i], x}));
        else terms.push_back(make_mul({c[i], make_pow(x, make_num(static_cast<long long>(i)))}));
    }
    return terms.empty() ? make_num(0) : simplify(make_add(terms));
}

/// Divide a polynomial through by its leading coefficient (monic form).
Expr monic(const Expr& poly, std::string_view var) {
    const auto co = polynomial_coefficients(simplify(poly), var);
    if (!co) return poly;
    const int deg = effective_degree(*co);
    if (deg < 0) return make_num(0);
    const Expr lead = (*co)[deg];
    std::vector<Expr> scaled;
    for (int i = 0; i <= deg; ++i) scaled.push_back(simplify(make_div((*co)[i], lead)));
    return from_coeffs(scaled, var);
}

} // namespace

PolyDivResult polynomial_divide(const Expr& dividend, const Expr& divisor,
                                std::string_view var) {
    PolyDivResult out;
    const auto num = polynomial_coefficients(simplify(dividend), var);
    const auto den = polynomial_coefficients(simplify(divisor), var);
    if (!num || !den) {
        out.status = PolyDivResult::Status::NotPolynomial;
        out.message = "polydiv: both sides must be polynomials in the variable";
        return out;
    }

    std::vector<Expr> r = *num;             // running remainder coefficients
    const std::vector<Expr>& d = *den;
    const int dd = effective_degree(d);
    if (dd < 0) {
        out.status = PolyDivResult::Status::DivByZero;
        out.message = "polydiv: divisor is zero";
        return out;
    }
    const Expr lead_d = d[dd];

    std::vector<Expr> q(1, make_num(0));
    int rdeg = effective_degree(r);
    while (rdeg >= dd) {
        const Expr factor = simplify(make_div(r[rdeg], lead_d));
        const int shift = rdeg - dd;
        if (static_cast<int>(q.size()) <= shift) q.resize(shift + 1, make_num(0));
        q[shift] = simplify(make_add({q[shift], factor}));
        // r -= factor · x^shift · d
        for (int i = 0; i <= dd; ++i) {
            r[shift + i] = simplify(make_sub(r[shift + i], make_mul({factor, d[i]})));
        }
        const int next = effective_degree(r);
        if (next >= rdeg) break; // safety: leading term failed to cancel
        rdeg = next;
    }

    out.status = PolyDivResult::Status::Ok;
    out.quotient = from_coeffs(q, var);
    out.remainder = from_coeffs(r, var);
    return out;
}

PolyGcdResult polynomial_gcd(const Expr& a, const Expr& b, std::string_view var) {
    PolyGcdResult out;
    if (!polynomial_coefficients(simplify(a), var) ||
        !polynomial_coefficients(simplify(b), var)) {
        out.status = PolyGcdResult::Status::NotPolynomial;
        out.message = "polygcd: both arguments must be polynomials in the variable";
        return out;
    }
    // Euclidean algorithm: gcd(x, y) = gcd(y, x mod y).
    Expr x = simplify(a);
    Expr y = simplify(b);
    while (!is_zero_poly(y, var)) {
        const PolyDivResult d = polynomial_divide(x, y, var);
        if (d.status != PolyDivResult::Status::Ok) break;
        x = y;
        y = d.remainder;
    }
    out.status = PolyGcdResult::Status::Ok;
    out.value = is_zero_poly(x, var) ? make_num(0) : monic(x, var);
    return out;
}

PolyGcdResult polynomial_lcm(const Expr& a, const Expr& b, std::string_view var) {
    const PolyGcdResult g = polynomial_gcd(a, b, var);
    if (g.status != PolyGcdResult::Status::Ok) return g;
    PolyGcdResult out;
    out.status = PolyGcdResult::Status::Ok;
    if (is_zero_poly(a, var) || is_zero_poly(b, var)) {
        out.value = make_num(0);
        return out;
    }
    // lcm = a·b / gcd(a, b), normalized to monic form.
    const PolyDivResult q =
        polynomial_divide(simplify(make_mul({a, b})), g.value, var);
    out.value = monic(q.quotient, var);
    return out;
}

} // namespace mathsolver
