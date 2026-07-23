#include "mathsolver/inequality.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"

namespace mathsolver {

namespace {

std::optional<double> try_eval(const Expr& e, const Bindings& b) {
    try {
        const double v = evaluate(e, b);
        return std::isfinite(v) ? std::optional<double>(v) : std::nullopt;
    } catch (const Error&) {
        return std::nullopt;
    }
}

/// Does the constant `c <op> 0` hold?
bool constant_holds(IneqOp op, double c) {
    switch (op) {
        case IneqOp::Lt: return c < 0;
        case IneqOp::Le: return c <= 0;
        case IneqOp::Gt: return c > 0;
        case IneqOp::Ge: return c >= 0;
    }
    return false;
}

/// Split a `together`-combined expression into numerator and denominator: a
/// denominator factor is a Pow with a negative integer exponent.
void split_num_den(const Expr& g, Expr& num, Expr& den) {
    std::vector<Expr> nums, dens;
    auto handle = [&](const Expr& f) {
        if (f->kind() == Kind::Pow && f->arg(1)->kind() == Kind::Number &&
            f->arg(1)->number().is_integer() && f->arg(1)->number().is_negative()) {
            const long long e = -f->arg(1)->number().num();
            dens.push_back(e == 1 ? f->arg(0) : make_pow(f->arg(0), make_num(e)));
        } else {
            nums.push_back(f);
        }
    };
    if (g->kind() == Kind::Mul) {
        for (const Expr& f : g->args()) handle(f);
    } else {
        handle(g);
    }
    num = nums.empty() ? make_num(1) : (nums.size() == 1 ? nums[0] : make_mul(nums));
    den = dens.empty() ? make_num(1) : (dens.size() == 1 ? dens[0] : make_mul(dens));
    num = simplify(num);
    den = simplify(den);
}

struct Breakpoint {
    double val;
    Expr exact;
    bool pole; ///< a denominator root (excluded), vs a numerator zero
};

/// Real roots of `poly == 0` in `var`, as breakpoints. A constant polynomial
/// contributes none.
std::vector<Breakpoint> roots_of(const Expr& poly, std::string_view var, bool pole) {
    std::vector<Breakpoint> out;
    const Expr p = simplify(poly);
    if (free_symbols(p).count(std::string(var)) == 0) return out;
    const SolveResult sr = solve(Equation{p, make_num(0)}, var, {});
    if (sr.status != SolveResult::Status::Solved &&
        sr.status != SolveResult::Status::NumericOnly) {
        return out;
    }
    for (const Solution& s : sr.solutions) {
        const auto v = try_eval(s.value, {});
        if (v) out.push_back({*v, s.value, pole});
    }
    return out;
}

IneqResult unsolved(std::string message) {
    IneqResult r;
    r.status = IneqResult::Status::Unsolved;
    r.message = std::move(message);
    return r;
}

} // namespace

IneqResult solve_inequality(const Expr& lhs, const Expr& rhs, IneqOp op,
                            std::string_view var_hint) {
    const Expr f = simplify(make_sub(lhs, rhs));
    const std::set<std::string> syms = free_symbols(f);

    std::string var;
    if (!var_hint.empty()) {
        var = std::string(var_hint);
        for (const std::string& s : syms) {
            if (s != var) {
                return unsolved("inequality must be in a single variable");
            }
        }
    } else if (syms.size() > 1) {
        return unsolved("inequality must be in a single variable; name one with "
                        "a trailing , <var>");
    } else {
        var = syms.empty() ? "x" : *syms.begin();
    }

    IneqResult result;
    result.variable = var;

    // No dependence on the variable: a constant relation, true or false outright.
    if (syms.count(var) == 0) {
        const auto c = try_eval(f, {});
        if (!c) return unsolved("could not evaluate the inequality");
        result.status = constant_holds(op, *c) ? IneqResult::Status::AllReals
                                               : IneqResult::Status::NoSolution;
        return result;
    }

    // Breakpoints: numerator zeros (where f = 0) and denominator poles (where f
    // is undefined). Sign can only change across these.
    Expr num, den;
    split_num_den(together(f), num, den);
    if (!polynomial_coefficients(num, var) || !polynomial_coefficients(den, var)) {
        result.warnings.push_back(
            "non-rational inequality: sign analysis is numeric — verify near any "
            "domain boundaries (sqrt, log, …)");
    }

    std::vector<Breakpoint> bps = roots_of(num, var, false);
    for (const Breakpoint& b : roots_of(den, var, true)) bps.push_back(b);
    std::sort(bps.begin(), bps.end(),
              [](const Breakpoint& a, const Breakpoint& b) { return a.val < b.val; });

    // Merge coincident breakpoints; a zero that is also a pole is a hole (kept
    // as an excluded pole).
    std::vector<Breakpoint> pts;
    for (const Breakpoint& b : bps) {
        if (!pts.empty() &&
            std::abs(b.val - pts.back().val) <= 1e-9 * (1.0 + std::abs(b.val))) {
            if (b.pole) pts.back().pole = true;
        } else {
            pts.push_back(b);
        }
    }
    const std::size_t k = pts.size();

    // Sign of f at a point, retried at a couple of nearby offsets if the first
    // sample lands outside the domain.
    auto region_satisfied = [&](double nominal, bool unbounded_low,
                                bool unbounded_high) -> bool {
        std::vector<double> tries{nominal};
        if (unbounded_low) tries.insert(tries.end(), {nominal - 1, nominal - 9});
        if (unbounded_high) tries.insert(tries.end(), {nominal + 1, nominal + 9});
        for (double x : tries) {
            const auto v = try_eval(f, Bindings{{var, x}});
            if (!v) continue;
            if (*v > 0) return op == IneqOp::Gt || op == IneqOp::Ge;
            if (*v < 0) return op == IneqOp::Lt || op == IneqOp::Le;
        }
        return false; // undefined across the region (or exactly zero)
    };

    // "in" flags for the alternating sequence R0, B0, R1, B1, …, B_{k-1}, R_k.
    std::vector<bool> region_in(k + 1, false);
    for (std::size_t i = 0; i <= k; ++i) {
        double sample;
        bool ul = false, uh = false;
        if (k == 0) {
            sample = 0.0;
            ul = uh = true;
        } else if (i == 0) {
            sample = pts.front().val - 1.0;
            ul = true;
        } else if (i == k) {
            sample = pts.back().val + 1.0;
            uh = true;
        } else {
            sample = 0.5 * (pts[i - 1].val + pts[i].val);
        }
        region_in[i] = region_satisfied(sample, ul, uh);
    }
    const bool nonstrict = op == IneqOp::Le || op == IneqOp::Ge;
    std::vector<bool> break_in(k, false);
    for (std::size_t j = 0; j < k; ++j) break_in[j] = nonstrict && !pts[j].pole;

    // Flatten to nodes and stitch maximal runs of "in" into intervals.
    struct Node {
        bool is_break;
        std::size_t idx;
        bool in;
    };
    std::vector<Node> nodes;
    for (std::size_t i = 0; i <= k; ++i) {
        nodes.push_back({false, i, region_in[i]});
        if (i < k) nodes.push_back({true, i, break_in[i]});
    }

    for (std::size_t a = 0; a < nodes.size();) {
        if (!nodes[a].in) {
            ++a;
            continue;
        }
        std::size_t b = a;
        while (b + 1 < nodes.size() && nodes[b + 1].in) ++b;
        SolutionInterval iv;
        // Left endpoint from the run's first node.
        const Node& L = nodes[a];
        if (!L.is_break && L.idx == 0) {
            iv.lo_inf = true;
        } else if (L.is_break) {
            iv.lo = pts[L.idx].exact;
            iv.lo_closed = true;
        } else {
            iv.lo = pts[L.idx - 1].exact; // region i>0 left boundary
            iv.lo_closed = false;
        }
        // Right endpoint from the run's last node.
        const Node& R = nodes[b];
        if (!R.is_break && R.idx == k) {
            iv.hi_inf = true;
        } else if (R.is_break) {
            iv.hi = pts[R.idx].exact;
            iv.hi_closed = true;
        } else {
            iv.hi = pts[R.idx].exact; // region i<k right boundary
            iv.hi_closed = false;
        }
        result.intervals.push_back(iv);
        a = b + 1;
    }

    if (result.intervals.empty()) {
        result.status = IneqResult::Status::NoSolution;
    } else if (result.intervals.size() == 1 && result.intervals[0].lo_inf &&
               result.intervals[0].hi_inf) {
        result.status = IneqResult::Status::AllReals;
        result.intervals.clear();
    } else {
        result.status = IneqResult::Status::Solved;
    }
    return result;
}

std::string format_solution_set(const IneqResult& r, bool latex) {
    const PrintStyle style = latex ? PrintStyle::LaTeX : PrintStyle::Plain;
    const std::string in = latex ? " \\in " : " ∈ ";
    switch (r.status) {
        case IneqResult::Status::AllReals:
            return latex ? r.variable + " \\in \\mathbb{R}"
                         : "all real " + r.variable;
        case IneqResult::Status::NoSolution:
            return latex ? r.variable + " \\in \\varnothing" : "no solution";
        case IneqResult::Status::Unsolved:
            return r.message.empty() ? "unable to solve" : r.message;
        case IneqResult::Status::Solved:
            break;
    }

    const std::string neg_inf = latex ? "-\\infty" : "-∞";
    const std::string pos_inf = latex ? "\\infty" : "∞";
    const std::string cup = latex ? " \\cup " : " ∪ ";

    std::string set;
    for (std::size_t i = 0; i < r.intervals.size(); ++i) {
        const SolutionInterval& iv = r.intervals[i];
        if (i > 0) set += cup;
        const bool point = !iv.lo_inf && !iv.hi_inf && iv.lo_closed && iv.hi_closed &&
                           to_string(iv.lo, style) == to_string(iv.hi, style);
        if (point) {
            set += latex ? "\\{" + to_string(iv.lo, style) + "\\}"
                         : "{" + to_string(iv.lo, style) + "}";
            continue;
        }
        set += iv.lo_closed && !iv.lo_inf ? "[" : "(";
        set += iv.lo_inf ? neg_inf : to_string(iv.lo, style);
        set += ", ";
        set += iv.hi_inf ? pos_inf : to_string(iv.hi, style);
        set += iv.hi_closed && !iv.hi_inf ? "]" : ")";
    }
    return r.variable + in + set;
}

} // namespace mathsolver
