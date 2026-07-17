#include "mathsolver/simplify.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/rational.hpp"

namespace mathsolver {
namespace {

constexpr int kMaxPasses = 32;
constexpr long long kMaxExpandExponent = 512;
constexpr long long kMaxPolynomialDegree = 1'000'000;

// ---------------------------------------------------------------------------
// Small predicates / accessors
// ---------------------------------------------------------------------------

const Rational* as_number(const Expr& e) {
    return e->kind() == Kind::Number ? &e->number() : nullptr;
}

bool is_number_value(const Expr& e, const Rational& v) {
    const Rational* n = as_number(e);
    return n != nullptr && *n == v;
}

bool is_constant(const Expr& e, ConstantId id) {
    return e->kind() == Kind::Constant && e->constant() == id;
}

bool is_function(const Expr& e, FunctionId id) {
    return e->kind() == Kind::Function && e->function() == id;
}

bool odd(long long v) {
    return v % 2 != 0;
}

/// Assemble a raw node directly (no canonicalizing factory pass). Used ONLY as
/// the overflow fallback in simplify_pass: when an exact number fold overflows,
/// we still want to keep the children's completed simplifications rather than
/// rolling the whole node back to its pre-pass form (FINDING A).
Expr raw_node(Kind kind, std::vector<Expr> args) {
    return std::make_shared<const ExprNode>(kind, Rational(0), std::string(),
                                            ConstantId::Pi, FunctionId::Sin,
                                            std::move(args));
}

/// Overflow fallback for Add/Mul: rebuild from the already-simplified children
/// with the offending number fold suppressed. Children are flattened (same
/// kind) and sorted by compare_expr so the result mirrors the canonical layout
/// minus the unrepresentable fold, and is stable/idempotent under later passes.
Expr raw_commutative(Kind kind, std::vector<Expr> children) {
    std::vector<Expr> args;
    args.reserve(children.size());
    for (Expr& c : children) {
        if (c->kind() == kind) {
            for (const Expr& g : c->args()) {
                args.push_back(g);
            }
        } else {
            args.push_back(std::move(c));
        }
    }
    std::sort(args.begin(), args.end(),
              [](const Expr& a, const Expr& b) { return compare_expr(a, b) < 0; });
    return raw_node(kind, std::move(args));
}

// ---------------------------------------------------------------------------
// Term / factor decomposition helpers
// ---------------------------------------------------------------------------

/// Split an Add term into (rational coefficient, non-numeric rest).
/// A pure Number term yields (value, 1).
std::pair<Rational, Expr> split_term(const Expr& t) {
    if (const Rational* n = as_number(t)) {
        return {*n, make_num(1)};
    }
    if (t->kind() == Kind::Mul) {
        if (const Rational* c = as_number(t->arg(0))) {
            std::vector<Expr> rest(t->args().begin() + 1, t->args().end());
            return {*c, make_mul(std::move(rest))};
        }
    }
    return {Rational(1), t};
}

/// Split a Mul factor into (base, numeric exponent) when the exponent is a
/// Number; nullopt exponent means the factor must be kept verbatim.
std::pair<Expr, std::optional<Rational>> split_factor(const Expr& f) {
    if (f->kind() == Kind::Pow) {
        if (const Rational* r = as_number(f->arg(1))) {
            return {f->arg(0), *r};
        }
        return {f, std::nullopt};
    }
    return {f, Rational(1)};
}

/// If `u` is a negation (a negative Number, or a Mul whose leading rational
/// is negative), return `u` with the sign flipped; nullopt otherwise.
std::optional<Expr> negated_argument(const Expr& u) {
    if (const Rational* n = as_number(u)) {
        if (n->is_negative()) {
            return make_num(-*n);
        }
        return std::nullopt;
    }
    if (u->kind() == Kind::Mul) {
        if (const Rational* c = as_number(u->arg(0)); c != nullptr && c->is_negative()) {
            std::vector<Expr> args;
            args.reserve(u->args().size());
            args.push_back(make_num(-*c));
            args.insert(args.end(), u->args().begin() + 1, u->args().end());
            return make_mul(std::move(args));
        }
    }
    return std::nullopt;
}

/// arg == r * pi for a rational r? (0, pi, and Mul(Number, pi) shapes.)
std::optional<Rational> pi_multiple(const Expr& u) {
    if (const Rational* n = as_number(u)) {
        if (n->is_zero()) {
            return Rational(0);
        }
        return std::nullopt;
    }
    if (is_constant(u, ConstantId::Pi)) {
        return Rational(1);
    }
    if (u->kind() == Kind::Mul && u->args().size() == 2) {
        if (const Rational* c = as_number(u->arg(0)); c != nullptr &&
            is_constant(u->arg(1), ConstantId::Pi)) {
            return *c;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Trig special values (DESIGN.md §7): rational multiples of pi with
// denominator in {1, 2, 3, 4, 6}, reduced mod 2*pi, all quadrants.
// Values are represented as scale * sqrt(root) with root in {1, 2, 3}.
// ---------------------------------------------------------------------------

struct ScaledRoot {
    Rational scale;
    long long root; // 1 => the value is just `scale`
};

Expr scaled_root_expr(const ScaledRoot& v) {
    if (v.root == 1 || v.scale.is_zero()) {
        return make_num(v.scale);
    }
    return make_mul({make_num(v.scale),
                     make_pow(make_num(v.root), make_num(Rational(1, 2)))});
}

/// sin(k * pi/12) for k in [0, 24); k is always a multiple of 12/q for
/// q in {1, 2, 3, 4, 6}, so the reference angle lands on {0, 2, 3, 4, 6}.
ScaledRoot sin_of_twelfths(long long k) {
    k = ((k % 24) + 24) % 24;
    long long sign = 1;
    if (k >= 12) {
        sign = -1;
        k -= 12;
    }
    if (k > 6) {
        k = 12 - k;
    }
    ScaledRoot v{Rational(0), 1};
    switch (k) {
        case 0: v = {Rational(0), 1}; break;
        case 2: v = {Rational(1, 2), 1}; break;
        case 3: v = {Rational(1, 2), 2}; break;
        case 4: v = {Rational(1, 2), 3}; break;
        case 6: v = {Rational(1), 1}; break;
        default: v = {Rational(0), 1}; break; // unreachable for allowed denominators
    }
    v.scale = Rational(sign) * v.scale;
    return v;
}

/// Exact value of sin/cos/tan at r*pi, when r's denominator is in
/// {1, 2, 3, 4, 6}. tan at odd multiples of pi/2 returns nullopt (left
/// unevaluated, never a throw).
std::optional<Expr> trig_special(FunctionId id, const Rational& r) {
    const long long q = r.den();
    if (q != 1 && q != 2 && q != 3 && q != 4 && q != 6) {
        return std::nullopt;
    }
    // k = (r mod 2) * 12, an exact integer in [0, 24). Reduce the numerator
    // mod 2q first so the multiplication cannot overflow.
    const long long n = ((r.num() % (2 * q)) + 2 * q) % (2 * q);
    const long long k = n * (12 / q);
    switch (id) {
        case FunctionId::Sin:
            return scaled_root_expr(sin_of_twelfths(k));
        case FunctionId::Cos:
            return scaled_root_expr(sin_of_twelfths(k + 6));
        case FunctionId::Tan: {
            const long long m = k % 12;
            switch (m) {
                case 0: return make_num(0);
                case 2: return scaled_root_expr({Rational(1, 3), 3});
                case 3: return make_num(1);
                case 4: return scaled_root_expr({Rational(1), 3});
                case 6: return std::nullopt; // tan undefined at odd pi/2
                case 8: return scaled_root_expr({Rational(-1), 3});
                case 9: return make_num(-1);
                case 10: return scaled_root_expr({Rational(-1, 3), 3});
                default: return std::nullopt;
            }
        }
        default:
            return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Inverse-trig special values (amended DESIGN.md §7). Arguments are
// recognized in their canonical AST shapes: Number, Pow(k, +-1/2), and
// Mul(Number, Pow(k, 1/2)); e.g. sqrt(3)/2 == Mul(1/2, Pow(3, 1/2)).
// ---------------------------------------------------------------------------

/// Recognize u == scale * sqrt(root) (root a positive integer).
std::optional<ScaledRoot> as_scaled_root(const Expr& u) {
    const auto root_part = [](const Expr& p) -> std::optional<ScaledRoot> {
        if (p->kind() != Kind::Pow) {
            return std::nullopt;
        }
        const Rational* base = as_number(p->arg(0));
        const Rational* exp = as_number(p->arg(1));
        if (base == nullptr || exp == nullptr || !base->is_integer() ||
            base->num() <= 1) {
            return std::nullopt;
        }
        if (*exp == Rational(1, 2)) {
            return ScaledRoot{Rational(1), base->num()};
        }
        if (*exp == Rational(-1, 2)) {
            // 1/sqrt(k) == sqrt(k)/k
            return ScaledRoot{Rational(1, base->num()), base->num()};
        }
        return std::nullopt;
    };
    if (const Rational* n = as_number(u)) {
        return ScaledRoot{*n, 1};
    }
    if (auto r = root_part(u)) {
        return r;
    }
    if (u->kind() == Kind::Mul && u->args().size() == 2) {
        if (const Rational* c = as_number(u->arg(0))) {
            if (auto r = root_part(u->arg(1))) {
                return ScaledRoot{*c * r->scale, r->root};
            }
        }
    }
    return std::nullopt;
}

Expr pi_times(const Rational& r) {
    return make_mul({make_num(r), make_const(ConstantId::Pi)});
}

std::optional<Expr> inverse_trig_special(FunctionId id, const Expr& u) {
    const auto v = as_scaled_root(u);
    if (!v) {
        return std::nullopt;
    }
    struct Entry {
        Rational scale;
        long long root;
        Rational result; // result is `result * pi`
    };
    static const std::vector<Entry> kAsin = {
        {Rational(0), 1, Rational(0)},
        {Rational(1, 2), 1, Rational(1, 6)},   {Rational(-1, 2), 1, Rational(-1, 6)},
        {Rational(1), 1, Rational(1, 2)},      {Rational(-1), 1, Rational(-1, 2)},
        {Rational(1, 2), 2, Rational(1, 4)},   {Rational(-1, 2), 2, Rational(-1, 4)},
        {Rational(1, 2), 3, Rational(1, 3)},   {Rational(-1, 2), 3, Rational(-1, 3)},
    };
    static const std::vector<Entry> kAcos = {
        {Rational(0), 1, Rational(1, 2)},
        {Rational(1, 2), 1, Rational(1, 3)},   {Rational(-1, 2), 1, Rational(2, 3)},
        {Rational(1), 1, Rational(0)},         {Rational(-1), 1, Rational(1)},
        {Rational(1, 2), 2, Rational(1, 4)},   {Rational(-1, 2), 2, Rational(3, 4)},
        {Rational(1, 2), 3, Rational(1, 6)},   {Rational(-1, 2), 3, Rational(5, 6)},
    };
    static const std::vector<Entry> kAtan = {
        {Rational(0), 1, Rational(0)},
        {Rational(1), 1, Rational(1, 4)},      {Rational(-1), 1, Rational(-1, 4)},
        {Rational(1), 3, Rational(1, 3)},      {Rational(-1), 3, Rational(-1, 3)},
        {Rational(1, 3), 3, Rational(1, 6)},   {Rational(-1, 3), 3, Rational(-1, 6)},
    };
    const std::vector<Entry>* table = nullptr;
    switch (id) {
        case FunctionId::Asin: table = &kAsin; break;
        case FunctionId::Acos: table = &kAcos; break;
        case FunctionId::Atan: table = &kAtan; break;
        default: return std::nullopt;
    }
    for (const Entry& entry : *table) {
        if (entry.scale == v->scale && entry.root == v->root) {
            return pi_times(entry.result);
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Per-kind local rules. Each function expects a node whose children are
// already simplified; it applies one round of rules (the fixpoint loop in
// simplify() handles cascades).
// ---------------------------------------------------------------------------

/// sin(u)^2 + cos(u)^2 -> 1, including a common coefficient
/// k*sin(u)^2 + k*cos(u)^2 -> k (k = numeric coefficient times any other
/// factors, matched structurally).
Expr apply_pythagorean(Expr e) {
    while (e->kind() == Kind::Add) {
        struct TrigTerm {
            std::size_t index;
            bool is_sin;
            Expr u;
            Expr key; // coefficient * remaining factors
        };
        std::vector<TrigTerm> found;
        const auto& args = e->args();
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto [coeff, rest] = split_term(args[i]);
            std::vector<Expr> factors;
            if (rest->kind() == Kind::Mul) {
                factors = rest->args();
            } else {
                factors = {rest};
            }
            for (std::size_t fi = 0; fi < factors.size(); ++fi) {
                const Expr& f = factors[fi];
                if (f->kind() != Kind::Pow || !is_number_value(f->arg(1), Rational(2))) {
                    continue;
                }
                const Expr& base = f->arg(0);
                if (!is_function(base, FunctionId::Sin) &&
                    !is_function(base, FunctionId::Cos)) {
                    continue;
                }
                std::vector<Expr> others;
                others.push_back(make_num(coeff));
                for (std::size_t oi = 0; oi < factors.size(); ++oi) {
                    if (oi != fi) {
                        others.push_back(factors[oi]);
                    }
                }
                found.push_back({i, is_function(base, FunctionId::Sin), base->arg(0),
                                 make_mul(std::move(others))});
                // Record EVERY eligible sin^2/cos^2 factor of this term as a
                // pairing candidate, not just the first: when the coefficient
                // itself contains a sin^2/cos^2 factor the correct partner may
                // be a later factor (FINDING 4).
            }
        }
        bool replaced = false;
        for (const TrigTerm& s : found) {
            if (!s.is_sin) {
                continue;
            }
            for (const TrigTerm& c : found) {
                if (c.is_sin || c.index == s.index) {
                    continue;
                }
                if (!structurally_equal(s.u, c.u) || !structurally_equal(s.key, c.key)) {
                    continue;
                }
                std::vector<Expr> next;
                next.reserve(args.size() - 1);
                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (i != s.index && i != c.index) {
                        next.push_back(args[i]);
                    }
                }
                next.push_back(s.key);
                e = make_add(std::move(next));
                replaced = true;
                break;
            }
            if (replaced) {
                break;
            }
        }
        if (!replaced) {
            break;
        }
    }
    return e;
}

/// Group Add terms with structurally-equal non-numeric parts, summing their
/// rational coefficients. Grouping is bucketed by hash_expr (consistent with
/// structural equality) so it is near-linear in the term count rather than the
/// old O(terms*distinct) linear scan (FINDING B). Behavior matches the former
/// collector exactly: candidates are scanned in insertion order, an overflowing
/// coefficient sum keeps the term as a separate group (deterministic and
/// idempotent), and zero-coefficient groups are dropped.
std::vector<Expr> collect_like_terms(const std::vector<Expr>& terms) {
    struct Group {
        Expr rest;
        Rational coeff;
    };
    std::vector<Group> groups;
    std::unordered_map<std::size_t, std::vector<std::size_t>> buckets;
    for (const Expr& t : terms) {
        auto [coeff, rest] = split_term(t);
        std::vector<std::size_t>& bucket = buckets[hash_expr(rest)];
        bool merged = false;
        for (const std::size_t gi : bucket) {
            if (!structurally_equal(groups[gi].rest, rest)) {
                continue;
            }
            try {
                groups[gi].coeff = groups[gi].coeff + coeff;
                merged = true;
                break;
            } catch (const OverflowError&) {
                continue; // keep as a separate group; deterministic and idempotent
            }
        }
        if (!merged) {
            bucket.push_back(groups.size());
            groups.push_back({std::move(rest), coeff});
        }
    }
    std::vector<Expr> out;
    out.reserve(groups.size());
    for (const Group& g : groups) {
        if (g.coeff.is_zero()) {
            continue;
        }
        out.push_back(make_mul({make_num(g.coeff), g.rest}));
    }
    return out;
}

/// Like-term collection: terms with structurally equal non-numeric parts
/// fold their rational coefficients.
Expr apply_add_rules(const Expr& e) {
    if (e->kind() != Kind::Add) {
        return e;
    }
    return apply_pythagorean(make_add(collect_like_terms(e->args())));
}

/// Like-factor collection: factors with structurally equal bases and numeric
/// exponents combine (x * x^2 -> x^3, x/x -> 1, sqrt(2)*sqrt(2) -> 2).
// --- ln(a)/ln(b) exact folding (v0.4, §7) ----------------------------------

/// Exact integer k-th root of n >= 2, if it exists (double estimate verified
/// by checked multiplication, same technique as the §2 factory folds).
std::optional<long long> exact_llroot(long long n, int k) {
    const auto estimate =
        static_cast<long long>(std::llround(std::pow(static_cast<double>(n), 1.0 / k)));
    for (long long c = std::max(2LL, estimate - 1); c <= estimate + 1; ++c) {
        long long acc = 1;
        bool overflow = false;
        for (int i = 0; i < k && !overflow; ++i) {
            overflow = __builtin_mul_overflow(acc, c, &acc) || acc > n;
        }
        if (!overflow && acc == n) {
            return c;
        }
    }
    return std::nullopt;
}

/// Decompose a positive rational a != 1 as r^k with k maximal (so r is the
/// primitive base). a < 1 decomposes 1/a and negates k.
std::pair<Rational, long long> primitive_power(Rational a) {
    long long sign = 1;
    if (a < Rational(1)) {
        a = Rational(1) / a;
        sign = -1;
    }
    for (int k = 62; k >= 2; --k) {
        const auto rn = a.num() >= 2 ? exact_llroot(a.num(), k)
                                     : std::optional<long long>(a.num()); // num == 1
        if (!rn) continue;
        if (a.den() == 1) {
            if (a.num() >= 2) return {Rational(*rn), sign * k};
            continue;
        }
        const auto rd = exact_llroot(a.den(), k);
        if (!rd) continue;
        if (a.num() == 1 && a.den() >= 2) return {Rational(1, *rd), sign * k};
        if (a.num() >= 2) return {Rational(*rn, *rd), sign * k};
    }
    return {a, sign};
}

/// ln(a) * ln(b)^-1 -> the exact rational log_b(a) when a and b are powers
/// of a common primitive base: ln(8)/ln(2) -> 3, ln(8)/ln(4) -> 3/2,
/// ln(1/8)/ln(2) -> -3. Returns nullopt when no exact fold exists.
std::optional<Rational> fold_ln_ratio(const Rational& a, const Rational& b) {
    if (!(a > Rational(0)) || !(b > Rational(0)) || a.is_one() || b.is_one()) {
        return std::nullopt;
    }
    const auto [ra, ka] = primitive_power(a);
    const auto [rb, kb] = primitive_power(b);
    if (ra != rb || kb == 0) {
        return std::nullopt;
    }
    return Rational(ka, kb);
}

Expr apply_mul_rules(const Expr& e) {
    if (e->kind() != Kind::Mul) {
        return e;
    }
    struct Entry {
        Expr base;
        Rational exp;
        Expr original;
    };
    std::vector<Entry> entries;      // factors eligible for combining
    std::vector<Expr> passthrough;   // symbolic-exponent factors, kept verbatim
    for (const Expr& f : e->args()) {
        auto [base, exp] = split_factor(f);
        if (exp) {
            entries.push_back({std::move(base), *exp, f});
        } else {
            passthrough.push_back(f);
        }
    }
    std::vector<Expr> out = std::move(passthrough);
    std::vector<bool> used(entries.size(), false);

    // ln(a) * ln(b)^-1 with numeric a, b folds to the exact rational log
    // when one exists (v0.4, §7): covers \log_b(a) as parsed and the
    // solver's ln(c)/ln(a) isolation results (2^x = 8 -> x = 3).
    const auto ln_number_arg = [](const Expr& base) -> const Rational* {
        if (base->kind() == Kind::Function && base->function() == FunctionId::Ln &&
            base->arg(0)->kind() == Kind::Number) {
            return &base->arg(0)->number();
        }
        return nullptr;
    };
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (used[i] || !(entries[i].exp == Rational(1))) continue;
        const Rational* a = ln_number_arg(entries[i].base);
        if (a == nullptr) continue;
        for (std::size_t j = 0; j < entries.size(); ++j) {
            if (i == j || used[j] || !(entries[j].exp == Rational(-1))) continue;
            const Rational* b = ln_number_arg(entries[j].base);
            if (b == nullptr) continue;
            if (const auto folded = fold_ln_ratio(*a, *b)) {
                used[i] = used[j] = true;
                out.push_back(make_num(*folded));
                break;
            }
        }
    }

    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (used[i]) {
            continue;
        }
        // Number-base exception (v0.4, §7): for a Number base only
        // non-integer exponents combine (2^(1/2)*2^(1/3) -> 2^(5/6)); a
        // plain Number factor never joins a radical group, so 2*sqrt(2)
        // stays 2*sqrt(2) and the radical normal form is confluent.
        const bool num_base = entries[i].base->kind() == Kind::Number;
        if (num_base && entries[i].exp.is_integer()) {
            out.push_back(entries[i].original);
            continue;
        }
        std::vector<std::size_t> group{i};
        for (std::size_t j = i + 1; j < entries.size(); ++j) {
            if (!used[j] && structurally_equal(entries[i].base, entries[j].base) &&
                !(num_base && entries[j].exp.is_integer())) {
                group.push_back(j);
            }
        }
        if (group.size() == 1) {
            out.push_back(entries[i].original);
            continue;
        }
        bool ok = true;
        Rational sum = entries[group[0]].exp;
        for (std::size_t gi = 1; gi < group.size() && ok; ++gi) {
            try {
                sum = sum + entries[group[gi]].exp;
            } catch (const OverflowError&) {
                ok = false;
            }
        }
        if (!ok) {
            for (const std::size_t gi : group) {
                out.push_back(entries[gi].original);
                used[gi] = true;
            }
            continue;
        }
        for (const std::size_t gi : group) {
            used[gi] = true;
        }
        out.push_back(make_pow(entries[i].base, make_num(sum)));
    }
    return make_mul(std::move(out));
}

/// n = s^2 * r with s maximal over small primes (trial division to 65536,
/// then a perfect-square check on the remainder); r is square-free for all
/// n whose square factors have a prime component below the bound.
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
        if (!__builtin_mul_overflow(c, c, &sq) && sq == n) {
            s *= c;
            n = 1;
            break;
        }
    }
    return {s, r * n};
}

/// Radical normal form (v0.4, §7): Pow(Number b > 0, non-integer p/q) ->
/// Number(b^m) * b^f with m = floor(p/q), f in (0,1); square roots also get
/// square-free radicands with rationalized denominators. Returns nullopt
/// when the node is already normal (or on 64-bit overflow).
std::optional<Expr> radical_normal_form(const Rational& b, const Rational& exp) {
    if (!(b > Rational(0)) || exp.is_integer()) {
        return std::nullopt;
    }
    const long long p = exp.num();
    const long long q = exp.den();
    const long long m = p >= 0 ? p / q : -((-p + q - 1) / q); // floor(p/q)
    const Rational f = exp - Rational(m);                     // in (0,1)

    Rational coeff(1);
    try {
        coeff = b.pow(m);
    } catch (const OverflowError&) {
        return std::nullopt;
    }

    Rational radicand = b;
    if (f == Rational(1, 2)) {
        // sqrt(n/d) = (sn/(sd*rd)) * sqrt(rn*rd): square-free radicand,
        // denominator rationalized.
        const auto [sn, rn] = square_free_split(b.num());
        const auto [sd, rd] = square_free_split(b.den());
        long long rad = 0;
        long long den = 0;
        if (__builtin_mul_overflow(rn, rd, &rad) || __builtin_mul_overflow(sd, rd, &den)) {
            return std::nullopt;
        }
        try {
            coeff = coeff * Rational(sn, den);
        } catch (const OverflowError&) {
            return std::nullopt;
        }
        radicand = Rational(rad);
    }

    if (m == 0 && radicand == b) {
        return std::nullopt; // already normal
    }
    if (radicand.is_one()) {
        return make_num(coeff);
    }
    return make_mul({make_num(coeff), make_pow(make_num(radicand), make_num(f))});
}

Expr apply_pow_rules(const Expr& e) {
    if (e->kind() != Kind::Pow) {
        return e;
    }
    const Expr& b = e->arg(0);
    const Expr& x = e->arg(1);
    // e^(ln u) -> u
    if (is_constant(b, ConstantId::E) && is_function(x, FunctionId::Ln)) {
        return x->arg(0);
    }
    const Rational* xr = as_number(x);
    if (xr == nullptr) {
        return e;
    }
    // Radical normal form for numeric bases (v0.4, §7): sqrt(8) -> 2*sqrt(2),
    // 1/sqrt(2) -> sqrt(2)/2, 2^(3/2) -> 2*sqrt(2).
    if (const Rational* br = as_number(b)) {
        if (auto normal = radical_normal_form(*br, *xr)) {
            return *normal;
        }
    }
    // abs(u)^(even integer) -> u^(even integer)
    if (is_function(b, FunctionId::Abs) && xr->is_integer() && !odd(xr->num())) {
        return make_pow(b->arg(0), x);
    }
    // Generalized (u^a)^b for rational Numbers a, b (DESIGN.md §7; integer b
    // is already a §2 factory fold, so b is non-integer here). The fold fires
    // exactly when it cannot restrict the real domain or change the value
    // under the §6 evaluator (domain extensions are acceptable):
    //   - a non-integer or an odd integer -> u^(a*b)  (u^a is already
    //     undefined for u < 0 when a is non-integer, and for odd integer a
    //     the sign survives, so equality holds wherever both are defined)
    //   - a an even integer: (u^a)^b == |u|^(a*b), so
    //       * a*b an even integer -> u^(a*b)      (abs is absorbed)
    //       * a*b an odd integer  -> abs(u)^(a*b) (sqrt(u^2) -> abs(u) is the
    //                                              a=2, b=1/2 instance)
    //       * a*b not an integer  -> no fold (u^(a*b) would restrict the
    //                                domain, e.g. (x^2)^(1/3))
    // On Rational overflow of a*b the node is left unchanged.
    if (b->kind() == Kind::Pow && !xr->is_integer()) {
        if (const Rational* a = as_number(b->arg(1))) {
            try {
                const Rational ab = *a * *xr;
                if (!a->is_integer() || odd(a->num())) {
                    return make_pow(b->arg(0), make_num(ab));
                }
                // a is an even integer.
                if (ab.is_integer()) {
                    if (odd(ab.num())) {
                        return make_pow(make_fn(FunctionId::Abs, b->arg(0)),
                                        make_num(ab));
                    }
                    return make_pow(b->arg(0), make_num(ab));
                }
            } catch (const OverflowError&) {
                // a*b unrepresentable: leave the node unchanged.
            }
        }
    }
    // (x*y)^n -> x^n * y^n for integer n
    if (b->kind() == Kind::Mul && xr->is_integer()) {
        std::vector<Expr> factors;
        factors.reserve(b->args().size());
        for (const Expr& f : b->args()) {
            factors.push_back(make_pow(f, x));
        }
        return make_mul(std::move(factors));
    }
    return e;
}

/// Structurally provable u >= 0 for all real values of u's variables (where
/// defined). Conservative: false only means "could not prove it".
bool provably_nonneg(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
            return !e->number().is_negative();
        case Kind::Constant:
            return true; // pi, e
        case Kind::Function:
            return e->function() == FunctionId::Abs || e->function() == FunctionId::Cosh;
        case Kind::Pow: {
            const Expr& b = e->arg(0);
            const Expr& p = e->arg(1);
            if (p->kind() == Kind::Number) {
                const Rational& r = p->number();
                if (r.is_integer() && r.num() % 2 == 0) {
                    return true; // even integer power (incl. negative)
                }
                if (!r.is_integer() && r.den() % 2 == 0) {
                    return true; // even root: principal value is nonnegative
                }
            }
            if (b->kind() == Kind::Constant && b->constant() == ConstantId::E) {
                return true; // e^u > 0
            }
            return provably_nonneg(b); // nonneg^anything, where defined
        }
        case Kind::Mul:
        case Kind::Add: {
            for (const Expr& a : e->args()) {
                if (!provably_nonneg(a)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

Expr apply_fn_rules(const Expr& e) {
    if (e->kind() != Kind::Function) {
        return e;
    }
    const FunctionId id = e->function();
    const Expr& u = e->arg(0);
    switch (id) {
        case FunctionId::Sin:
        case FunctionId::Cos:
        case FunctionId::Tan: {
            if (const auto r = pi_multiple(u)) {
                if (auto v = trig_special(id, *r)) {
                    return *v;
                }
                if (r->den() == 2 || r->den() == 1) {
                    return e; // tan at odd pi/2: leave unchanged, never throw
                }
            }
            if (auto pos = negated_argument(u)) {
                Expr inner = make_fn(id, std::move(*pos));
                return id == FunctionId::Cos ? inner : make_neg(std::move(inner));
            }
            if ((id == FunctionId::Sin && is_function(u, FunctionId::Asin)) ||
                (id == FunctionId::Cos && is_function(u, FunctionId::Acos)) ||
                (id == FunctionId::Tan && is_function(u, FunctionId::Atan))) {
                return u->arg(0); // valid where defined
            }
            return e;
        }
        case FunctionId::Asin:
        case FunctionId::Acos:
        case FunctionId::Atan: {
            if (auto v = inverse_trig_special(id, u)) {
                return *v;
            }
            return e;
        }
        case FunctionId::Sinh:
        case FunctionId::Tanh: {
            if (is_number_value(u, Rational(0))) {
                return make_num(0);
            }
            if (auto pos = negated_argument(u)) {
                return make_neg(make_fn(id, std::move(*pos)));
            }
            return e;
        }
        case FunctionId::Cosh: {
            if (is_number_value(u, Rational(0))) {
                return make_num(1);
            }
            if (auto pos = negated_argument(u)) {
                return make_fn(id, std::move(*pos));
            }
            return e;
        }
        case FunctionId::Ln: {
            if (is_number_value(u, Rational(1))) {
                return make_num(0);
            }
            if (is_constant(u, ConstantId::E)) {
                return make_num(1);
            }
            if (u->kind() == Kind::Pow && is_constant(u->arg(0), ConstantId::E)) {
                return u->arg(1); // ln(e^x) -> x
            }
            return e;
        }
        case FunctionId::Abs: {
            if (const Rational* n = as_number(u)) {
                return make_num(n->is_negative() ? -*n : *n);
            }
            if (provably_nonneg(u)) {
                // abs(pi) -> pi, abs(abs(u)) -> abs(u), abs(e^x) -> e^x,
                // abs(x^2) -> x^2, abs(sqrt(x)) -> sqrt(x), ...
                return u;
            }
            if (auto pos = negated_argument(u)) {
                return make_fn(FunctionId::Abs, std::move(*pos));
            }
            return e;
        }
    }
    return e;
}

Expr apply_rules(const Expr& n) {
    switch (n->kind()) {
        case Kind::Add: return apply_add_rules(n);
        case Kind::Mul: return apply_mul_rules(n);
        case Kind::Pow: return apply_pow_rules(n);
        case Kind::Function: return apply_fn_rules(n);
        default: return n;
    }
}

/// One bottom-up pass: simplify children, rebuild through the factories
/// (numeric folding happens there), then apply the local rules once.
Expr simplify_pass(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
        case Kind::Symbol:
        case Kind::Constant:
            return e;
        case Kind::Add:
        case Kind::Mul: {
            std::vector<Expr> children;
            children.reserve(e->args().size());
            for (const Expr& a : e->args()) {
                children.push_back(simplify_pass(a));
            }
            try {
                std::vector<Expr> rebuild_from = children;
                Expr rebuilt = e->kind() == Kind::Add ? make_add(std::move(rebuild_from))
                                                      : make_mul(std::move(rebuild_from));
                return apply_rules(rebuilt);
            } catch (const OverflowError&) {
            } catch (const DivisionByZeroError&) {
            }
            // The exact number fold overflowed. Preserve the children's
            // completed simplifications instead of reverting the whole subtree
            // (which stranded rules like ln(e) -> 1 in a sticky state). See
            // FINDING A / REVIEW-NOTES fix item 2.
            return raw_commutative(e->kind(), std::move(children));
        }
        case Kind::Pow: {
            Expr base = simplify_pass(e->arg(0));
            Expr exponent = simplify_pass(e->arg(1));
            // Pow keeps the classic whole-node containment: make_pow itself can
            // raise (integer overflow, 0^-1) with nothing partially-folded to
            // preserve, so falling back to the pre-pass node is correct and
            // matches DESIGN.md's division-by-zero containment.
            try {
                return apply_rules(make_pow(std::move(base), std::move(exponent)));
            } catch (const OverflowError&) {
            } catch (const DivisionByZeroError&) {
            }
            return e;
        }
        case Kind::Function: {
            Expr arg = simplify_pass(e->arg(0));
            try {
                return apply_rules(make_fn(e->function(), std::move(arg)));
            } catch (const OverflowError&) {
            } catch (const DivisionByZeroError&) {
            }
            return e;
        }
    }
    return e;
}

// ---------------------------------------------------------------------------
// expand helpers
// ---------------------------------------------------------------------------

/// Multiply the (already expanded) factors, distributing over Add factors.
/// Like terms are collected after every factor so the working set stays
/// bounded by the number of DISTINCT monomials instead of the raw cross-product
/// size (FINDING B) -- e.g. (a+..+j)^7 collapses each stage to <= 11440 terms
/// rather than materializing 10^7. The final result is identical to a full
/// distribution followed by like-term collection.
Expr distribute(const std::vector<Expr>& factors) {
    std::vector<Expr> terms{make_num(1)};
    for (const Expr& f : factors) {
        const std::vector<Expr> fterms =
            f->kind() == Kind::Add ? f->args() : std::vector<Expr>{f};
        std::vector<Expr> next;
        next.reserve(terms.size() * fterms.size());
        for (const Expr& t : terms) {
            for (const Expr& ft : fterms) {
                next.push_back(make_mul({t, ft}));
            }
        }
        terms = collect_like_terms(next);
    }
    return make_add(std::move(terms));
}

Expr expand_core(const Expr& e) {
    switch (e->kind()) {
        case Kind::Number:
        case Kind::Symbol:
        case Kind::Constant:
            return e;
        case Kind::Add: {
            std::vector<Expr> terms;
            terms.reserve(e->args().size());
            for (const Expr& a : e->args()) {
                terms.push_back(expand_core(a));
            }
            return make_add(std::move(terms));
        }
        case Kind::Mul: {
            std::vector<Expr> factors;
            factors.reserve(e->args().size());
            for (const Expr& a : e->args()) {
                factors.push_back(expand_core(a));
            }
            return distribute(factors);
        }
        case Kind::Pow: {
            Expr base = expand_core(e->arg(0));
            Expr exponent = expand_core(e->arg(1));
            if (const Rational* n = as_number(exponent);
                n != nullptr && n->is_integer() && base->kind() == Kind::Add &&
                n->num() >= 2 && n->num() <= kMaxExpandExponent) {
                const std::vector<Expr> repeated(static_cast<std::size_t>(n->num()), base);
                return distribute(repeated);
            }
            return make_pow(std::move(base), std::move(exponent));
        }
        case Kind::Function:
            return make_fn(e->function(), expand_core(e->arg(0)));
    }
    return e;
}

/// Decompose one Add term of an expanded expression as
/// coefficient * symbol^degree. Returns nullopt when the symbol occurs in a
/// non-polynomial position (inside a function, a non-symbol Pow base, an
/// exponent, or with a negative/non-integer power).
std::optional<std::pair<long long, Expr>> polynomial_term(const Expr& t,
                                                          std::string_view symbol) {
    const std::vector<Expr> factors =
        t->kind() == Kind::Mul ? t->args() : std::vector<Expr>{t};
    long long degree = 0;
    std::vector<Expr> coeff_factors;
    for (const Expr& f : factors) {
        if (!contains_symbol(f, symbol)) {
            coeff_factors.push_back(f);
            continue;
        }
        if (f->kind() == Kind::Symbol) {
            // Guard BEFORE adding: degree is non-negative, so this cannot wrap.
            if (degree >= kMaxPolynomialDegree) {
                return std::nullopt;
            }
            degree += 1;
            continue;
        }
        if (f->kind() == Kind::Pow && f->arg(0)->kind() == Kind::Symbol &&
            f->arg(0)->symbol_name() == symbol) {
            const Rational* r = as_number(f->arg(1));
            if (r == nullptr || !r->is_integer() || r->is_negative()) {
                return std::nullopt;
            }
            // Overflow-safe bound check BEFORE adding: both operands are
            // non-negative here, so a huge x-power can no longer wrap signed
            // int64 and misclassify the polynomial (FINDING 3).
            if (r->num() > kMaxPolynomialDegree - degree) {
                return std::nullopt;
            }
            degree += r->num();
            continue;
        }
        return std::nullopt;
    }
    return std::make_pair(degree, make_mul(std::move(coeff_factors)));
}

// ---------------------------------------------------------------------------
// factor helpers
// ---------------------------------------------------------------------------

/// Quadratic (in its unique-symbol candidate) with numeric coefficients and
/// rational roots -> leading * (x - r1) * (x - r2).
std::optional<Expr> factor_quadratic(const Expr& s) {
    for (const std::string& name : free_symbols(s)) {
        const auto pc = polynomial_coefficients(s, name);
        if (!pc || pc->size() != 3) {
            continue;
        }
        const Rational* a = as_number((*pc)[2]);
        const Rational* b = as_number((*pc)[1]);
        const Rational* c = as_number((*pc)[0]);
        if (a == nullptr || b == nullptr || c == nullptr) {
            continue;
        }
        const Rational disc = *b * *b - Rational(4) * *a * *c;
        if (disc.is_negative()) {
            continue;
        }
        const Expr root = make_pow(make_num(disc), make_num(Rational(1, 2)));
        if (root->kind() != Kind::Number) {
            continue; // irrational discriminant: no rational roots
        }
        const Rational d = root->number();
        const Rational r1 = (-*b + d) / (Rational(2) * *a);
        const Rational r2 = (-*b - d) / (Rational(2) * *a);
        const Expr x = make_sym(name);
        const Expr f1 = make_sub(x, make_num(r1));
        if (r1 == r2) {
            return make_mul({make_num(*a), make_pow(f1, make_num(2))});
        }
        return make_mul({make_num(*a), f1, make_sub(x, make_num(r2))});
    }
    return std::nullopt;
}

Rational rational_gcd(const Rational& a, const Rational& b) {
    const long long num = std::gcd(a.num(), b.num());
    const long long lcm = std::lcm(a.den(), b.den()); // dens fit; lcm may overflow
    if (lcm <= 0) {
        throw OverflowError("lcm overflow in rational gcd");
    }
    return Rational(num, lcm);
}

/// Extract common numeric content and common symbolic factors from an Add.
std::optional<Expr> factor_common(const Expr& s) {
    if (s->kind() != Kind::Add) {
        return std::nullopt;
    }
    struct Term {
        Rational coeff;
        std::vector<std::pair<Expr, Rational>> powers; // numeric-exponent factors
        std::vector<Expr> other;                       // kept verbatim
    };
    std::vector<Term> terms;
    for (const Expr& t : s->args()) {
        auto [coeff, rest] = split_term(t);
        Term term{coeff, {}, {}};
        const std::vector<Expr> factors =
            rest->kind() == Kind::Mul ? rest->args() : std::vector<Expr>{rest};
        for (const Expr& f : factors) {
            if (is_number_value(f, Rational(1))) {
                continue; // the pure-number term's placeholder rest
            }
            auto [base, exp] = split_factor(f);
            if (exp) {
                term.powers.emplace_back(std::move(base), *exp);
            } else {
                term.other.push_back(f);
            }
        }
        terms.push_back(std::move(term));
    }
    // Numeric content (positive; sign extracted only if every term is negative).
    Rational g = terms.front().coeff;
    for (const Term& t : terms) {
        g = rational_gcd(g, t.coeff);
    }
    if (g.is_negative()) {
        g = -g;
    }
    const bool all_negative = std::ranges::all_of(
        terms, [](const Term& t) { return t.coeff.is_negative(); });
    if (all_negative) {
        g = -g;
    }
    // Common symbolic bases: present in every term with a numeric exponent;
    // extract the minimum exponent when it is positive.
    std::vector<std::pair<Expr, Rational>> common;
    for (const auto& [base, exp] : terms.front().powers) {
        Rational min_exp = exp;
        bool everywhere = true;
        for (const Term& t : terms) {
            bool found = false;
            for (const auto& [b2, e2] : t.powers) {
                if (structurally_equal(base, b2)) {
                    found = true;
                    if (e2 < min_exp) {
                        min_exp = e2;
                    }
                    break;
                }
            }
            if (!found) {
                everywhere = false;
                break;
            }
        }
        if (everywhere && !min_exp.is_negative() && !min_exp.is_zero()) {
            common.emplace_back(base, min_exp);
        }
    }
    if (g.is_one() && common.empty()) {
        return std::nullopt;
    }
    // Rebuild the residual sum.
    std::vector<Expr> residual_terms;
    residual_terms.reserve(terms.size());
    for (const Term& t : terms) {
        std::vector<Expr> factors;
        factors.push_back(make_num(t.coeff / g));
        for (const auto& [base, exp] : t.powers) {
            Rational reduced = exp;
            for (const auto& [cb, ce] : common) {
                if (structurally_equal(base, cb)) {
                    reduced = exp - ce;
                    break;
                }
            }
            if (!reduced.is_zero()) {
                factors.push_back(make_pow(base, make_num(reduced)));
            }
        }
        factors.insert(factors.end(), t.other.begin(), t.other.end());
        residual_terms.push_back(make_mul(std::move(factors)));
    }
    std::vector<Expr> out;
    out.push_back(make_num(g));
    for (const auto& [base, exp] : common) {
        out.push_back(make_pow(base, make_num(exp)));
    }
    out.push_back(make_add(std::move(residual_terms)));
    return make_mul(std::move(out));
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Expr simplify(const Expr& e) {
    Expr current = e;
    for (int i = 0; i < kMaxPasses; ++i) {
        Expr next = simplify_pass(current);
        if (structurally_equal(next, current)) {
            return next;
        }
        current = std::move(next);
    }
    return current;
}

Equation simplify(const Equation& eq) {
    return {simplify(eq.lhs), simplify(eq.rhs)};
}

Expr expand(const Expr& e) {
    try {
        return simplify(expand_core(e));
    } catch (const OverflowError&) {
    } catch (const DivisionByZeroError&) {
    }
    return simplify(e);
}

Expr collect(const Expr& e, std::string_view symbol) {
    const Expr s = simplify(e);
    try {
        const std::vector<Expr> terms =
            s->kind() == Kind::Add ? s->args() : std::vector<Expr>{s};
        std::map<long long, std::vector<Expr>> parts;
        std::vector<Expr> remainder;
        for (const Expr& t : terms) {
            if (auto p = polynomial_term(t, symbol)) {
                parts[p->first].push_back(std::move(p->second));
            } else {
                remainder.push_back(t);
            }
        }
        std::vector<Expr> out = std::move(remainder);
        for (const auto& [degree, coeff_parts] : parts) {
            const Expr coeff = simplify(make_add(coeff_parts));
            if (coeff->kind() == Kind::Number && coeff->number().is_zero()) {
                continue;
            }
            if (degree == 0) {
                out.push_back(coeff);
            } else {
                out.push_back(make_mul(
                    {coeff, make_pow(make_sym(std::string(symbol)), make_num(degree))}));
            }
        }
        return make_add(std::move(out));
    } catch (const OverflowError&) {
    } catch (const DivisionByZeroError&) {
    }
    return s;
}

std::optional<std::vector<Expr>> polynomial_coefficients(const Expr& e,
                                                         std::string_view symbol) {
    try {
        const Expr expanded = expand(e);
        const std::vector<Expr> terms =
            expanded->kind() == Kind::Add ? expanded->args() : std::vector<Expr>{expanded};
        std::map<long long, std::vector<Expr>> parts;
        long long max_degree = 0;
        for (const Expr& t : terms) {
            const auto p = polynomial_term(t, symbol);
            if (!p) {
                return std::nullopt;
            }
            max_degree = std::max(max_degree, p->first);
            parts[p->first].push_back(p->second);
        }
        std::vector<Expr> coeffs;
        coeffs.reserve(static_cast<std::size_t>(max_degree) + 1);
        for (long long d = 0; d <= max_degree; ++d) {
            const auto it = parts.find(d);
            coeffs.push_back(it == parts.end() ? make_num(0)
                                               : simplify(make_add(it->second)));
        }
        while (coeffs.size() > 1 && coeffs.back()->kind() == Kind::Number &&
               coeffs.back()->number().is_zero()) {
            coeffs.pop_back();
        }
        return coeffs;
    } catch (const OverflowError&) {
        return std::nullopt;
    } catch (const DivisionByZeroError&) {
        return std::nullopt;
    }
}

Expr factor(const Expr& e) {
    const Expr s = simplify(e);
    try {
        if (auto quadratic = factor_quadratic(s)) {
            return *quadratic;
        }
        if (auto common = factor_common(s)) {
            return *common;
        }
    } catch (const OverflowError&) {
    } catch (const DivisionByZeroError&) {
    }
    return s;
}

} // namespace mathsolver
