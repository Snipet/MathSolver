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

namespace {

/// The resultant recursion over the polynomial remainder. Both inputs are
/// polynomials (checked by the caller). res(f, g) is 0 when they share a root.
Expr resultant_rec(const Expr& f, const Expr& g, std::string_view var) {
    const auto cf = polynomial_coefficients(simplify(f), var);
    const auto cg = polynomial_coefficients(simplify(g), var);
    const int m = cf ? effective_degree(*cf) : -1;
    const int n = cg ? effective_degree(*cg) : -1;
    if (m < 0 || n < 0) return make_num(0); // a zero polynomial
    if (n == 0) return simplify(make_pow((*cg)[0], make_num(m))); // g constant: g^m
    if (m == 0) return simplify(make_pow((*cf)[0], make_num(n))); // f constant: f^n

    const auto sign = [](int k) { return make_num((k % 2 == 0) ? 1 : -1); };
    if (m < n) {
        // res(f, g) = (-1)^(m·n) res(g, f); keep the first degree the larger.
        return simplify(make_mul({sign(m * n), resultant_rec(g, f, var)}));
    }
    const Expr r = polynomial_divide(f, g, var).remainder;
    const auto crv = polynomial_coefficients(simplify(r), var);
    const int p = crv ? effective_degree(*crv) : -1;
    if (p < 0) return make_num(0); // remainder 0 → common factor → resultant 0
    // res(f, g) = (-1)^(m·n) · lc(g)^(m - p) · res(g, r).
    return simplify(make_mul({sign(m * n),
                              make_pow((*cg)[n], make_num(m - p)),
                              resultant_rec(g, r, var)}));
}

} // namespace

PolyGcdResult polynomial_resultant(const Expr& a, const Expr& b, std::string_view var) {
    PolyGcdResult out;
    if (!polynomial_coefficients(simplify(a), var) ||
        !polynomial_coefficients(simplify(b), var)) {
        out.status = PolyGcdResult::Status::NotPolynomial;
        out.message = "resultant: both arguments must be polynomials in the variable";
        return out;
    }
    out.status = PolyGcdResult::Status::Ok;
    out.value = simplify(resultant_rec(a, b, var));
    return out;
}

PolyBezoutResult polynomial_bezout(const Expr& a, const Expr& b, std::string_view var) {
    PolyBezoutResult out;
    if (!polynomial_coefficients(simplify(a), var) ||
        !polynomial_coefficients(simplify(b), var)) {
        out.status = PolyBezoutResult::Status::NotPolynomial;
        out.message = "bezout: both arguments must be polynomials in the variable";
        return out;
    }
    // Extended Euclidean algorithm. Invariant: s_i·a + t_i·b = r_i throughout,
    // so when r reaches the gcd its (s, t) are the Bézout cofactors.
    Expr r0 = simplify(a), r1 = simplify(b);
    Expr s0 = make_num(1), s1 = make_num(0);
    Expr t0 = make_num(0), t1 = make_num(1);
    // Degree strictly drops each step, so the loop terminates; the cap is a
    // safety net against a division that fails to reduce.
    for (int guard = 0; !is_zero_poly(r1, var) && guard < 4096; ++guard) {
        const PolyDivResult d = polynomial_divide(r0, r1, var);
        if (d.status != PolyDivResult::Status::Ok) break;
        const Expr q = d.quotient;
        Expr r2 = simplify(d.remainder);
        Expr s2 = simplify(make_sub(s0, make_mul({q, s1})));
        Expr t2 = simplify(make_sub(t0, make_mul({q, t1})));
        r0 = r1; r1 = r2;
        s0 = s1; s1 = s2;
        t0 = t1; t1 = t2;
    }

    out.status = PolyBezoutResult::Status::Ok;
    if (is_zero_poly(r0, var)) { // a = b = 0
        out.gcd = make_num(0);
        out.s = make_num(0);
        out.t = make_num(0);
        return out;
    }
    // Normalize to a monic gcd, scaling the cofactors by the same leading factor.
    const auto co = polynomial_coefficients(simplify(r0), var);
    const int deg = co ? effective_degree(*co) : -1;
    const Expr lead = (deg >= 0) ? (*co)[deg] : make_num(1);
    out.gcd = simplify(make_div(r0, lead));
    out.s = simplify(make_div(s0, lead));
    out.t = simplify(make_div(t0, lead));
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

CompanionResult companion_matrix(const Expr& poly, std::string_view var) {
    CompanionResult out;
    const auto co = polynomial_coefficients(simplify(poly), var);
    if (!co) {
        out.status = CompanionResult::Status::NotPolynomial;
        out.message = "companion: input must be a polynomial in the variable";
        return out;
    }
    const int n = effective_degree(*co);
    if (n < 1) {
        out.status = CompanionResult::Status::DegreeTooLow;
        out.message = "companion: polynomial must have degree ≥ 1";
        return out;
    }
    const Expr lead = (*co)[n];  // nonzero by construction of effective_degree

    // n×n, top row = (-a_{n-1}, ..., -a_1, -a_0) with a_k = c[k]/lead,
    // first subdiagonal all ones, everything else zero.
    std::vector<std::vector<Expr>> m(
        static_cast<std::size_t>(n), std::vector<Expr>(static_cast<std::size_t>(n), make_num(0)));
    for (int j = 0; j < n; ++j) {
        const Expr coeff = (*co)[n - 1 - j];
        m[0][static_cast<std::size_t>(j)] =
            simplify(make_neg(make_div(coeff, lead)));
    }
    for (int i = 1; i < n; ++i) {
        m[static_cast<std::size_t>(i)][static_cast<std::size_t>(i - 1)] = make_num(1);
    }

    out.status = CompanionResult::Status::Ok;
    out.matrix = std::move(m);
    return out;
}

} // namespace mathsolver
