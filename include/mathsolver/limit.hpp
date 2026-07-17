#pragma once

// Limits of single-variable expressions: exact where the structure allows
// (direct substitution, L'Hôpital on 0/0 quotients, the x = 1/u reduction at
// infinity), numeric extrapolation as an honest fallback.

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

struct LimitResult {
    enum class Status {
        Exact,        ///< value holds the exact limit.
        Numeric,      ///< value holds a Number estimated by extrapolation.
        Diverges,     ///< |f| grows without bound; sign is +1/-1 (0 unsigned).
        DoesNotExist, ///< left and right limits disagree (see warnings).
        Unsolved,     ///< no method applied / no numeric convergence.
    };
    Status status = Status::Unsolved;
    Expr value;  ///< Exact/Numeric only.
    int sign = 0;
    std::string method;
    std::vector<std::string> warnings;
};

/// Limit of f as var -> point. `direction` is -1 (left), +1 (right), or 0
/// (two-sided: both one-sided limits must agree). `point` must be numeric.
LimitResult limit(const Expr& f, std::string_view var, const Expr& point,
                  int direction = 0);

/// Limit of f as var -> +inf (positive = true) or -inf.
LimitResult limit_at_infinity(const Expr& f, std::string_view var,
                              bool positive);

} // namespace mathsolver
