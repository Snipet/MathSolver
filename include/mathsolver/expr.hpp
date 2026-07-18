#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/rational.hpp"

namespace mathsolver {

class ExprNode;

/// Immutable, shared expression handle. See DESIGN.md §2 for the canonical
/// form invariants every constructed Expr satisfies.
using Expr = std::shared_ptr<const ExprNode>;

enum class Kind { Number, Symbol, Constant, Add, Mul, Pow, Function };

enum class ConstantId { Pi, E, I };

enum class FunctionId {
    Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Asinh, Acosh, Atanh,
    Gamma, Digamma, Erf, Erfc, Fib, Harmonic,
    Ln, Abs
};

/// Lowercase canonical name: "sin", "asin", "ln", "abs", ...
std::string_view function_name(FunctionId id);

/// Inverse of function_name; also accepts the "arc" spellings
/// ("arcsin" -> Asin). Returns nullopt for unknown names. Note that names
/// the parser rewrites (exp, sqrt, log, sec, csc, cot) are NOT FunctionIds
/// and return nullopt here.
std::optional<FunctionId> function_from_name(std::string_view name);

/// Single tagged AST node. Construct only through the factory functions
/// below (they enforce the canonical form); the constructors are internal.
class ExprNode {
public:
    Kind kind() const noexcept { return kind_; }

    /// Kind::Number only.
    const Rational& number() const;
    /// Kind::Symbol only.
    const std::string& symbol_name() const;
    /// Kind::Constant only.
    ConstantId constant() const;
    /// Kind::Function only.
    FunctionId function() const;

    /// Add/Mul: >= 2 sorted args; Pow: exactly {base, exponent};
    /// Function: exactly 1; other kinds: empty.
    const std::vector<Expr>& args() const noexcept { return args_; }
    const Expr& arg(std::size_t i) const { return args_.at(i); }

    // -- Internal: use the factories. Public only for std::make_shared. --
    ExprNode(Kind kind, Rational number, std::string symbol, ConstantId constant,
             FunctionId function, std::vector<Expr> args);

private:
    Kind kind_;
    Rational number_;
    std::string symbol_;
    ConstantId constant_;
    FunctionId function_;
    std::vector<Expr> args_;
};

// ---------------------------------------------------------------------------
// Factories (light canonicalization only — see DESIGN.md §2 for exact rules).
// ---------------------------------------------------------------------------

Expr make_num(long long n);
Expr make_num(const Rational& r);
Expr make_sym(std::string name);
Expr make_const(ConstantId id);

/// Flattens nested Adds, folds Number terms exactly, drops a zero fold,
/// sorts by compare_expr. 0 args -> 0; 1 arg -> that arg.
Expr make_add(std::vector<Expr> terms);
/// Flattens, folds Numbers, any literal 0 -> 0, drops 1s, sorts.
/// 0 args -> 1; 1 arg -> that arg.
Expr make_mul(std::vector<Expr> factors);
/// e==0 -> 1 (0^0 == 1 by convention); e==1 -> b; b==1 -> 1; b==0 with
/// positive rational e -> 0; exact numeric folds per DESIGN.md §2 (rational
/// exponents stay symbolic on overflow; negative bases fold for odd root
/// indices only). Structural folds per §2: (u^r)^s -> u^(r*s) for Number
/// r,s with s integer; (c*rest)^s with integer Number s and Number c ->
/// c^s * rest^s.
Expr make_pow(Expr base, Expr exponent);
/// No auto-evaluation (sin(0) stays sin(0); simplify handles it).
Expr make_fn(FunctionId id, Expr arg);

// Sugar (all reduce to the canonical node set):
Expr make_neg(Expr e);              // -e        -> Mul(-1, e)
Expr make_sub(Expr a, Expr b);      // a - b     -> Add(a, Mul(-1, b))
Expr make_div(Expr a, Expr b);      // a / b     -> Mul(a, Pow(b, -1))
Expr make_sqrt(Expr e);             // sqrt(e)   -> Pow(e, 1/2)
Expr make_exp(Expr e);              // exp(e)    -> Pow(E, e)

// Convenience operators forwarding to the factories (found via ADL).
Expr operator+(const Expr& a, const Expr& b);
Expr operator-(const Expr& a, const Expr& b);
Expr operator*(const Expr& a, const Expr& b);
Expr operator/(const Expr& a, const Expr& b);
Expr operator-(const Expr& a);

// ---------------------------------------------------------------------------
// Core utilities (implemented in expr.cpp).
// ---------------------------------------------------------------------------

/// Strict total order for the canonical form; returns -1/0/+1.
/// Kind rank: Number < Constant < Symbol < Pow < Function < Mul < Add;
/// tie-breaks per DESIGN.md §2.
int compare_expr(const Expr& a, const Expr& b);

bool structurally_equal(const Expr& a, const Expr& b);

/// Consistent with structurally_equal.
std::size_t hash_expr(const Expr& e);

bool contains_symbol(const Expr& e, std::string_view name);

std::set<std::string> free_symbols(const Expr& e);

/// Replace every occurrence of the symbol `name` with `replacement`,
/// rebuilding through the factories (so the result is canonical).
Expr substitute(const Expr& e, std::string_view name, const Expr& replacement);

/// S-expression dump for tests/debugging, e.g. "(add 2 (mul 3 x))".
/// Numbers print as "2" or "3/2"; constants as "pi"/"e"; functions as
/// "(sin x)"; pow as "(pow x 2)".
std::string debug_string(const Expr& e);

/// An equation lhs = rhs. Not an Expr node.
struct Equation {
    Expr lhs;
    Expr rhs;
};

} // namespace mathsolver
