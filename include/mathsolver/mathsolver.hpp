#pragma once

// Umbrella header for the MathSolver computer-algebra library.

#include <string_view>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/integrate.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"
#include "mathsolver/transform.hpp"

namespace mathsolver {

inline constexpr std::string_view k_version = "0.5.0";

} // namespace mathsolver
