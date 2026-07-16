#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// One solution of an equation.
struct Solution {
    Expr value;        ///< Exact expression, or a Number for numeric roots.
    bool exact;        ///< false for Newton/bisection approximations.
    std::string note;  ///< e.g. "principal solution; general: x = pi/6 + 2*pi*n".
};

struct SolveResult {
    enum class Status {
        Solved,          ///< Exact solutions found.
        NumericOnly,     ///< Only numeric approximations found.
        NoRealSolution,  ///< Provably no real solution (e.g. x^2 = -1).
        AllReals,        ///< Identity: every value of the symbol works.
        Unsolved         ///< Could not solve; see warnings.
    };

    Status status = Status::Unsolved;
    std::vector<Solution> solutions;
    std::string method;  ///< "linear", "quadratic formula", "isolation",
                         ///< "rational roots + quadratic", "numeric (Newton/bisection)".
    std::vector<std::string> warnings;
};

/// Controls for the numeric fallback root search.
struct NumericOptions {
    double lo = -100.0;
    double hi = 100.0;
    int scan_points = 4001;
    double tol = 1e-12;
    int max_iter = 128;
};

/// Solve `eq` for `symbol`. Strategy (DESIGN.md §9): trivial/identity check,
/// then polynomial path (linear, quadratic formula, rational-root peeling,
/// poly-in-x^k substitution), then isolation of a uniquely-occurring symbol
/// through Add/Mul/Pow/function-inverse layers, then the numeric fallback.
/// Exact candidates are verified by substitution; failures are dropped with
/// a warning.
SolveResult solve(const Equation& eq, std::string_view symbol,
                  const NumericOptions& opts = {});

/// Numeric-only root search on [opts.lo, opts.hi]: sign-change scan +
/// Newton (derivative from differentiate()) with bisection fallback,
/// even-multiplicity detection via |f| minima, root dedup and verification.
/// Roots are reported sorted ascending, exact = false.
SolveResult solve_numeric(const Equation& eq, std::string_view symbol,
                          const NumericOptions& opts = {});

} // namespace mathsolver
