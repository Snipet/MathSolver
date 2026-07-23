#pragma once

// Umbrella header for the MathSolver computer-algebra library.

#include <string_view>

#include "mathsolver/apart.hpp"
#include "mathsolver/derivative.hpp"
#include "mathsolver/discrete.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/fit.hpp"
#include "mathsolver/integrate.hpp"
#include "mathsolver/limit.hpp"
#include "mathsolver/ode.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/rational.hpp"
#include "mathsolver/series.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/stats.hpp"
#include "mathsolver/vector_calculus.hpp"
#include "mathsolver/solver.hpp"
#include "mathsolver/transform.hpp"

namespace mathsolver {

inline constexpr std::string_view k_version = "0.6.0";

} // namespace mathsolver
