#pragma once

#include <map>
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
        Solved,          ///< Exact real solutions found.
        SolvedComplex,   ///< No real solutions; exact complex roots reported
                         ///< (polynomial paths with a negative discriminant).
        NumericOnly,     ///< Only numeric approximations found.
        NoRealSolution,  ///< Provably no real solution (e.g. sin(x) = 2).
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

/// Result of solving a system of equations linear in the requested symbols
/// (DESIGN.md §9b). Other free symbols act as symbolic parameters.
struct SystemSolveResult {
    enum class Status {
        Solved,           ///< Unique solution for every requested symbol.
        NoSolution,       ///< Inconsistent system (a row reduces to 0 = c, c != 0).
        Underdetermined,  ///< Consistent; some requested symbols are free.
        Unsolved          ///< Not linear in the requested symbols, or verification failed.
    };

    Status status = Status::Unsolved;
    /// Solved: one simplified Expr per requested symbol, free of all
    /// requested symbols. Underdetermined: entries only for pivot symbols;
    /// they may reference the free symbols.
    std::map<std::string, Expr> values;
    /// Underdetermined only: requested symbols with no pivot (absent from
    /// `values`).
    std::vector<std::string> free_variables;
    std::string method;  ///< "gaussian elimination"
    std::vector<std::string> warnings;
};

/// Solve `eqs` for `symbols` by Gaussian elimination over exact Expr
/// arithmetic: joint-linearity extraction (rejects cross-terms like x*y),
/// symbolic pivots allowed with a "valid only when ... != 0" warning,
/// solutions verified by substitution per the §9.5 doctrine.
SystemSolveResult solve_system(const std::vector<Equation>& eqs,
                               const std::vector<std::string>& symbols);

} // namespace mathsolver
