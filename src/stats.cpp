// Summary statistics (stats.hpp).
//
// When every data value is rational the statistics are computed EXACTLY over
// the rationals — mean and variance stay fractions and the standard deviation
// is a simplified radical (sqrt of the exact variance) — so `stats "1,2,4"`
// reports mean 7/3, not 2.333. Quartiles use the Moore & McCabe method (the
// median splits the data; for an odd count it is excluded from both halves).
// On non-rational data or a 64-bit overflow we fall back to double precision.

#include "mathsolver/stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

// Median of the sorted slice [lo, hi); works for Rational and double.
template <typename T>
T median_of(const std::vector<T>& s, int lo, int hi) {
    const int m = hi - lo;
    if (m % 2 == 1) return s[lo + m / 2];
    return (s[lo + m / 2 - 1] + s[lo + m / 2]) / T(2);
}

// Render a double compactly as a plain decimal (~6 sig figs, never scientific)
// so it round-trips through Rational::from_decimal_string.
std::string plain_decimal(double v) {
    if (!std::isfinite(v) || v == 0.0) return "0";
    const int mag = static_cast<int>(std::floor(std::log10(std::fabs(v))));
    int dec = std::clamp(6 - 1 - mag, 0, 12);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", dec, v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s.empty() ? "0" : s;
}

Expr num_expr(double v) { return make_num(Rational::from_decimal_string(plain_decimal(v))); }

struct Value {
    double d = 0.0;
    Rational r;
    bool has_r = false;
};

// Parse a coordinate to a numeric value plus, when it is a plain rational, its
// exact value. Returns false if it is not a finite constant.
bool parse_value(const std::string& s, Value& out) {
    try {
        const Expr e = simplify(parse_expression(s));
        if (e->kind() == Kind::Number) {
            out.r = e->number();
            out.has_r = true;
            out.d = out.r.to_double();
        } else {
            out.d = evaluate(e);
        }
        return std::isfinite(out.d);
    } catch (const Error&) {
        return false;
    }
}

void push(StatsResult& R, std::string label, Expr value) {
    R.items.push_back({std::move(label), std::move(value)});
}

// Exact path over the rationals. Throws OverflowError to signal a fallback.
StatsResult stats_exact(std::vector<Rational> xs) {
    StatsResult R;
    const int n = static_cast<int>(xs.size());
    R.n = n;
    std::sort(xs.begin(), xs.end());

    Rational sum(0);
    for (const Rational& x : xs) sum = sum + x;
    const Rational mean = sum / Rational(n);

    Rational ss(0); // Σ(x − mean)²
    for (const Rational& x : xs) {
        const Rational d = x - mean;
        ss = ss + d * d;
    }

    push(R, "n", make_num(static_cast<long long>(n)));
    push(R, "sum", make_num(sum));
    push(R, "mean", make_num(mean));
    push(R, "min", make_num(xs.front()));
    if (n >= 2) {
        const int half = n / 2;
        push(R, "Q1", make_num(median_of(xs, 0, half)));
        push(R, "median", make_num(median_of(xs, 0, n)));
        push(R, "Q3", make_num(median_of(xs, n - half, n)));
    } else {
        push(R, "median", make_num(median_of(xs, 0, n)));
    }
    push(R, "max", make_num(xs.back()));
    push(R, "range", make_num(xs.back() - xs.front()));
    if (n >= 2) {
        const int half = n / 2;
        const Rational iqr = median_of(xs, n - half, n) - median_of(xs, 0, half);
        push(R, "IQR", make_num(iqr));
        const Rational var_p = ss / Rational(n);
        const Rational var_s = ss / Rational(n - 1);
        push(R, "variance (pop)", make_num(var_p));
        push(R, "stdev (pop)", simplify(make_sqrt(make_num(var_p))));
        push(R, "variance (sample)", make_num(var_s));
        push(R, "stdev (sample)", simplify(make_sqrt(make_num(var_s))));
    }
    R.exact = true;
    R.status = StatsResult::Status::Ok;
    return R;
}

StatsResult stats_numeric(std::vector<double> xs) {
    StatsResult R;
    const int n = static_cast<int>(xs.size());
    R.n = n;
    std::sort(xs.begin(), xs.end());

    const double sum = std::accumulate(xs.begin(), xs.end(), 0.0);
    const double mean = sum / n;
    double ss = 0.0;
    for (double x : xs) ss += (x - mean) * (x - mean);

    push(R, "n", make_num(static_cast<long long>(n)));
    push(R, "sum", num_expr(sum));
    push(R, "mean", num_expr(mean));
    push(R, "min", num_expr(xs.front()));
    if (n >= 2) {
        const int half = n / 2;
        push(R, "Q1", num_expr(median_of(xs, 0, half)));
        push(R, "median", num_expr(median_of(xs, 0, n)));
        push(R, "Q3", num_expr(median_of(xs, n - half, n)));
    } else {
        push(R, "median", num_expr(median_of(xs, 0, n)));
    }
    push(R, "max", num_expr(xs.back()));
    push(R, "range", num_expr(xs.back() - xs.front()));
    if (n >= 2) {
        const int half = n / 2;
        push(R, "IQR", num_expr(median_of(xs, n - half, n) - median_of(xs, 0, half)));
        const double var_p = ss / n;
        const double var_s = ss / (n - 1);
        push(R, "variance (pop)", num_expr(var_p));
        push(R, "stdev (pop)", num_expr(std::sqrt(var_p)));
        push(R, "variance (sample)", num_expr(var_s));
        push(R, "stdev (sample)", num_expr(std::sqrt(var_s)));
    }
    R.exact = false;
    R.status = StatsResult::Status::Ok;
    return R;
}

} // namespace

StatsResult compute_stats(const std::vector<std::string>& data) {
    StatsResult R;
    if (data.empty()) {
        R.message = "no data values";
        return R;
    }
    std::vector<double> xd;
    std::vector<Rational> xr;
    bool all_rational = true;
    for (const std::string& tok : data) {
        Value v;
        if (!parse_value(tok, v)) {
            R.message = "could not read data value '" + tok + "'";
            return R;
        }
        xd.push_back(v.d);
        if (v.has_r) xr.push_back(v.r);
        else all_rational = false;
    }

    if (all_rational) {
        try {
            return stats_exact(std::move(xr));
        } catch (const OverflowError&) {
            // Exact sums overflowed long long — fall through to numeric.
        }
    }
    return stats_numeric(std::move(xd));
}

std::vector<std::string> parse_stat_data(std::string_view data) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        std::size_t a = cur.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) {
            cur.clear();
            return;
        }
        std::size_t b = cur.find_last_not_of(" \t\r\n");
        out.push_back(cur.substr(a, b - a + 1));
        cur.clear();
    };
    for (char ch : data) {
        if (ch == ',' || ch == ';' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') flush();
        else cur.push_back(ch);
    }
    flush();
    return out;
}

} // namespace mathsolver
