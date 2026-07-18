#include "mathsolver/integrate.hpp"

// DESIGN.md §8b — rule-based indefinite integration:
//   1. linearity, 2. table with linear inner arguments, 3. polynomial /
//   expansion (retry-once guard), 4. derivative-divides u-substitution
//   (fresh-symbol recursion, depth 8), 5. pattern-bounded integration by
//   parts (incl. the cyclic e^(ax)*sin/cos(bx) closed forms), 6. rational
//   functions by partial fractions (§9 rational-root factoring + unknown
//   coefficients solved exactly with solve_system), 7. trig powers
//   (half-angle rewrite for squares; peel-and-Pythagoras for odd powers).
// Every candidate antiderivative is self-verified by differentiating it
// back and comparing numerically before it is returned.
// Definite integrals (§8b): FTC on a verified antiderivative behind a
// 64-interval evaluability grid and a quadrature cross-check; adaptive
// Simpson fallback otherwise.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"

namespace mathsolver {
namespace {

constexpr int kMaxSubstitutionDepth = 8;  // §8b stage 4
constexpr int kMaxPartsDepth = 3;         // §8b stage 5

// ---------------------------------------------------------------------------
// Internal result type for the pipeline.
// ---------------------------------------------------------------------------

struct Attempt {
    bool ok = false;
    Expr F;
    std::vector<std::string> methods;  // §8b labels, joined with " + " at the end
    std::vector<std::string> warnings;
};

void merge_unique(std::vector<std::string>& dst, const std::vector<std::string>& src) {
    for (const auto& s : src)
        if (std::find(dst.begin(), dst.end(), s) == dst.end()) dst.push_back(s);
}

std::string join_methods(const std::vector<std::string>& ms) {
    std::string out;
    for (const auto& m : ms) {
        if (!out.empty()) out += " + ";
        out += m;
    }
    return out;
}

bool is_number(const Expr& e) {
    return e->kind() == Kind::Number;
}

bool is_zero_number(const Expr& e) {
    return e->kind() == Kind::Number && e->number().is_zero();
}

bool is_constant_e(const Expr& e) {
    return e->kind() == Kind::Constant && e->constant() == ConstantId::E;
}

Expr sqrt_of(const Expr& e) {
    return make_pow(e, make_num(Rational(1, 2)));
}

Expr square(const Expr& e) {
    return make_pow(e, make_num(2));
}

// ---------------------------------------------------------------------------
// Linear inner argument u = a*x + b (a != 0; a, b free of x; plain x counts).
// ---------------------------------------------------------------------------

struct Linear {
    Expr a;
    Expr b;
};

std::optional<Linear> linear_in(const Expr& e, const std::string& sym) {
    if (!contains_symbol(e, sym)) return std::nullopt;
    const auto coeffs = polynomial_coefficients(e, sym);
    if (!coeffs || coeffs->size() != 2) return std::nullopt;
    return Linear{(*coeffs)[1], (*coeffs)[0]};
}

// Antiderivative of a polynomial given by its coefficients (degree order).
Expr poly_antiderivative(const std::vector<Expr>& coeffs, const Expr& x) {
    std::vector<Expr> terms;
    for (std::size_t k = 0; k < coeffs.size(); ++k) {
        if (is_zero_number(coeffs[k])) continue;
        const long long kk = static_cast<long long>(k) + 1;
        terms.push_back(make_mul({coeffs[k], make_num(Rational(1, kk)),
                                  make_pow(x, make_num(kk))}));
    }
    return make_add(std::move(terms));
}

// ---------------------------------------------------------------------------
// Conservative structural sign analysis (for the 1/(u^2 + c) table entry).
// "Provably" means: at every real point where the expression is defined.
// False only means "could not prove it", never "it is false".
// ---------------------------------------------------------------------------

bool provably_positive(const Expr& e);

bool provably_nonneg(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
            return !e->number().is_negative();
        case Kind::Constant:
            return true;  // pi, e
        case Kind::Pow: {
            const Expr& p = e->arg(1);
            if (p->kind() == Kind::Number && p->number().is_integer() &&
                p->number().num() % 2 == 0)
                return true;  // even integer power
            return provably_positive(e->arg(0));
        }
        case Kind::Mul:
        case Kind::Add: {
            for (const auto& a : e->args())
                if (!provably_nonneg(a)) return false;
            return true;
        }
        default:
            return false;
    }
}

bool provably_positive(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
            return e->number() > Rational(0);
        case Kind::Constant:
            return true;  // pi, e
        case Kind::Pow: {
            const Expr& p = e->arg(1);
            // b^(negative even integer): defined => b != 0 => positive.
            if (p->kind() == Kind::Number && p->number().is_integer() &&
                p->number().num() % 2 == 0 && p->number().is_negative())
                return true;
            return provably_positive(e->arg(0));
        }
        case Kind::Mul: {
            for (const auto& a : e->args())
                if (!provably_positive(a)) return false;
            return true;
        }
        case Kind::Add: {
            bool one_positive = false;
            for (const auto& a : e->args()) {
                if (provably_positive(a)) {
                    one_positive = true;
                } else if (!provably_nonneg(a)) {
                    return false;
                }
            }
            return one_positive;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Stage 2: the table with linear inner arguments (§8b).
// Returns nullopt when not applicable; an Attempt with ok == false is a
// *terminal* answer (abs(u): piecewise, out of scope).
// ---------------------------------------------------------------------------

std::optional<Attempt> try_table(const Expr& e, const std::string& sym) {
    // Plain x -> x^2/2.
    if (e->kind() == Kind::Symbol && e->symbol_name() == sym) {
        Attempt res;
        res.ok = true;
        res.F = make_div(square(e), make_num(2));
        res.methods = {"power rule"};
        return res;
    }

    if (e->kind() == Kind::Function) {
        const Expr& u = e->arg(0);
        const auto lin = linear_in(u, sym);
        if (!lin) return std::nullopt;
        Expr Fu;
        switch (e->function()) {
            case FunctionId::Sin: Fu = make_neg(make_fn(FunctionId::Cos, u)); break;
            case FunctionId::Cos: Fu = make_fn(FunctionId::Sin, u); break;
            case FunctionId::Tan:
                Fu = make_neg(make_fn(
                    FunctionId::Ln, make_fn(FunctionId::Abs, make_fn(FunctionId::Cos, u))));
                break;
            case FunctionId::Sinh: Fu = make_fn(FunctionId::Cosh, u); break;
            case FunctionId::Cosh: Fu = make_fn(FunctionId::Sinh, u); break;
            case FunctionId::Tanh:
                Fu = make_fn(FunctionId::Ln, make_fn(FunctionId::Cosh, u));
                break;
            case FunctionId::Ln:
                Fu = make_sub(make_mul({u, make_fn(FunctionId::Ln, u)}), u);
                break;
            case FunctionId::Asin:
                Fu = make_add({make_mul({u, e}),
                               sqrt_of(make_sub(make_num(1), square(u)))});
                break;
            case FunctionId::Acos:
                Fu = make_sub(make_mul({u, e}),
                              sqrt_of(make_sub(make_num(1), square(u))));
                break;
            case FunctionId::Atan:
                Fu = make_sub(make_mul({u, e}),
                              make_div(make_fn(FunctionId::Ln,
                                               make_add({make_num(1), square(u)})),
                                       make_num(2)));
                break;
            case FunctionId::Asinh:
                Fu = make_sub(make_mul({u, e}),
                              sqrt_of(make_add({square(u), make_num(1)})));
                break;
            case FunctionId::Acosh:
                Fu = make_sub(make_mul({u, e}),
                              sqrt_of(make_add({square(u), make_num(-1)})));
                break;
            case FunctionId::Atanh:
                Fu = make_add({make_mul({u, e}),
                               make_div(make_fn(FunctionId::Ln,
                                                make_sub(make_num(1), square(u))),
                                        make_num(2))});
                break;
            case FunctionId::Abs: {
                // §8b: abs(u) has a piecewise antiderivative; out of scope.
                Attempt bad;
                bad.warnings = {
                    "the antiderivative of abs(...) is piecewise and out of scope"};
                return bad;
            }
            case FunctionId::Gamma:
            case FunctionId::Digamma:
            case FunctionId::Erf:
            case FunctionId::Erfc:
            case FunctionId::Fib:
            case FunctionId::Harmonic:
                // No linear-argument table forms; later stages may still apply.
                return std::nullopt;
        }
        Attempt res;
        res.ok = true;
        res.F = make_div(Fu, lin->a);
        res.methods = {"table"};
        return res;
    }

    if (e->kind() != Kind::Pow) return std::nullopt;
    const Expr& base = e->arg(0);
    const Expr& expo = e->arg(1);

    if (expo->kind() == Kind::Number) {
        const Rational r = expo->number();

        // u^r for linear u (covers x^n, (5x+2)^7, 1/(2x+3), sqrt(x), ...).
        if (const auto lin = linear_in(base, sym)) {
            Attempt res;
            res.ok = true;
            if (r == Rational(-1)) {
                res.F = make_div(make_fn(FunctionId::Ln, make_fn(FunctionId::Abs, base)),
                                 lin->a);
                res.methods = {"table"};
            } else {
                const Rational rp = r + Rational(1);
                res.F = make_div(make_pow(base, make_num(rp)),
                                 make_mul({make_num(rp), lin->a}));
                res.methods = {"power rule"};
            }
            return res;
        }

        // cos(u)^(-2) -> tan(u)/a; sin(u)^(-2) -> -cos(u)/(a*sin(u)).
        if (r == Rational(-2) && base->kind() == Kind::Function) {
            const Expr& u = base->arg(0);
            if (const auto lin = linear_in(u, sym)) {
                Attempt res;
                res.methods = {"table"};
                if (base->function() == FunctionId::Cos) {
                    res.ok = true;
                    res.F = make_div(make_fn(FunctionId::Tan, u), lin->a);
                    return res;
                }
                if (base->function() == FunctionId::Sin) {
                    res.ok = true;
                    res.F = make_div(make_neg(make_fn(FunctionId::Cos, u)),
                                     make_mul({lin->a, base}));
                    return res;
                }
            }
        }

        // 1/(quadratic with no real roots) -> atan, by completing the square:
        // D = c2*(v^2 + k), v = x + c1/(2 c2), k = (c0 - c1^2/(4 c2))/c2 > 0.
        // Coefficients may be symbolic parameters (free of x, per §8b stage 2's
        // u = a*x + b scope); a positivity that cannot be decided structurally
        // is *assumed* and reported in an explicit warning — never silently.
        if (r == Rational(-1)) {
            const auto coeffs = polynomial_coefficients(base, sym);
            if (coeffs && coeffs->size() == 3 &&
                !(is_number((*coeffs)[2]) && (*coeffs)[2]->number().is_zero())) {
                const Expr& c2 = (*coeffs)[2];
                const Expr& c1 = (*coeffs)[1];
                const Expr& c0 = (*coeffs)[0];
                const Expr v = simplify(make_add(
                    {make_sym(sym), make_div(c1, make_mul({make_num(2), c2}))}));
                const Expr k = simplify(make_div(
                    make_sub(c0, make_div(square(c1), make_mul({make_num(4), c2}))),
                    c2));
                bool k_ok = false;
                std::vector<std::string> assumed;
                if (is_number(k)) {
                    k_ok = k->number() > Rational(0);
                } else if (provably_positive(simplify(expand(make_neg(k))))) {
                    k_ok = false;  // k < 0 certain: a log form, not this rule
                } else {
                    k_ok = true;  // param c: assume > 0 (§8b), and say so
                    assumed.push_back("result assumes " + to_string(k) + " > 0");
                }
                if (k_ok && !is_number(c2))
                    assumed.push_back("result assumes " + to_string(c2) +
                                      " is nonzero");
                if (k_ok) {
                    const Expr sq = simplify(sqrt_of(k));
                    Attempt res;
                    res.ok = true;
                    res.F = make_div(make_fn(FunctionId::Atan, make_div(v, sq)),
                                     make_mul({c2, sq}));
                    res.methods = {"table"};
                    res.warnings = std::move(assumed);
                    return res;
                }
            }
        }

        // 1/sqrt(c - u^2) -> asin(u/sqrt(c))/a.
        if (r == Rational(-1, 2)) {
            const auto coeffs = polynomial_coefficients(base, sym);
            if (coeffs && coeffs->size() == 3 && is_number((*coeffs)[2]) &&
                is_number((*coeffs)[1]) && is_number((*coeffs)[0])) {
                const Rational q2 = (*coeffs)[2]->number();
                const Rational q1 = (*coeffs)[1]->number();
                const Rational q0 = (*coeffs)[0]->number();
                if (q2 < Rational(0)) {
                    // base = -(alpha*x + beta)^2 + c
                    const Rational c = q0 + q1 * q1 / (Rational(4) * (-q2));
                    if (c > Rational(0)) {
                        const Expr alpha = simplify(sqrt_of(make_num(-q2)));
                        const Expr beta = simplify(
                            make_div(make_num(-q1), make_mul({make_num(2), alpha})));
                        const Expr u = simplify(
                            make_add({make_mul({alpha, make_sym(sym)}), beta}));
                        Attempt res;
                        res.ok = true;
                        res.F = make_div(
                            make_fn(FunctionId::Asin,
                                    make_div(u, simplify(sqrt_of(make_num(c))))),
                            alpha);
                        res.methods = {"table"};
                        return res;
                    }
                }
            }
        }
        return std::nullopt;
    }

    // Gaussian exponent: e^(quadratic with numeric negative leading
    // coefficient) has no elementary antiderivative but a classic erf form,
    // by completing the square: with the exponent -a(x - m)^2 + s,
    // the integral is e^s sqrt(pi)/(2 sqrt(a)) erf(sqrt(a)(x - m)).
    if (is_constant_e(base)) {
        const auto q = polynomial_coefficients(expo, sym);
        if (q && q->size() == 3 && is_number((*q)[2]) &&
            (*q)[2]->number() < Rational(0)) {
            const Rational a = -(*q)[2]->number();
            const Expr& c1 = (*q)[1];
            const Expr& c0 = (*q)[0];
            const Expr m = simplify(make_div(c1, make_num(a * Rational(2))));
            const Expr s = simplify(make_add(
                {c0, make_div(square(c1), make_num(a * Rational(4)))}));
            const Expr root_a = simplify(sqrt_of(make_num(a)));
            const Expr arg = simplify(
                make_mul({root_a, make_sub(make_sym(std::string(sym)), m)}));
            Attempt res;
            res.ok = true;
            res.F = simplify(make_mul(
                {make_pow(make_const(ConstantId::E), s),
                 make_div(sqrt_of(make_const(ConstantId::Pi)),
                          make_mul({make_num(2), root_a})),
                 make_fn(FunctionId::Erf, arg)}));
            res.methods = {"gaussian (erf)"};
            return res;
        }
    }

    // Exponentials with a linear exponent: e^u and c^u (numeric c > 0, c != 1).
    if (const auto lin = linear_in(expo, sym)) {
        if (is_constant_e(base)) {
            Attempt res;
            res.ok = true;
            res.F = make_div(e, lin->a);
            res.methods = {"table"};
            return res;
        }
        if (base->kind() == Kind::Number && base->number() > Rational(0) &&
            !base->number().is_one()) {
            Attempt res;
            res.ok = true;
            res.F = make_div(e, make_mul({lin->a, make_fn(FunctionId::Ln, base)}));
            res.methods = {"table"};
            return res;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Stage 4 helpers: derivative-divides u-substitution.
// ---------------------------------------------------------------------------

// Candidate inner subexpressions: function applications and their arguments,
// Pow bases and exponents — anything containing the symbol except plain x.
void collect_candidates(const Expr& e, const std::string& sym, std::vector<Expr>& out) {
    if (!contains_symbol(e, sym)) return;
    const auto add = [&](const Expr& c) {
        if (!contains_symbol(c, sym)) return;
        if (c->kind() == Kind::Symbol) return;  // u = x is trivial
        for (const auto& seen : out)
            if (structurally_equal(seen, c)) return;
        out.push_back(c);
    };
    if (e->kind() == Kind::Function) {
        add(e);
        add(e->arg(0));
    } else if (e->kind() == Kind::Pow) {
        add(e->arg(0));
        add(e->arg(1));
    }
    for (const auto& a : e->args()) collect_candidates(a, sym, out);
}

// Structural replacement of a subexpression (rebuilt through the factories).
Expr replace_subexpr(const Expr& e, const Expr& target, const Expr& repl) {
    if (structurally_equal(e, target)) return repl;
    switch (e->kind()) {
        case Kind::Number:
        case Kind::Symbol:
        case Kind::Constant:
            return e;
        case Kind::Add:
        case Kind::Mul: {
            std::vector<Expr> args;
            args.reserve(e->args().size());
            for (const auto& a : e->args())
                args.push_back(replace_subexpr(a, target, repl));
            return e->kind() == Kind::Add ? make_add(std::move(args))
                                          : make_mul(std::move(args));
        }
        case Kind::Pow:
            return make_pow(replace_subexpr(e->arg(0), target, repl),
                            replace_subexpr(e->arg(1), target, repl));
        case Kind::Function:
            return make_fn(e->function(), replace_subexpr(e->arg(0), target, repl));
    }
    return e;
}

// Reciprocal that pushes the -1 into existing powers: (b^p)^(-1) -> b^(-p)
// (valid on the real domain wherever b^p is defined and nonzero). Without
// this, e.g. exp(x) / exp(x) stays as e^x * (e^x)^(-1) and never cancels,
// which starves derivative-divides of its cancellation.
Expr reciprocal(const Expr& e) {
    if (e->kind() == Kind::Number && !e->number().is_zero())
        return make_num(Rational(1) / e->number());
    if (e->kind() == Kind::Mul) {
        std::vector<Expr> inv;
        inv.reserve(e->args().size());
        for (const auto& f : e->args()) inv.push_back(reciprocal(f));
        return make_mul(std::move(inv));
    }
    if (e->kind() == Kind::Pow) return make_pow(e->arg(0), make_neg(e->arg(1)));
    return make_pow(e, make_num(-1));
}

// num/den with same-base power factors merged by summing exponents (simplify
// only collects like factors with syntactically equal bases and numeric
// exponents, so e^x * e^(-x) needs this hand-rolled merge to become 1).
Expr divide_cancel(const Expr& num, const Expr& den) {
    std::vector<Expr> factors;
    const auto flatten = [&factors](const Expr& e) {
        if (e->kind() == Kind::Mul)
            for (const auto& f : e->args()) factors.push_back(f);
        else
            factors.push_back(e);
    };
    flatten(num);
    flatten(reciprocal(den));

    std::vector<Expr> bases;
    std::vector<std::vector<Expr>> exps;
    for (const auto& f : factors) {
        Expr b = f;
        Expr p = make_num(1);
        if (f->kind() == Kind::Pow) {
            b = f->arg(0);
            p = f->arg(1);
        }
        bool merged = false;
        for (std::size_t i = 0; i < bases.size(); ++i) {
            if (structurally_equal(bases[i], b)) {
                exps[i].push_back(p);
                merged = true;
                break;
            }
        }
        if (!merged) {
            bases.push_back(b);
            exps.push_back({p});
        }
    }
    std::vector<Expr> rebuilt;
    rebuilt.reserve(bases.size());
    for (std::size_t i = 0; i < bases.size(); ++i)
        rebuilt.push_back(make_pow(bases[i], simplify(make_add(exps[i]))));
    return simplify(make_mul(std::move(rebuilt)));
}

std::string fresh_symbol(const Expr& scope, const std::string& sym) {
    const auto used = free_symbols(scope);
    for (int i = 0;; ++i) {
        std::string name = "_u" + std::to_string(i);
        if (name != sym && !used.contains(name)) return name;
    }
}

// ---------------------------------------------------------------------------
// Stage 5 helpers: pattern-bounded integration by parts.
// ---------------------------------------------------------------------------

// Cyclic closed forms: e^(a*x+..) * sin/cos(b*x+..) (§8b stage 5).
std::optional<Attempt> try_cyclic_parts(const Expr& e, const std::string& sym) {
    if (e->kind() != Kind::Mul || e->args().size() != 2) return std::nullopt;
    for (int i = 0; i < 2; ++i) {
        const Expr& ef = e->arg(static_cast<std::size_t>(i));
        const Expr& tf = e->arg(static_cast<std::size_t>(1 - i));
        if (ef->kind() != Kind::Pow || !is_constant_e(ef->arg(0))) continue;
        if (tf->kind() != Kind::Function) continue;
        const bool is_sin = tf->function() == FunctionId::Sin;
        if (!is_sin && tf->function() != FunctionId::Cos) continue;
        const auto la = linear_in(ef->arg(1), sym);
        const auto lb = linear_in(tf->arg(0), sym);
        if (!la || !lb) continue;
        const Expr& a = la->a;
        const Expr& b = lb->a;
        const Expr denom = simplify(make_add({square(a), square(b)}));
        if (is_zero_number(denom)) continue;
        const Expr& v = tf->arg(0);
        const Expr sinv = make_fn(FunctionId::Sin, v);
        const Expr cosv = make_fn(FunctionId::Cos, v);
        const Expr comb =
            is_sin ? make_sub(make_mul({a, sinv}), make_mul({b, cosv}))
                   : make_add({make_mul({a, cosv}), make_mul({b, sinv})});
        Attempt res;
        res.ok = true;
        res.F = make_div(make_mul({ef, comb}), denom);
        res.methods = {"integration by parts"};
        return res;
    }
    return std::nullopt;
}

// Is `f` a {sin, cos, sinh, cosh, e^}(linear u) factor for parts stage (b)?
bool is_parts_oscillator(const Expr& f, const std::string& sym) {
    if (f->kind() == Kind::Function) {
        switch (f->function()) {
            case FunctionId::Sin:
            case FunctionId::Cos:
            case FunctionId::Sinh:
            case FunctionId::Cosh:
                return linear_in(f->arg(0), sym).has_value();
            default:
                return false;
        }
    }
    return f->kind() == Kind::Pow && is_constant_e(f->arg(0)) &&
           linear_in(f->arg(1), sym).has_value();
}

// ---------------------------------------------------------------------------
// Stage 6: rational functions P/Q via partial fractions (§8b stage 6).
// Both P and Q need rational Number coefficients; Q must factor into linear
// factors (with multiplicity) plus at most ONE irreducible quadratic, total
// degree <= 6. The unknown partial-fraction coefficients are matched through
// polynomial_coefficients and solved EXACTLY with solve_system (§9b).
//
// The rational-root helpers below mirror the §9 machinery; solver.cpp keeps
// its copies private (and its public behavior must not change), so they are
// reimplemented here.
// ---------------------------------------------------------------------------

constexpr std::size_t kMaxDenominatorDegree = 6;  // §8b stage 6

bool checked_mul_ll(long long a, long long b, long long& out) {
    return !__builtin_mul_overflow(a, b, &out);
}

/// All-Number coefficients of a polynomial in `sym` (degree order), else
/// nullopt (non-polynomial shapes and symbolic-parameter coefficients).
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

/// Exact Horner evaluation; propagates OverflowError to the caller.
Rational horner(const std::vector<Rational>& coeffs, const Rational& x) {
    Rational acc(0);
    for (std::size_t i = coeffs.size(); i-- > 0;) acc = acc * x + coeffs[i];
    return acc;
}

/// Divisors of n (> 0); complete when n <= ~4e12, best-effort beyond.
std::vector<long long> divisors_of(long long n) {
    std::vector<long long> divs;
    for (long long i = 1; i <= 2000000 && i * i <= n; ++i) {
        if (n % i == 0) {
            divs.push_back(i);
            if (i != n / i) divs.push_back(n / i);
        }
    }
    return divs;
}

/// One rational root of the polynomial (rational-root theorem), or nullopt.
std::optional<Rational> find_rational_root(const std::vector<Rational>& coeffs) {
    long long lcm_den = 1;
    for (const Rational& c : coeffs) {
        const long long g = std::gcd(lcm_den, c.den());
        long long scaled = 0;
        if (!checked_mul_ll(lcm_den / g, c.den(), scaled)) return std::nullopt;
        lcm_den = scaled;
    }
    std::vector<long long> ints(coeffs.size());
    for (std::size_t i = 0; i < coeffs.size(); ++i) {
        if (!checked_mul_ll(coeffs[i].num(), lcm_den / coeffs[i].den(), ints[i]))
            return std::nullopt;
    }
    long long all_gcd = 0;
    for (const long long v : ints) all_gcd = std::gcd(all_gcd, v);
    if (all_gcd > 1) {
        for (long long& v : ints) v /= all_gcd;
    }

    const long long a0 = std::abs(ints.front());
    const long long an = std::abs(ints.back());
    if (a0 == 0 || an == 0) return std::nullopt;  // zero roots are peeled first
    for (const long long p : divisors_of(a0)) {
        for (const long long q : divisors_of(an)) {
            for (const int sign : {1, -1}) {
                const Rational candidate(sign * p, q);
                try {
                    if (horner(coeffs, candidate).is_zero()) return candidate;
                } catch (const Error&) {
                    // overflow while testing this candidate: skip it
                }
            }
        }
    }
    return std::nullopt;
}

/// Synthetic division of coeffs (ascending, degree n) by (x - r).
/// The remainder is known to be zero (r is a verified root).
std::vector<Rational> deflate(const std::vector<Rational>& coeffs, const Rational& r) {
    const std::size_t n = coeffs.size() - 1;
    std::vector<Rational> q(n);
    q[n - 1] = coeffs[n];
    for (std::size_t i = n - 1; i >= 1; --i) q[i - 1] = coeffs[i] + r * q[i];
    return q;
}

/// Exact polynomial long division: num = quot*den + rem, deg rem < deg den.
/// Requires den.back() != 0 and deg den >= 1.
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
        if (!f.is_zero()) {
            for (std::size_t i = 0; i <= dd; ++i)
                num[k - dd + i] = num[k - dd + i] - f * den[i];
        }
        if (k == dd) break;
    }
    num.resize(dd);
    while (num.size() > 1 && num.back().is_zero()) num.pop_back();
    rem = std::move(num);
}

struct LinearFactor {
    Rational root;
    int multiplicity = 0;
};

struct QuadraticFactor {
    Rational p;  // monic x^2 + p*x + q, irreducible (p^2 - 4q < 0)
    Rational q;
};

struct DenominatorFactors {
    Rational lc;  // leading coefficient of Q
    std::vector<LinearFactor> linear;
    std::optional<QuadraticFactor> quadratic;
};

/// Q = lc * prod (x - r_i)^{m_i} * (x^2 + p*x + q)?  — or nullopt when Q
/// does not split that way over the rationals (§8b stage 6 scope).
std::optional<DenominatorFactors> factor_denominator(std::vector<Rational> q) {
    DenominatorFactors out;
    out.lc = q.back();
    std::vector<Rational> roots;
    while (q.size() > 1 && q.front().is_zero()) {  // factor x
        roots.push_back(Rational(0));
        q.erase(q.begin());
    }
    while (q.size() >= 2) {
        if (q.size() == 2) {
            roots.push_back(-q[0] / q[1]);
            q = {q[1]};
            break;
        }
        const auto r = find_rational_root(q);
        if (!r) break;
        roots.push_back(*r);
        q = deflate(q, *r);
    }
    if (q.size() == 3) {
        const Rational disc = q[1] * q[1] - Rational(4) * q[2] * q[0];
        if (!(disc < Rational(0)))
            return std::nullopt;  // irrational real roots: out of scope
        out.quadratic = QuadraticFactor{q[1] / q[2], q[0] / q[2]};
    } else if (q.size() != 1) {
        return std::nullopt;  // an unfactored remainder of degree >= 3
    }
    std::sort(roots.begin(), roots.end());
    for (const Rational& r : roots) {
        if (!out.linear.empty() && out.linear.back().root == r)
            ++out.linear.back().multiplicity;
        else
            out.linear.push_back({r, 1});
    }
    return out;
}

/// Polynomial Expr from ascending Rational coefficients.
Expr poly_from_coeffs(const std::vector<Rational>& coeffs, const Expr& x) {
    std::vector<Expr> terms;
    for (std::size_t k = 0; k < coeffs.size(); ++k) {
        if (coeffs[k].is_zero()) continue;
        terms.push_back(make_mul({make_num(coeffs[k]),
                                  make_pow(x, make_num(static_cast<long long>(k)))}));
    }
    return make_add(std::move(terms));
}

std::optional<Attempt> try_partial_fractions(const Expr& e, const std::string& sym) {
    // Split e into numerator/denominator by the sign of integer Pow exponents.
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
    if (e->kind() == Kind::Mul) {
        for (const Expr& f : e->args()) classify(f);
    } else {
        classify(e);
    }
    if (den_factors.empty()) return std::nullopt;

    const Expr x = make_sym(sym);
    const Expr Q = make_mul(std::move(den_factors));
    if (!contains_symbol(Q, sym)) return std::nullopt;
    const auto den = rational_poly_coeffs(Q, sym);
    if (!den) return std::nullopt;
    const std::size_t den_degree = den->size() - 1;
    if (den_degree < 1 || den_degree > kMaxDenominatorDegree) return std::nullopt;
    const auto num = rational_poly_coeffs(make_mul(std::move(num_factors)), sym);
    if (!num) return std::nullopt;

    // P/Q = quot + rem/Q with deg rem < deg Q; fold Q's leading coefficient
    // into the remainder so the factored denominator is monic.
    std::vector<Rational> quot;
    std::vector<Rational> rem;
    poly_divide(*num, *den, quot, rem);
    const auto factors = factor_denominator(*den);
    if (!factors) return std::nullopt;
    for (Rational& c : rem) c = c / factors->lc;

    // Ansatz: rem/Qm = sum_ij A_ij/(x - r_i)^j + (B*x + C)/(x^2 + p*x + q),
    // multiplied through by the monic Qm so both sides are polynomials.
    struct Piece {
        std::string name;
        Rational root;
        int power = 1;
    };
    const auto linear_basis = [&](std::size_t skip, int reduce_by) {
        std::vector<Expr> fs;
        for (std::size_t k = 0; k < factors->linear.size(); ++k) {
            const int m = factors->linear[k].multiplicity - (k == skip ? reduce_by : 0);
            if (m > 0)
                fs.push_back(make_pow(make_sub(x, make_num(factors->linear[k].root)),
                                      make_num(m)));
        }
        if (factors->quadratic)
            fs.push_back(make_add({square(x),
                                   make_mul({make_num(factors->quadratic->p), x}),
                                   make_num(factors->quadratic->q)}));
        return make_mul(std::move(fs));
    };

    std::vector<Piece> pieces;
    std::vector<Expr> rhs_terms;
    std::vector<std::string> unknowns;
    for (std::size_t i = 0; i < factors->linear.size(); ++i) {
        for (int j = 1; j <= factors->linear[i].multiplicity; ++j) {
            std::string name =
                "_p" + std::to_string(i) + "_" + std::to_string(j);
            rhs_terms.push_back(make_mul({make_sym(name), linear_basis(i, j)}));
            pieces.push_back({name, factors->linear[i].root, j});
            unknowns.push_back(std::move(name));
        }
    }
    const std::string b_name = "_pB";
    const std::string c_name = "_pC";
    if (factors->quadratic) {
        // The quadratic term's cofactor is every linear factor at full power.
        std::vector<Expr> quad_cofactor;
        for (std::size_t k = 0; k < factors->linear.size(); ++k)
            quad_cofactor.push_back(
                make_pow(make_sub(x, make_num(factors->linear[k].root)),
                         make_num(factors->linear[k].multiplicity)));
        rhs_terms.push_back(
            make_mul({make_add({make_mul({make_sym(b_name), x}), make_sym(c_name)}),
                      make_mul(std::move(quad_cofactor))}));
        unknowns.push_back(b_name);
        unknowns.push_back(c_name);
    }

    // Coefficient matching through polynomial_coefficients, solved exactly.
    const Expr lhs = poly_from_coeffs(rem, x);
    const auto match =
        polynomial_coefficients(make_sub(make_add(std::move(rhs_terms)), lhs), sym);
    if (!match) return std::nullopt;
    std::vector<Equation> eqs;
    eqs.reserve(match->size());
    for (const Expr& c : *match) eqs.push_back(Equation{c, make_num(0)});
    const SystemSolveResult sys = solve_system(eqs, unknowns);
    if (sys.status != SystemSolveResult::Status::Solved) return std::nullopt;

    // Integrate the pieces.
    std::vector<Expr> parts;
    bool quot_nonzero = false;
    for (const Rational& c : quot) quot_nonzero = quot_nonzero || !c.is_zero();
    if (quot_nonzero) {
        std::vector<Expr> quot_exprs;
        quot_exprs.reserve(quot.size());
        for (const Rational& c : quot) quot_exprs.push_back(make_num(c));
        parts.push_back(poly_antiderivative(quot_exprs, x));
    }
    for (const Piece& p : pieces) {
        const Expr A = sys.values.at(p.name);
        if (is_zero_number(A)) continue;
        const Expr xr = make_sub(x, make_num(p.root));
        if (p.power == 1) {
            // A/(x-r) -> A*ln(abs(x-r))
            parts.push_back(
                make_mul({A, make_fn(FunctionId::Ln, make_fn(FunctionId::Abs, xr))}));
        } else {
            // A/(x-r)^k -> -A/((k-1)*(x-r)^(k-1))
            parts.push_back(make_mul({A, make_num(Rational(-1, p.power - 1)),
                                      make_pow(xr, make_num(1 - p.power))}));
        }
    }
    if (factors->quadratic) {
        const Expr B = sys.values.at(b_name);
        const Expr C = sys.values.at(c_name);
        const Rational p = factors->quadratic->p;
        const Rational q = factors->quadratic->q;
        const Expr S = make_add({square(x), make_mul({make_num(p), x}), make_num(q)});
        // (B*x + C)/(x^2+p*x+q), completing the square with v = x + p/2,
        // k = q - p^2/4 > 0:  B/2 * ln(x^2+p*x+q)  +  (C - B*p/2)/sqrt(k)
        // * atan(v/sqrt(k)).  (S > 0 everywhere, so ln needs no abs.)
        if (!is_zero_number(B))
            parts.push_back(
                make_mul({B, make_num(Rational(1, 2)), make_fn(FunctionId::Ln, S)}));
        const Expr C2 = simplify(make_sub(C, make_mul({B, make_num(p / Rational(2))})));
        if (!is_zero_number(C2)) {
            const Rational k = q - p * p / Rational(4);
            const Expr sq = simplify(sqrt_of(make_num(k)));
            const Expr v = make_add({x, make_num(p / Rational(2))});
            parts.push_back(make_mul(
                {C2, make_div(make_fn(FunctionId::Atan, make_div(v, sq)), sq)}));
        }
    }
    Attempt res;
    res.ok = true;
    res.F = make_add(std::move(parts));
    res.methods = {"partial fractions"};
    return res;
}

// ---------------------------------------------------------------------------
// The pipeline (§8b): first applicable technique wins.
// ---------------------------------------------------------------------------

Attempt integrate_impl(const Expr& e_in, const std::string& sym, int depth,
                       int parts_depth, bool expand_allowed);

// ---------------------------------------------------------------------------
// Stage 7: trig powers (§8b stage 7). sin^2/cos^2 rewrite by half-angle and
// retry the pipeline; odd powers 3 and 5 peel one factor, rewrite the even
// remainder by Pythagoras, and let stage 4's derivative-divides finish.
// ---------------------------------------------------------------------------

std::optional<Attempt> try_trig_powers(const Expr& e, const std::string& sym,
                                       int depth, int parts_depth) {
    if (e->kind() != Kind::Pow) return std::nullopt;
    const Expr& base = e->arg(0);
    const Expr& expo = e->arg(1);
    if (base->kind() != Kind::Function || expo->kind() != Kind::Number)
        return std::nullopt;
    const bool is_sin = base->function() == FunctionId::Sin;
    if (!is_sin && base->function() != FunctionId::Cos) return std::nullopt;
    const Rational& n = expo->number();
    const Expr& u = base->arg(0);
    if (!linear_in(u, sym)) return std::nullopt;

    Expr rewritten;
    if (n == Rational(2)) {
        // sin^2(u) = (1 - cos(2u))/2; cos^2(u) = (1 + cos(2u))/2.
        const Expr cos2u = make_fn(FunctionId::Cos, make_mul({make_num(2), u}));
        rewritten = make_mul({make_num(Rational(1, 2)),
                              is_sin ? make_sub(make_num(1), cos2u)
                                     : make_add({make_num(1), cos2u})});
    } else if (n == Rational(3) || n == Rational(5)) {
        // sin^(2k+1)(u) = (1 - cos^2(u))^k * sin(u) (and symmetrically).
        const long long k = (n.num() - 1) / 2;
        const Expr other =
            make_fn(is_sin ? FunctionId::Cos : FunctionId::Sin, u);
        rewritten = make_mul(
            {make_pow(make_sub(make_num(1), square(other)), make_num(k)), base});
    } else {
        return std::nullopt;
    }
    auto sub = integrate_impl(rewritten, sym, depth + 1, parts_depth,
                              /*expand_allowed=*/true);
    if (!sub.ok) return std::nullopt;
    Attempt res;
    res.ok = true;
    res.F = std::move(sub.F);
    res.methods = {"trig identity"};
    merge_unique(res.methods, sub.methods);
    res.warnings = std::move(sub.warnings);
    return res;
}

// Stage 5 body, split out for readability.
std::optional<Attempt> try_parts(const Expr& e, const std::string& sym, int depth,
                                 int parts_depth, bool expand_allowed) {
    if (parts_depth >= kMaxPartsDepth || e->kind() != Kind::Mul) return std::nullopt;

    if (auto cyc = try_cyclic_parts(e, sym)) return cyc;

    const Expr x = make_sym(sym);
    const auto& args = e->args();

    // (b) poly(x) * {sin, cos, sinh, cosh, e^}(linear u): reduce the degree.
    for (std::size_t i = 0; i < args.size(); ++i) {
        const Expr& f = args[i];
        if (!is_parts_oscillator(f, sym)) continue;
        std::vector<Expr> rest;
        for (std::size_t j = 0; j < args.size(); ++j)
            if (j != i) rest.push_back(args[j]);
        const Expr P = make_mul(std::move(rest));
        if (!contains_symbol(P, sym)) continue;  // linearity's job
        if (!polynomial_coefficients(P, sym)) continue;
        const auto G = try_table(f, sym);  // table integral of the oscillator
        if (!G || !G->ok) continue;
        const Expr remainder = simplify(make_mul({differentiate(P, sym), G->F}));
        auto sub = integrate_impl(remainder, sym, depth, parts_depth + 1,
                                  expand_allowed);
        if (!sub.ok) continue;
        Attempt res;
        res.ok = true;
        res.F = make_sub(make_mul({P, G->F}), sub.F);
        res.methods = {"integration by parts"};
        merge_unique(res.methods, sub.methods);
        res.warnings = std::move(sub.warnings);
        return res;
    }

    // (c) poly(x) * {ln, atan, asin, acos}(w): parts with dv = poly.
    for (std::size_t i = 0; i < args.size(); ++i) {
        const Expr& f = args[i];
        if (f->kind() != Kind::Function) continue;
        const auto id = f->function();
        if (id != FunctionId::Ln && id != FunctionId::Atan && id != FunctionId::Asin &&
            id != FunctionId::Acos)
            continue;
        const Expr& w = f->arg(0);
        if (!contains_symbol(w, sym)) continue;
        std::vector<Expr> rest;
        for (std::size_t j = 0; j < args.size(); ++j)
            if (j != i) rest.push_back(args[j]);
        const Expr P = make_mul(std::move(rest));
        if (!contains_symbol(P, sym)) continue;  // f(linear w) alone is table's job
        const auto pc = polynomial_coefficients(P, sym);
        if (!pc) continue;
        const Expr Q = poly_antiderivative(*pc, x);
        Expr fprime;
        switch (id) {
            case FunctionId::Ln:
                fprime = make_pow(w, make_num(-1));
                break;
            case FunctionId::Atan:
                fprime = make_pow(make_add({make_num(1), square(w)}), make_num(-1));
                break;
            case FunctionId::Asin:
                fprime = make_pow(make_sub(make_num(1), square(w)),
                                  make_num(Rational(-1, 2)));
                break;
            case FunctionId::Acos:
                fprime = make_neg(make_pow(make_sub(make_num(1), square(w)),
                                           make_num(Rational(-1, 2))));
                break;
            default:
                continue;
        }
        const Expr remainder =
            simplify(make_mul({Q, fprime, differentiate(w, sym)}));
        auto sub = integrate_impl(remainder, sym, depth, parts_depth + 1,
                                  expand_allowed);
        if (!sub.ok) continue;
        Attempt res;
        res.ok = true;
        res.F = make_sub(make_mul({f, Q}), sub.F);
        res.methods = {"integration by parts"};
        merge_unique(res.methods, sub.methods);
        res.warnings = std::move(sub.warnings);
        return res;
    }
    return std::nullopt;
}

Attempt integrate_impl(const Expr& e_in, const std::string& sym, int depth,
                       int parts_depth, bool expand_allowed) {
    Attempt failed;
    if (depth > kMaxSubstitutionDepth) return failed;

    const Expr x = make_sym(sym);
    const Expr e = simplify(e_in);

    // Symbol-free integrand: ∫c dx = c*x (c pulled out by linearity, ∫1 = x).
    if (!contains_symbol(e, sym)) {
        Attempt res;
        res.ok = true;
        res.F = make_mul({e, x});
        if (e->kind() == Kind::Number && e->number().is_one())
            res.methods = {"power rule"};
        else
            res.methods = {"linearity", "power rule"};
        return res;
    }

    // ---- Stage 1: linearity ------------------------------------------------
    if (e->kind() == Kind::Add) {
        Attempt res;
        res.ok = true;
        res.methods = {"linearity"};
        std::vector<Expr> parts;
        for (const auto& term : e->args()) {
            auto sub = integrate_impl(term, sym, depth, parts_depth, expand_allowed);
            merge_unique(res.warnings, sub.warnings);
            if (!sub.ok) {  // §8b: any term Unsolved -> whole integral Unsolved
                failed.warnings = std::move(res.warnings);
                return failed;
            }
            parts.push_back(sub.F);
            merge_unique(res.methods, sub.methods);
        }
        res.F = make_add(std::move(parts));
        return res;
    }
    if (e->kind() == Kind::Mul) {
        std::vector<Expr> free_part, dep_part;
        for (const auto& f : e->args())
            (contains_symbol(f, sym) ? dep_part : free_part).push_back(f);
        if (!free_part.empty()) {
            auto sub = integrate_impl(make_mul(std::move(dep_part)), sym, depth,
                                      parts_depth, expand_allowed);
            if (sub.ok) {
                Attempt res;
                res.ok = true;
                res.F = make_mul({make_mul(std::move(free_part)), sub.F});
                res.methods = {"linearity"};
                merge_unique(res.methods, sub.methods);
                res.warnings = std::move(sub.warnings);
                return res;
            }
            // Fall through: later stages get a look at the full product.
        }
    }

    // ---- Stage 2: table with linear inner arguments -------------------------
    try {
        if (auto t = try_table(e, sym)) return *t;  // ok==false is terminal (abs)
    } catch (const Error&) {
        // A table probe must never sink the pipeline (e.g. Rational overflow).
    }

    // ---- Stage 3: polynomial / expansion (retry-once guard) -----------------
    if (const auto coeffs = polynomial_coefficients(e, sym)) {
        Attempt res;
        res.ok = true;
        res.F = poly_antiderivative(*coeffs, x);
        res.methods = {"power rule"};
        return res;
    }
    if (expand_allowed) {
        const Expr expanded = expand(e);
        if (!structurally_equal(expanded, e)) {
            auto sub = integrate_impl(expanded, sym, depth, parts_depth,
                                      /*expand_allowed=*/false);
            if (sub.ok) return sub;
        }
    }

    // ---- Stage 4: u-substitution (derivative-divides) ------------------------
    {
        std::vector<Expr> candidates;
        collect_candidates(e, sym, candidates);
        for (const auto& u : candidates) {
            try {
                const Expr du = differentiate(u, sym);
                if (is_zero_number(du)) continue;
                const Expr quotient = divide_cancel(e, du);
                const std::string t = fresh_symbol(quotient, sym);
                const Expr replaced =
                    simplify(replace_subexpr(quotient, u, make_sym(t)));
                if (contains_symbol(replaced, sym)) continue;
                auto sub = integrate_impl(replaced, t, depth + 1, parts_depth,
                                          /*expand_allowed=*/true);
                if (!sub.ok) continue;
                Attempt res;
                res.ok = true;
                res.F = substitute(sub.F, t, u);
                res.methods = {"u-substitution"};
                merge_unique(res.methods, sub.methods);
                res.warnings = std::move(sub.warnings);
                return res;
            } catch (const Error&) {
                continue;  // a candidate that blows up is just not the right u
            }
        }
    }

    // ---- Stage 5: integration by parts (pattern-bounded, depth <= 3) --------
    try {
        if (auto p = try_parts(e, sym, depth, parts_depth, expand_allowed)) return *p;
    } catch (const Error&) {
    }

    // ---- Stage 6: rational functions / partial fractions --------------------
    try {
        if (auto pf = try_partial_fractions(e, sym)) return *pf;
    } catch (const Error&) {
        // exact coefficient arithmetic overflowed: this stage is not applicable
    }

    // ---- Stage 7: trig powers -------------------------------------------------
    try {
        if (auto tp = try_trig_powers(e, sym, depth, parts_depth)) return *tp;
    } catch (const Error&) {
    }

    // ---- Stage 8: give up honestly -------------------------------------------
    return failed;
}

// ---------------------------------------------------------------------------
// Self-verification doctrine (§8b, mandatory): differentiate the candidate
// back and compare numerically at ~5 sample points.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Definite integration helpers (§8b).
// ---------------------------------------------------------------------------

constexpr int kGridIntervals = 64;        // §8b: ~64-point evaluability grid
constexpr double kSimpsonRelTol = 1e-10;  // §8b: adaptive Simpson tolerance
constexpr int kSimpsonMaxDepth = 40;

/// Decimal-converted rational of a double (mirrors the §9 numeric-root
/// conversion; solver.cpp keeps its copy private).
Rational rational_from_double(double x) {
    if (!std::isfinite(x)) return Rational(0);
    const double ax = std::abs(x);
    int decimals = 15;
    if (ax >= 1.0) {
        decimals =
            std::clamp(15 - static_cast<int>(std::floor(std::log10(ax))) - 1, 0, 15);
    }
    for (int d = decimals; d >= 0; --d) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", d, x);
        try {
            return Rational::from_decimal_string(buf);
        } catch (const Error&) {
            // too many digits for 64 bits: retry with fewer decimals
        }
    }
    return Rational(0);
}

/// Where does the integrand fail to evaluate finitely on 65 evenly spaced
/// points of [a, b]?  (The count is odd on purpose: the midpoint is always
/// probed, so e.g. 1/x on [-1, 1] fails at x = 0.)  A failure only at the
/// interval endpoints (e.g. sin(x)/x at 0) is distinguished so the numeric
/// path can integrate over a slightly shrunken open interval instead of
/// refusing an integrable integrand.
enum class GridStatus { Ok, EndpointGap, InteriorGap };

GridStatus grid_status(const Expr& f, const std::string& sym, double a, double b) {
    bool endpoint_gap = false;
    for (int i = 0; i <= kGridIntervals; ++i) {
        const double xv = a + (b - a) * (static_cast<double>(i) / kGridIntervals);
        try {
            Bindings bind;
            bind[sym] = xv;
            (void)evaluate(f, bind);  // throws on domain gaps / non-finite values
        } catch (const Error&) {
            if (i != 0 && i != kGridIntervals) return GridStatus::InteriorGap;
            endpoint_gap = true;
        }
    }
    return endpoint_gap ? GridStatus::EndpointGap : GridStatus::Ok;
}

struct Quadrature {
    bool ok = false;         // false: the integrand failed to evaluate somewhere
    bool converged = true;   // false: max depth hit before the tolerance
    double value = 0.0;
};

struct SimpsonContext {
    const Expr& f;
    const std::string& sym;
    bool converged = true;

    double eval(double xv) {
        Bindings b;
        b[sym] = xv;
        return evaluate(f, b);
    }

    // Classic adaptive Simpson with Richardson extrapolation; tol is relative
    // (scaled by max(1, |segment|)) and halves per split.
    double recurse(double a, double fa, double m, double fm, double b, double fb,
                   double whole, double tol, int depth) {
        const double lm = (a + m) / 2;
        const double rm = (m + b) / 2;
        const double flm = eval(lm);
        const double frm = eval(rm);
        const double left = (m - a) / 6 * (fa + 4 * flm + fm);
        const double right = (b - m) / 6 * (fm + 4 * frm + fb);
        const double delta = left + right - whole;
        if (std::fabs(delta) <= 15 * tol * std::max(1.0, std::fabs(left + right)))
            return left + right + delta / 15;
        if (depth >= kSimpsonMaxDepth) {
            converged = false;
            return left + right;
        }
        return recurse(a, fa, lm, flm, m, fm, left, tol / 2, depth + 1) +
               recurse(m, fm, rm, frm, b, fb, right, tol / 2, depth + 1);
    }
};

Quadrature adaptive_simpson(const Expr& f, const std::string& sym, double a,
                            double b) {
    Quadrature q;
    if (a == b) {
        q.ok = true;
        return q;
    }
    SimpsonContext ctx{f, sym};
    try {
        const double fa = ctx.eval(a);
        const double fb = ctx.eval(b);
        const double m = (a + b) / 2;
        const double fm = ctx.eval(m);
        const double whole = (b - a) / 6 * (fa + 4 * fm + fb);
        q.value = ctx.recurse(a, fa, m, fm, b, fb, whole, kSimpsonRelTol, 0);
        q.ok = true;
        q.converged = ctx.converged;
    } catch (const Error&) {
        q.ok = false;  // hit a non-evaluable point between grid samples
    }
    return q;
}

enum class Verification { Passed, Failed, AllSkipped };

Verification verify_antiderivative(const Expr& F, const Expr& integrand,
                                   const std::string& sym) {
    // Positives, negatives and fractions, away from the common domain edges.
    static constexpr double kSamples[] = {-2.4, -1.1, 0.3, 0.55, 1.7, 2.6};
    try {
        const Expr dF = differentiate(F, sym);
        int checked = 0;
        for (const double v : kSamples) {
            double lhs = 0.0;
            double rhs = 0.0;
            try {
                Bindings b;
                b[sym] = v;
                lhs = evaluate(dF, b);   // throws EvalError on domain/non-finite
                rhs = evaluate(integrand, b);
            } catch (const Error&) {
                continue;  // skip points where either side fails to evaluate
            }
            ++checked;
            const double scale = std::max({1.0, std::fabs(lhs), std::fabs(rhs)});
            if (std::fabs(lhs - rhs) > 1e-6 * scale) return Verification::Failed;
        }
        return checked == 0 ? Verification::AllSkipped : Verification::Passed;
    } catch (const Error&) {
        return Verification::AllSkipped;  // could not even build/evaluate dF
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------

IntegrateResult integrate(const Expr& e, std::string_view symbol) {
    const std::string sym(symbol);
    IntegrateResult res;

    Attempt attempt;
    try {
        attempt = integrate_impl(e, sym, /*depth=*/0, /*parts_depth=*/0,
                                 /*expand_allowed=*/true);
    } catch (const Error&) {
        attempt = Attempt{};  // internal arithmetic blow-up -> honest Unsolved
    }

    if (!attempt.ok) {
        res.status = IntegrateResult::Status::Unsolved;
        res.warnings = std::move(attempt.warnings);
        const std::string giveup = "no applicable integration rule";
        if (std::find(res.warnings.begin(), res.warnings.end(), giveup) ==
            res.warnings.end())
            res.warnings.push_back(giveup);
        return res;
    }

    Expr F;
    try {
        F = simplify(attempt.F);
    } catch (const Error&) {
        res.status = IntegrateResult::Status::Unsolved;
        res.warnings = std::move(attempt.warnings);
        res.warnings.emplace_back("no applicable integration rule");
        return res;
    }

    switch (verify_antiderivative(F, e, sym)) {
        case Verification::Failed:
            res.status = IntegrateResult::Status::Unsolved;
            res.warnings = std::move(attempt.warnings);
            res.warnings.emplace_back("a candidate antiderivative failed verification");
            return res;
        case Verification::AllSkipped:
            res.warnings = std::move(attempt.warnings);
            res.warnings.emplace_back("could not verify numerically");
            break;
        case Verification::Passed:
            res.warnings = std::move(attempt.warnings);
            break;
    }
    res.status = IntegrateResult::Status::Integrated;
    res.antiderivative = F;
    res.method = join_methods(attempt.methods);
    return res;
}

DefiniteIntegralResult integrate_definite(const Expr& e, std::string_view symbol,
                                          const Expr& lo, const Expr& hi) {
    const std::string sym(symbol);
    DefiniteIntegralResult res;

    // Bounds must be symbol-free and finite (§8b).
    if (!free_symbols(lo).empty() || !free_symbols(hi).empty()) {
        res.warnings.emplace_back("integration bounds must be symbol-free");
        return res;
    }
    double a = 0.0;
    double b = 0.0;
    try {
        a = evaluate(lo);  // throws on non-finite values too
        b = evaluate(hi);
    } catch (const Error&) {
        res.warnings.emplace_back(
            "integration bounds must evaluate to finite numbers");
        return res;
    }

    Expr f;
    try {
        f = simplify(e);
    } catch (const Error&) {
        f = e;
    }
    const GridStatus grid = grid_status(f, sym, a, b);
    // A quadrature that exhausted its depth budget produced an untrustworthy
    // number — the integral may be divergent — so it is never published (§8b).
    constexpr const char* kDivergentWarning =
        "numeric quadrature failed to converge; the integral may be divergent";

    // (a) FTC on a verified antiderivative, behind the evaluability grid and
    // a quadrature cross-check.
    IntegrateResult indef;
    try {
        indef = integrate(e, sym);
    } catch (const Error&) {
        indef = IntegrateResult{};
    }
    if (indef.status == IntegrateResult::Status::Integrated) {
        if (grid != GridStatus::Ok) {
            res.warnings.emplace_back(
                "integrand is not evaluable everywhere on the interval; FTC is "
                "unsafe — attempting numeric quadrature");
        } else {
            Expr value;
            std::optional<double> value_num;
            try {
                value = simplify(make_sub(substitute(indef.antiderivative, sym, hi),
                                          substitute(indef.antiderivative, sym, lo)));
                value_num = evaluate(value);
            } catch (const Error&) {
                value_num = std::nullopt;
            }
            if (!value_num) {
                res.warnings.emplace_back(
                    "could not evaluate the FTC value; falling back to numeric "
                    "quadrature");
            } else {
                const Quadrature q = adaptive_simpson(f, sym, a, b);
                const double scale =
                    std::max({1.0, std::fabs(*value_num), std::fabs(q.value)});
                if (q.ok && q.converged &&
                    std::fabs(*value_num - q.value) <= 1e-6 * scale) {
                    res.status = DefiniteIntegralResult::Status::Exact;
                    res.value = value;
                    res.method = "FTC";
                    res.warnings = indef.warnings;
                    return res;
                }
                if (q.ok && !q.converged) {
                    res.status = DefiniteIntegralResult::Status::Unsolved;
                    res.warnings.emplace_back(kDivergentWarning);
                    return res;
                }
                if (q.ok) {
                    // FTC across an undetected discontinuity loses (§8b).
                    res.warnings.emplace_back(
                        "FTC and numeric quadrature disagree (possible "
                        "discontinuity in the interval); preferring the numeric "
                        "value");
                    res.status = DefiniteIntegralResult::Status::Numeric;
                    res.value = make_num(rational_from_double(q.value));
                    res.method = "numeric (adaptive Simpson)";
                    return res;
                }
                // Quadrature hit a non-evaluable point: shared failure path.
            }
        }
    }

    // (b) Adaptive Simpson on the evaluable integrand.  An integrand that is
    // evaluable except at the interval endpoints (removable or endpoint
    // singularity, e.g. sin(x)/x at 0) is integrated over a slightly
    // shrunken open interval; an interior gap stays Unsolved.
    if (grid == GridStatus::InteriorGap) {
        res.status = DefiniteIntegralResult::Status::Unsolved;
        res.warnings.emplace_back(
            "integrand is not evaluable on the integration interval");
        return res;
    }
    double qa = a;
    double qb = b;
    if (grid == GridStatus::EndpointGap) {
        const double nudge = (b - a) * 1e-9;
        qa = a + nudge;
        qb = b - nudge;
        res.warnings.emplace_back(
            "integrand is not evaluable at an integration endpoint; "
            "integrating over a slightly smaller open interval");
    }
    const Quadrature q = adaptive_simpson(f, sym, qa, qb);
    if (!q.ok) {
        res.status = DefiniteIntegralResult::Status::Unsolved;
        res.warnings.emplace_back(
            "integrand is not evaluable on the integration interval");
        return res;
    }
    if (!q.converged) {
        res.status = DefiniteIntegralResult::Status::Unsolved;
        res.warnings.emplace_back(kDivergentWarning);
        return res;
    }
    res.status = DefiniteIntegralResult::Status::Numeric;
    res.value = make_num(rational_from_double(q.value));
    res.method = "numeric (adaptive Simpson)";
    return res;
}

} // namespace mathsolver
