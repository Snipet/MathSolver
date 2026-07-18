// Multivariate and vector calculus (vector_calculus.hpp).

#include "mathsolver/vector_calculus.hpp"

#include <cmath>
#include <optional>
#include <string>

#include "mathsolver/derivative.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

void require_nonempty(const std::vector<std::string>& vars) {
    if (vars.empty()) {
        throw Error("vector calculus: at least one variable is required");
    }
    for (std::size_t i = 0; i < vars.size(); ++i) {
        for (std::size_t j = i + 1; j < vars.size(); ++j) {
            if (vars[i] == vars[j]) {
                throw Error("vector calculus: duplicate variable '" + vars[i] +
                            "' in the variable list");
            }
        }
    }
}

void require_match(const ExprVec& field, const std::vector<std::string>& vars,
                   const char* op) {
    if (field.size() != vars.size()) {
        throw Error(std::string(op) + ": the field has " +
                    std::to_string(field.size()) + " components but " +
                    std::to_string(vars.size()) + " variables were given");
    }
}

/// Square root of a nonnegative exact rational, when it is itself rational.
/// The candidate check divides instead of squaring: c*c overflows (UB) for
/// values near LLONG_MAX, which the rest of the library never risks.
std::optional<Rational> exact_sqrt(const Rational& r) {
    if (r.num() < 0) {
        return std::nullopt;
    }
    auto isqrt = [](long long n) -> std::optional<long long> {
        if (n < 0) return std::nullopt;
        if (n == 0) return 0;
        const long long s = static_cast<long long>(
            std::llround(std::sqrt(static_cast<double>(n))));
        for (long long c : {s - 1, s, s + 1}) {
            if (c > 0 && n / c == c && n % c == 0) return c;
        }
        return std::nullopt;
    };
    const auto n = isqrt(r.num());
    const auto d = isqrt(r.den());
    if (n && d) {
        return Rational{*n, *d};
    }
    return std::nullopt;
}

} // namespace

ExprVec gradient(const Expr& f, const std::vector<std::string>& vars) {
    require_nonempty(vars);
    ExprVec g;
    g.reserve(vars.size());
    for (const std::string& v : vars) {
        g.push_back(simplify(differentiate(f, v)));
    }
    return g;
}

Expr divergence(const ExprVec& field, const std::vector<std::string>& vars) {
    require_nonempty(vars);
    require_match(field, vars, "divergence");
    std::vector<Expr> terms;
    for (std::size_t i = 0; i < vars.size(); ++i) {
        terms.push_back(differentiate(field[i], vars[i]));
    }
    return simplify(make_add(std::move(terms)));
}

ExprVec curl(const ExprVec& field, const std::vector<std::string>& vars) {
    if (vars.size() != 3 || field.size() != 3) {
        throw Error("curl: the vector curl needs a 3-component field over 3 "
                    "variables (use curl2d for a planar field)");
    }
    const auto d = [&](std::size_t comp, std::size_t var) {
        return differentiate(field[comp], vars[var]);
    };
    // (∂F3/∂y − ∂F2/∂z, ∂F1/∂z − ∂F3/∂x, ∂F2/∂x − ∂F1/∂y)
    return {simplify(make_sub(d(2, 1), d(1, 2))),
            simplify(make_sub(d(0, 2), d(2, 0))),
            simplify(make_sub(d(1, 0), d(0, 1)))};
}

Expr curl2d(const ExprVec& field, const std::vector<std::string>& vars) {
    if (vars.size() != 2 || field.size() != 2) {
        throw Error("curl2d: the scalar curl needs a 2-component field over 2 "
                    "variables");
    }
    return simplify(make_sub(differentiate(field[1], vars[0]),
                             differentiate(field[0], vars[1])));
}

Expr laplacian(const Expr& f, const std::vector<std::string>& vars) {
    require_nonempty(vars);
    std::vector<Expr> terms;
    for (const std::string& v : vars) {
        terms.push_back(differentiate(differentiate(f, v), v));
    }
    return simplify(make_add(std::move(terms)));
}

ExprMat jacobian(const ExprVec& field, const std::vector<std::string>& vars) {
    require_nonempty(vars);
    if (field.empty()) {
        throw Error("jacobian: the field has no components");
    }
    ExprMat j;
    for (const Expr& fi : field) {
        std::vector<Expr> row;
        for (const std::string& v : vars) {
            row.push_back(simplify(differentiate(fi, v)));
        }
        j.push_back(std::move(row));
    }
    return j;
}

ExprMat hessian(const Expr& f, const std::vector<std::string>& vars) {
    require_nonempty(vars);
    ExprMat h;
    for (const std::string& vi : vars) {
        const Expr di = differentiate(f, vi);
        std::vector<Expr> row;
        for (const std::string& vj : vars) {
            row.push_back(simplify(differentiate(di, vj)));
        }
        h.push_back(std::move(row));
    }
    return h;
}

Expr directional_derivative(const Expr& f, const std::vector<std::string>& vars,
                            const ExprVec& direction) {
    require_nonempty(vars);
    if (direction.size() != vars.size()) {
        throw Error("directional derivative: the direction has " +
                    std::to_string(direction.size()) +
                    " components but " + std::to_string(vars.size()) +
                    " variables were given");
    }
    // Normalize whenever the direction is constant (no free symbols): exact
    // rational scale for perfect squares, symbolic 1/sqrt otherwise. A
    // direction with free symbols stays un-normalized (documented).
    std::vector<Expr> sq;
    for (const Expr& c : direction) {
        sq.push_back(make_pow(c, make_num(2)));
    }
    const Expr norm2 = simplify(make_add(std::move(sq)));
    Expr scale = make_num(1);
    if (free_symbols(norm2).empty()) {
        if (norm2->kind() == Kind::Number) {
            if (norm2->number().is_zero()) {
                throw Error("directional derivative: the direction is the "
                            "zero vector");
            }
            if (norm2->number() < Rational{0}) {
                throw Error("directional derivative: the direction has a "
                            "negative squared norm (not a real direction)");
            }
            const auto root = exact_sqrt(norm2->number());
            scale = root ? make_div(make_num(1), make_num(*root))
                         : make_div(make_num(1), make_sqrt(norm2));
        } else {
            // Constant but irrational (e.g. (pi, 0)): 1/sqrt(norm2), which
            // simplify folds where the power rules allow.
            scale = make_div(make_num(1), make_sqrt(norm2));
        }
    }
    const ExprVec g = gradient(f, vars);
    std::vector<Expr> terms;
    for (std::size_t i = 0; i < vars.size(); ++i) {
        terms.push_back(make_mul({g[i], direction[i]}));
    }
    return simplify(make_mul({scale, make_add(std::move(terms))}));
}

// --- rendering --------------------------------------------------------------

std::string vec_to_string(const ExprVec& v, PrintStyle style) {
    if (style == PrintStyle::LaTeX) {
        std::string out = "\\begin{pmatrix} ";
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i > 0) out += " \\\\ ";
            out += to_string(v[i], style);
        }
        return out + " \\end{pmatrix}";
    }
    std::string out = "(";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += ", ";
        out += to_string(v[i], style);
    }
    return out + ")";
}

std::string mat_to_string(const ExprMat& m, PrintStyle style) {
    if (style == PrintStyle::LaTeX) {
        std::string out = "\\begin{pmatrix} ";
        for (std::size_t i = 0; i < m.size(); ++i) {
            if (i > 0) out += " \\\\ ";
            for (std::size_t j = 0; j < m[i].size(); ++j) {
                if (j > 0) out += " & ";
                out += to_string(m[i][j], style);
            }
        }
        return out + " \\end{pmatrix}";
    }
    std::string out = "[";
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (i > 0) out += "; ";
        for (std::size_t j = 0; j < m[i].size(); ++j) {
            if (j > 0) out += ", ";
            out += to_string(m[i][j], style);
        }
    }
    return out + "]";
}

} // namespace mathsolver
