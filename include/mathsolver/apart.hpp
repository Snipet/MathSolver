#pragma once

// Partial-fraction expansion over the rationals.

#include <string_view>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Decompose a rational function of `var` into a polynomial part plus a sum
/// of partial fractions with linear and irreducible-quadratic denominators:
///
///   apart((3x+2)/((x+1)(x+2)), "x")  ==  4/(x + 2) - 1/(x + 1)
///
/// The denominator's factored structure is preserved (each `(...)^-k` factor
/// is factored individually by rational-root deflation), improper inputs are
/// reduced by polynomial division first, and the fraction coefficients are
/// found by undetermined coefficients through the exact linear-system solver
/// — so numerators may contain symbolic parameters.  A sum decomposes
/// termwise; a term with no denominator in `var` is returned unchanged.
///
/// Throws Error when the input is not a rational function of `var`, when a
/// denominator has non-numeric coefficients, or when a denominator factor
/// cannot be split into linear and quadratic factors over the rationals
/// (e.g. an expanded quartic with two irreducible quadratic factors).
Expr apart(const Expr& e, std::string_view var);

} // namespace mathsolver
