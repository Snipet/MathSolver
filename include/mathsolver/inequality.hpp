#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// A relational operator for an inequality `lhs <op> rhs`.
enum class IneqOp { Lt, Le, Gt, Ge };

/// One interval of a solution set. An infinite end ignores its Expr endpoint
/// and is always open. A single point is lo == hi with both ends closed.
struct SolutionInterval {
    bool lo_inf = false;
    bool hi_inf = false;
    Expr lo; ///< exact left endpoint (valid unless lo_inf)
    Expr hi; ///< exact right endpoint (valid unless hi_inf)
    bool lo_closed = false;
    bool hi_closed = false;
};

/// The solution set of a one-variable inequality.
struct IneqResult {
    enum class Status { Solved, AllReals, NoSolution, Unsolved };

    Status status = Status::Unsolved;
    /// Disjoint intervals in ascending order (Solved only).
    std::vector<SolutionInterval> intervals;
    std::string variable;
    /// Why the solver gave up (Unsolved), or extra context.
    std::string message;
    std::vector<std::string> warnings;
};

/// Solve `lhs <op> rhs` for its one free variable (inferred when `var_hint` is
/// empty). The method: combine `lhs - rhs` over a common denominator, take the
/// real roots of the numerator (zeros) and denominator (poles) as breakpoints,
/// sign-test the field between them, and assemble the intervals where the
/// relation holds — exact for polynomial and rational inequalities.
IneqResult solve_inequality(const Expr& lhs, const Expr& rhs, IneqOp op,
                            std::string_view var_hint = "");

/// Render a solution set as `x ∈ (-2, 2)` (plain) or LaTeX (`x \in (-2, 2)`).
std::string format_solution_set(const IneqResult& r, bool latex);

} // namespace mathsolver
