#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/expr.hpp"

namespace mathsolver {

/// One labelled summary statistic (its value kept as an Expr so exact results
/// render as fractions / radicals: mean 7/3, stdev sqrt(6)/3).
struct StatItem {
    std::string label;
    Expr value;
};

struct StatsResult {
    enum class Status { Ok, Error };

    Status status = Status::Error;
    /// Ordered summary statistics (n, sum, mean, quartiles, spread, …).
    std::vector<StatItem> items;
    int n = 0;
    /// True when every statistic was computed exactly over the rationals.
    bool exact = false;
    std::string message;
};

/// Summary statistics of a data list. Each value is parsed as a constant
/// expression; when they are all rational the statistics are exact (mean and
/// variance stay fractions, standard deviation is a simplified radical),
/// falling back to double precision on non-rational data or 64-bit overflow.
StatsResult compute_stats(const std::vector<std::string>& data);

/// Split a data blob on commas, semicolons, or whitespace into value strings.
std::vector<std::string> parse_stat_data(std::string_view data);

} // namespace mathsolver
