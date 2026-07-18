#include "mathsolver/expr.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>

namespace mathsolver {

// ---------------------------------------------------------------------------
// Function names
// ---------------------------------------------------------------------------

std::string_view function_name(FunctionId id) {
    switch (id) {
        case FunctionId::Sin: return "sin";
        case FunctionId::Cos: return "cos";
        case FunctionId::Tan: return "tan";
        case FunctionId::Asin: return "asin";
        case FunctionId::Acos: return "acos";
        case FunctionId::Atan: return "atan";
        case FunctionId::Sinh: return "sinh";
        case FunctionId::Cosh: return "cosh";
        case FunctionId::Tanh: return "tanh";
        case FunctionId::Asinh: return "asinh";
        case FunctionId::Acosh: return "acosh";
        case FunctionId::Atanh: return "atanh";
        case FunctionId::Gamma: return "gamma";
        case FunctionId::Digamma: return "digamma";
        case FunctionId::Erf: return "erf";
        case FunctionId::Erfc: return "erfc";
        case FunctionId::Fib: return "fib";
        case FunctionId::Harmonic: return "harmonic";
        case FunctionId::Ln: return "ln";
        case FunctionId::Abs: return "abs";
    }
    throw std::logic_error("function_name: invalid FunctionId");
}

std::optional<FunctionId> function_from_name(std::string_view name) {
    if (name == "sin") return FunctionId::Sin;
    if (name == "cos") return FunctionId::Cos;
    if (name == "tan") return FunctionId::Tan;
    if (name == "asin" || name == "arcsin") return FunctionId::Asin;
    if (name == "acos" || name == "arccos") return FunctionId::Acos;
    if (name == "atan" || name == "arctan") return FunctionId::Atan;
    if (name == "sinh") return FunctionId::Sinh;
    if (name == "cosh") return FunctionId::Cosh;
    if (name == "tanh") return FunctionId::Tanh;
    if (name == "asinh" || name == "arcsinh") return FunctionId::Asinh;
    if (name == "acosh" || name == "arccosh") return FunctionId::Acosh;
    if (name == "atanh" || name == "arctanh") return FunctionId::Atanh;
    if (name == "gamma") return FunctionId::Gamma;
    if (name == "digamma" || name == "psi") return FunctionId::Digamma;
    if (name == "erf") return FunctionId::Erf;
    if (name == "erfc") return FunctionId::Erfc;
    if (name == "fib" || name == "fibonacci") return FunctionId::Fib;
    if (name == "harmonic") return FunctionId::Harmonic;
    if (name == "ln") return FunctionId::Ln;
    if (name == "abs") return FunctionId::Abs;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ExprNode
// ---------------------------------------------------------------------------

ExprNode::ExprNode(Kind kind, Rational number, std::string symbol, ConstantId constant,
                   FunctionId function, std::vector<Expr> args)
    : kind_(kind),
      number_(number),
      symbol_(std::move(symbol)),
      constant_(constant),
      function_(function),
      args_(std::move(args)) {}

const Rational& ExprNode::number() const {
    if (kind_ != Kind::Number) {
        throw std::logic_error("ExprNode::number() called on a non-Number node");
    }
    return number_;
}

const std::string& ExprNode::symbol_name() const {
    if (kind_ != Kind::Symbol) {
        throw std::logic_error("ExprNode::symbol_name() called on a non-Symbol node");
    }
    return symbol_;
}

ConstantId ExprNode::constant() const {
    if (kind_ != Kind::Constant) {
        throw std::logic_error("ExprNode::constant() called on a non-Constant node");
    }
    return constant_;
}

FunctionId ExprNode::function() const {
    if (kind_ != Kind::Function) {
        throw std::logic_error("ExprNode::function() called on a non-Function node");
    }
    return function_;
}

namespace {

Expr node(Kind kind, Rational number, std::string symbol, ConstantId constant,
          FunctionId function, std::vector<Expr> args) {
    return std::make_shared<const ExprNode>(kind, number, std::move(symbol), constant, function,
                                            std::move(args));
}

Expr composite(Kind kind, std::vector<Expr> args) {
    return node(kind, Rational(0), std::string(), ConstantId::Pi, FunctionId::Sin,
                std::move(args));
}

int kind_rank(Kind k) {
    switch (k) {
        case Kind::Number: return 0;
        case Kind::Constant: return 1;
        case Kind::Symbol: return 2;
        case Kind::Pow: return 3;
        case Kind::Function: return 4;
        case Kind::Mul: return 5;
        case Kind::Add: return 6;
    }
    throw std::logic_error("kind_rank: invalid Kind");
}

unsigned long long magnitude_ull(long long x) noexcept {
    return x < 0 ? 0ULL - static_cast<unsigned long long>(x)
                 : static_cast<unsigned long long>(x);
}

// ---------------------------------------------------------------------------
// Order-independent numeric folding (DESIGN.md §2): exact sums/products are
// accumulated in a 128-bit rational, reduced as we go, so the same multiset
// of Number args never produces a different result — or a spurious
// OverflowError — depending on input order. Overflow is raised only when the
// final reduced value does not fit 64 bits (or a truly unrepresentable
// intermediate exceeds even 128 bits, which implies the final value cannot
// fit either once the inputs are pre-sorted deterministically).
// ---------------------------------------------------------------------------

unsigned __int128 magnitude_u128(__int128 x) noexcept {
    return x < 0 ? static_cast<unsigned __int128>(0) - static_cast<unsigned __int128>(x)
                 : static_cast<unsigned __int128>(x);
}

unsigned __int128 gcd_u128(unsigned __int128 a, unsigned __int128 b) noexcept {
    while (b != 0) {
        const unsigned __int128 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

__int128 checked_mul_128(__int128 a, __int128 b) {
    __int128 r = 0;
    if (__builtin_mul_overflow(a, b, &r)) {
        throw OverflowError("rational arithmetic overflow in numeric folding");
    }
    return r;
}

__int128 checked_add_128(__int128 a, __int128 b) {
    __int128 r = 0;
    if (__builtin_add_overflow(a, b, &r)) {
        throw OverflowError("rational arithmetic overflow in numeric folding");
    }
    return r;
}

/// Exact rational with 128-bit components; always reduced, den > 0.
struct Wide {
    __int128 num;
    __int128 den;

    void reduce() {
        if (num == 0) {
            den = 1;
            return;
        }
        const auto g =
            static_cast<__int128>(gcd_u128(magnitude_u128(num), magnitude_u128(den)));
        num /= g;
        den /= g;
    }

    void add(const Rational& r) {
        const __int128 rn = r.num();
        const __int128 rd = r.den();
        num = checked_add_128(checked_mul_128(num, rd), checked_mul_128(rn, den));
        den = checked_mul_128(den, rd);
        reduce();
    }

    void mul(const Rational& r) {
        // Cross-cancel first so the products stay as small as possible.
        const auto g1 = static_cast<__int128>(
            gcd_u128(magnitude_u128(num), static_cast<unsigned __int128>(r.den())));
        const auto g2 = static_cast<__int128>(
            gcd_u128(magnitude_u128(static_cast<__int128>(r.num())),
                     magnitude_u128(den)));
        num = checked_mul_128(num / g1, static_cast<__int128>(r.num()) / g2);
        den = checked_mul_128(den / g2, static_cast<__int128>(r.den()) / g1);
        if (num == 0) {
            den = 1;
        }
    }

    Rational to_rational() const {
        constexpr auto kMaxNum = static_cast<unsigned __int128>(LLONG_MAX);
        const unsigned __int128 num_limit = num < 0 ? kMaxNum + 1 : kMaxNum;
        if (magnitude_u128(num) > num_limit ||
            static_cast<unsigned __int128>(den) > kMaxNum) {
            throw OverflowError("numeric fold does not fit in a 64-bit rational");
        }
        return Rational(static_cast<long long>(num), static_cast<long long>(den));
    }
};

/// Fold a multiset of Rationals order-independently: sort deterministically
/// (the comparison is overflow-free), then accumulate in 128 bits.
Rational fold_numbers(std::vector<Rational> values, bool multiply) {
    std::sort(values.begin(), values.end(),
              [](const Rational& a, const Rational& b) { return a < b; });
    Wide acc{multiply ? 1 : 0, 1};
    for (const Rational& v : values) {
        if (multiply) {
            acc.mul(v);
        } else {
            acc.add(v);
        }
    }
    return acc.to_rational();
}

/// Exact integer q-th root of x, if one exists. Uses a double estimate that
/// is then verified (and corrected by +-1) with checked integer
/// multiplication, so there are no floating-point false positives.
std::optional<unsigned long long> exact_root(unsigned long long x, long long q) {
    if (x == 0) {
        return 0ULL;
    }
    if (x == 1) {
        return 1ULL;
    }
    const auto estimate = static_cast<long long>(
        std::llround(std::pow(static_cast<double>(x), 1.0 / static_cast<double>(q))));
    const unsigned long long low = estimate > 2 ? static_cast<unsigned long long>(estimate) - 1 : 1;
    for (unsigned long long candidate = low; candidate <= low + 2; ++candidate) {
        if (candidate == 1) {
            continue; // 1^q == 1 < x here (x >= 2), and the loop below would run q times
        }
        // candidate^q with overflow bail-out (overflow means candidate^q > x).
        unsigned long long power = 1;
        bool overflow = false;
        for (long long i = 0; i < q && !overflow; ++i) {
            overflow = __builtin_mul_overflow(power, candidate, &power);
        }
        if (!overflow && power == x) {
            return candidate;
        }
        if (!overflow && power > x) {
            break; // candidates only grow
        }
    }
    return std::nullopt;
}

/// pow(base, p/q) folded to an exact rational, if that is possible.
/// base != 0, q > 1, gcd(p, q) == 1.
std::optional<Rational> exact_rational_pow(const Rational& base, long long p, long long q) {
    bool negative = base.is_negative();
    if (negative && q % 2 == 0) {
        return std::nullopt; // even root of a negative: not real
    }
    const auto root_num = exact_root(magnitude_ull(base.num()), q);
    if (!root_num) {
        return std::nullopt;
    }
    const auto root_den = exact_root(static_cast<unsigned long long>(base.den()), q);
    if (!root_den) {
        return std::nullopt;
    }
    // Roots are <= the original magnitudes, so they fit in long long.
    const long long rn = negative ? static_cast<long long>(0ULL - *root_num)
                                  : static_cast<long long>(*root_num);
    return Rational(rn, static_cast<long long>(*root_den)).pow(p);
}

void flatten_into(Kind kind, std::vector<Expr>& out, const Expr& e) {
    if (e->kind() == kind) {
        for (const auto& a : e->args()) {
            flatten_into(kind, out, a);
        }
    } else {
        out.push_back(e);
    }
}

void sort_args(std::vector<Expr>& args) {
    std::sort(args.begin(), args.end(),
              [](const Expr& a, const Expr& b) { return compare_expr(a, b) < 0; });
}

std::size_t hash_combine(std::size_t seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

} // namespace

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

Expr make_num(long long n) {
    return make_num(Rational(n));
}

Expr make_num(const Rational& r) {
    return node(Kind::Number, r, std::string(), ConstantId::Pi, FunctionId::Sin, {});
}

Expr make_sym(std::string name) {
    return node(Kind::Symbol, Rational(0), std::move(name), ConstantId::Pi, FunctionId::Sin, {});
}

Expr make_const(ConstantId id) {
    return node(Kind::Constant, Rational(0), std::string(), id, FunctionId::Sin, {});
}

Expr make_add(std::vector<Expr> terms) {
    std::vector<Expr> flat;
    flat.reserve(terms.size());
    for (const auto& t : terms) {
        flatten_into(Kind::Add, flat, t);
    }
    std::vector<Rational> numbers;
    std::vector<Expr> args;
    args.reserve(flat.size());
    for (auto& t : flat) {
        if (t->kind() == Kind::Number) {
            numbers.push_back(t->number());
        } else {
            args.push_back(std::move(t));
        }
    }
    const Rational sum = fold_numbers(std::move(numbers), /*multiply=*/false);
    if (!sum.is_zero()) {
        args.push_back(make_num(sum));
    }
    if (args.empty()) {
        return make_num(0);
    }
    if (args.size() == 1) {
        return args.front();
    }
    sort_args(args);
    return composite(Kind::Add, std::move(args));
}

Expr make_mul(std::vector<Expr> factors) {
    std::vector<Expr> flat;
    flat.reserve(factors.size());
    for (const auto& f : factors) {
        flatten_into(Kind::Mul, flat, f);
    }
    // A literal 0 factor annihilates everything; check before folding so a
    // huge sibling number cannot raise a spurious OverflowError first.
    for (const auto& f : flat) {
        if (f->kind() == Kind::Number && f->number().is_zero()) {
            return make_num(0);
        }
    }
    std::vector<Rational> numbers;
    std::vector<Expr> args;
    args.reserve(flat.size());
    for (auto& f : flat) {
        if (f->kind() == Kind::Number) {
            numbers.push_back(f->number());
        } else {
            args.push_back(std::move(f));
        }
    }
    const Rational product = fold_numbers(std::move(numbers), /*multiply=*/true);
    if (args.empty()) {
        return make_num(product);
    }
    if (!product.is_one()) {
        args.push_back(make_num(product));
    }
    if (args.size() == 1) {
        return args.front();
    }
    sort_args(args);
    return composite(Kind::Mul, std::move(args));
}

Expr make_pow(Expr base, Expr exponent) {
    if (exponent->kind() == Kind::Number) {
        const Rational& e = exponent->number();
        if (e.is_zero()) {
            return make_num(1); // 0^0 == 1 by convention
        }
        if (e.is_one()) {
            return base;
        }
    }
    if (base->kind() == Kind::Number) {
        const Rational& b = base->number();
        if (b.is_one()) {
            return make_num(1);
        }
        if (exponent->kind() == Kind::Number) {
            const Rational& e = exponent->number();
            if (b.is_zero()) {
                if (e.is_negative()) {
                    throw DivisionByZeroError("zero raised to a negative power");
                }
                return make_num(0);
            }
            if (e.is_integer()) {
                return make_num(b.pow(e.num())); // overflow throws (integer path only)
            }
            try {
                if (auto folded = exact_rational_pow(b, e.num(), e.den())) {
                    return make_num(*folded);
                }
            } catch (const OverflowError&) {
                // The exact result exists mathematically but does not fit
                // 64 bits (e.g. pow(4, 101/2) = 2^101): stay symbolic.
            }
        }
    }
    // Structural folds (DESIGN.md §2, required for the printer round-trip).
    if (exponent->kind() == Kind::Number && exponent->number().is_integer()) {
        // (u^r)^s -> u^(r*s) when r and s are both Numbers and s is integer.
        if (base->kind() == Kind::Pow && base->arg(1)->kind() == Kind::Number) {
            return make_pow(base->arg(0),
                            make_num(base->arg(1)->number() * exponent->number()));
        }
        // (c * rest)^s with a Number factor c -> c^s * rest^s.
        if (base->kind() == Kind::Mul && base->arg(0)->kind() == Kind::Number) {
            std::vector<Expr> rest(base->args().begin() + 1, base->args().end());
            Expr rest_pow = make_pow(make_mul(std::move(rest)), exponent);
            return make_mul({make_pow(base->arg(0), std::move(exponent)),
                             std::move(rest_pow)});
        }
    }
    return composite(Kind::Pow, {std::move(base), std::move(exponent)});
}

Expr make_fn(FunctionId id, Expr arg) {
    return node(Kind::Function, Rational(0), std::string(), ConstantId::Pi, id,
                {std::move(arg)});
}

Expr make_neg(Expr e) {
    return make_mul({make_num(-1), std::move(e)});
}

Expr make_sub(Expr a, Expr b) {
    return make_add({std::move(a), make_neg(std::move(b))});
}

Expr make_div(Expr a, Expr b) {
    return make_mul({std::move(a), make_pow(std::move(b), make_num(-1))});
}

Expr make_sqrt(Expr e) {
    return make_pow(std::move(e), make_num(Rational(1, 2)));
}

Expr make_exp(Expr e) {
    return make_pow(make_const(ConstantId::E), std::move(e));
}

Expr operator+(const Expr& a, const Expr& b) {
    return make_add({a, b});
}

Expr operator-(const Expr& a, const Expr& b) {
    return make_sub(a, b);
}

Expr operator*(const Expr& a, const Expr& b) {
    return make_mul({a, b});
}

Expr operator/(const Expr& a, const Expr& b) {
    return make_div(a, b);
}

Expr operator-(const Expr& a) {
    return make_neg(a);
}

// ---------------------------------------------------------------------------
// Core utilities
// ---------------------------------------------------------------------------

int compare_expr(const Expr& a, const Expr& b) {
    if (a.get() == b.get()) {
        return 0;
    }
    const int ra = kind_rank(a->kind());
    const int rb = kind_rank(b->kind());
    if (ra != rb) {
        return ra < rb ? -1 : 1;
    }
    switch (a->kind()) {
        case Kind::Number: {
            const auto cmp = a->number() <=> b->number();
            if (cmp == std::strong_ordering::equal) return 0;
            return cmp == std::strong_ordering::less ? -1 : 1;
        }
        case Kind::Constant: {
            const int ia = static_cast<int>(a->constant());
            const int ib = static_cast<int>(b->constant());
            if (ia != ib) return ia < ib ? -1 : 1;
            return 0;
        }
        case Kind::Symbol: {
            const int cmp = a->symbol_name().compare(b->symbol_name());
            if (cmp != 0) return cmp < 0 ? -1 : 1;
            return 0;
        }
        case Kind::Function: {
            const int ia = static_cast<int>(a->function());
            const int ib = static_cast<int>(b->function());
            if (ia != ib) return ia < ib ? -1 : 1;
            return compare_expr(a->arg(0), b->arg(0));
        }
        case Kind::Pow: {
            const int base_cmp = compare_expr(a->arg(0), b->arg(0));
            if (base_cmp != 0) return base_cmp;
            return compare_expr(a->arg(1), b->arg(1));
        }
        case Kind::Add:
        case Kind::Mul: {
            const auto& xs = a->args();
            const auto& ys = b->args();
            const std::size_t n = std::min(xs.size(), ys.size());
            for (std::size_t i = 0; i < n; ++i) {
                const int cmp = compare_expr(xs[i], ys[i]);
                if (cmp != 0) return cmp;
            }
            if (xs.size() != ys.size()) return xs.size() < ys.size() ? -1 : 1;
            return 0;
        }
    }
    throw std::logic_error("compare_expr: invalid Kind");
}

bool structurally_equal(const Expr& a, const Expr& b) {
    return compare_expr(a, b) == 0;
}

std::size_t hash_expr(const Expr& e) {
    std::size_t h = std::hash<int>{}(kind_rank(e->kind()));
    switch (e->kind()) {
        case Kind::Number:
            h = hash_combine(h, std::hash<long long>{}(e->number().num()));
            h = hash_combine(h, std::hash<long long>{}(e->number().den()));
            return h;
        case Kind::Constant:
            return hash_combine(h, std::hash<int>{}(static_cast<int>(e->constant())));
        case Kind::Symbol:
            return hash_combine(h, std::hash<std::string>{}(e->symbol_name()));
        case Kind::Function:
            h = hash_combine(h, std::hash<int>{}(static_cast<int>(e->function())));
            return hash_combine(h, hash_expr(e->arg(0)));
        case Kind::Pow:
        case Kind::Add:
        case Kind::Mul:
            for (const auto& a : e->args()) {
                h = hash_combine(h, hash_expr(a));
            }
            return h;
    }
    throw std::logic_error("hash_expr: invalid Kind");
}

bool contains_symbol(const Expr& e, std::string_view name) {
    if (e->kind() == Kind::Symbol) {
        return e->symbol_name() == name;
    }
    return std::ranges::any_of(e->args(),
                               [&](const Expr& a) { return contains_symbol(a, name); });
}

namespace {

void collect_symbols(const Expr& e, std::set<std::string>& out) {
    if (e->kind() == Kind::Symbol) {
        out.insert(e->symbol_name());
        return;
    }
    for (const auto& a : e->args()) {
        collect_symbols(a, out);
    }
}

} // namespace

std::set<std::string> free_symbols(const Expr& e) {
    std::set<std::string> out;
    collect_symbols(e, out);
    return out;
}

Expr substitute(const Expr& e, std::string_view name, const Expr& replacement) {
    switch (e->kind()) {
        case Kind::Number:
        case Kind::Constant:
            return e;
        case Kind::Symbol:
            return e->symbol_name() == name ? replacement : e;
        case Kind::Add:
        case Kind::Mul: {
            std::vector<Expr> args;
            args.reserve(e->args().size());
            bool changed = false;
            for (const auto& a : e->args()) {
                args.push_back(substitute(a, name, replacement));
                changed = changed || args.back().get() != a.get();
            }
            if (!changed) {
                return e;
            }
            return e->kind() == Kind::Add ? make_add(std::move(args))
                                          : make_mul(std::move(args));
        }
        case Kind::Pow: {
            Expr base = substitute(e->arg(0), name, replacement);
            Expr exponent = substitute(e->arg(1), name, replacement);
            if (base.get() == e->arg(0).get() && exponent.get() == e->arg(1).get()) {
                return e;
            }
            return make_pow(std::move(base), std::move(exponent));
        }
        case Kind::Function: {
            Expr arg = substitute(e->arg(0), name, replacement);
            if (arg.get() == e->arg(0).get()) {
                return e;
            }
            return make_fn(e->function(), std::move(arg));
        }
    }
    throw std::logic_error("substitute: invalid Kind");
}

std::string debug_string(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
            return e->number().to_string();
        case Kind::Symbol:
            return e->symbol_name();
        case Kind::Constant:
            return e->constant() == ConstantId::Pi ? "pi" : "e";
        case Kind::Function:
            return "(" + std::string(function_name(e->function())) + " " +
                   debug_string(e->arg(0)) + ")";
        case Kind::Pow:
            return "(pow " + debug_string(e->arg(0)) + " " + debug_string(e->arg(1)) + ")";
        case Kind::Add:
        case Kind::Mul: {
            std::string out = e->kind() == Kind::Add ? "(add" : "(mul";
            for (const auto& a : e->args()) {
                out += " ";
                out += debug_string(a);
            }
            out += ")";
            return out;
        }
    }
    throw std::logic_error("debug_string: invalid Kind");
}

} // namespace mathsolver
