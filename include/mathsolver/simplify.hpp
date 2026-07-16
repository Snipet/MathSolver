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

} // namespace mathsolver
