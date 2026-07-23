#include "mathsolver/trig.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mathsolver/printer.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

/// Split an angle into a list of atoms to be summed, expanding positive/negative
/// integer multiples into repeated copies (2x → x, x; -3y → -y, -y, -y). A
/// non-integer or symbolic multiple, or a constant, stays a single atom.
std::vector<Expr> angle_atoms(const Expr& angle) {
    const Expr t = simplify(angle);
    const std::vector<Expr> terms =
        t->kind() == Kind::Add ? t->args() : std::vector<Expr>{t};

    std::vector<Expr> out;
    for (const Expr& term : terms) {
        long long coeff = 1;
        Expr base = term;
        if (term->kind() == Kind::Mul) {
            std::vector<Expr> rest;
            bool found = false;
            for (const Expr& f : term->args()) {
                if (!found && f->kind() == Kind::Number && f->number().is_integer()) {
                    coeff = f->number().num();
                    found = true;
                } else {
                    rest.push_back(f);
                }
            }
            if (found) {
                base = rest.empty()      ? make_num(1)
                       : rest.size() == 1 ? rest[0]
                                          : make_mul(rest);
            }
        }
        // Only expand an integer multiple (|coeff| >= 2) of a non-constant base;
        // fold the sign into each repeated copy so simplify's parity rules
        // (sin(-u) = -sin u) tidy the result.
        if ((coeff >= 2 || coeff <= -2) && base->kind() != Kind::Number) {
            const Expr unit = coeff < 0 ? simplify(make_mul({make_num(-1), base})) : base;
            for (long long i = 0; i < (coeff < 0 ? -coeff : coeff); ++i) {
                out.push_back(unit);
            }
        } else {
            out.push_back(term);
        }
    }
    return out;
}

/// (sin, cos) of the sum of `atoms[i..]`, built purely from the addition
/// formulas — no simplification here (the caller simplifies once at the end).
std::pair<Expr, Expr> sin_cos_of(const std::vector<Expr>& atoms, std::size_t i) {
    if (i >= atoms.size()) return {make_num(0), make_num(1)}; // sin 0 = 0, cos 0 = 1
    const Expr sA = make_fn(FunctionId::Sin, atoms[i]);
    const Expr cA = make_fn(FunctionId::Cos, atoms[i]);
    if (i + 1 == atoms.size()) return {sA, cA};
    const auto [sB, cB] = sin_cos_of(atoms, i + 1);
    // sin(A+B) = sinA cosB + cosA sinB;  cos(A+B) = cosA cosB - sinA sinB.
    return {make_add({make_mul({sA, cB}), make_mul({cA, sB})}),
            make_sub(make_mul({cA, cB}), make_mul({sA, sB}))};
}

Expr expand_rec(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
        case Kind::Symbol:
        case Kind::Constant:
            return e;
        case Kind::Add: {
            std::vector<Expr> out;
            for (const Expr& a : e->args()) out.push_back(expand_rec(a));
            return make_add(std::move(out));
        }
        case Kind::Mul: {
            std::vector<Expr> out;
            for (const Expr& a : e->args()) out.push_back(expand_rec(a));
            return make_mul(std::move(out));
        }
        case Kind::Pow:
            return make_pow(expand_rec(e->arg(0)), expand_rec(e->arg(1)));
        case Kind::Function: {
            const Expr arg = expand_rec(e->arg(0));
            const FunctionId id = e->function();
            if (id == FunctionId::Sin || id == FunctionId::Cos || id == FunctionId::Tan) {
                const std::vector<Expr> atoms = angle_atoms(arg);
                const auto [s, c] = sin_cos_of(atoms, 0);
                if (id == FunctionId::Sin) return s;
                if (id == FunctionId::Cos) return c;
                return make_div(s, c); // tan = sin / cos
            }
            return make_fn(id, arg);
        }
    }
    return e;
}

} // namespace

Expr trig_expand(const Expr& e) {
    return simplify(expand_rec(simplify(e)));
}

// ---------------------------------------------------------------------------
// trig_reduce: products/powers of sin & cos -> sums of multiple-angle trig,
// via the complex-exponential form. Each factor sin(u)^p / cos(u)^p is
// expanded into a sum of e^{i m u} terms with Gaussian-rational coefficients;
// the factors are convolved, the angles collapsed and simplified, and each
// conjugate pair e^{iθ}, e^{-iθ} folded back into 2Re(c)cos θ - 2Im(c)sin θ.
// ---------------------------------------------------------------------------

namespace {

/// A Gaussian rational a + b·i.
struct Cx {
    Rational re, im;
};
Cx cadd(const Cx& a, const Cx& b) { return {a.re + b.re, a.im + b.im}; }
Cx cmul(const Cx& a, const Cx& b) {
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}
Cx cscale(const Cx& a, const Rational& r) { return {a.re * r, a.im * r}; }

/// i^n for any integer n, as a Gaussian unit.
Cx i_pow(long long n) {
    switch (((n % 4) + 4) % 4) {
        case 0: return {Rational(1), Rational(0)};
        case 1: return {Rational(0), Rational(1)};
        case 2: return {Rational(-1), Rational(0)};
        default: return {Rational(0), Rational(-1)};
    }
}

/// Binomial coefficient C(p, k) for small p (multiplicative, exact in 64 bits
/// for the powers trig_reduce accepts).
long long binomial(int p, int k) {
    if (k < 0 || k > p) return 0;
    long long result = 1;
    for (int i = 0; i < k; ++i) {
        result = result * (p - i) / (i + 1);
    }
    return result;
}

// An angle is a linear combination of atomic trig arguments, keyed by the
// argument's printed form; the coefficients are integer multiples.
using Angle = std::map<std::string, long long>;

Angle add_angles(const Angle& a, const Angle& b) {
    Angle out = a;
    for (const auto& [k, v] : b) {
        out[k] += v;
        if (out[k] == 0) out.erase(k);
    }
    return out;
}

using ExpMap = std::map<Angle, Cx>;

void accumulate(ExpMap& m, const Angle& a, const Cx& c) {
    auto it = m.find(a);
    if (it == m.end()) m[a] = c;
    else it->second = cadd(it->second, c);
}

/// Highest power trig_reduce will expand (keeps binomials/2^p in range).
constexpr int k_max_power = 24;

/// Reduce a single monomial (a product) to a sum of multiple-angle trig.
Expr reduce_monomial(const Expr& mono, std::map<std::string, Expr>& atoms) {
    const std::vector<Expr> factors =
        mono->kind() == Kind::Mul ? mono->args() : std::vector<Expr>{mono};

    std::vector<Expr> coeff_factors;
    struct Trig {
        bool is_sin;
        std::string key;
        int power;
    };
    std::vector<Trig> trigs;

    for (const Expr& f : factors) {
        const Expr* fn = nullptr;
        int power = 1;
        if (f->kind() == Kind::Function) {
            fn = &f;
        } else if (f->kind() == Kind::Pow && f->arg(0)->kind() == Kind::Function &&
                   f->arg(1)->kind() == Kind::Number && f->arg(1)->number().is_integer() &&
                   !f->arg(1)->number().is_negative()) {
            fn = &f->arg(0);
            power = static_cast<int>(f->arg(1)->number().num());
        }
        if (fn && ((*fn)->function() == FunctionId::Sin || (*fn)->function() == FunctionId::Cos) &&
            power >= 1 && power <= k_max_power) {
            const Expr arg = simplify((*fn)->arg(0));
            const std::string key = to_string(arg, PrintStyle::Plain);
            atoms.emplace(key, arg);
            trigs.push_back({(*fn)->function() == FunctionId::Sin, key, power});
        } else {
            coeff_factors.push_back(f);
        }
    }

    const Expr coeff = coeff_factors.empty()      ? make_num(1)
                       : coeff_factors.size() == 1 ? coeff_factors[0]
                                                   : make_mul(coeff_factors);
    if (trigs.empty()) return mono;

    // Convolve every factor's exponential expansion.
    ExpMap acc;
    acc[Angle{}] = Cx{Rational(1), Rational(0)};
    for (const Trig& t : trigs) {
        const int p = t.power;
        const long long two_p = 1LL << p;
        ExpMap factor;
        for (int k = 0; k <= p; ++k) {
            const long long m = p - 2 * k;
            Angle ang;
            if (m != 0) ang[t.key] = m;
            const long long bin = binomial(p, k);
            Cx c;
            if (t.is_sin) {
                // sin(u)^p = (1/(2i)^p) Σ C(p,k)(-1)^k e^{i(p-2k)u}
                const long long sign = (k % 2 == 0) ? 1 : -1;
                c = cscale(i_pow(-p), Rational(sign * bin, two_p));
            } else {
                // cos(u)^p = (1/2^p) Σ C(p,k) e^{i(p-2k)u}
                c = Cx{Rational(bin, two_p), Rational(0)};
            }
            accumulate(factor, ang, c);
        }
        ExpMap next;
        for (const auto& [aKey, aC] : acc) {
            for (const auto& [bKey, bC] : factor) {
                accumulate(next, add_angles(aKey, bKey), cmul(aC, bC));
            }
        }
        acc.swap(next);
    }

    // Collapse to canonical angles (different keys can reduce to the same angle
    // when the atoms are dependent, e.g. 2x and x). `expand` gives a canonical
    // distributed form so θ and -θ match up as strings.
    std::map<std::string, std::pair<Expr, Cx>> grouped;
    for (const auto& [ang, c] : acc) {
        std::vector<Expr> terms;
        for (const auto& [key, coef] : ang) {
            terms.push_back(make_mul({make_num(coef), atoms.at(key)}));
        }
        const Expr angle = terms.empty() ? make_num(0) : expand(make_add(terms));
        const std::string s = to_string(angle, PrintStyle::Plain);
        auto it = grouped.find(s);
        if (it == grouped.end()) grouped[s] = {angle, c};
        else it->second.second = cadd(it->second.second, c);
    }

    // Fold each conjugate pair e^{iθ}, e^{-iθ} into 2Re(c)cos θ - 2Im(c)sin θ,
    // choosing the representative that prints without a leading '-'.
    std::vector<Expr> out;
    std::set<std::string> done;
    for (const auto& [s, pr] : grouped) {
        if (done.count(s)) continue;
        if (s == "0") {
            done.insert(s);
            out.push_back(make_num(pr.second.re)); // e^{i0} = 1
            continue;
        }
        const Expr neg = expand(make_mul({make_num(-1), pr.first}));
        const std::string negS = to_string(neg, PrintStyle::Plain);
        done.insert(s);
        done.insert(negS);
        Expr angle = pr.first;
        Cx c = pr.second;
        if (!s.empty() && s[0] == '-') { // prefer +θ as the representative
            const auto it = grouped.find(negS);
            angle = neg;
            c = it != grouped.end() ? it->second.second : Cx{c.re, Rational(-1) * c.im};
        }
        out.push_back(make_mul({make_num(Rational(2) * c.re), make_fn(FunctionId::Cos, angle)}));
        out.push_back(make_mul({make_num(Rational(-2) * c.im), make_fn(FunctionId::Sin, angle)}));
    }
    return make_mul({coeff, make_add(out)});
}

} // namespace

Expr trig_reduce(const Expr& e) {
    const Expr ex = expand(simplify(e));
    const std::vector<Expr> monos =
        ex->kind() == Kind::Add ? ex->args() : std::vector<Expr>{ex};
    std::map<std::string, Expr> atoms;
    std::vector<Expr> parts;
    for (const Expr& m : monos) parts.push_back(reduce_monomial(m, atoms));
    return simplify(make_add(parts));
}

} // namespace mathsolver
