// Least-squares regression (fit.hpp).
//
// Polynomial fits are solved EXACTLY over the rationals: the normal equations
// XᵀX·c = Xᵀy reduce to power sums S_m = Σ xᵢ^m and moment sums T_a = Σ xᵢ^a·yᵢ,
// all exact when the data are rational, and Gaussian elimination over Rational
// yields exact coefficients — so perfectly collinear data returns 2·x + 1, not
// a rounded decimal. On integer overflow (large data / high degree) or when a
// value is not rational, we fall back to double-precision least squares.
//
// Exp/Power/Log are inherently transcendental, so they are fit numerically by
// linearizing (regressing on ln y and/or ln x) and reported to a few decimals.

#include "mathsolver/fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/simplify.hpp"

namespace mathsolver {

namespace {

// --- Generic Gaussian elimination, exact for Rational, stable for double. ----

double magnitude(const Rational& r) { return std::fabs(r.to_double()); }
double magnitude(double d) { return std::fabs(d); }
bool exactly_zero(const Rational& r) { return r.is_zero(); }
bool exactly_zero(double d) { return d == 0.0; }

// Solve A·x = b (A is n×n). Returns nullopt if the matrix is singular. For
// Rational, arithmetic is exact and may throw OverflowError (caught upstream).
template <typename T>
std::optional<std::vector<T>> gauss_solve(std::vector<std::vector<T>> a, std::vector<T> b) {
    const int n = static_cast<int>(b.size());
    for (int col = 0; col < n; ++col) {
        int pivot = -1;
        double best = 0.0;
        for (int r = col; r < n; ++r) {
            if (exactly_zero(a[r][col])) continue;
            const double m = magnitude(a[r][col]);
            if (pivot < 0 || m > best) {
                best = m;
                pivot = r;
            }
        }
        if (pivot < 0) return std::nullopt; // singular
        std::swap(a[col], a[pivot]);
        std::swap(b[col], b[pivot]);
        const T diag = a[col][col];
        for (int r = 0; r < n; ++r) {
            if (r == col) continue;
            if (exactly_zero(a[r][col])) continue;
            const T factor = a[r][col] / diag;
            for (int k = col; k < n; ++k) a[r][k] = a[r][k] - factor * a[col][k];
            b[r] = b[r] - factor * b[col];
        }
    }
    std::vector<T> x(n);
    for (int i = 0; i < n; ++i) x[i] = b[i] / a[i][i];
    return x;
}

// --- Coordinate parsing ------------------------------------------------------

struct Coord {
    double value = 0.0;
    Rational exact;
    bool has_exact = false;
};

// Parse one coordinate string to a numeric value plus, when it is a plain
// rational, its exact value. Returns nullopt if it is not a finite constant.
std::optional<Coord> parse_coord(const std::string& s) {
    try {
        const Expr e = simplify(parse_expression(s));
        Coord c;
        if (e->kind() == Kind::Number) {
            c.exact = e->number();
            c.has_exact = true;
            c.value = c.exact.to_double();
        } else {
            c.value = evaluate(e);
        }
        if (!std::isfinite(c.value)) return std::nullopt;
        return c;
    } catch (const Error&) {
        return std::nullopt;
    }
}

// --- Numeric helpers ---------------------------------------------------------

double r2_score(const std::vector<double>& xs, const std::vector<double>& ys,
                const auto& predict) {
    double ybar = 0.0;
    for (double y : ys) ybar += y;
    ybar /= static_cast<double>(ys.size());
    double ss_res = 0.0;
    double ss_tot = 0.0;
    for (std::size_t i = 0; i < ys.size(); ++i) {
        const double resid = ys[i] - predict(xs[i]);
        ss_res += resid * resid;
        ss_tot += (ys[i] - ybar) * (ys[i] - ybar);
    }
    if (ss_tot == 0.0) return ss_res == 0.0 ? 1.0 : 0.0;
    return 1.0 - ss_res / ss_tot;
}

// Render a double to a compact plain decimal (~6 significant figures, never
// scientific) so it round-trips through Rational::from_decimal_string.
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

// --- Polynomial fit ----------------------------------------------------------

const char* poly_label(int degree) {
    switch (degree) {
        case 1: return "linear";
        case 2: return "quadratic";
        case 3: return "cubic";
        case 4: return "quartic";
        default: return "polynomial";
    }
}

Expr poly_expr(const std::vector<Expr>& coeffs, std::string_view var) {
    std::vector<Expr> terms;
    const Expr x = make_sym(std::string(var));
    for (std::size_t b = 0; b < coeffs.size(); ++b) {
        if (b == 0) terms.push_back(coeffs[b]);
        else if (b == 1) terms.push_back(make_mul({coeffs[b], x}));
        else terms.push_back(make_mul({coeffs[b], make_pow(x, make_num(static_cast<long long>(b)))}));
    }
    return simplify(make_add(std::move(terms)));
}

FitResult fit_poly(const std::vector<Coord>& xs, const std::vector<Coord>& ys, int degree,
                   std::string_view var) {
    FitResult R;
    R.variable = std::string(var);
    R.n = static_cast<int>(xs.size());
    R.model = poly_label(degree);

    std::set<double> distinct;
    for (const Coord& c : xs) distinct.insert(c.value);
    if (static_cast<int>(distinct.size()) < degree + 1) {
        char buf[160];
        std::snprintf(buf, sizeof buf,
                      "need at least %d distinct x-values for a degree-%d fit (have %d)",
                      degree + 1, degree, static_cast<int>(distinct.size()));
        R.message = buf;
        return R;
    }

    std::vector<double> xd(xs.size());
    std::vector<double> yd(ys.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        xd[i] = xs[i].value;
        yd[i] = ys[i].value;
    }

    const bool all_rational = std::ranges::all_of(xs, [](const Coord& c) { return c.has_exact; }) &&
                              std::ranges::all_of(ys, [](const Coord& c) { return c.has_exact; });

    // Exact rational path via the normal equations.
    if (all_rational) {
        try {
            const int d = degree;
            std::vector<Rational> S(2 * d + 1, Rational(0));
            std::vector<Rational> T(d + 1, Rational(0));
            for (std::size_t i = 0; i < xs.size(); ++i) {
                Rational p(1);
                for (int m = 0; m <= 2 * d; ++m) {
                    S[m] = S[m] + p;
                    p = p * xs[i].exact;
                }
                Rational q(1);
                for (int a = 0; a <= d; ++a) {
                    T[a] = T[a] + q * ys[i].exact;
                    q = q * xs[i].exact;
                }
            }
            std::vector<std::vector<Rational>> A(d + 1, std::vector<Rational>(d + 1, Rational(0)));
            for (int a = 0; a <= d; ++a)
                for (int b = 0; b <= d; ++b) A[a][b] = S[a + b];
            if (auto sol = gauss_solve<Rational>(A, T)) {
                std::vector<Expr> coeffs;
                coeffs.reserve(sol->size());
                std::vector<double> cd;
                for (const Rational& c : *sol) {
                    coeffs.push_back(make_num(c));
                    cd.push_back(c.to_double());
                }
                R.expr = poly_expr(coeffs, var);
                R.exact = true;
                R.status = FitResult::Status::Ok;
                R.r2 = r2_score(xd, yd, [&](double x) {
                    double acc = 0.0;
                    for (auto it = cd.rbegin(); it != cd.rend(); ++it) acc = acc * x + *it;
                    return acc;
                });
                return R;
            }
            // Singular despite enough distinct x — fall through to numeric.
        } catch (const OverflowError&) {
            // Exact power sums overflowed long long — fall through to numeric.
        }
    }

    // Numeric least squares (double normal equations).
    const int d = degree;
    std::vector<double> S(2 * d + 1, 0.0);
    std::vector<double> Tv(d + 1, 0.0);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        double p = 1.0;
        for (int m = 0; m <= 2 * d; ++m) {
            S[m] += p;
            p *= xd[i];
        }
        double q = 1.0;
        for (int a = 0; a <= d; ++a) {
            Tv[a] += q * yd[i];
            q *= xd[i];
        }
    }
    std::vector<std::vector<double>> A(d + 1, std::vector<double>(d + 1, 0.0));
    for (int a = 0; a <= d; ++a)
        for (int b = 0; b <= d; ++b) A[a][b] = S[a + b];
    auto sol = gauss_solve<double>(A, Tv);
    if (!sol) {
        R.message = "the fit is degenerate (singular normal equations)";
        return R;
    }
    std::vector<Expr> coeffs;
    for (double c : *sol) coeffs.push_back(num_expr(c));
    R.expr = poly_expr(coeffs, var);
    R.exact = false;
    R.status = FitResult::Status::Ok;
    R.r2 = r2_score(xd, yd, [&](double x) {
        double acc = 0.0;
        for (auto it = sol->rbegin(); it != sol->rend(); ++it) acc = acc * x + *it;
        return acc;
    });
    return R;
}

// --- Linearized numeric fits (exp / power / log) -----------------------------

// Ordinary least squares of v on u: v ≈ slope·u + intercept. Assumes ≥ 2 points
// with non-degenerate u. Returns {slope, intercept}.
std::pair<double, double> linear_ls(const std::vector<double>& u, const std::vector<double>& v) {
    const double n = static_cast<double>(u.size());
    double su = 0, sv = 0, suu = 0, suv = 0;
    for (std::size_t i = 0; i < u.size(); ++i) {
        su += u[i];
        sv += v[i];
        suu += u[i] * u[i];
        suv += u[i] * v[i];
    }
    const double denom = n * suu - su * su;
    const double slope = denom == 0.0 ? 0.0 : (n * suv - su * sv) / denom;
    const double intercept = (sv - slope * su) / n;
    return {slope, intercept};
}

FitResult fit_linearized(const std::vector<Coord>& xs, const std::vector<Coord>& ys,
                         FitModel model, std::string_view var) {
    FitResult R;
    R.variable = std::string(var);
    R.n = static_cast<int>(xs.size());

    std::vector<double> xd(xs.size());
    std::vector<double> yd(ys.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        xd[i] = xs[i].value;
        yd[i] = ys[i].value;
    }

    const bool need_pos_x = model == FitModel::Power || model == FitModel::Log;
    const bool need_pos_y = model == FitModel::Exp || model == FitModel::Power;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (need_pos_x && !(xd[i] > 0.0)) {
            R.message = "this model needs every x value to be positive";
            return R;
        }
        if (need_pos_y && !(yd[i] > 0.0)) {
            R.message = "this model needs every y value to be positive";
            return R;
        }
    }

    std::vector<double> u(xs.size());
    std::vector<double> v(ys.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        u[i] = need_pos_x ? std::log(xd[i]) : xd[i];         // ln x for power/log
        v[i] = (model == FitModel::Log) ? yd[i] : std::log(yd[i]);
    }
    const auto [slope, intercept] = linear_ls(u, v);

    const Expr x = make_sym(std::string(var));
    if (model == FitModel::Exp) {
        const double a = std::exp(intercept);
        const double b = slope;
        R.model = "exponential";
        R.expr = simplify(make_mul({num_expr(a), make_exp(make_mul({num_expr(b), x}))}));
        R.r2 = r2_score(xd, yd, [&](double xx) { return a * std::exp(b * xx); });
    } else if (model == FitModel::Power) {
        const double a = std::exp(intercept);
        const double b = slope;
        R.model = "power";
        R.expr = simplify(make_mul({num_expr(a), make_pow(x, num_expr(b))}));
        R.r2 = r2_score(xd, yd, [&](double xx) { return a * std::pow(xx, b); });
    } else { // Log
        const double a = intercept;
        const double b = slope;
        R.model = "logarithmic";
        R.expr = simplify(make_add({num_expr(a), make_mul({num_expr(b), make_fn(FunctionId::Ln, x)})}));
        R.r2 = r2_score(xd, yd, [&](double xx) { return a + b * std::log(xx); });
    }
    R.exact = false;
    R.status = FitResult::Status::Ok;
    return R;
}

} // namespace

FitResult fit(const std::vector<std::string>& xs, const std::vector<std::string>& ys,
              FitModel model, int degree, std::string_view variable) {
    FitResult R;
    R.variable = std::string(variable);
    if (xs.size() != ys.size()) {
        R.message = "x and y must have the same number of values";
        return R;
    }
    if (xs.size() < 2) {
        R.message = "need at least 2 data points";
        return R;
    }
    if (model == FitModel::Poly && (degree < 1 || degree > 8)) {
        R.message = "polynomial degree must be between 1 and 8";
        return R;
    }

    std::vector<Coord> xc;
    std::vector<Coord> yc;
    xc.reserve(xs.size());
    yc.reserve(ys.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        auto cx = parse_coord(xs[i]);
        auto cy = parse_coord(ys[i]);
        if (!cx) {
            R.message = "could not read x value '" + xs[i] + "'";
            return R;
        }
        if (!cy) {
            R.message = "could not read y value '" + ys[i] + "'";
            return R;
        }
        xc.push_back(*cx);
        yc.push_back(*cy);
    }

    if (model == FitModel::Poly) return fit_poly(xc, yc, degree, variable);
    return fit_linearized(xc, yc, model, variable);
}

InterpResult interp(const std::vector<std::string>& xs, const std::vector<std::string>& ys,
                    std::string_view variable) {
    InterpResult R;
    R.variable = std::string(variable);
    if (xs.size() != ys.size()) {
        R.message = "x and y must have the same number of values";
        return R;
    }
    if (xs.empty()) {
        R.message = "need at least 1 data point";
        return R;
    }
    const int n = static_cast<int>(xs.size());
    R.n = n;

    std::vector<Coord> xc;
    std::vector<Coord> yc;
    for (int i = 0; i < n; ++i) {
        auto cx = parse_coord(xs[i]);
        auto cy = parse_coord(ys[i]);
        if (!cx) {
            R.message = "could not read x value '" + xs[i] + "'";
            return R;
        }
        if (!cy) {
            R.message = "could not read y value '" + ys[i] + "'";
            return R;
        }
        xc.push_back(*cx);
        yc.push_back(*cy);
    }
    std::set<double> distinct;
    for (const Coord& c : xc) distinct.insert(c.value);
    if (static_cast<int>(distinct.size()) < n) {
        R.message = "x values must be distinct";
        return R;
    }

    const bool all_rational = std::ranges::all_of(xc, [](const Coord& c) { return c.has_exact; }) &&
                              std::ranges::all_of(yc, [](const Coord& c) { return c.has_exact; });

    // Solve the n×n Vandermonde system  Σ c_j · x_i^j = y_i  for c_0..c_{n-1}.
    std::vector<Expr> coeffs;
    int deg = 0;
    if (all_rational) {
        try {
            std::vector<std::vector<Rational>> A(n, std::vector<Rational>(n, Rational(0)));
            std::vector<Rational> b(n);
            for (int i = 0; i < n; ++i) {
                Rational p(1);
                for (int j = 0; j < n; ++j) {
                    A[i][j] = p;
                    p = p * xc[i].exact;
                }
                b[i] = yc[i].exact;
            }
            if (auto sol = gauss_solve<Rational>(A, b)) {
                for (int k = 0; k < n; ++k)
                    if (!(*sol)[k].is_zero()) deg = k;
                for (const Rational& c : *sol) coeffs.push_back(make_num(c));
                R.exact = true;
            }
        } catch (const OverflowError&) {
            coeffs.clear();
            R.exact = false;
        }
    }
    if (coeffs.empty()) {
        // Numeric fallback (non-rational data, or exact overflow).
        std::vector<std::vector<double>> A(n, std::vector<double>(n, 0.0));
        std::vector<double> b(n);
        double scale = 0.0;
        for (int i = 0; i < n; ++i) {
            double p = 1.0;
            for (int j = 0; j < n; ++j) {
                A[i][j] = p;
                p *= xc[i].value;
            }
            b[i] = yc[i].value;
            scale = std::max(scale, std::fabs(yc[i].value));
        }
        auto sol = gauss_solve<double>(A, b);
        if (!sol) {
            R.message = "interpolation failed (ill-conditioned system)";
            return R;
        }
        const double tol = 1e-9 * std::max(1.0, scale);
        for (int k = 0; k < n; ++k)
            if (std::fabs((*sol)[k]) > tol) deg = k;
        for (double c : *sol) coeffs.push_back(num_expr(c));
    }

    R.degree = deg;
    R.expr = poly_expr(coeffs, variable); // simplify drops zero high-order terms
    R.status = InterpResult::Status::Ok;
    return R;
}

std::optional<std::pair<FitModel, int>> parse_fit_model(std::string_view name) {
    if (name == "linear") return std::pair{FitModel::Poly, 1};
    if (name == "quadratic" || name == "quad") return std::pair{FitModel::Poly, 2};
    if (name == "cubic") return std::pair{FitModel::Poly, 3};
    if (name == "quartic") return std::pair{FitModel::Poly, 4};
    if (name == "poly" || name == "polynomial") return std::pair{FitModel::Poly, -1};
    if (name == "exp" || name == "exponential") return std::pair{FitModel::Exp, 0};
    if (name == "power" || name == "pow") return std::pair{FitModel::Power, 0};
    if (name == "log" || name == "logarithmic" || name == "ln") return std::pair{FitModel::Log, 0};
    return std::nullopt;
}

std::pair<std::vector<std::string>, std::vector<std::string>> parse_point_data(
    std::string_view data) {
    std::vector<std::string> xs;
    std::vector<std::string> ys;
    std::string record;
    auto flush = [&](const std::string& rec) {
        // Trim.
        std::size_t a = rec.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return;
        std::size_t b = rec.find_last_not_of(" \t\r\n");
        const std::string r = rec.substr(a, b - a + 1);
        const std::size_t comma = r.find(',');
        if (comma == std::string::npos)
            throw ParseError("each data point must be 'x,y'", 0, r.size());
        if (r.find(',', comma + 1) != std::string::npos)
            throw ParseError("each data point must be exactly 'x,y'", 0, r.size());
        xs.push_back(r.substr(0, comma));
        ys.push_back(r.substr(comma + 1));
    };
    for (char ch : data) {
        if (ch == ';' || ch == '\n') {
            flush(record);
            record.clear();
        } else {
            record.push_back(ch);
        }
    }
    flush(record);
    return {xs, ys};
}

} // namespace mathsolver
