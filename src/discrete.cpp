// Discrete calculus (discrete.hpp): closed-form sums/products by exact
// undetermined-coefficient fitting, telescoping through apart(), and linear
// recurrences by characteristic roots.
//
// The fitting trick used throughout: the closed form of Σ p(k)·r^k is known
// to have the shape q(n)·r^n + C (deg q = deg p; drop r^n when r = 1 and
// raise the degree instead), so the unknown coefficients are pinned down by
// equating the ansatz to exact partial sums at deg+2 consecutive points and
// solving the linear system with the exact Gaussian-elimination solver —
// then verified at one extra point.

#include "mathsolver/discrete.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mathsolver/evaluator.hpp"

#include "mathsolver/apart.hpp"
#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"
#include "polyfactor.hpp"

namespace mathsolver {

namespace {

bool is_zero(const Expr& e) {
    return e->kind() == Kind::Number && e->number().is_zero();
}

bool is_one(const Expr& e) {
    return e->kind() == Kind::Number && e->number() == Rational{1};
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

Expr product_of(std::vector<Expr> fs) {
    if (fs.empty()) {
        return make_num(1);
    }
    if (fs.size() == 1) {
        return fs.front();
    }
    return simplify(make_mul(std::move(fs)));
}

std::optional<long long> as_integer(const Expr& e) {
    const auto r = as_num(simplify(e));
    if (r && r->is_integer()) {
        return r->num();
    }
    return std::nullopt;
}

/// Split term = p(var) · r^var: geometric factors are Pow(base, e) with e
/// linear in var and base free of var; r accumulates base^slope, and
/// base^intercept joins the polynomial part.
void split_geometric(const Expr& term, const std::string& var, Expr& r,
                     Expr& poly) {
    std::vector<Expr> rs;
    std::vector<Expr> ps;
    for (const Expr& f : factors_of(simplify(term))) {
        if (f->kind() == Kind::Pow && !contains_symbol(f->arg(0), var) &&
            contains_symbol(f->arg(1), var)) {
            const Expr slope = simplify(differentiate(f->arg(1), var));
            if (!contains_symbol(slope, var)) {
                const Expr intercept = simplify(make_sub(
                    f->arg(1), make_mul({slope, make_sym(var)})));
                rs.push_back(make_pow(f->arg(0), slope));
                if (!is_zero(intercept)) {
                    ps.push_back(make_pow(f->arg(0), intercept));
                }
                continue;
            }
        }
        ps.push_back(f);
    }
    r = product_of(std::move(rs));
    poly = product_of(std::move(ps));
}

/// Exact partial sum  Σ_{k=lo}^{m} term  by direct accumulation.
Expr partial_sum(const Expr& term, const std::string& var, long long lo,
                 long long m) {
    std::vector<Expr> pieces;
    for (long long k = lo; k <= m; ++k) {
        pieces.push_back(substitute(term, var, make_num(k)));
    }
    if (pieces.empty()) {
        return make_num(0);
    }
    return simplify(make_add(std::move(pieces)));
}

/// Fit unknown coefficients so that Σ u_i · basis_i(n) equals `target(m)`
/// at consecutive integer points m = m0, m0+1, ...  Returns the solved
/// values (empty on failure) and appends any solver warnings.
std::map<std::string, Expr> fit_unknowns(
    const std::vector<Expr>& basis, const std::string& var,
    const std::vector<std::pair<long long, Expr>>& samples,
    std::vector<std::string>& warnings) {
    std::vector<std::string> names;
    std::vector<Expr> ansatz_terms;
    for (std::size_t i = 0; i < basis.size(); ++i) {
        names.push_back("__fit" + std::to_string(i));
        ansatz_terms.push_back(make_mul({make_sym(names.back()), basis[i]}));
    }
    const Expr ansatz = make_add(std::move(ansatz_terms));
    std::vector<Equation> eqs;
    for (const auto& [m, value] : samples) {
        eqs.push_back(
            {simplify(substitute(ansatz, var, make_num(m))), value});
    }
    const SystemSolveResult sys = solve_system(eqs, names);
    if (sys.status != SystemSolveResult::Status::Solved) {
        return {};
    }
    warnings.insert(warnings.end(), sys.warnings.begin(), sys.warnings.end());
    return sys.values;
}

Expr apply_fit(const std::vector<Expr>& basis,
               const std::map<std::string, Expr>& values) {
    std::vector<Expr> terms;
    std::size_t i = 0;
    for (const Expr& b : basis) {
        const auto it = values.find("__fit" + std::to_string(i));
        ++i;
        if (it == values.end() || is_zero(simplify(it->second))) {
            continue;
        }
        terms.push_back(make_mul({it->second, b}));
    }
    if (terms.empty()) {
        return make_num(0);
    }
    return simplify(make_add(std::move(terms)));
}

/// n^i as an Expr (1 for i = 0).
Expr npow(const std::string& var, int i) {
    if (i == 0) {
        return make_num(1);
    }
    if (i == 1) {
        return make_sym(var);
    }
    return make_pow(make_sym(var), make_num(i));
}

SumResult unsolved(std::string why) {
    SumResult r;
    r.status = SumResult::Status::Unsolved;
    r.warnings.push_back(std::move(why));
    return r;
}

/// c / k with c free of the summation variable -> the scale and nullopt
/// otherwise.
std::optional<Expr> harmonic_scale(const Expr& term, const std::string& var) {
    const Expr inv = make_pow(make_sym(var), make_num(-1));
    if (structurally_equal(term, inv)) {
        return make_num(1);
    }
    if (term->kind() == Kind::Mul && term->args().size() == 2) {
        const Expr& a = term->args()[0];
        const Expr& b = term->args()[1];
        if (structurally_equal(b, inv) && !contains_symbol(a, var)) {
            return a;
        }
        if (structurally_equal(a, inv) && !contains_symbol(b, var)) {
            return b;
        }
    }
    return std::nullopt;
}

/// Closed form S(n) = Σ_{k=lo}^{n} of one product term, or Unsolved.
SumResult sum_term_closed(const Expr& term, const std::string& var,
                          long long lo) {
    // Σ c/k from lo to n = c (H(n) - H(lo-1)): harmonic numbers are first-
    // class, so the "sum" of the harmonic tail has an honest closed form.
    if (lo >= 1) {
        if (const auto c = harmonic_scale(term, var)) {
            SumResult res;
            res.status = SumResult::Status::Exact;
            res.value = simplify(make_mul(
                {*c, make_sub(make_fn(FunctionId::Harmonic, make_sym(var)),
                              make_fn(FunctionId::Harmonic,
                                      make_num(lo - 1)))}));
            res.method = "harmonic numbers";
            return res;
        }
    }
    Expr r, poly;
    split_geometric(term, var, r, poly);
    const auto pc = polynomial_coefficients(poly, var);
    if (!pc) {
        return unsolved(std::format(
            "no closed form for the summand '{}'",
            to_string(term, PrintStyle::Plain)));
    }
    const int d = static_cast<int>(pc->size()) - 1;
    const bool geometric = !is_one(r);

    // Ansatz basis in the bound variable n.
    std::vector<Expr> basis;
    if (!geometric) {
        for (int i = 0; i <= d + 1; ++i) {
            basis.push_back(npow(var, i));
        }
    } else {
        const Expr rn = make_pow(r, make_sym(var));
        for (int i = 0; i <= d; ++i) {
            basis.push_back(simplify(make_mul({npow(var, i), rn})));
        }
        basis.push_back(make_num(1)); // the constant C
    }

    std::vector<std::pair<long long, Expr>> samples;
    for (long long m = lo; m < lo + static_cast<long long>(basis.size()); ++m) {
        samples.push_back({m, partial_sum(term, var, lo, m)});
    }
    SumResult res;
    std::map<std::string, Expr> values =
        fit_unknowns(basis, var, samples, res.warnings);
    if (values.empty()) {
        return unsolved("the closed-form fit did not solve");
    }
    const Expr closed = apply_fit(basis, values);

    // Verify at one extra point. Structural zero-testing fails on symbolic
    // ratios (simplify cannot cancel x^6/(x-1) against a polynomial), so
    // the difference is checked numerically over random parameter values.
    const long long check = lo + static_cast<long long>(basis.size());
    const Expr diff = simplify(make_sub(
        simplify(substitute(closed, var, make_num(check))),
        partial_sum(term, var, lo, check)));
    if (!is_zero(diff)) {
        bool verified = false;
        const std::set<std::string> params = free_symbols(diff);
        for (const double seed : {0.7317, 1.9173, -2.3391}) {
            Bindings b;
            double offset = 0.0;
            for (const std::string& p : params) {
                b[p] = seed + offset;
                offset += 0.6180339887;
            }
            try {
                if (std::abs(evaluate(diff, b)) < 1e-6) {
                    verified = true;
                    break;
                }
                verified = false;
                break; // a genuinely nonzero difference: fit is wrong
            } catch (const Error&) {
                // Singular sample (e.g. x = 1 pivot): try the next seed.
            }
        }
        if (!verified) {
            return unsolved("closed-form verification failed");
        }
    }
    res.status = SumResult::Status::Exact;
    res.value = closed;
    res.method = geometric ? "geometric closed-form fit" : "polynomial fit";
    return res;
}

/// The C constant of the geometric ansatz is the infinite sum when |r| < 1.
SumResult sum_term_infinite(const Expr& term, const std::string& var,
                            long long lo) {
    Expr r, poly;
    split_geometric(term, var, r, poly);
    const auto pc = polynomial_coefficients(poly, var);
    if (!pc) {
        // Not p(k)·r^k: try telescoping through partial fractions.
        SumResult res;
        Expr decomposed;
        try {
            decomposed = apart(term, var);
        } catch (const Error&) {
            return unsolved(std::format(
                "no closed form for the summand '{}'",
                to_string(term, PrintStyle::Plain)));
        }
        // Every additive piece must be c/(k - root)^1 with integer root
        // < lo, and the coefficients must cancel in total.
        std::vector<std::pair<Expr, long long>> pieces; // (c_i, root_i)
        const std::vector<Expr> parts =
            decomposed->kind() == Kind::Add ? decomposed->args()
                                            : std::vector<Expr>{decomposed};
        std::vector<Expr> coef_sum;
        for (const Expr& piece : parts) {
            Expr c = make_num(1);
            std::optional<long long> root;
            bool ok = true;
            for (const Expr& f : factors_of(piece)) {
                if (f->kind() == Kind::Pow && contains_symbol(f->arg(0), var)) {
                    const auto e = as_num(f->arg(1));
                    if (!e || *e != Rational{-1} || root.has_value()) {
                        ok = false;
                        break;
                    }
                    // Base is (k - root): root = k - base (expand first —
                    // simplify alone keeps the nested Add unfolded).
                    const auto rt = as_integer(simplify(expand(
                        make_sub(make_sym(var), f->arg(0)))));
                    if (!rt) {
                        ok = false;
                        break;
                    }
                    root = *rt;
                } else if (contains_symbol(f, var)) {
                    ok = false;
                    break;
                } else {
                    c = simplify(make_mul({c, f}));
                }
            }
            if (!ok || !root) {
                return unsolved(
                    "the summand's partial fractions are not simple integer "
                    "poles");
            }
            if (*root >= lo) {
                return unsolved(std::format(
                    "the summand has a pole inside the summation range (at "
                    "{} = {})",
                    var, *root));
            }
            pieces.push_back({c, *root});
            coef_sum.push_back(c);
        }
        if (!is_zero(simplify(make_add(std::move(coef_sum))))) {
            res.status = SumResult::Status::Diverges;
            res.method = "harmonic comparison";
            return res;
        }
        // Σ_{k=lo}^{∞} Σ_i c_i/(k - root_i) = -Σ_i c_i · H(lo - 1 - root_i)
        // with H(m) = Σ_{j=1}^{m} 1/j (the divergent parts cancel).
        std::vector<Expr> total;
        for (const auto& [c, root] : pieces) {
            const long long m = lo - 1 - root;
            Rational h{0};
            for (long long j = 1; j <= m; ++j) {
                h = h + Rational{1, j};
            }
            total.push_back(make_mul({make_num(-1), c, make_num(h)}));
        }
        res.status = SumResult::Status::Exact;
        res.value = simplify(make_add(std::move(total)));
        res.method = "telescoping (partial fractions)";
        return res;
    }

    const bool zero_poly = pc->size() == 1 && is_zero(pc->front());
    if (zero_poly) {
        SumResult res;
        res.status = SumResult::Status::Exact;
        res.value = make_num(0);
        return res;
    }
    if (is_one(r)) {
        SumResult res;
        res.status = SumResult::Status::Diverges;
        res.method = "polynomial terms do not vanish";
        return res;
    }
    const Expr r_simple = simplify(r);
    const auto rn = as_num(r_simple);
    SumResult res;
    if (rn) {
        // Exact rational comparison: to_double rounds (2^62-1)/2^62 to 1.0
        // and would misreport a convergent series as divergent.
        const Rational mag = rn->num() < 0 ? -*rn : *rn;
        if (!(mag < Rational{1})) {
            res.status = SumResult::Status::Diverges;
            res.method = "|ratio| >= 1";
            return res;
        }
    } else if (free_symbols(r_simple).empty()) {
        // Constant but irrational ratio (sqrt(2), e, ...): classify
        // numerically — it is NOT a free parameter and must not get the
        // "valid for |r| < 1" escape hatch.
        double magnitude = std::numeric_limits<double>::infinity();
        try {
            magnitude = std::abs(evaluate(r_simple, Bindings{}));
        } catch (const Error&) {
        }
        if (magnitude >= 1.0) {
            res.status = SumResult::Status::Diverges;
            res.method = "|ratio| >= 1";
            return res;
        }
    } else {
        res.warnings.push_back(std::format(
            "valid for |{}| < 1", to_string(r, PrintStyle::Plain)));
    }
    // Fit the finite form and keep only the constant C (q(n)·r^n -> 0).
    const SumResult finite = sum_term_closed(term, var, lo);
    if (finite.status != SumResult::Status::Exact) {
        return finite;
    }
    res.warnings.insert(res.warnings.end(), finite.warnings.begin(),
                        finite.warnings.end());
    // Drop every additive piece containing r^n.
    std::vector<Expr> keep;
    const std::vector<Expr> parts = finite.value->kind() == Kind::Add
                                        ? finite.value->args()
                                        : std::vector<Expr>{finite.value};
    for (const Expr& piece : parts) {
        if (!contains_symbol(piece, var)) {
            keep.push_back(piece);
        }
    }
    res.status = SumResult::Status::Exact;
    res.value = keep.empty() ? make_num(0)
                             : simplify(make_add(std::move(keep)));
    res.method = "geometric series";
    return res;
}

} // namespace

SumResult sum_finite(const Expr& term, std::string_view var, const Expr& lo,
                     const Expr& hi) {
    const std::string v{var};
    if (v.empty()) {
        throw Error("sum needs a summation variable");
    }
    const auto lo_i = as_integer(lo);
    if (!lo_i) {
        return unsolved("the lower bound must be an integer");
    }
    const auto hi_i = as_integer(hi);
    if (!hi_i && simplify(hi)->kind() == Kind::Number) {
        // A fractional numeric bound must not masquerade as a symbol (the
        // fitted closed form would happily evaluate at n = -5/2).
        return unsolved("the upper bound must be an integer or a symbol");
    }
    if (hi_i && *hi_i < *lo_i) {
        SumResult r;
        r.status = SumResult::Status::Exact;
        r.value = make_num(0);
        r.method = "empty range";
        return r;
    }
    try {
        // Small numeric ranges: sum directly (always exact, no fitting).
        if (hi_i && *hi_i - *lo_i <= 64) {
            SumResult r;
            r.status = SumResult::Status::Exact;
            r.value = partial_sum(simplify(term), v, *lo_i, *hi_i);
            r.method = "direct summation";
            return r;
        }

        const Expr t = simplify(term);
        const std::vector<Expr> terms =
            t->kind() == Kind::Add ? t->args() : std::vector<Expr>{t};
        std::vector<Expr> pieces;
        SumResult out;
        for (const Expr& one : terms) {
            SumResult part = sum_term_closed(one, v, *lo_i);
            if (part.status != SumResult::Status::Exact) {
                return part;
            }
            out.warnings.insert(out.warnings.end(), part.warnings.begin(),
                                part.warnings.end());
            if (out.method.empty()) {
                out.method = part.method;
            } else if (out.method != part.method) {
                out.method = "termwise closed forms";
            }
            pieces.push_back(part.value);
        }
        const Expr closed = simplify(make_add(std::move(pieces)));
        out.status = SumResult::Status::Exact;
        out.value = simplify(substitute(closed, v, hi));
        return out;
    } catch (const Error& e) {
        return unsolved(e.what()); // 64-bit rational overflow etc.
    }
}

SumResult sum_infinite(const Expr& term, std::string_view var, const Expr& lo) {
    const std::string v{var};
    if (v.empty()) {
        throw Error("sum needs a summation variable");
    }
    const auto lo_i = as_integer(lo);
    if (!lo_i) {
        return unsolved("the lower bound must be an integer");
    }
    const Expr t = simplify(term);
    try {
        // Whole-expression pass FIRST: partial-fraction cancellation can
        // happen ACROSS additive terms (1/k - 1/(k+1) telescopes to 1 even
        // though each term alone is harmonically divergent), so a termwise
        // divergence verdict is only trustworthy when the whole-expression
        // telescoping did not resolve it.
        SumResult whole = sum_term_infinite(t, v, *lo_i);
        if (whole.status != SumResult::Status::Unsolved ||
            t->kind() != Kind::Add) {
            return whole;
        }
        std::vector<Expr> pieces;
        SumResult out;
        for (const Expr& one : t->args()) {
            SumResult part = sum_term_infinite(one, v, *lo_i);
            if (part.status != SumResult::Status::Exact) {
                return part;
            }
            out.warnings.insert(out.warnings.end(), part.warnings.begin(),
                                part.warnings.end());
            if (out.method.empty()) {
                out.method = part.method;
            } else if (out.method != part.method) {
                out.method = "termwise closed forms";
            }
            pieces.push_back(part.value);
        }
        out.status = SumResult::Status::Exact;
        out.value = simplify(make_add(std::move(pieces)));
        return out;
    } catch (const Error& e) {
        // Exact arithmetic can overflow 64-bit rationals (harmonic numbers
        // past ~H(44)); that is an honest Unsolved, not an exception.
        return unsolved(e.what());
    }
}

SumResult product_finite(const Expr& term, std::string_view var, const Expr& lo,
                         const Expr& hi) {
    const std::string v{var};
    if (v.empty()) {
        throw Error("product needs a variable");
    }
    const auto lo_i = as_integer(lo);
    if (!lo_i) {
        return unsolved("the lower bound must be an integer");
    }
    const auto hi_i = as_integer(hi);
    if (!hi_i && simplify(hi)->kind() == Kind::Number) {
        return unsolved("the upper bound must be an integer or a symbol");
    }
    if (hi_i) {
        if (*hi_i < *lo_i) {
            SumResult r;
            r.status = SumResult::Status::Exact;
            r.value = make_num(1);
            r.method = "empty range";
            return r;
        }
        if (*hi_i - *lo_i > 512) {
            return unsolved("numeric products are capped at 512 factors");
        }
        try {
            std::vector<Expr> fs;
            for (long long k = *lo_i; k <= *hi_i; ++k) {
                fs.push_back(substitute(simplify(term), v, make_num(k)));
            }
            SumResult r;
            r.status = SumResult::Status::Exact;
            r.value = simplify(make_mul(std::move(fs)));
            r.method = "direct product";
            return r;
        } catch (const Error& e) {
            return unsolved(e.what()); // 64-bit rational overflow
        }
    }
    // Symbolic upper bound: constant and geometric factors only.
    Expr rr, poly;
    split_geometric(simplify(term), v, rr, poly);
    if (contains_symbol(poly, v)) {
        return unsolved(
            "symbolic products need a constant or geometric term (no "
            "factorial closed form yet)");
    }
    const Expr count = simplify(
        make_add({make_sub(hi, lo), make_num(1)}));            // hi - lo + 1
    std::vector<Expr> fs;
    if (!is_one(poly)) {
        fs.push_back(make_pow(poly, count));
    }
    if (!is_one(rr)) {
        // r^(Σ k) = r^((lo + hi)(hi - lo + 1)/2).
        const Expr ksum = simplify(make_div(
            make_mul({make_add({lo, hi}), count}), make_num(2)));
        fs.push_back(make_pow(rr, ksum));
    }
    SumResult r;
    r.status = SumResult::Status::Exact;
    r.value = product_of(std::move(fs));
    r.method = "geometric product";
    return r;
}

// --- rsolve -----------------------------------------------------------------

namespace {

std::string trim_ws(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
    return std::string(s.substr(b, e - b));
}

std::vector<std::string> split_signed_terms(const std::string& side) {
    std::vector<std::string> terms;
    std::string current;
    int depth = 0;
    for (const char c : side) {
        if (c == '(' || c == '[' || c == '{') ++depth;
        if (c == ')' || c == ']' || c == '}') --depth;
        if ((c == '+' || c == '-') && depth == 0 && !trim_ws(current).empty()) {
            terms.push_back(current);
            current.clear();
        }
        current += c;
    }
    if (!trim_ws(current).empty()) {
        terms.push_back(current);
    }
    return terms;
}

/// Parsed a(...) reference: relative "a(n+2)" (offset) or absolute "a(3)"
/// (index — only meaningful in initial conditions).
struct ARef {
    bool absolute = false;
    long long value = 0;
};

/// Parse an "a(n+2)" / "a(3)" reference in `term`, filling `coef` with the
/// text before it. Returns nullopt (with empty rest_err) when the term does
/// not reference a(...), or nullopt with a message on malformed input.
std::optional<ARef> parse_a_ref(const std::string& term, std::string& coef,
                                std::string& rest_err) {
    const std::size_t apos = term.find("a(");
    if (apos == std::string::npos) {
        return std::nullopt;
    }
    coef = trim_ws(term.substr(0, apos));
    if (!coef.empty() && coef.back() == '*') {
        coef = trim_ws(coef.substr(0, coef.size() - 1));
    }
    const std::size_t close = term.find(')', apos);
    if (close == std::string::npos) {
        rest_err = "unbalanced parentheses in '" + term + "'";
        return std::nullopt;
    }
    const std::string inside = trim_ws(term.substr(apos + 2, close - apos - 2));
    const std::string after = trim_ws(term.substr(close + 1));
    if (!after.empty()) {
        rest_err = "unexpected '" + after + "' after a(...)";
        return std::nullopt;
    }
    ARef ref;
    if (inside == "n") {
        return ref;
    }
    if (inside.starts_with("n")) {
        try {
            ref.value = std::stoll(trim_ws(inside.substr(1)));
        } catch (...) {
            rest_err = "cannot read the index '" + inside + "'";
            return std::nullopt;
        }
        return ref;
    }
    try {
        ref.value = std::stoll(inside);
        ref.absolute = true;
        return ref;
    } catch (...) {
        rest_err = "cannot read the index '" + inside + "'";
        return std::nullopt;
    }
}

Rational parse_coef(const std::string& text) {
    if (text.empty() || text == "+") {
        return Rational{1};
    }
    if (text == "-") {
        return Rational{-1};
    }
    Expr c;
    try {
        c = simplify(parse_expression(text));
    } catch (const Error&) {
        throw Error(std::format("rsolve: cannot read the coefficient '{}'", text));
    }
    if (c->kind() != Kind::Number) {
        throw Error(std::format(
            "rsolve: the coefficient '{}' must be numeric", text));
    }
    return c->number();
}

} // namespace

RsolveResult rsolve(std::string_view recurrence,
                    const std::vector<std::string>& conditions) {
    const std::string text{recurrence};
    const std::size_t eq_pos = text.find('=');
    if (eq_pos == std::string::npos) {
        throw Error("rsolve: the recurrence needs '=', e.g. "
                    "a(n+2) = a(n+1) + a(n)");
    }

    // Collect a(n+j) coefficients (RHS terms move left, negated) and the
    // forcing pieces (non-a terms; LHS pieces move right, negated).
    std::map<long long, Rational> coeffs;
    std::vector<Expr> forcing_terms;
    const auto eat_side = [&](const std::string& side, bool left) {
        for (const std::string& term : split_signed_terms(side)) {
            std::string coef_text;
            std::string err;
            const auto ref = parse_a_ref(term, coef_text, err);
            if (!err.empty()) {
                throw Error("rsolve: " + err);
            }
            if (ref) {
                if (ref->absolute) {
                    throw Error(std::format(
                        "rsolve: the recurrence must use a(n)-relative "
                        "indices (got 'a({})')",
                        ref->value));
                }
                Rational c = parse_coef(coef_text);
                if (!left) {
                    c = -c;
                }
                coeffs[ref->value] = (coeffs.contains(ref->value)
                                          ? coeffs[ref->value]
                                          : Rational{0}) + c;
            } else {
                Expr g;
                try {
                    g = parse_expression(term);
                } catch (const Error& e) {
                    throw Error(std::format(
                        "rsolve: cannot read the term '{}': {}", trim_ws(term),
                        e.what()));
                }
                forcing_terms.push_back(left ? simplify(make_neg(g)) : g);
            }
        }
    };
    eat_side(text.substr(0, eq_pos), true);
    eat_side(text.substr(eq_pos + 1), false);

    if (coeffs.empty()) {
        throw Error("rsolve: the recurrence has no a(...) terms");
    }
    // Shift so the lowest reference is a(n).
    const long long shift = coeffs.begin()->first;
    std::map<long long, Rational> shifted;
    for (const auto& [j, c] : coeffs) {
        if (!c.is_zero()) {
            shifted[j - shift] = c;
        }
    }
    Expr forcing = forcing_terms.empty()
                       ? make_num(0)
                       : simplify(make_add(std::move(forcing_terms)));
    if (shift != 0) {
        forcing = simplify(substitute(
            forcing, "n", make_sub(make_sym("n"), make_num(shift))));
    }
    if (contains_symbol(forcing, "a")) {
        throw Error("rsolve: only linear recurrences with constant "
                    "coefficients are supported");
    }
    if (shifted.empty()) {
        throw Error("rsolve: every a(...) coefficient cancels — the equation "
                    "is not a recurrence");
    }
    const long long order = shifted.rbegin()->first;
    if (order < 1) {
        throw Error("rsolve: the recurrence must relate at least two indices");
    }
    if (!shifted.contains(0) || shifted.at(0).is_zero()) {
        throw Error("rsolve: the trailing term's coefficient is zero — shift "
                    "the recurrence so a(n) appears");
    }

    // Characteristic polynomial and its exact roots.
    std::vector<Rational> charpoly(static_cast<std::size_t>(order) + 1,
                                   Rational{0});
    for (const auto& [j, c] : shifted) {
        charpoly[static_cast<std::size_t>(j)] = c;
    }
    internal::FactoredPoly fp;
    try {
        internal::factor_rational_poly(charpoly, 1, fp);
    } catch (const Error& e) {
        throw Error(std::string("rsolve: ") + e.what());
    }

    RsolveResult result;
    result.order = static_cast<int>(order);
    const Expr n = make_sym("n");
    std::vector<Expr> basis;
    for (const auto& [root, m] : fp.roots) {
        const Expr rn = make_pow(make_num(root), n);
        for (int i = 0; i < m; ++i) {
            basis.push_back(simplify(make_mul({npow("n", i), rn})));
        }
    }
    for (const auto& [bc, m] : fp.quads) {
        const Rational b = bc.first;
        const Rational c = bc.second;
        const Rational disc = b * b - Rational{4} * c;
        if (disc < Rational{0}) {
            throw Error("rsolve: complex characteristic roots are not "
                        "supported yet");
        }
        const Expr sq = make_sqrt(make_num(disc));
        for (const int sign : {+1, -1}) {
            const Expr root = simplify(make_div(
                make_add({make_num(-b), make_mul({make_num(sign), sq})}),
                make_num(2)));
            const Expr rn = make_pow(root, n);
            for (int i = 0; i < m; ++i) {
                basis.push_back(simplify(make_mul({npow("n", i), rn})));
            }
        }
    }

    // Particular solution for p(n)·s^n forcing by undetermined coefficients.
    Expr particular = make_num(0);
    if (!is_zero(forcing)) {
        Expr s, poly;
        split_geometric(forcing, "n", s, poly);
        const auto pc = polynomial_coefficients(poly, "n");
        if (!pc) {
            throw Error(std::format(
                "rsolve: unsupported forcing '{}' (use p(n)·s^n forms)",
                to_string(forcing, PrintStyle::Plain)));
        }
        const int d = static_cast<int>(pc->size()) - 1;
        int mu = 0; // resonance: multiplicity of s among the roots
        if (const auto sn = as_num(simplify(s))) {
            const auto it = fp.roots.find(*sn);
            if (it != fp.roots.end()) {
                mu = it->second;
            }
        }
        const Expr sn_expr = make_pow(s, n);
        std::vector<Expr> pbasis;
        for (int i = 0; i <= d; ++i) {
            pbasis.push_back(simplify(
                make_mul({npow("n", mu), npow("n", i), sn_expr})));
        }
        // Residual sampling: Σ c_j q(n+j) - g(n) at d+1 points.
        std::vector<std::string> names;
        std::vector<Expr> ansatz_terms;
        for (std::size_t i = 0; i < pbasis.size(); ++i) {
            names.push_back("__pfit" + std::to_string(i));
            ansatz_terms.push_back(
                make_mul({make_sym(names.back()), pbasis[i]}));
        }
        const Expr ansatz = make_add(std::move(ansatz_terms));
        std::vector<Equation> eqs;
        for (int m = 0; m <= d; ++m) {
            std::vector<Expr> lhs_terms;
            for (const auto& [j, c] : shifted) {
                lhs_terms.push_back(make_mul(
                    {make_num(c),
                     substitute(ansatz, "n", make_num(m + j))}));
            }
            eqs.push_back({simplify(make_add(std::move(lhs_terms))),
                           simplify(substitute(forcing, "n", make_num(m)))});
        }
        const SystemSolveResult sys = solve_system(eqs, names);
        if (sys.status != SystemSolveResult::Status::Solved) {
            throw Error("rsolve: the particular-solution fit did not solve");
        }
        std::vector<Expr> pterms;
        for (std::size_t i = 0; i < pbasis.size(); ++i) {
            const Expr val = sys.values.at(names[i]);
            if (!is_zero(simplify(val))) {
                pterms.push_back(make_mul({val, pbasis[i]}));
            }
        }
        particular = pterms.empty() ? make_num(0)
                                    : simplify(make_add(std::move(pterms)));
        result.method = "characteristic roots + undetermined coefficients";
    } else {
        result.method = "characteristic roots";
    }

    // Constants from the initial conditions.
    std::map<long long, Expr> ics;
    for (const std::string& cond : conditions) {
        const std::size_t eq = cond.find('=');
        const std::string name = eq == std::string::npos
                                     ? std::string{}
                                     : trim_ws(cond.substr(0, eq));
        std::string coef_text;
        std::string err;
        std::optional<ARef> ref;
        if (!name.empty()) {
            ref = parse_a_ref(name, coef_text, err);
        }
        if (!ref || !ref->absolute || !err.empty() || !coef_text.empty()) {
            throw Error(std::format(
                "rsolve: initial conditions look like a(0)=1 (got '{}')",
                cond));
        }
        const long long idx = ref->value;
        if (ics.contains(idx)) {
            throw Error(std::format("rsolve: duplicate condition for a({})",
                                    idx));
        }
        ics[idx] = simplify(parse_expression(trim_ws(cond.substr(eq + 1))));
    }
    for (long long k = 0; k < order; ++k) {
        if (!ics.contains(k)) {
            ics[k] = make_num(0);
            result.warnings.push_back(std::format("assuming a({}) = 0", k));
        }
    }
    if (static_cast<long long>(ics.size()) > order) {
        throw Error(std::format(
            "rsolve: an order-{} recurrence takes {} initial conditions",
            order, order));
    }

    std::vector<std::string> cnames;
    std::vector<Expr> general_terms;
    for (std::size_t i = 0; i < basis.size(); ++i) {
        cnames.push_back("__rc" + std::to_string(i));
        general_terms.push_back(make_mul({make_sym(cnames.back()), basis[i]}));
    }
    const Expr general = make_add(std::move(general_terms));
    std::vector<Equation> ic_eqs;
    for (const auto& [k, value] : ics) {
        ic_eqs.push_back(
            {simplify(make_add(
                 {simplify(substitute(general, "n", make_num(k))),
                  simplify(substitute(particular, "n", make_num(k)))})),
             value});
    }
    const SystemSolveResult sys = solve_system(ic_eqs, cnames);
    if (sys.status != SystemSolveResult::Status::Solved) {
        throw Error("rsolve: could not determine the constants from the "
                    "initial conditions");
    }
    result.warnings.insert(result.warnings.end(), sys.warnings.begin(),
                           sys.warnings.end());
    std::vector<Expr> out_terms;
    for (std::size_t i = 0; i < basis.size(); ++i) {
        const Expr val = simplify(sys.values.at(cnames[i]));
        if (!is_zero(val)) {
            out_terms.push_back(make_mul({val, basis[i]}));
        }
    }
    if (!is_zero(particular)) {
        out_terms.push_back(particular);
    }
    result.solution = out_terms.empty()
                          ? make_num(0)
                          : simplify(make_add(std::move(out_terms)));
    return result;
}

// ---------------------------------------------------------------------------
// Sequence recognition (discrete.hpp)
// ---------------------------------------------------------------------------

namespace {

/// Pretty rational coefficient for recurrence text: 1 -> "", -1 -> "-",
/// 3/2 -> "(3/2)*".
std::string coeff_text(const Rational& c) {
    if (c == Rational(1)) {
        return "";
    }
    if (c == Rational(-1)) {
        return "-";
    }
    const std::string t = c.to_string();
    return (c.den() == 1 ? t : "(" + t + ")") + "*";
}

/// Exact m x m Gaussian solve; nullopt when singular.
std::optional<std::vector<Rational>> solve_rational(
    std::vector<std::vector<Rational>> m, std::vector<Rational> y) {
    const std::size_t n = m.size();
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t p = col;
        while (p < n && m[p][col].is_zero()) ++p;
        if (p == n) {
            return std::nullopt;
        }
        std::swap(m[p], m[col]);
        std::swap(y[p], y[col]);
        for (std::size_t i = 0; i < n; ++i) {
            if (i == col || m[i][col].is_zero()) continue;
            const Rational f = m[i][col] / m[col][col];
            for (std::size_t j = col; j < n; ++j) {
                m[i][j] = m[i][j] - f * m[col][j];
            }
            y[i] = y[i] - f * y[col];
        }
    }
    std::vector<Rational> x(n, Rational(0));
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = y[i] / m[i][i];
    }
    return x;
}

} // namespace

SeqResult recognize_sequence(const std::vector<Rational>& a) {
    const std::size_t n = a.size();
    if (n < 4) {
        throw Error("seq needs at least 4 terms to see a pattern");
    }
    SeqResult out;
    const Expr nv = make_sym("n");

    // 1. Geometric: every term nonzero with a constant ratio.
    bool geometric = !a[0].is_zero();
    Rational ratio(1);
    if (geometric) {
        ratio = a[1] / a[0];
        for (std::size_t i = 1; geometric && i < n; ++i) {
            geometric = !a[i].is_zero() &&
                        (i + 1 == n || a[i + 1] / a[i] == ratio);
        }
    }
    if (geometric && ratio != Rational(1)) {
        out.kind = SeqResult::Kind::Geometric;
        out.description =
            std::format("geometric with ratio {}", ratio.to_string());
        out.formula = simplify(make_mul(
            {make_num(a[0]), make_pow(make_num(ratio), nv)}));
        Rational t = a.back();
        for (int i = 0; i < 3; ++i) {
            t = t * ratio;
            out.next.push_back(t);
        }
        return out;
    }

    // 2. Finite differences: vanishing at depth d (with at least two zero
    // entries as evidence) means a degree d-1 polynomial; the closed form
    // is Newton's forward formula over the top-left diagonal.
    std::vector<std::vector<Rational>> table{a};
    bool vanished = false;
    while (table.back().size() >= 3) {
        const std::vector<Rational>& prev = table.back();
        std::vector<Rational> row;
        for (std::size_t i = 0; i + 1 < prev.size(); ++i) {
            row.push_back(prev[i + 1] - prev[i]);
        }
        const bool zero = std::all_of(row.begin(), row.end(),
                                      [](const Rational& r) { return r.is_zero(); });
        table.push_back(std::move(row));
        if (zero) {
            vanished = true;
            break;
        }
    }
    if (vanished) {
        const int degree = static_cast<int>(table.size()) - 2;
        std::vector<Expr> terms;
        Rational fact(1);
        for (int j = 0; j <= degree; ++j) {
            if (j > 0) {
                fact = fact * Rational(j);
            }
            const Rational lead = table[static_cast<std::size_t>(j)][0];
            if (lead.is_zero()) {
                continue;
            }
            // Δ^j a(0) / j! * n (n-1) ... (n-j+1)
            std::vector<Expr> prod{make_num(lead / fact)};
            for (int i = 0; i < j; ++i) {
                prod.push_back(make_sub(nv, make_num(i)));
            }
            terms.push_back(make_mul(std::move(prod)));
        }
        out.formula = terms.empty()
                          ? make_num(0)
                          : simplify(expand(make_add(std::move(terms))));
        if (degree <= 0) {
            out.kind = SeqResult::Kind::Polynomial;
            out.description = "constant";
        } else if (degree == 1) {
            out.kind = SeqResult::Kind::Arithmetic;
            out.description = std::format("arithmetic with difference {}",
                                          table[1][0].to_string());
        } else {
            out.kind = SeqResult::Kind::Polynomial;
            out.description = std::format("polynomial of degree {}", degree);
        }
        for (int i = 0; i < 3; ++i) {
            const long long m = static_cast<long long>(n) + i;
            const double check = 0; (void)check;
            const Expr v = simplify(substitute(out.formula, "n", make_num(m)));
            out.next.push_back(v->number());
        }
        return out;
    }

    // 3. Linear recurrence of order 2..3: coefficients from an exact solve
    // over the first windows, verified against every remaining term.
    for (std::size_t m = 2; m <= 3; ++m) {
        if (n < 2 * m + 1) {
            continue;
        }
        std::vector<std::vector<Rational>> mat;
        std::vector<Rational> rhs;
        for (std::size_t i = 0; i < m; ++i) {
            std::vector<Rational> row;
            for (std::size_t j = 0; j < m; ++j) {
                row.push_back(a[i + j]);
            }
            mat.push_back(std::move(row));
            rhs.push_back(a[i + m]);
        }
        const auto c = solve_rational(std::move(mat), std::move(rhs));
        if (!c) {
            continue;
        }
        bool ok = true;
        for (std::size_t i = m; ok && i + m < n; ++i) {
            Rational acc(0);
            for (std::size_t j = 0; j < m; ++j) {
                acc = acc + (*c)[j] * a[i + j];
            }
            ok = acc == a[i + m];
        }
        const bool trivial = std::all_of(c->begin(), c->end(),
                                         [](const Rational& r) { return r.is_zero(); });
        if (!ok || trivial) {
            continue;
        }
        out.kind = SeqResult::Kind::Recurrence;
        std::string rhs_text;
        for (std::size_t j = m; j-- > 0;) {
            if ((*c)[j].is_zero()) {
                continue;
            }
            std::string term =
                coeff_text((*c)[j].is_negative() && !rhs_text.empty()
                               ? -(*c)[j]
                               : (*c)[j]) +
                (j == 0 ? "a(n)" : std::format("a(n+{})", j));
            if (rhs_text.empty()) {
                rhs_text = term;
            } else {
                rhs_text += ((*c)[j].is_negative() ? " - " : " + ") + term;
            }
        }
        out.recurrence = std::format("a(n+{}) = {}", m, rhs_text);
        out.description =
            std::format("linear recurrence of order {}", m);
        if (m == 2 && (*c)[0] == Rational(1) && (*c)[1] == Rational(1)) {
            out.description += a[0].is_zero() && a[1] == Rational(1)
                                   ? " (Fibonacci)"
                                   : " (Fibonacci-type)";
        }
        std::vector<Rational> ext(a);
        for (int i = 0; i < 3; ++i) {
            Rational acc(0);
            for (std::size_t j = 0; j < m; ++j) {
                acc = acc + (*c)[j] * ext[ext.size() - m + j];
            }
            ext.push_back(acc);
            out.next.push_back(acc);
        }
        // Closed form through rsolve where its machinery reaches.
        try {
            std::vector<std::string> conds;
            for (std::size_t i = 0; i < m; ++i) {
                conds.push_back(
                    std::format("a({}) = {}", i, a[i].to_string()));
            }
            const RsolveResult rr = rsolve(out.recurrence, conds);
            out.formula = rr.solution;
            out.warnings.insert(out.warnings.end(), rr.warnings.begin(),
                                rr.warnings.end());
        } catch (const Error&) {
            out.warnings.push_back(
                "no closed form found for the recurrence (complex or "
                "unsupported characteristic roots)");
        }
        return out;
    }

    out.kind = SeqResult::Kind::Unknown;
    out.description =
        "no pattern found (tried geometric ratios, finite differences, and "
        "linear recurrences up to order 3); more terms may help";
    return out;
}

} // namespace mathsolver
