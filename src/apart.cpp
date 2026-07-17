// Partial-fraction expansion (apart.hpp).
//
// Pipeline per additive term:
//   1. Split the term's factors into numerator and `(base)^-m` denominators.
//   2. Factor each base by rational-root deflation (multiplicities merge
//      across bases, so (x+1)^-1 * (x^2+3x+2)^-1 yields (x+1)^2 (x+2)).
//   3. Reduce an improper numerator by polynomial division.
//   4. Solve for the fraction coefficients by undetermined coefficients over
//      the exact Gaussian-elimination system solver.

#include "mathsolver/apart.hpp"

#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"

namespace mathsolver {

namespace {

bool is_zero(const Expr& e) {
    return e->kind() == Kind::Number && e->number().is_zero();
}

std::optional<Rational> as_num(const Expr& e) {
    if (e->kind() == Kind::Number) {
        return e->number();
    }
    return std::nullopt;
}

std::vector<Expr> factors_of(const Expr& e) {
    if (e->kind() == Kind::Mul) {
        return e->args();
    }
    return {e};
}

Expr product(std::vector<Expr> fs) {
    if (fs.empty()) {
        return make_num(1);
    }
    if (fs.size() == 1) {
        return fs.front();
    }
    return simplify(make_mul(std::move(fs)));
}

// --- exact polynomial factoring by rational-root deflation ------------------

constexpr long long k_divisor_cap = 1'000'000'000'000LL; // 1e12

std::vector<long long> divisors_of(long long v) {
    if (v < 0) {
        v = -v;
    }
    if (v == 0 || v > k_divisor_cap) {
        throw Error("apart: denominator coefficients are too large to factor");
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
Rational eval_poly(const std::vector<Rational>& c, const Rational& x) {
    Rational acc{0};
    for (std::size_t i = c.size(); i-- > 0;) {
        acc = acc * x + c[i];
    }
    return acc;
}

/// Divide by (x - r), assuming r is a root. Ascending coefficients in/out.
std::vector<Rational> deflate(const std::vector<Rational>& c, const Rational& r) {
    std::vector<Rational> q(c.size() - 1, Rational{0});
    Rational carry = c.back();
    for (std::size_t i = c.size() - 1; i-- > 0;) {
        q[i] = carry;
        carry = c[i] + carry * r;
    }
    return q; // carry is the remainder, zero by assumption
}

/// The factored denominator: monic linear factors (x - root)^m, monic
/// irreducible quadratics (x^2 + bx + c)^m, and the constant pulled out.
struct FactoredDen {
    std::map<Rational, int> linear;                          // root -> mult
    std::map<std::pair<Rational, Rational>, int> quads;      // (b, c) -> mult
    Rational lead{1};
};

/// Factor one monic-normalized base polynomial, merging `outer_mult` copies
/// of each of its factors into `den`.
void factor_base(std::vector<Rational> c, int outer_mult, FactoredDen& den) {
    // Normalize monic, folding the leading coefficient into den.lead.
    const Rational lead = c.back();
    for (int m = 0; m < outer_mult; ++m) {
        den.lead = den.lead * lead;
    }
    for (Rational& v : c) {
        v = v / lead;
    }

    // Roots at zero.
    while (c.size() > 1 && c.front().is_zero()) {
        den.linear[Rational{0}] += outer_mult;
        c.erase(c.begin());
    }

    // Rational roots by the rational-root theorem on the integer-cleared
    // polynomial, deflating on every hit (re-deriving candidates each time).
    while (c.size() > 2) {
        long long lcm = 1;
        for (const Rational& v : c) {
            lcm = std::lcm(lcm, v.den());
            if (lcm > k_divisor_cap) {
                throw Error("apart: denominator coefficients are too large to factor");
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
                        den.linear[r] += outer_mult;
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
        return; // fully deflated (monic remainder is the constant 1)
    }
    if (c.size() == 2) {
        // A monic linear remainder root is rational; the loop above exits
        // only at degree <= 2, so this arises solely from a degree-1 base.
        den.linear[-c[0]] += outer_mult;
        return;
    }
    if (c.size() == 3) {
        den.quads[{c[1], c[0]}] += outer_mult; // x^2 + c1 x + c0
        return;
    }
    throw Error(
        "apart: cannot factor the denominator into linear and quadratic "
        "factors over the rationals");
}

/// (x - root) or (x^2 + bx + c) as an Expr.
Expr linear_factor(const std::string& var, const Rational& root) {
    return simplify(make_sub(make_sym(var), make_num(root)));
}

Expr quad_factor(const std::string& var, const Rational& b, const Rational& c) {
    const Expr x = make_sym(var);
    return simplify(make_add(
        {make_pow(x, make_num(2)), make_mul({make_num(b), x}), make_num(c)}));
}

/// Ascending-coefficient polynomial as an Expr.
Expr poly_expr(const std::vector<Expr>& coeffs, const std::string& var) {
    std::vector<Expr> terms;
    for (std::size_t k = 0; k < coeffs.size(); ++k) {
        if (is_zero(coeffs[k])) {
            continue;
        }
        if (k == 0) {
            terms.push_back(coeffs[k]);
        } else {
            terms.push_back(
                make_mul({coeffs[k], make_pow(make_sym(var), make_num(
                                          static_cast<long long>(k)))}));
        }
    }
    if (terms.empty()) {
        return make_num(0);
    }
    return simplify(make_add(std::move(terms)));
}

/// Multiply an ascending Rational polynomial by (x - r) or a quadratic —
/// used to build the full denominator for the polynomial-division step.
std::vector<Rational> mul_linear(std::vector<Rational> p, const Rational& r) {
    std::vector<Rational> out(p.size() + 1, Rational{0});
    for (std::size_t i = 0; i < p.size(); ++i) {
        out[i + 1] = out[i + 1] + p[i];
        out[i] = out[i] - p[i] * r;
    }
    return out;
}

std::vector<Rational> mul_quad(std::vector<Rational> p, const Rational& b,
                               const Rational& c) {
    std::vector<Rational> out(p.size() + 2, Rational{0});
    for (std::size_t i = 0; i < p.size(); ++i) {
        out[i + 2] = out[i + 2] + p[i];
        out[i + 1] = out[i + 1] + p[i] * b;
        out[i] = out[i] + p[i] * c;
    }
    return out;
}

/// One decomposed term of the result.
struct PartTerm {
    Expr numerator;  ///< coefficient (linear) or alpha*x + beta (quadratic)
    Expr factor;     ///< the monic factor expression
    int power;
};

Expr apart_term(const Expr& term_in, const std::string& var) {
    const Expr term = simplify(term_in);

    // 1. Split numerator factors from (base)^-m denominator factors.
    std::vector<Expr> num_factors;
    std::vector<std::pair<Expr, int>> den_bases;
    for (const Expr& f : factors_of(term)) {
        if (f->kind() == Kind::Pow && contains_symbol(f->arg(0), var)) {
            const auto e = as_num(f->arg(1));
            if (e && e->is_integer() && e->num() < 0) {
                den_bases.emplace_back(f->arg(0), static_cast<int>(-e->num()));
                continue;
            }
        }
        num_factors.push_back(f);
    }
    if (den_bases.empty()) {
        return term; // nothing to decompose
    }

    const Expr n_expr = product(std::move(num_factors));
    const auto n_coeffs = polynomial_coefficients(n_expr, var);
    if (!n_coeffs) {
        throw Error(std::string("apart: the numerator of '") +
                    to_string(term, PrintStyle::Plain) +
                    "' is not a polynomial in " + var);
    }

    // 2. Factor every base, merging multiplicities.
    FactoredDen den;
    for (const auto& [base, mult] : den_bases) {
        const auto bc = polynomial_coefficients(base, var);
        if (!bc) {
            throw Error(std::string("apart: the denominator factor '") +
                        to_string(base, PrintStyle::Plain) +
                        "' is not a polynomial in " + var);
        }
        std::vector<Rational> rc;
        for (const Expr& coefficient : *bc) {
            const auto r = as_num(coefficient);
            if (!r) {
                throw Error(
                    "apart: denominator coefficients must be numeric (got '" +
                    to_string(coefficient, PrintStyle::Plain) + "')");
            }
            rc.push_back(*r);
        }
        factor_base(std::move(rc), mult, den);
    }

    // Total denominator degree and the full monic polynomial.
    int degree = 0;
    std::vector<Rational> d_monic{Rational{1}};
    for (const auto& [root, m] : den.linear) {
        degree += m;
        for (int i = 0; i < m; ++i) {
            d_monic = mul_linear(std::move(d_monic), root);
        }
    }
    for (const auto& [bc, m] : den.quads) {
        degree += 2 * m;
        for (int i = 0; i < m; ++i) {
            d_monic = mul_quad(std::move(d_monic), bc.first, bc.second);
        }
    }

    // 3. Fold the constant into the numerator; divide out an improper part.
    std::vector<Expr> rem;
    for (const Expr& coefficient : *n_coeffs) {
        rem.push_back(simplify(make_div(coefficient, make_num(den.lead))));
    }
    std::vector<Expr> quotient;
    const std::size_t dd = d_monic.size() - 1; // divisor degree, >= 1
    if (rem.size() - 1 >= dd) {
        quotient.assign(rem.size() - dd, make_num(0));
        for (std::size_t k = rem.size() - 1; k >= dd; --k) {
            const Expr qc = simplify(rem[k]); // monic divisor: no division
            quotient[k - dd] = qc;
            if (!is_zero(qc)) {
                for (std::size_t i = 0; i <= dd; ++i) {
                    rem[k - dd + i] = simplify(make_sub(
                        rem[k - dd + i], make_mul({qc, make_num(d_monic[i])})));
                }
            }
        }
        rem.resize(dd);
    }
    const Expr r_expr = poly_expr(rem, var);

    // 4. Undetermined coefficients: unknown numerators over each factor
    //    power, multiplied through by the monic denominator.
    std::vector<PartTerm> parts;
    std::vector<std::string> unknowns;
    auto fresh = [&]() {
        std::string name = "__pf" + std::to_string(unknowns.size());
        unknowns.push_back(name);
        return make_sym(name);
    };
    std::vector<Expr> lhs_terms; // numerator * cofactor
    auto cofactor = [&](const Expr& skip_factor, int skip_power) {
        std::vector<Expr> fs;
        for (const auto& [root, m] : den.linear) {
            const Expr f = linear_factor(var, root);
            const int p =
                m - (f->kind() == skip_factor->kind() &&
                             to_string(f, PrintStyle::Plain) ==
                                 to_string(skip_factor, PrintStyle::Plain)
                         ? skip_power
                         : 0);
            if (p > 0) {
                fs.push_back(make_pow(f, make_num(p)));
            }
        }
        for (const auto& [bc, m] : den.quads) {
            const Expr f = quad_factor(var, bc.first, bc.second);
            const int p = m - (to_string(f, PrintStyle::Plain) ==
                                       to_string(skip_factor, PrintStyle::Plain)
                                   ? skip_power
                                   : 0);
            if (p > 0) {
                fs.push_back(make_pow(f, make_num(p)));
            }
        }
        return product(std::move(fs));
    };
    for (const auto& [root, m] : den.linear) {
        const Expr f = linear_factor(var, root);
        for (int j = 1; j <= m; ++j) {
            const Expr u = fresh();
            parts.push_back({u, f, j});
            lhs_terms.push_back(make_mul({u, cofactor(f, j)}));
        }
    }
    for (const auto& [bc, m] : den.quads) {
        const Expr f = quad_factor(var, bc.first, bc.second);
        for (int j = 1; j <= m; ++j) {
            const Expr u1 = fresh();
            const Expr u2 = fresh();
            parts.push_back(
                {simplify(make_add({make_mul({u1, make_sym(var)}), u2})), f, j});
            lhs_terms.push_back(
                make_mul({make_add({make_mul({u1, make_sym(var)}), u2}),
                          cofactor(f, j)}));
        }
    }

    const Expr eq_expr =
        simplify(make_sub(make_add(std::move(lhs_terms)), r_expr));
    const auto eq_coeffs = polynomial_coefficients(eq_expr, var);
    if (!eq_coeffs) {
        throw Error("apart: internal error building the coefficient system");
    }
    std::vector<Equation> eqs;
    for (const Expr& coefficient : *eq_coeffs) {
        eqs.push_back({coefficient, make_num(0)});
    }
    const SystemSolveResult sys = solve_system(eqs, unknowns);
    if (sys.status != SystemSolveResult::Status::Solved) {
        throw Error("apart: the coefficient system did not solve uniquely");
    }

    // 5. Assemble: quotient polynomial + the nonzero fraction terms.
    std::vector<Expr> out;
    const Expr q_expr = poly_expr(quotient, var);
    if (!is_zero(q_expr)) {
        out.push_back(q_expr);
    }
    for (const PartTerm& p : parts) {
        Expr numerator = p.numerator;
        for (const auto& [name, value] : sys.values) {
            numerator = substitute(numerator, name, value);
        }
        numerator = simplify(numerator);
        if (is_zero(numerator)) {
            continue;
        }
        out.push_back(simplify(make_mul(
            {numerator,
             make_pow(p.factor, make_num(static_cast<long long>(-p.power)))})));
    }
    if (out.empty()) {
        return make_num(0);
    }
    if (out.size() == 1) {
        return out.front();
    }
    return make_add(std::move(out));
}

} // namespace

Expr apart(const Expr& e, std::string_view var) {
    const std::string v{var};
    if (v.empty()) {
        throw Error("apart needs a variable");
    }
    const Expr s = simplify(e);
    const std::vector<Expr> terms =
        s->kind() == Kind::Add ? s->args() : std::vector<Expr>{s};
    std::vector<Expr> raw;
    for (const Expr& term : terms) {
        const Expr d = apart_term(term, v);
        if (d->kind() == Kind::Add) {
            for (const Expr& t : d->args()) {
                raw.push_back(t);
            }
        } else if (!is_zero(d)) {
            raw.push_back(d);
        }
    }

    // Merge like denominators across input terms (a sum decomposes termwise,
    // so e.g. 2/(x+1) from one term and -1/(x+1) from another combine here).
    std::vector<std::string> key_order;
    std::map<std::string, std::vector<Expr>> numerators;
    std::map<std::string, Expr> denominators;
    for (const Expr& term : raw) {
        std::vector<Expr> num_fs;
        std::vector<Expr> den_fs;
        for (const Expr& f : factors_of(term)) {
            const auto p = f->kind() == Kind::Pow ? as_num(f->arg(1))
                                                  : std::nullopt;
            if (p && p->is_integer() && p->num() < 0 &&
                contains_symbol(f->arg(0), v)) {
                den_fs.push_back(f);
            } else {
                num_fs.push_back(f);
            }
        }
        const Expr den = product(den_fs);
        const std::string key = to_string(den, PrintStyle::Plain);
        if (!numerators.contains(key)) {
            key_order.push_back(key);
            denominators[key] = den;
        }
        numerators[key].push_back(product(num_fs));
    }
    std::vector<Expr> out;
    for (const std::string& key : key_order) {
        const Expr numerator = simplify(make_add(std::move(numerators[key])));
        if (is_zero(numerator)) {
            continue;
        }
        out.push_back(simplify(make_mul({numerator, denominators[key]})));
    }

    if (out.empty()) {
        return make_num(0);
    }
    if (out.size() == 1) {
        return out.front();
    }
    return make_add(std::move(out));
}

} // namespace mathsolver
