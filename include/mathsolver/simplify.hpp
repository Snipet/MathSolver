#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Apply the safe rewrite rule set (DESIGN.md §7: numeric folding, like-term
/// and like-factor collection, power rules, exp/ln inverses, trig special
/// values and parity, sin^2+cos^2, abs rules) bottom-up, iterated to a
/// fixpoint (bounded). Idempotent: simplify(simplify(e)) == simplify(e).
Expr simplify(const Expr& e);

/// Simplify both sides.
Equation simplify(const Equation& eq);

/// Distribute Mul over Add and expand positive-integer powers of sums,
/// then simplify. expand((x+1)^2) == x^2 + 2x + 1.
Expr expand(const Expr& e);

/// Regroup as a polynomial in `symbol` with simplified coefficients:
/// collect(x*y + x*z + 1, "x") == (y+z)*x + 1. Terms not fitting a
/// polynomial shape in `symbol` are left as an additional additive
/// remainder. The result obeys the standard canonical form; degree-ordered
/// display is the printer's job.
Expr collect(const Expr& e, std::string_view symbol);

/// If expand(e) is a polynomial in `symbol` (finite non-negative integer
/// powers, coefficients free of `symbol`), return coefficients indexed by
/// degree (c[0] constant term, back() nonzero, all simplified). Otherwise
/// nullopt. The zero polynomial returns {0} (a single zero constant term).
std::optional<std::vector<Expr>> polynomial_coefficients(const Expr& e,
                                                         std::string_view symbol);

/// Best-effort factoring: pull common numeric/symbolic factors out of an
/// Add; split quadratics with rational roots into linear factors. Returns
/// the (simplified) input unchanged when it cannot do better. Never throws
/// merely because the expression is not factorable.
Expr factor(const Expr& e);

/// Best-effort rational-expression cancellation. Splits e (after simplify)
/// into numerator N and denominator D by the sign of integer Number Pow
/// exponents; if both are univariate polynomials in the same single symbol
/// with rational Number coefficients (degree <= 32 each), divides both by
/// their polynomial GCD (content included) and returns the normalized
/// quotient N'/D'. In every other case — no denominator, non-polynomial
/// parts, symbolic coefficients, more than one symbol, degree over the cap,
/// or 64-bit rational overflow anywhere — returns simplify(e) unchanged.
/// Never throws OverflowError; value-preserving wherever the original
/// denominator is nonzero (formal cancellation, same doctrine as x/x -> 1).
/// Idempotent.
Expr cancel(const Expr& e);

/// Cancel each side independently.
Equation cancel(const Equation& eq);

/// Combine the additive terms of e (after simplify) into a single fraction
/// N/D over their least common denominator: `1/x + 1/y -> (x + y)/(x*y)`,
/// `1/(x-1) + 1/(x+1) -> 2*x/((x-1)*(x+1))`, `a + 1/x -> (a*x + 1)/x`. The
/// LCD is the product of each distinct denominator base (compared
/// structurally, split by the sign of integer Number Pow exponents) raised
/// to the maximum multiplicity across terms; each numerator is scaled by
/// D/(its denominator) and the scaled numerators summed. No GCD, factoring,
/// or expansion — fully multivariate, denominator left factored. Returns
/// simplify(e) unchanged when nothing has a symbolic denominator, or on any
/// 64-bit overflow (never throws). Value-preserving where every original
/// denominator is nonzero (formal, same doctrine as x/x -> 1). Idempotent.
Expr together(const Expr& e);

/// Combine each side independently.
Equation together(const Equation& eq);

} // namespace mathsolver
