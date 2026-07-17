#include "mathsolver/solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {
namespace {

using Status = SolveResult::Status;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kRangeEps = 1e-9;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

const Rational* as_number(const Expr& e) {
    return e->kind() == Kind::Number ? &e->number() : nullptr;
}

bool is_num_zero(const Expr& e) {
    const Rational* r = as_number(e);
    return r != nullptr && r->is_zero();
}

// Range-check for inverse-function isolation. When `c` is an exact rational
// literal, decide with an EXACT rational comparison (`rat_pred`) so that a value
// that is out of range by less than kRangeEps is still rejected. Only genuinely
// inexact constant sides (those carrying pi/e) fall back to the tolerance-based
// float comparison (`float_pred`), where the slack absorbs representation error.
template <typename RatPred, typename FloatPred>
bool out_of_range(const Expr& c, const std::optional<double>& cv, RatPred rat_pred,
                  FloatPred float_pred) {
    if (const Rational* r = as_number(c)) {
        return rat_pred(*r);
    }
    return cv && float_pred(*cv);
}

std::string pretty(const Expr& e) {
    return to_string(e, PrintStyle::Plain);
}

int count_sym(const Expr& e, std::string_view name) {
    if (e->kind() == Kind::Symbol) {
        return e->symbol_name() == name ? 1 : 0;
    }
    int count = 0;
    for (const Expr& a : e->args()) {
        count += count_sym(a, name);
    }
    return count;
}

/// evaluate() guarded against every solver-relevant exception.
std::optional<double> try_eval(const Expr& e, const Bindings& b = {}) {
    try {
        return evaluate(e, b);
    } catch (const Error&) {
        return std::nullopt;
    }
}

/// Numeric value of a symbol-free expression (constants allowed);
/// nullopt when symbols remain or evaluation fails.
std::optional<double> numeric_value(const Expr& e) {
    if (!free_symbols(e).empty()) {
        return std::nullopt;
    }
    return try_eval(e);
}

bool checked_mul_ll(long long a, long long b, long long& out) {
    return !__builtin_mul_overflow(a, b, &out);
}

// ---------------------------------------------------------------------------
// sqrt of a positive rational, with square factors extracted:
// sqrt(8) -> 2*sqrt(2), sqrt(16) -> 4, sqrt(1/2) -> (1/2)*sqrt(2).
// ---------------------------------------------------------------------------

/// n = s^2 * r with s maximal over small primes; r*s^2 == n always holds.
std::pair<long long, long long> square_free_split(long long n) {
    long long s = 1;
    long long r = 1;
    for (long long i = 2; i <= 65536 && i * i <= n; ++i) {
        while (n % (i * i) == 0) {
            s *= i;
            n /= i * i;
        }
        if (n % i == 0) {
            r *= i;
            n /= i;
        }
    }
    const auto root = static_cast<long long>(std::llround(std::sqrt(static_cast<double>(n))));
    for (long long c = std::max(1LL, root - 1); c <= root + 1; ++c) {
        long long sq = 0;
        if (checked_mul_ll(c, c, sq) && sq == n) {
            s *= c;
            n = 1;
            break;
        }
    }
    return {s, r * n};
}

Expr sqrt_of_rational(const Rational& d) {
    const Expr half = make_num(Rational(1, 2));
    long long nm = 0;
    if (!checked_mul_ll(d.num(), d.den(), nm)) {
        return make_pow(make_num(d), half);
    }
    const auto [s, r] = square_free_split(nm);
    const Rational coeff(s, d.den());
    if (r == 1) {
        return make_num(coeff);
    }
    return make_mul({make_num(coeff), make_pow(make_num(r), half)});
}

/// sqrt of a simplified discriminant. Numbers get their square part
/// extracted; a Mul with a positive Number coefficient factors it out
/// (sqrt(c*u) == sqrt(c)*sqrt(u) for c > 0), so sqrt(4*pi) -> 2*sqrt(pi).
Expr sqrt_expr(const Expr& d) {
    if (const Rational* r = as_number(d)) {
        return r->is_negative() ? make_sqrt(d) : sqrt_of_rational(*r);
    }
    if (d->kind() == Kind::Mul) {
        const Rational* c = as_number(d->arg(0));
        if (c != nullptr && !c->is_negative() && !c->is_zero()) {
            const std::vector<Expr> rest(d->args().begin() + 1, d->args().end());
            return make_mul({sqrt_of_rational(*c), make_sqrt(make_mul(rest))});
        }
    }
    return make_sqrt(d);
}

// ---------------------------------------------------------------------------
// Exact-candidate verification (DESIGN.md §9 point 5)
// ---------------------------------------------------------------------------

enum class Verdict { Confirmed, Rejected, Conditional };

Verdict verify_candidate(const Equation& eq, const Expr& f, std::string_view symbol,
                         const Expr& value) {
    Expr residual;
    try {
        residual = substitute(f, symbol, value);
    } catch (const Error&) {
        return Verdict::Conditional;
    }

    std::optional<Expr> lhs_sub;
    std::optional<Expr> rhs_sub;
    try {
        lhs_sub = substitute(eq.lhs, symbol, value);
        rhs_sub = substitute(eq.rhs, symbol, value);
    } catch (const Error&) {
        lhs_sub.reset();
        rhs_sub.reset();
    }

    const std::set<std::string> extras = free_symbols(residual);
    std::vector<Bindings> samples;
    if (extras.empty()) {
        samples.emplace_back();
    } else {
        for (const double test_value : {1.7320508, 0.3141593}) {
            Bindings b;
            for (const std::string& name : extras) {
                b[name] = test_value;
            }
            samples.push_back(std::move(b));
        }
    }

    int evaluated = 0;
    int nonzero = 0;
    for (const Bindings& b : samples) {
        double scale = 1.0;
        if (lhs_sub && rhs_sub) {
            const auto lv = try_eval(*lhs_sub, b);
            const auto rv = try_eval(*rhs_sub, b);
            if (lv && rv) {
                scale = std::max({1.0, std::abs(*lv), std::abs(*rv)});
            }
        }
        const auto rv = try_eval(residual, b);
        if (!rv) {
            continue;
        }
        ++evaluated;
        if (std::abs(*rv) > 1e-6 * scale) {
            ++nonzero;
        }
    }

    if (evaluated == 0) {
        return Verdict::Conditional;
    }
    if (nonzero == evaluated) {
        return Verdict::Rejected;
    }
    if (nonzero == 0 && evaluated == static_cast<int>(samples.size())) {
        return Verdict::Confirmed;
    }
    return Verdict::Conditional;
}

/// Dedupe, verify and move exact candidates into `res.solutions`.
/// Returns true when at least one candidate survived.
bool finalize_exact(SolveResult& res, std::vector<Solution> candidates, const Equation& eq,
                    const Expr& f, std::string_view symbol) {
    std::vector<Solution> unique;
    for (Solution& s : candidates) {
        const bool duplicate =
            std::any_of(unique.begin(), unique.end(), [&](const Solution& u) {
                return structurally_equal(u.value, s.value);
            });
        if (!duplicate) {
            unique.push_back(std::move(s));
        }
    }
    for (Solution& s : unique) {
        switch (verify_candidate(eq, f, symbol, s.value)) {
        case Verdict::Confirmed:
            res.solutions.push_back(std::move(s));
            break;
        case Verdict::Conditional:
            res.warnings.push_back(std::format("{} = {} may be valid only under domain conditions",
                                               symbol, pretty(s.value)));
            res.solutions.push_back(std::move(s));
            break;
        case Verdict::Rejected:
            res.warnings.push_back(std::format(
                "candidate {} = {} failed verification and was dropped", symbol, pretty(s.value)));
            break;
        }
    }
    return !res.solutions.empty();
}

/// Sort solutions ascending by numeric value when every value evaluates
/// cleanly with no free symbols; otherwise keep the construction order.
void sort_solutions(std::vector<Solution>& solutions) {
    std::vector<double> keys(solutions.size());
    for (std::size_t i = 0; i < solutions.size(); ++i) {
        const auto v = numeric_value(solutions[i].value);
        if (!v) {
            return;
        }
        keys[i] = *v;
    }
    std::vector<std::size_t> order(solutions.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return keys[a] < keys[b]; });
    std::vector<Solution> sorted;
    sorted.reserve(solutions.size());
    for (const std::size_t i : order) {
        sorted.push_back(std::move(solutions[i]));
    }
    solutions = std::move(sorted);
}

// ---------------------------------------------------------------------------
// Isolation path (DESIGN.md §9 point 3)
// ---------------------------------------------------------------------------

enum class IsoStatus { Ok, NoReal, Fail };

struct IsoState {
    std::string_view symbol;
    std::vector<Solution> solutions;
    std::vector<std::string> warnings;
};

IsoStatus isolate(const Expr& side, const Expr& c, IsoState& st, const std::string& note = "");

IsoStatus combine_branches(std::initializer_list<IsoStatus> statuses) {
    bool any_ok = false;
    for (const IsoStatus s : statuses) {
        if (s == IsoStatus::Fail) {
            return IsoStatus::Fail;
        }
        if (s == IsoStatus::Ok) {
            any_ok = true;
        }
    }
    return any_ok ? IsoStatus::Ok : IsoStatus::NoReal;
}

IsoStatus isolate_add(const Expr& side, const Expr& c, IsoState& st) {
    std::vector<Expr> free_terms;
    Expr x_term;
    for (const Expr& t : side->args()) {
        if (contains_symbol(t, st.symbol)) {
            if (x_term) {
                return IsoStatus::Fail;
            }
            x_term = t;
        } else {
            free_terms.push_back(t);
        }
    }
    if (!x_term) {
        return IsoStatus::Fail;
    }
    const Expr moved = simplify(make_sub(c, make_add(std::move(free_terms))));
    return isolate(x_term, moved, st);
}

IsoStatus isolate_mul(const Expr& side, const Expr& c, IsoState& st) {
    std::vector<Expr> free_factors;
    Expr x_factor;
    for (const Expr& t : side->args()) {
        if (contains_symbol(t, st.symbol)) {
            if (x_factor) {
                return IsoStatus::Fail;
            }
            x_factor = t;
        } else {
            free_factors.push_back(t);
        }
    }
    if (!x_factor) {
        return IsoStatus::Fail;
    }
    const Expr divisor = make_mul(std::move(free_factors));
    if (!free_symbols(divisor).empty()) {
        st.warnings.push_back(
            std::format("solution valid only when {} != 0", pretty(divisor)));
    }
    const Expr moved = simplify(make_div(c, divisor));
    return isolate(x_factor, moved, st);
}

/// u^(p/q) = c with p/q a reduced rational Number (DESIGN.md §9 parity table).
IsoStatus isolate_rational_pow(const Expr& base, const Rational& expo, const Expr& c,
                               IsoState& st) {
    const long long p = expo.num();
    const long long q = expo.den();
    const bool p_even = (p % 2) == 0;
    const bool q_even = (q % 2) == 0;
    const Rational inv(q, p); // reduced; sign normalizes into the numerator
    const Expr inv_expr = make_num(inv);

    const auto cv = numeric_value(c);
    const bool c_known = cv.has_value();
    const bool c_zero = is_num_zero(c);
    // Exact rational literals decide the c >= 0 requirement without float slack;
    // constant-bearing sides fall back to the numeric value.
    const Rational* cr = as_number(c);
    const bool c_negative = cr ? cr->is_negative() : (c_known && *cv < 0.0);

    if (c_zero) {
        // u^negative == 0 is unsatisfiable; u^positive == 0 -> u == 0.
        if (p < 0) {
            return IsoStatus::NoReal;
        }
        return isolate(base, make_num(0), st);
    }

    if (p_even) { // q odd: u = +-c^(q/p), needs c >= 0
        if (c_negative) {
            return IsoStatus::NoReal;
        }
        if (!c_known) {
            st.warnings.push_back(
                std::format("solutions valid only when {} >= 0", pretty(c)));
        }
        const Expr root = simplify(make_pow(c, inv_expr));
        const IsoStatus plus = isolate(base, root, st);
        const IsoStatus minus = isolate(base, simplify(make_neg(root)), st);
        return combine_branches({plus, minus});
    }
    if (q_even) { // p odd: even root has nonnegative range, single solution
        if (c_negative) {
            return IsoStatus::NoReal;
        }
        if (!c_known) {
            st.warnings.push_back(
                std::format("solutions valid only when {} >= 0", pretty(c)));
        }
        return isolate(base, simplify(make_pow(c, inv_expr)), st);
    }
    // p and q both odd: single solution; sign-extract negative numeric c.
    if (c_negative) {
        const Expr root = simplify(make_neg(make_pow(simplify(make_neg(c)), inv_expr)));
        return isolate(base, root, st);
    }
    if (!c_known && p < 0) {
        st.warnings.push_back(std::format("solution valid only when {} != 0", pretty(c)));
    }
    return isolate(base, simplify(make_pow(c, inv_expr)), st);
}

/// a^u = c with the symbol in the exponent: u = ln(c)/ln(a), numeric a > 0.
IsoStatus isolate_exponent(const Expr& base, const Expr& expo, const Expr& c, IsoState& st) {
    const auto av = numeric_value(base);
    if (!av || *av <= 0.0) {
        return IsoStatus::Fail;
    }
    if (free_symbols(c).empty()) {
        const auto cv = try_eval(c);
        if (!cv) {
            return IsoStatus::Fail;
        }
        if (*cv <= 0.0) {
            return IsoStatus::NoReal;
        }
    } else {
        st.warnings.push_back(std::format("solutions valid only when {} > 0", pretty(c)));
    }
    const Expr moved =
        simplify(make_div(make_fn(FunctionId::Ln, c), make_fn(FunctionId::Ln, base)));
    return isolate(expo, moved, st);
}

IsoStatus isolate_pow(const Expr& side, const Expr& c, IsoState& st) {
    const Expr& base = side->arg(0);
    const Expr& expo = side->arg(1);
    const bool x_in_base = contains_symbol(base, st.symbol);
    const bool x_in_expo = contains_symbol(expo, st.symbol);
    if (x_in_base && x_in_expo) {
        return IsoStatus::Fail;
    }
    if (x_in_base) {
        const Rational* e = as_number(expo);
        if (!e) {
            return IsoStatus::Fail;
        }
        return isolate_rational_pow(base, *e, c, st);
    }
    return isolate_exponent(base, expo, c, st);
}

IsoStatus isolate_function(const Expr& side, const Expr& c, IsoState& st) {
    const FunctionId id = side->function();
    const Expr& u = side->arg(0);
    std::optional<double> cv;
    if (free_symbols(c).empty()) {
        cv = try_eval(c);
    }
    const bool arg_is_symbol =
        u->kind() == Kind::Symbol && u->symbol_name() == st.symbol;

    const auto range_warning = [&](std::string_view condition) {
        st.warnings.push_back(
            std::format("solutions valid only when {} {}", pretty(c), condition));
    };
    const auto periodic_warning = [&](std::string_view period) {
        st.warnings.push_back(std::format(
            "only the principal solution of {}({}) = {} is reported; the general family "
            "repeats with period {} in the argument",
            function_name(id), pretty(u), pretty(c), period));
    };

    switch (id) {
    case FunctionId::Sin:
    case FunctionId::Cos: {
        if (out_of_range(
                c, cv, [](const Rational& r) { return r > Rational(1) || r < Rational(-1); },
                [](double v) { return std::abs(v) > 1.0 + kRangeEps; })) {
            return IsoStatus::NoReal;
        }
        if (!cv) {
            range_warning("is in [-1, 1]");
        }
        const bool is_sin = id == FunctionId::Sin;
        const Expr root =
            simplify(make_fn(is_sin ? FunctionId::Asin : FunctionId::Acos, c));
        if (arg_is_symbol) {
            const std::string note =
                is_sin ? std::format("principal solution; general: {0} = {1} + 2*pi*n or "
                                     "{0} = pi - {1} + 2*pi*n",
                                     st.symbol, pretty(root))
                       : std::format("principal solution; general: {0} = ±{1} + 2*pi*n",
                                     st.symbol, pretty(root));
            return isolate(u, root, st, note);
        }
        periodic_warning("2*pi");
        return isolate(u, root, st);
    }
    case FunctionId::Tan: {
        const Expr root = simplify(make_fn(FunctionId::Atan, c));
        if (arg_is_symbol) {
            const std::string note = std::format(
                "principal solution; general: {0} = {1} + pi*n", st.symbol, pretty(root));
            return isolate(u, root, st, note);
        }
        periodic_warning("pi");
        return isolate(u, root, st);
    }
    case FunctionId::Asin:
        if (cv && (*cv < -kPi / 2 - kRangeEps || *cv > kPi / 2 + kRangeEps)) {
            return IsoStatus::NoReal;
        }
        if (!cv) {
            range_warning("is in [-pi/2, pi/2]");
        }
        return isolate(u, simplify(make_fn(FunctionId::Sin, c)), st);
    case FunctionId::Acos:
        // Lower bound 0 is an exact rational: reject an exactly-negative literal
        // with no slack. The upper bound pi is irrational, so it keeps kRangeEps.
        if (out_of_range(c, cv, [](const Rational& r) { return r.is_negative(); },
                         [](double v) { return v < -kRangeEps; })
            || (cv && *cv > kPi + kRangeEps)) {
            return IsoStatus::NoReal;
        }
        if (!cv) {
            range_warning("is in [0, pi]");
        }
        return isolate(u, simplify(make_fn(FunctionId::Cos, c)), st);
    case FunctionId::Atan:
        if (cv && std::abs(*cv) >= kPi / 2 - kRangeEps) {
            return IsoStatus::NoReal;
        }
        if (!cv) {
            range_warning("is in (-pi/2, pi/2)");
        }
        return isolate(u, simplify(make_fn(FunctionId::Tan, c)), st);
    case FunctionId::Ln:
        return isolate(u, simplify(make_exp(c)), st);
    case FunctionId::Abs: {
        if (out_of_range(c, cv, [](const Rational& r) { return r.is_negative(); },
                         [](double v) { return v < -kRangeEps; })) {
            return IsoStatus::NoReal;
        }
        if (!cv) {
            range_warning(">= 0");
        }
        if (is_num_zero(c)) {
            return isolate(u, c, st);
        }
        const IsoStatus plus = isolate(u, simplify(c), st);
        const IsoStatus minus = isolate(u, simplify(make_neg(c)), st);
        return combine_branches({plus, minus});
    }
    case FunctionId::Sinh: {
        // sinh^-1(c) = ln(c + sqrt(c^2 + 1))
        const Expr root = simplify(make_fn(
            FunctionId::Ln,
            make_add({c, make_sqrt(make_add({make_pow(c, make_num(2)), make_num(1)}))})));
        return isolate(u, root, st);
    }
    case FunctionId::Cosh: {
        if (out_of_range(c, cv, [](const Rational& r) { return r < Rational(1); },
                         [](double v) { return v < 1.0 - kRangeEps; })) {
            return IsoStatus::NoReal;
        }
        if (!cv) {
            range_warning(">= 1");
        }
        const Expr root = simplify(make_fn(
            FunctionId::Ln,
            make_add({c, make_sqrt(make_sub(make_pow(c, make_num(2)), make_num(1)))})));
        const IsoStatus plus = isolate(u, root, st);
        const IsoStatus minus = isolate(u, simplify(make_neg(root)), st);
        return combine_branches({plus, minus});
    }
    case FunctionId::Tanh: {
        if (out_of_range(
                c, cv, [](const Rational& r) { return r >= Rational(1) || r <= Rational(-1); },
                [](double v) { return std::abs(v) >= 1.0 - kRangeEps; })) {
            return IsoStatus::NoReal;
        }
        if (!cv) {
            range_warning("is in (-1, 1)");
        }
        // tanh^-1(c) = (1/2)*ln((1+c)/(1-c))
        const Expr root = simplify(make_mul(
            {make_num(Rational(1, 2)),
             make_fn(FunctionId::Ln, make_div(make_add({make_num(1), c}),
                                              make_sub(make_num(1), c)))}));
        return isolate(u, root, st);
    }
    }
    return IsoStatus::Fail;
}

IsoStatus isolate(const Expr& side, const Expr& c, IsoState& st, const std::string& note) {
    if (side->kind() == Kind::Symbol && side->symbol_name() == st.symbol) {
        st.solutions.push_back(Solution{simplify(c), true, note});
        return IsoStatus::Ok;
    }
    if (!contains_symbol(side, st.symbol)) {
        return IsoStatus::Fail;
    }
    try {
        switch (side->kind()) {
        case Kind::Add:
            return isolate_add(side, c, st);
        case Kind::Mul:
            return isolate_mul(side, c, st);
        case Kind::Pow:
            return isolate_pow(side, c, st);
        case Kind::Function:
            return isolate_function(side, c, st);
        case Kind::Number:
        case Kind::Constant:
        case Kind::Symbol:
            break;
        }
    } catch (const Error&) {
        return IsoStatus::Fail;
    }
    return IsoStatus::Fail;
}

// ---------------------------------------------------------------------------
// Polynomial path (DESIGN.md §9 point 2)
// ---------------------------------------------------------------------------

struct PolyResult {
    enum class Kind { Roots, NoReal, Fail };
    Kind kind = Kind::Fail;
    std::vector<Expr> roots;             ///< exact roots (unverified candidates)
    std::vector<Rational> remainder;     ///< degree >= 3 leftover for the numeric fallback
    std::string method;
    std::vector<std::string> warnings;
};

PolyResult solve_linear(const Expr& c0, const Expr& c1) {
    PolyResult pr;
    pr.method = "linear";
    try {
        // expand() so -(y - 3) style results distribute to 3 - y.
        pr.roots.push_back(expand(make_div(make_neg(c0), c1)));
        if (!free_symbols(c1).empty()) {
            pr.warnings.push_back(std::format("solution valid only when {} != 0", pretty(c1)));
        }
        pr.kind = PolyResult::Kind::Roots;
    } catch (const Error&) {
        pr.kind = PolyResult::Kind::Fail;
    }
    return pr;
}

PolyResult solve_quadratic(const Expr& a, const Expr& b, const Expr& c) {
    PolyResult pr;
    pr.method = "quadratic formula";
    try {
        const Expr disc =
            simplify(make_sub(make_pow(b, make_num(2)), make_mul({make_num(4), a, c})));
        const Expr neg_b = make_neg(b);
        const Expr two_a = make_mul({make_num(2), a});

        std::optional<Expr> sqrt_disc;
        if (const Rational* d = as_number(disc)) {
            if (d->is_negative()) {
                pr.kind = PolyResult::Kind::NoReal;
                return pr;
            }
            if (d->is_zero()) {
                pr.roots.push_back(simplify(make_div(neg_b, two_a)));
            } else {
                sqrt_disc = sqrt_of_rational(*d);
            }
        } else if (free_symbols(disc).empty()) {
            const auto dv = try_eval(disc);
            if (dv && *dv < -1e-12) {
                pr.kind = PolyResult::Kind::NoReal;
                return pr;
            }
            if (!dv) {
                pr.warnings.push_back(
                    std::format("solutions are real only when {} >= 0", pretty(disc)));
            }
            sqrt_disc = sqrt_expr(disc);
        } else {
            pr.warnings.push_back(
                std::format("solutions are real only when {} >= 0", pretty(disc)));
            sqrt_disc = sqrt_expr(disc);
        }

        if (sqrt_disc) {
            pr.roots.push_back(simplify(make_div(make_sub(neg_b, *sqrt_disc), two_a)));
            pr.roots.push_back(simplify(make_div(make_add({neg_b, *sqrt_disc}), two_a)));
        }
        if (!free_symbols(a).empty()) {
            pr.warnings.push_back(
                std::format("roots are valid only when {} != 0", pretty(a)));
        }
        pr.kind = PolyResult::Kind::Roots;
    } catch (const Error&) {
        pr = PolyResult{};
    }
    return pr;
}

std::optional<std::vector<Rational>> to_rational_coeffs(const std::vector<Expr>& coeffs) {
    std::vector<Rational> out;
    out.reserve(coeffs.size());
    for (const Expr& c : coeffs) {
        const Rational* r = as_number(c);
        if (!r) {
            return std::nullopt;
        }
        out.push_back(*r);
    }
    return out;
}

/// Exact Horner evaluation; propagates OverflowError to the caller.
Rational horner(const std::vector<Rational>& coeffs, const Rational& x) {
    Rational acc(0);
    for (std::size_t i = coeffs.size(); i-- > 0;) {
        acc = acc * x + coeffs[i];
    }
    return acc;
}

/// Divisors of n (> 0); complete when n <= ~4e12, best-effort beyond.
std::vector<long long> divisors_of(long long n) {
    std::vector<long long> divs;
    for (long long i = 1; i <= 2000000 && i * i <= n; ++i) {
        if (n % i == 0) {
            divs.push_back(i);
            if (i != n / i) {
                divs.push_back(n / i);
            }
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
        if (!checked_mul_ll(lcm_den / g, c.den(), scaled)) {
            return std::nullopt;
        }
        lcm_den = scaled;
    }
    std::vector<long long> ints(coeffs.size());
    for (std::size_t i = 0; i < coeffs.size(); ++i) {
        if (!checked_mul_ll(coeffs[i].num(), lcm_den / coeffs[i].den(), ints[i])) {
            return std::nullopt;
        }
    }
    long long all_gcd = 0;
    for (const long long v : ints) {
        all_gcd = std::gcd(all_gcd, v);
    }
    if (all_gcd > 1) {
        for (long long& v : ints) {
            v /= all_gcd;
        }
    }

    const long long a0 = std::abs(ints.front());
    const long long an = std::abs(ints.back());
    if (a0 == 0 || an == 0) {
        return std::nullopt; // zero roots are peeled before this is called
    }
    for (const long long p : divisors_of(a0)) {
        for (const long long q : divisors_of(an)) {
            for (const int sign : {1, -1}) {
                const Rational candidate(sign * p, q);
                try {
                    if (horner(coeffs, candidate).is_zero()) {
                        return candidate;
                    }
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
    for (std::size_t i = n - 1; i >= 1; --i) {
        q[i - 1] = coeffs[i] + r * q[i];
    }
    return q;
}

PolyResult solve_poly(const std::vector<Expr>& coeffs, std::string_view symbol, int depth);

/// Biquadratic-style detection: the polynomial only uses powers that are
/// multiples of k >= 2. Substitute y = x^k, solve, back-substitute.
PolyResult try_substitution(const std::vector<Expr>& coeffs, long long k,
                            std::string_view symbol, int depth) {
    std::vector<Expr> sub_coeffs;
    for (std::size_t i = 0; i < coeffs.size(); i += static_cast<std::size_t>(k)) {
        sub_coeffs.push_back(coeffs[i]);
    }
    PolyResult sub = solve_poly(sub_coeffs, symbol, depth + 1);
    if (sub.kind == PolyResult::Kind::NoReal) {
        sub.method = std::format("substitution (x^{}) + {}", k, sub.method);
        return sub;
    }
    if (sub.kind != PolyResult::Kind::Roots || !sub.remainder.empty()) {
        return PolyResult{}; // Fail: let the caller try other strategies
    }

    PolyResult out;
    out.method = std::format("substitution (x^{}) + {}", k, sub.method);
    out.warnings = std::move(sub.warnings);
    IsoState st{symbol, {}, {}};
    bool any_ok = false;
    for (const Expr& y_root : sub.roots) {
        const Expr xk = make_pow(make_sym(std::string(symbol)), make_num(k));
        const IsoStatus s = isolate(xk, y_root, st);
        if (s == IsoStatus::Fail) {
            return PolyResult{};
        }
        any_ok = any_ok || s == IsoStatus::Ok;
    }
    out.warnings.insert(out.warnings.end(), st.warnings.begin(), st.warnings.end());
    if (!any_ok) {
        out.kind = PolyResult::Kind::NoReal;
        return out;
    }
    for (Solution& s : st.solutions) {
        out.roots.push_back(std::move(s.value));
    }
    out.kind = PolyResult::Kind::Roots;
    return out;
}

PolyResult solve_poly(const std::vector<Expr>& coeffs, std::string_view symbol, int depth) {
    const std::size_t degree = coeffs.size() - 1;
    if (depth > 8 || degree < 1) {
        return PolyResult{};
    }
    if (degree == 1) {
        return solve_linear(coeffs[0], coeffs[1]);
    }
    if (degree == 2) {
        return solve_quadratic(coeffs[2], coeffs[1], coeffs[0]);
    }

    // Poly in x^k: gcd of the degrees carrying a (possibly symbolic) nonzero
    // coefficient. Structural Number-zero coefficients do not contribute.
    long long k = 0;
    for (std::size_t i = 1; i < coeffs.size(); ++i) {
        if (!is_num_zero(coeffs[i])) {
            k = std::gcd(k, static_cast<long long>(i));
        }
    }
    if (k >= 2) {
        PolyResult sub = try_substitution(coeffs, k, symbol, depth);
        if (sub.kind != PolyResult::Kind::Fail) {
            return sub;
        }
    }

    // Rational-root peeling needs all-Number coefficients.
    auto rats_opt = to_rational_coeffs(coeffs);
    if (!rats_opt) {
        return PolyResult{};
    }
    std::vector<Rational> rats = std::move(*rats_opt);

    PolyResult pr;
    try {
        while (rats.size() > 1 && rats.front().is_zero()) {
            pr.roots.push_back(make_num(0));
            rats.erase(rats.begin());
        }
        while (rats.size() - 1 >= 3) {
            const auto root = find_rational_root(rats);
            if (!root) {
                break;
            }
            pr.roots.push_back(make_num(*root));
            rats = deflate(rats, *root);
        }
    } catch (const Error&) {
        return PolyResult{};
    }

    const std::size_t rem_degree = rats.size() - 1;
    bool used_quadratic = false;
    if (rem_degree == 2) {
        const PolyResult tail = solve_quadratic(make_num(rats[2]), make_num(rats[1]),
                                                make_num(rats[0]));
        if (tail.kind == PolyResult::Kind::Fail) {
            return PolyResult{};
        }
        // A no-real-root quadratic remainder simply contributes nothing.
        pr.roots.insert(pr.roots.end(), tail.roots.begin(), tail.roots.end());
        used_quadratic = true;
    } else if (rem_degree == 1) {
        const PolyResult tail = solve_linear(make_num(rats[0]), make_num(rats[1]));
        if (tail.kind == PolyResult::Kind::Fail) {
            return PolyResult{};
        }
        pr.roots.insert(pr.roots.end(), tail.roots.begin(), tail.roots.end());
    } else if (rem_degree >= 3) {
        if (pr.roots.empty()) {
            return PolyResult{}; // nothing peeled: let the generic fallback run on f
        }
        pr.remainder = std::move(rats);
    }

    if (pr.roots.empty() && pr.remainder.empty()) {
        pr.kind = PolyResult::Kind::NoReal; // every factor was proven rootless
        pr.method = "rational roots + quadratic";
        return pr;
    }
    pr.method = pr.remainder.empty()
                    ? (used_quadratic ? "rational roots + quadratic" : "rational roots")
                    : "rational roots + numeric";
    pr.kind = PolyResult::Kind::Roots;
    return pr;
}

// ---------------------------------------------------------------------------
// Numeric fallback (DESIGN.md §9 point 4)
// ---------------------------------------------------------------------------

Rational rational_from_double(double x) {
    if (!std::isfinite(x)) {
        return Rational(0);
    }
    const double ax = std::abs(x);
    int decimals = 15;
    if (ax >= 1.0) {
        decimals = std::clamp(15 - static_cast<int>(std::floor(std::log10(ax))) - 1, 0, 15);
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

SolveResult numeric_core(const Expr& f, const Equation& eq, std::string_view symbol,
                         const NumericOptions& opts) {
    SolveResult res;
    res.method = "numeric (Newton/bisection)";

    std::set<std::string> extras = free_symbols(f);
    extras.erase(std::string(symbol));
    if (!extras.empty()) {
        std::string names;
        for (const std::string& name : extras) {
            names += names.empty() ? name : ", " + name;
        }
        res.status = Status::Unsolved;
        res.warnings.push_back(std::format(
            "cannot search numerically: other free symbols remain: {}", names));
        return res;
    }

    double lo = opts.lo;
    double hi = opts.hi;
    if (lo > hi) {
        std::swap(lo, hi);
    }
    const int n = std::max(opts.scan_points, 3);
    const double step = (hi - lo) / (n - 1);
    const std::string symbol_name(symbol);

    Expr df;
    try {
        df = differentiate(f, symbol);
    } catch (const Error&) {
        df = nullptr;
    }
    const auto feval = [&](double t) -> std::optional<double> {
        return try_eval(f, Bindings{{symbol_name, t}});
    };
    const auto dfeval = [&](double t) -> std::optional<double> {
        if (!df) {
            return std::nullopt;
        }
        return try_eval(df, Bindings{{symbol_name, t}});
    };

    std::vector<double> ts(static_cast<std::size_t>(n));
    std::vector<std::optional<double>> vs(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        ts[i] = (i == n - 1) ? hi : lo + i * step;
        vs[i] = feval(ts[i]);
    }

    // Identity guard: a non-polynomial identity that simplify could not fold
    // (e.g. sin(2x) - 2*sin(x)*cos(x)) evaluates to ~0 at every point. Without
    // this check the harvesting below would emit one "root" per grid point.
    // When |f| is below the root tolerance across nearly the whole evaluable
    // grid, report it as an identity (DESIGN step 1: f == 0 -> AllReals) rather
    // than a dense set of spurious numeric roots.
    {
        int evaluable = 0;
        int near_zero = 0;
        for (int i = 0; i < n; ++i) {
            if (!vs[i]) {
                continue;
            }
            ++evaluable;
            if (std::abs(*vs[i]) < 1e-9 * std::max(1.0, std::abs(ts[i]))) {
                ++near_zero;
            }
        }
        if (evaluable > 0 && near_zero > 0.9 * evaluable) {
            res.status = Status::AllReals;
            res.method = "identity";
            res.warnings.push_back(
                "f is numerically zero across the search interval; the equation "
                "appears to be an identity wherever it is defined");
            return res;
        }
    }

    // Provenance matters: a root found without a sign change is numerically
    // indistinguishable from a near-miss tangency, so it carries a note.
    struct NumericRoot {
        double x;
        bool tangency;
    };
    std::vector<NumericRoot> roots;

    const auto refine = [&](double a, double b, double fa) -> double {
        double x = 0.5 * (a + b);
        for (int iter = 0; iter < std::max(opts.max_iter, 1); ++iter) {
            const auto fx = feval(x);
            if (!fx) {
                x = 0.5 * (a + x); // domain hiccup inside the bracket: shrink
                continue;
            }
            if (*fx == 0.0) {
                return x;
            }
            if ((*fx > 0.0) == (fa > 0.0)) {
                a = x;
                fa = *fx;
            } else {
                b = x;
            }
            if (b - a < opts.tol * std::max(1.0, std::abs(x))) {
                break;
            }
            double next = std::numeric_limits<double>::quiet_NaN();
            const auto d = dfeval(x);
            if (d && *d != 0.0) {
                next = x - *fx / *d;
            }
            x = (std::isfinite(next) && next > a && next < b) ? next : 0.5 * (a + b);
        }
        return 0.5 * (a + b);
    };

    // Exact grid hits and sign-change brackets.
    for (int i = 0; i < n; ++i) {
        if (!vs[i] || *vs[i] != 0.0) {
            continue;
        }
        // Classify by the sign of the nearest nonzero neighbours: a zero the
        // curve crosses is a genuine sign-change root, while one it only
        // touches (both sides share a sign) is a tangency/even-multiplicity
        // root and must carry the note (DESIGN §9 step 4) — the same as when
        // the touch point falls between grid nodes instead of exactly on one.
        int l = i - 1;
        while (l >= 0 && (!vs[l] || *vs[l] == 0.0)) {
            --l;
        }
        int r = i + 1;
        while (r < n && (!vs[r] || *vs[r] == 0.0)) {
            ++r;
        }
        const bool tangency =
            (l >= 0 && r < n) && ((*vs[l] > 0.0) == (*vs[r] > 0.0));
        roots.push_back({ts[i], tangency});
    }
    for (int i = 0; i + 1 < n; ++i) {
        if (!vs[i] || !vs[i + 1]) {
            continue; // domain gap: skip
        }
        const double va = *vs[i];
        const double vb = *vs[i + 1];
        if (va == 0.0 || vb == 0.0) {
            continue;
        }
        if ((va > 0.0) != (vb > 0.0)) {
            roots.push_back({refine(ts[i], ts[i + 1], va), false});
        }
    }

    // Even-multiplicity roots: local |f| minima that are tiny but never cross 0.
    for (int i = 1; i + 1 < n; ++i) {
        if (!vs[i - 1] || !vs[i] || !vs[i + 1]) {
            continue;
        }
        const double vl = *vs[i - 1];
        const double vm = *vs[i];
        const double vr = *vs[i + 1];
        if (vm == 0.0 || vl == 0.0 || vr == 0.0) {
            continue;
        }
        if ((vl > 0.0) != (vm > 0.0) || (vm > 0.0) != (vr > 0.0)) {
            continue; // a sign change: already handled by bracketing
        }
        if (std::abs(vm) > std::abs(vl) || std::abs(vm) > std::abs(vr)) {
            continue; // not a local minimum of |f|
        }
        const double local_scale = std::max({1.0, std::abs(vl), std::abs(vr)});
        if (std::abs(vm) > 1e-3 * local_scale) {
            continue;
        }
        double a = ts[i - 1];
        double b = ts[i + 1];
        for (int iter = 0; iter < 200 && (b - a) > opts.tol; ++iter) {
            const double m1 = a + (b - a) / 3.0;
            const double m2 = b - (b - a) / 3.0;
            const auto f1 = feval(m1);
            const auto f2 = feval(m2);
            if (!f1 || !f2) {
                break;
            }
            if (std::abs(*f1) < std::abs(*f2)) {
                b = m2;
            } else {
                a = m1;
            }
        }
        double x_star = 0.5 * (a + b);
        for (int iter = 0; iter < 8; ++iter) { // Newton polish
            const auto fx = feval(x_star);
            const auto d = dfeval(x_star);
            if (!fx || !d || *d == 0.0) {
                break;
            }
            const double next = x_star - *fx / *d;
            if (!std::isfinite(next) || std::abs(next - x_star) > 2.0 * step) {
                break;
            }
            x_star = next;
        }
        const auto fx = feval(x_star);
        if (fx && std::abs(*fx) < 1e-7 * std::max(1.0, std::abs(x_star))) {
            roots.push_back({x_star, true});
        }
    }

    // Dedup, verify by substitution, sort ascending. When a tangency root
    // coincides with a sign-change root, the sign-change observation wins.
    std::sort(roots.begin(), roots.end(),
              [](const NumericRoot& a, const NumericRoot& b) { return a.x < b.x; });
    std::vector<NumericRoot> unique;
    for (const NumericRoot& r : roots) {
        if (!unique.empty() && r.x - unique.back().x < 1e-8 * std::max(1.0, std::abs(r.x))) {
            unique.back().tangency = unique.back().tangency && r.tangency;
            continue;
        }
        unique.push_back(r);
    }
    for (const NumericRoot& r : unique) {
        const auto fr = feval(r.x);
        if (!fr) {
            continue;
        }
        double scale = 1.0;
        const auto lv = try_eval(eq.lhs, Bindings{{symbol_name, r.x}});
        const auto rv = try_eval(eq.rhs, Bindings{{symbol_name, r.x}});
        if (lv && rv) {
            scale = std::max({1.0, std::abs(*lv), std::abs(*rv)});
        }
        if (std::abs(*fr) >= 1e-6 * scale) {
            continue;
        }
        std::string note;
        if (r.tangency) {
            note = "tangency-type root: |f| has a near-zero minimum here; "
                   "no sign change observed";
        }
        res.solutions.push_back(
            Solution{make_num(rational_from_double(r.x)), false, std::move(note)});
    }

    res.warnings.push_back(std::format(
        "numeric search covered [{:g}, {:g}]; roots outside this interval are not reported",
        lo, hi));
    res.status = res.solutions.empty() ? Status::Unsolved : Status::NumericOnly;
    return res;
}

/// Step-1 trivial handling for an f free of the symbol; nullopt otherwise.
std::optional<SolveResult> classify_no_symbol(const Expr& f, std::string_view symbol) {
    if (contains_symbol(f, symbol)) {
        return std::nullopt;
    }
    SolveResult res;
    if (is_num_zero(f)) {
        res.status = Status::AllReals;
        res.method = "identity";
        return res;
    }
    if (free_symbols(f).empty()) {
        const auto v = try_eval(f);
        if (v && std::abs(*v) > 1e-12) {
            res.status = Status::NoRealSolution;
            res.method = "contradiction";
            return res;
        }
    }
    res.status = Status::Unsolved;
    res.warnings.push_back(std::format(
        "equation does not contain '{}' and could not be classified", symbol));
    return res;
}

std::optional<Expr> simplified_difference(const Equation& eq, SolveResult& res) {
    try {
        return simplify(make_sub(eq.lhs, eq.rhs));
    } catch (const Error& err) {
        res.status = Status::Unsolved;
        res.warnings.push_back(std::format("could not simplify the equation: {}", err.what()));
        return std::nullopt;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

SolveResult solve(const Equation& eq, std::string_view symbol, const NumericOptions& opts) {
    SolveResult res;
    const auto f_opt = simplified_difference(eq, res);
    if (!f_opt) {
        return res;
    }
    const Expr f = *f_opt;

    // 1. Trivial: identity / contradiction / does not determine the symbol.
    if (auto trivial = classify_no_symbol(f, symbol)) {
        return *trivial;
    }

    std::vector<std::string> carried_warnings;

    // 2. Polynomial path.
    PolyResult pr;
    try {
        if (const auto coeffs = polynomial_coefficients(f, symbol)) {
            if (coeffs->size() == 1) {
                // Degree-0 polynomial: f is constant in the symbol even though
                // it may still contain it structurally (e.g. x + 1 - (x + 1),
                // which simplify leaves as Add(1, x, Mul(-1, Add(1, x)))).
                // Classify as identity/contradiction here; otherwise the
                // numeric fallback would report every grid point as a root.
                if (auto trivial = classify_no_symbol(coeffs->front(), symbol)) {
                    return *trivial;
                }
            } else {
                pr = solve_poly(*coeffs, symbol, 0);
            }
        }
    } catch (const Error&) {
        pr = PolyResult{};
    }
    if (pr.kind == PolyResult::Kind::NoReal) {
        res.status = Status::NoRealSolution;
        res.method = std::move(pr.method);
        res.warnings = std::move(pr.warnings);
        return res;
    }
    if (pr.kind == PolyResult::Kind::Roots) {
        res.method = pr.method;
        res.warnings = pr.warnings;
        std::vector<Solution> candidates;
        candidates.reserve(pr.roots.size());
        for (Expr& r : pr.roots) {
            candidates.push_back(Solution{std::move(r), true, ""});
        }
        const bool kept_exact = finalize_exact(res, std::move(candidates), eq, f, symbol);

        if (!pr.remainder.empty()) {
            // Numeric roots of the irreducible remainder factor.
            std::vector<Expr> terms;
            const Expr x = make_sym(std::string(symbol));
            for (std::size_t i = 0; i < pr.remainder.size(); ++i) {
                terms.push_back(make_mul(
                    {make_num(pr.remainder[i]), make_pow(x, make_num(static_cast<long long>(i)))}));
            }
            const Expr rem = simplify(make_add(std::move(terms)));
            SolveResult tail = numeric_core(rem, Equation{rem, make_num(0)}, symbol, opts);
            for (Solution& s : tail.solutions) {
                res.solutions.push_back(std::move(s));
            }
            res.warnings.insert(res.warnings.end(), tail.warnings.begin(),
                                tail.warnings.end());
        }
        if (!res.solutions.empty()) {
            sort_solutions(res.solutions);
            res.status = kept_exact ? Status::Solved : Status::NumericOnly;
            return res;
        }
        // Everything dropped: fall through to the numeric fallback, keeping
        // the verification warnings.
        carried_warnings = std::move(res.warnings);
        res = SolveResult{};
    }

    // 3. Isolation path: the symbol occurs exactly once after simplify.
    if (count_sym(f, symbol) == 1) {
        IsoState st{symbol, {}, {}};
        const IsoStatus status = isolate(f, make_num(0), st);
        if (status == IsoStatus::NoReal) {
            res.status = Status::NoRealSolution;
            res.method = "isolation";
            res.warnings = std::move(st.warnings);
            return res;
        }
        if (status == IsoStatus::Ok) {
            res.method = "isolation";
            res.warnings = std::move(st.warnings);
            if (finalize_exact(res, std::move(st.solutions), eq, f, symbol)) {
                sort_solutions(res.solutions);
                res.status = Status::Solved;
                return res;
            }
            carried_warnings.insert(carried_warnings.end(), res.warnings.begin(),
                                    res.warnings.end());
            res = SolveResult{};
        }
    }

    // 4. Numeric fallback.
    SolveResult numeric = numeric_core(f, eq, symbol, opts);
    numeric.warnings.insert(numeric.warnings.begin(), carried_warnings.begin(),
                            carried_warnings.end());
    return numeric;
}

SolveResult solve_numeric(const Equation& eq, std::string_view symbol,
                          const NumericOptions& opts) {
    SolveResult res;
    const auto f_opt = simplified_difference(eq, res);
    if (!f_opt) {
        return res;
    }
    if (auto trivial = classify_no_symbol(*f_opt, symbol)) {
        return *trivial;
    }
    return numeric_core(*f_opt, eq, symbol, opts);
}

// ---------------------------------------------------------------------------
// Linear systems (DESIGN.md §9b)
// ---------------------------------------------------------------------------

namespace {

/// Joint-linearity extraction for one equation: peel the requested symbols in
/// order via polynomial_coefficients. Returns the augmented row
/// [a_1, ..., a_n, rhs] meaning a_1*x_1 + ... + a_n*x_n = rhs, or nullopt when
/// the equation is not jointly linear in the requested symbols (degree > 1 in
/// any of them, a non-polynomial shape, or a cross-term like x*y whose linear
/// coefficient still contains a requested symbol).
std::optional<std::vector<Expr>> linear_row(const Equation& eq,
                                            const std::vector<std::string>& symbols) {
    const std::size_t n = symbols.size();
    std::vector<Expr> row(n + 1);
    Expr rest = simplify(expand(make_sub(eq.lhs, eq.rhs)));
    for (std::size_t i = 0; i < n; ++i) {
        const auto coeffs = polynomial_coefficients(rest, symbols[i]);
        if (!coeffs || coeffs->size() > 2) {
            return std::nullopt;  // non-polynomial or degree >= 2
        }
        Expr lin = coeffs->size() == 2 ? (*coeffs)[1] : make_num(0);
        // The linear coefficient must be free of *all* requested symbols;
        // this rejects cross-terms like x*y (degree 1 in each variable
        // separately, but not jointly linear).
        for (const std::string& s : symbols) {
            if (contains_symbol(lin, s)) {
                return std::nullopt;
            }
        }
        row[i] = std::move(lin);
        rest = (*coeffs)[0];  // constant term: free of symbols[i]
    }
    // rest is the symbol-free constant term c of (lhs - rhs), so the row
    // reads  sum a_i * x_i = -c.
    row[n] = simplify(make_neg(rest));
    return row;
}

/// §9.5 verification doctrine, adapted to systems: substitute `values` into
/// every input equation and simplify(lhs - rhs). Residuals that do not reduce
/// to 0 are checked numerically with fixed test values for parameters and
/// free variables. An EvalError at every sample keeps the solution with a
/// domain warning; a residual that is clearly nonzero at every sample where
/// it evaluates demotes the result to Unsolved, naming the equation.
/// `has_conditional_row` is true when elimination produced a symbolic
/// "0 = c" row (kept with an "inconsistent unless c = 0" warning): the
/// solution is then valid only under that condition, so a residual that is
/// nonzero at the fixed test values is expected and must not demote it.
void verify_system(SystemSolveResult& res, const std::vector<Equation>& eqs,
                   bool has_conditional_row) {
    for (const Equation& eq : eqs) {
        const std::string eq_text = std::format("{} = {}", pretty(eq.lhs), pretty(eq.rhs));

        Expr lhs_sub;
        Expr rhs_sub;
        Expr residual;
        try {
            lhs_sub = eq.lhs;
            rhs_sub = eq.rhs;
            for (const auto& [name, value] : res.values) {
                lhs_sub = substitute(lhs_sub, name, value);
                rhs_sub = substitute(rhs_sub, name, value);
            }
            residual = simplify(make_sub(lhs_sub, rhs_sub));
        } catch (const Error&) {
            res.warnings.push_back(std::format(
                "solution may be valid only under domain conditions ({})", eq_text));
            continue;
        }
        if (is_num_zero(residual)) {
            continue;  // reduces to 0 symbolically: verified
        }

        // Numeric check with fixed test values for any remaining free
        // symbols (symbolic parameters and free variables).
        const std::set<std::string> extras = free_symbols(residual);
        std::vector<Bindings> samples;
        if (extras.empty()) {
            samples.emplace_back();
        } else {
            for (const double test_value : {1.7320508, 0.3141593}) {
                Bindings b;
                for (const std::string& name : extras) {
                    b[name] = test_value;
                }
                samples.push_back(std::move(b));
            }
        }

        int evaluated = 0;
        int nonzero = 0;
        for (const Bindings& b : samples) {
            double scale = 1.0;
            const auto lv = try_eval(lhs_sub, b);
            const auto rv = try_eval(rhs_sub, b);
            if (lv && rv) {
                scale = std::max({1.0, std::abs(*lv), std::abs(*rv)});
            }
            const auto value = try_eval(residual, b);
            if (!value) {
                continue;
            }
            ++evaluated;
            if (std::abs(*value) > 1e-6 * scale) {
                ++nonzero;
            }
        }

        if (evaluated == 0) {
            // EvalError at every sample: keep, with a domain warning.
            res.warnings.push_back(std::format(
                "solution may be valid only under domain conditions ({})", eq_text));
            continue;
        }
        if (nonzero == evaluated) {
            if (has_conditional_row && !extras.empty()) {
                // The residual carries parameters and the system only holds
                // under an already-reported "inconsistent unless ... = 0"
                // condition: keep (conditional solutions survive, §9.5).
                res.warnings.push_back(std::format(
                    "solution may be valid only under domain conditions ({})", eq_text));
                continue;
            }
            // Clearly nonzero everywhere it evaluates: demote.
            res.status = SystemSolveResult::Status::Unsolved;
            res.values.clear();
            res.free_variables.clear();
            res.warnings.push_back(
                std::format("solution failed verification for equation {}", eq_text));
            return;
        }
        if (nonzero == 0 && evaluated == static_cast<int>(samples.size())) {
            continue;  // ~0 at every sample: verified numerically
        }
        // Vanishes at some samples but not others (or some samples failed
        // to evaluate): keep with a warning.
        res.warnings.push_back(std::format(
            "solution may be valid only under domain conditions ({})", eq_text));
    }
}

/// solve_system minus the overflow guard: an OverflowError thrown by exact
/// coefficient arithmetic anywhere in extraction, elimination,
/// back-substitution, or verification escapes to the wrapper below.
SystemSolveResult solve_system_impl(const std::vector<Equation>& eqs,
                                    const std::vector<std::string>& symbols) {
    SystemSolveResult res;
    res.method = "gaussian elimination";

    // Requested symbols, deduplicated with the first occurrence winning
    // (a duplicate column would spuriously look like a free variable).
    std::vector<std::string> syms;
    for (const std::string& s : symbols) {
        if (std::find(syms.begin(), syms.end(), s) == syms.end()) {
            syms.push_back(s);
        }
    }
    const std::size_t n = syms.size();
    const std::size_t m = eqs.size();

    // Joint-linearity extraction: one augmented row per equation.
    std::vector<std::vector<Expr>> rows;
    rows.reserve(m);
    for (const Equation& eq : eqs) {
        auto row = linear_row(eq, syms);
        if (!row) {
            res.warnings.emplace_back("system is not linear in the requested variables");
            return res;  // Unsolved
        }
        rows.push_back(std::move(*row));
    }

    // Gauss-Jordan elimination over exact Expr arithmetic; every touched
    // entry is simplify()ed. Pivot choice prefers nonzero Number pivots; a
    // symbolic pivot is allowed with a "valid only when ... != 0" warning.
    std::size_t pivot_rows = 0;
    std::vector<std::size_t> pivot_col_of_row;  // column of row r's pivot, r < pivot_rows
    for (std::size_t col = 0; col < n && pivot_rows < m; ++col) {
        std::size_t chosen = m;
        for (std::size_t r = pivot_rows; r < m; ++r) {
            const Expr& entry = rows[r][col];
            if (is_num_zero(entry)) {
                continue;
            }
            if (entry->kind() == Kind::Number) {
                chosen = r;  // nonzero Number pivot: best choice, stop looking
                break;
            }
            if (chosen == m) {
                chosen = r;  // first symbolic candidate; keep scanning for a Number
            }
        }
        if (chosen == m) {
            continue;  // no pivot in this column: the symbol stays free
        }
        std::swap(rows[pivot_rows], rows[chosen]);
        const Expr pivot = rows[pivot_rows][col];
        if (!free_symbols(pivot).empty()) {
            res.warnings.push_back(
                std::format("valid only when {} != 0", pretty(pivot)));
        }
        // Normalize the pivot row, then eliminate the column everywhere else.
        // Entries left of `col` are already zero in every involved row.
        for (std::size_t k = col; k <= n; ++k) {
            rows[pivot_rows][k] = simplify(make_div(rows[pivot_rows][k], pivot));
        }
        for (std::size_t r = 0; r < m; ++r) {
            if (r == pivot_rows || is_num_zero(rows[r][col])) {
                continue;
            }
            const Expr factor = rows[r][col];
            for (std::size_t k = col; k <= n; ++k) {
                // expand() before simplify(): simplify alone does not
                // distribute a numeric factor over an Add, so a
                // mathematically-zero entry like -2*(c + 1) + 2*c + 2 would
                // survive the zero test and be chosen as a bogus symbolic
                // pivot (turning underdetermined/inconsistent systems into
                // "Solved") or leak unsimplified into "inconsistent unless"
                // warnings.
                rows[r][k] = simplify(expand(
                    make_sub(rows[r][k], make_mul({factor, rows[pivot_rows][k]}))));
            }
        }
        pivot_col_of_row.push_back(col);
        ++pivot_rows;
    }

    // Leftover rows have all-zero coefficients: each reads 0 = c.
    bool has_conditional_row = false;
    for (std::size_t r = pivot_rows; r < m; ++r) {
        const Expr& c = rows[r][n];
        if (is_num_zero(c)) {
            continue;
        }
        if (free_symbols(c).empty()) {
            if (c->kind() == Kind::Number) {
                // An exact rational that survived is_num_zero above is
                // exactly nonzero: inconsistent, no epsilon involved
                // (x = 1; x = 1.0000000000001 has no solution).
                res.status = SystemSolveResult::Status::NoSolution;
                return res;
            }
            // Constant-bearing shapes (pi, e): decide numerically when the
            // value is clearly nonzero; a near-zero residual (below eval
            // precision, e.g. pi - 3.14159265358979) is NOT silently
            // accepted — fall through to the conditional warning. An
            // EvalError also keeps the row with a warning.
            const auto value = try_eval(c);
            if (value && std::abs(*value) > 1e-12) {
                res.status = SystemSolveResult::Status::NoSolution;
                return res;
            }
        }
        has_conditional_row = true;
        res.warnings.push_back(std::format("inconsistent unless {} = 0", pretty(c)));
    }

    // Assemble the answer.
    if (pivot_rows == n) {
        res.status = SystemSolveResult::Status::Solved;
        for (std::size_t r = 0; r < pivot_rows; ++r) {
            res.values[syms[pivot_col_of_row[r]]] = rows[r][n];
        }
    } else {
        res.status = SystemSolveResult::Status::Underdetermined;
        std::vector<bool> is_pivot_col(n, false);
        for (const std::size_t col : pivot_col_of_row) {
            is_pivot_col[col] = true;
        }
        for (std::size_t col = 0; col < n; ++col) {
            if (!is_pivot_col[col]) {
                res.free_variables.push_back(syms[col]);
            }
        }
        for (std::size_t r = 0; r < pivot_rows; ++r) {
            // x_pivot = rhs - sum over free columns k of a_k * x_k.
            std::vector<Expr> terms{rows[r][n]};
            for (std::size_t k = 0; k < n; ++k) {
                if (!is_pivot_col[k] && !is_num_zero(rows[r][k])) {
                    terms.push_back(make_neg(make_mul({rows[r][k], make_sym(syms[k])})));
                }
            }
            res.values[syms[pivot_col_of_row[r]]] = simplify(make_add(std::move(terms)));
        }
    }

    verify_system(res, eqs, has_conditional_row);
    return res;
}

} // namespace

SystemSolveResult solve_system(const std::vector<Equation>& eqs,
                               const std::vector<std::string>& symbols) {
    try {
        return solve_system_impl(eqs, symbols);
    } catch (const OverflowError&) {
        // Exact coefficient arithmetic left 64-bit rationals (§9b): report,
        // never throw — mirroring the single-equation path, which degrades
        // gracefully instead of surfacing a bare error.
        SystemSolveResult res;
        res.method = "gaussian elimination";
        res.warnings.emplace_back(
            "coefficient arithmetic overflowed 64-bit rationals");
        return res;  // status defaults to Unsolved
    }
}

} // namespace mathsolver
