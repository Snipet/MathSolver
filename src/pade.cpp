// Padé approximants (pade.hpp): the rational function P(x)/Q(x) matching a
// function's Maclaurin series to the highest order the degrees allow.
//
// Given the Taylor coefficients c_0..c_{m+n} of f about 0, the denominator
// Q(x) = 1 + q_1 x + ... + q_n x^n is fixed (up to the Q(0) = 1 normalization)
// by requiring the series of f(x)Q(x) - P(x) to vanish through order m + n.
// The coefficients of x^{m+1}..x^{m+n} give n linear equations in q_1..q_n:
//
//     sum_{j=1}^{n} q_j c_{k-j} = -c_k,   k = m+1 .. m+n   (c_i = 0 for i < 0)
//
// solved exactly via solve_system. The numerator then reads off directly:
//
//     a_k = sum_{j=0}^{min(k,n)} q_j c_{k-j},   k = 0 .. m.

#include "mathsolver/pade.hpp"

#include <algorithm>
#include <format>
#include <set>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/series.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"

namespace mathsolver {

namespace {

// The series machinery differentiates m + n times, so it inherits the same
// order cap (20! is the largest factorial fitting in long long exactly).
constexpr int k_max_total = 20;

/// Build the polynomial sum_k coeffs[k] * var^k in canonical form.
Expr build_poly(const std::vector<Expr>& coeffs, std::string_view var) {
    const Expr x = make_sym(std::string{var});
    std::vector<Expr> terms;
    terms.reserve(coeffs.size());
    for (std::size_t k = 0; k < coeffs.size(); ++k) {
        terms.push_back(coeffs[k] * make_pow(x, make_num(static_cast<long long>(k))));
    }
    return make_add(std::move(terms));
}

/// A denominator-unknown name (q1, q2, …) that does not collide with any free
/// symbol of `f`; underscores are prepended until it is clear.
std::string unknown_prefix(const std::set<std::string>& reserved, int n) {
    std::string prefix = "q";
    for (int guard = 0; guard < 64; ++guard) {
        bool clash = false;
        for (int j = 1; j <= n; ++j) {
            if (reserved.count(prefix + std::to_string(j))) {
                clash = true;
                break;
            }
        }
        if (!clash) return prefix;
        prefix = "_" + prefix;
    }
    return prefix;
}

} // namespace

PadeResult pade(const Expr& f, std::string_view var, int m, int n) {
    const std::string v{var};
    if (v.empty()) {
        throw Error("pade needs a variable");
    }
    if (m < 0 || n < 0) {
        throw Error(std::format("pade orders must be non-negative, got [{}/{}]", m, n));
    }
    const int total = m + n;
    if (total > k_max_total) {
        throw Error(std::format("pade order m + n must be in [0, {}], got {}",
                                k_max_total, total));
    }

    // Maclaurin coefficients c_0..c_total as exact expressions, degree-indexed
    // and zero-padded (series drops exactly-zero terms, so pad up to total).
    const Expr s = series(f, v, make_num(0), total);
    const auto coeffs = polynomial_coefficients(simplify(s), v);
    if (!coeffs) {
        throw Error(std::format(
            "pade: the Maclaurin series of the input is not a polynomial in {}", v));
    }
    std::vector<Expr> c = *coeffs;
    while (static_cast<int>(c.size()) < total + 1) c.push_back(make_num(0));

    // Denominator coefficients q_0..q_n, with q_0 = 1 by normalization.
    std::vector<Expr> q(n + 1, make_num(0));
    q[0] = make_num(1);

    if (n > 0) {
        const std::string prefix = unknown_prefix(free_symbols(f), n);
        std::vector<std::string> syms;
        syms.reserve(n);
        for (int j = 1; j <= n; ++j) syms.push_back(prefix + std::to_string(j));

        std::vector<Equation> eqs;
        eqs.reserve(n);
        for (int k = m + 1; k <= m + n; ++k) {
            std::vector<Expr> lhs;
            for (int j = 1; j <= n; ++j) {
                const int idx = k - j; // c_{k-j}, zero when idx < 0
                if (idx < 0) continue;
                lhs.push_back(make_sym(prefix + std::to_string(j)) * c[idx]);
            }
            eqs.push_back({make_add(std::move(lhs)), make_neg(c[k])});
        }

        const SystemSolveResult sr = solve_system(eqs, syms);
        using St = SystemSolveResult::Status;
        if (sr.status == St::Solved) {
            for (int j = 1; j <= n; ++j) {
                q[j] = sr.values.at(prefix + std::to_string(j));
            }
        } else if (sr.status == St::Underdetermined) {
            // A defective (non-normal) Padé-table entry — the denominator is
            // not unique. Take the minimal particular solution: free
            // coefficients set to 0. It still satisfies every order equation,
            // so P/Q is a valid [m/n] approximant (equal to a lower-order one).
            for (int j = 1; j <= n; ++j) {
                const std::string name = prefix + std::to_string(j);
                const auto it = sr.values.find(name);
                Expr val = it != sr.values.end() ? it->second : make_num(0);
                for (const std::string& fv : sr.free_variables) {
                    val = substitute(val, fv, make_num(0));
                }
                q[j] = simplify(val);
            }
        } else {
            // Inconsistent or non-linear: the approximant genuinely fails.
            throw Error(std::format(
                "pade: the [{}/{}] approximant does not exist for this input",
                m, n));
        }
    }

    // Numerator a_k = sum_{j=0}^{min(k,n)} q_j c_{k-j}, k = 0..m.
    std::vector<Expr> a(m + 1, make_num(0));
    for (int k = 0; k <= m; ++k) {
        std::vector<Expr> terms;
        for (int j = 0; j <= std::min(k, n); ++j) {
            terms.push_back(q[j] * c[k - j]);
        }
        a[k] = make_add(std::move(terms));
    }

    const Expr P = simplify(build_poly(a, v));
    const Expr Q = simplify(build_poly(q, v));
    const Expr approx = simplify(make_div(P, Q));
    return {approx, P, Q, m, n};
}

} // namespace mathsolver
