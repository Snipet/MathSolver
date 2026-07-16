#pragma once

#include <string_view>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// Symbolic derivative d(e)/d(symbol), returned already simplified.
/// Implements linearity, the n-ary product rule, the general power rule
/// d(u^v) = u^v * (v'*ln(u) + v*u'/u) with clean special cases for constant
/// exponents and base e, and the chain rule through every FunctionId
/// (table in DESIGN.md §8). d(abs(u)) = u'*u/abs(u) (undefined at u == 0).
Expr differentiate(const Expr& e, std::string_view symbol);

} // namespace mathsolver
