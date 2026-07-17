// Continuous-time LTI numerics (sys_lti.hpp).

#include "sys_lti.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>

#include "mathsolver/mathsolver.hpp"

namespace mathsolver::plugins::sys {

namespace {

bool near_zero(double v) {
    return std::abs(v) < 1e-12;
}

/// Trim exactly-zero high-order coefficients (keeping at least one entry).
void trim_leading(std::vector<double>& c) {
    while (c.size() > 1 && near_zero(c.back())) {
        c.pop_back();
    }
}

} // namespace

std::vector<double> poly_from_expr(const std::string& text, const std::string& var,
                                   int max_degree) {
    Expr e;
    try {
        e = simplify(parse_expression(text));
    } catch (const Error& err) {
        throw SysError(std::format("cannot parse '{}': {}", text, err.what()));
    }
    for (const std::string& sym : free_symbols(e)) {
        if (sym != var) {
            throw SysError(std::format(
                "'{}' contains the symbol '{}' — polynomials must be in {} with "
                "numeric coefficients",
                text, sym, var));
        }
    }
    std::vector<double> coeffs;
    Expr d = e;
    double factorial = 1.0;
    for (int k = 0; k <= max_degree; ++k) {
        if (k > 0) {
            factorial *= k;
            try {
                d = simplify(differentiate(d, var));
            } catch (const Error& err) {
                throw SysError(std::format("cannot differentiate '{}': {}", text,
                                           err.what()));
            }
        }
        if (d->kind() == Kind::Number && d->number().is_zero()) {
            break;
        }
        double value = 0.0;
        try {
            value = evaluate(d, Bindings{{var, 0.0}});
        } catch (const Error&) {
            throw SysError(std::format(
                "'{}' is not a polynomial in {} (degree <= {}) with numeric "
                "coefficients",
                text, var, max_degree));
        }
        coeffs.push_back(value / factorial);
        // Termination check happens on the NEXT round via the derivative; a
        // non-polynomial (sin, exp, 1/s) never reaches the zero expression.
        if (k == max_degree) {
            const Expr next = simplify(differentiate(d, var));
            if (!(next->kind() == Kind::Number && next->number().is_zero())) {
                throw SysError(std::format(
                    "'{}' is not a polynomial in {} of degree <= {}", text, var,
                    max_degree));
            }
        }
    }
    if (coeffs.empty()) {
        coeffs.push_back(0.0);
    }
    trim_leading(coeffs);
    return coeffs;
}

namespace {

RationalTF make_tf_in(const std::string& num_text, const std::string& den_text,
                      const std::string& var, const char* domain) {
    RationalTF tf;
    tf.num = poly_from_expr(num_text, var);
    tf.den = poly_from_expr(den_text, var);
    if (tf.den.size() == 1 && near_zero(tf.den[0])) {
        throw SysError("the denominator is identically zero");
    }
    if (tf.num.size() > tf.den.size()) {
        throw SysError(std::format(
            "{} must be proper: numerator degree {} exceeds denominator degree {}",
            domain, tf.num.size() - 1, tf.den.size() - 1));
    }
    const double lead = tf.den.back();
    for (double& c : tf.den) c /= lead;
    for (double& c : tf.num) c /= lead;
    return tf;
}

} // namespace

RationalTF make_tf(const std::string& num_text, const std::string& den_text) {
    return make_tf_in(num_text, den_text, "s", "H(s)");
}

RationalTF make_tfz(const std::string& num_text, const std::string& den_text) {
    return make_tf_in(num_text, den_text, "z", "H(z)");
}

// --- ODE parsing ------------------------------------------------------------

namespace {

struct OdeTerm {
    double coef = 1.0;
    char var = 0; ///< 'y', 'u', or 0 for a bare constant
    int order = 0;
};

/// Parse one side of the ODE into terms. Grammar per term:
///   [sign] [decimal-coefficient] ['*'] ('y' | 'u') primes
void parse_side(const std::string& side, bool negate_all,
                std::vector<OdeTerm>& out) {
    std::size_t i = 0;
    const auto skip_ws = [&] {
        while (i < side.size() && std::isspace(static_cast<unsigned char>(side[i]))) {
            ++i;
        }
    };
    skip_ws();
    if (i >= side.size()) {
        throw SysError("an ODE side is empty");
    }
    bool first = true;
    while (i < side.size()) {
        skip_ws();
        double sign = negate_all ? -1.0 : 1.0;
        if (side[i] == '+' || side[i] == '-') {
            if (side[i] == '-') sign = -sign;
            ++i;
        } else if (!first) {
            throw SysError(std::format("expected '+' or '-' before '{}'",
                                       side.substr(i)));
        }
        first = false;
        skip_ws();
        OdeTerm term;
        term.coef = sign;
        // Optional decimal coefficient.
        std::size_t num_start = i;
        while (i < side.size() &&
               (std::isdigit(static_cast<unsigned char>(side[i])) || side[i] == '.')) {
            ++i;
        }
        if (i > num_start) {
            term.coef *= std::stod(side.substr(num_start, i - num_start));
        }
        skip_ws();
        if (i < side.size() && side[i] == '*') {
            ++i;
            skip_ws();
        }
        if (i < side.size() && (side[i] == 'y' || side[i] == 'u')) {
            term.var = side[i];
            ++i;
            while (i < side.size() && side[i] == '\'') {
                ++term.order;
                ++i;
            }
        } else if (i == num_start) {
            throw SysError(std::format(
                "expected a y or u term at '{}'", side.substr(num_start)));
        } else {
            throw SysError(
                "constant terms are not allowed — the ODE must be linear "
                "time-invariant in y and u");
        }
        out.push_back(term);
        skip_ws();
    }
}

} // namespace

RationalTF ode_to_tf(const std::string& equation) {
    const std::size_t eq_pos = equation.find('=');
    if (eq_pos == std::string::npos) {
        throw SysError("an ODE needs '=', e.g. y'' + 3y' + 2y = u' + u");
    }
    std::vector<OdeTerm> terms;
    parse_side(equation.substr(0, eq_pos), false, terms);
    parse_side(equation.substr(eq_pos + 1), true, terms); // moved to the left

    int deg_y = -1;
    int deg_u = -1;
    for (const OdeTerm& t : terms) {
        if (t.var == 'y') deg_y = std::max(deg_y, t.order);
        if (t.var == 'u') deg_u = std::max(deg_u, t.order);
    }
    if (deg_y < 0) {
        throw SysError("the ODE has no y terms — nothing to solve for");
    }
    if (deg_u < 0) {
        throw SysError("the ODE has no input u terms — H(s) needs an input");
    }
    if (deg_u > deg_y) {
        throw SysError(std::format(
            "improper system: the input derivative order {} exceeds the output "
            "order {}",
            deg_u, deg_y));
    }

    // Laplace with zero initial conditions: sum(a_k s^k) Y = sum(b_k s^k) U
    // where y-terms keep their sign and u-terms flip (they were moved left).
    RationalTF tf;
    tf.den.assign(static_cast<std::size_t>(deg_y) + 1, 0.0);
    tf.num.assign(static_cast<std::size_t>(deg_u) + 1, 0.0);
    for (const OdeTerm& t : terms) {
        if (t.var == 'y') {
            tf.den[static_cast<std::size_t>(t.order)] += t.coef;
        } else {
            tf.num[static_cast<std::size_t>(t.order)] -= t.coef;
        }
    }
    trim_leading(tf.den);
    trim_leading(tf.num);
    if (near_zero(tf.den.back())) {
        throw SysError("the y terms cancel — the ODE has no dynamics");
    }
    if (tf.num.size() > tf.den.size()) {
        throw SysError("improper system after cancellation");
    }
    const double lead = tf.den.back();
    for (double& c : tf.den) c /= lead;
    for (double& c : tf.num) c /= lead;
    return tf;
}

// --- roots, evaluation, gain ------------------------------------------------

std::vector<cd> poly_roots(const std::vector<double>& coeffs_in) {
    std::vector<double> coeffs = coeffs_in;
    trim_leading(coeffs);
    const std::size_t n = coeffs.size() - 1;
    if (n == 0) {
        return {};
    }
    // Monic complex copy.
    std::vector<cd> a(coeffs.begin(), coeffs.end());
    for (cd& c : a) {
        c /= coeffs.back();
    }
    // Durand-Kerner from spiral start points.
    std::vector<cd> r(n);
    const cd seed{0.4, 0.9};
    r[0] = cd{1.0, 0.0};
    for (std::size_t k = 0; k < n; ++k) {
        r[k] = std::pow(seed, static_cast<double>(k + 1));
    }
    const auto eval_monic = [&](cd x) {
        cd v{1.0, 0.0};
        for (std::size_t k = n; k-- > 0;) {
            v = v * x + a[k];
        }
        return v;
    };
    for (int iter = 0; iter < 500; ++iter) {
        double moved = 0.0;
        for (std::size_t k = 0; k < n; ++k) {
            cd denom{1.0, 0.0};
            for (std::size_t j = 0; j < n; ++j) {
                if (j != k) {
                    denom *= (r[k] - r[j]);
                }
            }
            if (std::abs(denom) < 1e-300) {
                denom = cd{1e-300, 0.0};
            }
            const cd delta = eval_monic(r[k]) / denom;
            r[k] -= delta;
            moved = std::max(moved, std::abs(delta));
        }
        if (moved < 1e-13) {
            break;
        }
    }
    // Snap conjugate symmetry: tiny imaginary parts become exactly real.
    for (cd& root : r) {
        if (std::abs(root.imag()) < 1e-8 * (1.0 + std::abs(root.real()))) {
            root = cd{root.real(), 0.0};
        }
    }
    return r;
}

cd tf_eval(const RationalTF& tf, cd s) {
    const auto horner = [&](const std::vector<double>& c) {
        cd v{0.0, 0.0};
        for (std::size_t k = c.size(); k-- > 0;) {
            v = v * s + c[k];
        }
        return v;
    };
    return horner(tf.num) / horner(tf.den);
}

double dc_gain(const RationalTF& tf) {
    if (near_zero(tf.den.front())) {
        return std::numeric_limits<double>::infinity();
    }
    return tf.num.front() / tf.den.front();
}

// --- stability margins ------------------------------------------------------

namespace {

/// Continuous phase of H(jw) in radians (principal value; callers track
/// wrap-around across scan steps by keeping steps small).
double phase_at(const RationalTF& tf, double w) {
    return std::arg(tf_eval(tf, cd{0.0, w}));
}

double mag_at(const RationalTF& tf, double w) {
    return std::abs(tf_eval(tf, cd{0.0, w}));
}

/// Bisection refinement of a bracketed sign change of `f` on [a, b].
template <typename F>
double refine_crossing(F&& f, double a, double b) {
    double fa = f(a);
    for (int it = 0; it < 80; ++it) {
        const double m = 0.5 * (a + b);
        const double fm = f(m);
        if ((fm > 0.0) == (fa > 0.0)) {
            a = m;
            fa = fm;
        } else {
            b = m;
        }
    }
    return 0.5 * (a + b);
}

} // namespace

Margins compute_margins(const RationalTF& tf, double wmin, double wmax) {
    Margins m;
    constexpr int kScan = 2000;
    double prev_w = wmin;
    // Track phase continuously (unwrap between scan samples) so the -180
    // crossing detection survives principal-value jumps.
    double prev_phase = phase_at(tf, wmin);
    double unwrap = 0.0;
    double prev_mag = mag_at(tf, wmin);
    for (int i = 1; i < kScan; ++i) {
        const double t = static_cast<double>(i) / (kScan - 1);
        const double w = wmin * std::pow(wmax / wmin, t);
        double ph = phase_at(tf, w);
        double cont = ph + unwrap;
        if (cont - prev_phase > std::numbers::pi) {
            unwrap -= 2.0 * std::numbers::pi;
            cont = ph + unwrap;
        } else if (prev_phase - cont > std::numbers::pi) {
            unwrap += 2.0 * std::numbers::pi;
            cont = ph + unwrap;
        }
        const double mag = mag_at(tf, w);

        // Phase crossover(s): continuous phase passes an odd multiple of pi,
        // i.e. H crosses the negative real axis. The principal phase JUMPS
        // there (-pi -> +pi), so refine on Im H(jw), which genuinely changes
        // sign at the crossing (and cannot vanish elsewhere in a small
        // bracket that stays near the negative axis).
        const double pa = (prev_phase + std::numbers::pi) / (2.0 * std::numbers::pi);
        const double pb = (cont + std::numbers::pi) / (2.0 * std::numbers::pi);
        if (std::floor(pa) != std::floor(pb)) {
            const double wc = refine_crossing(
                [&](double x) { return tf_eval(tf, cd{0.0, x}).imag(); }, prev_w,
                w);
            const double g = mag_at(tf, wc);
            if (g > 1e-12) {
                m.gain.push_back({-20.0 * std::log10(g), wc});
            }
        }
        // Gain crossover: |H| passes through 1.
        if ((prev_mag > 1.0) != (mag > 1.0)) {
            const double wc = refine_crossing(
                [&](double x) { return mag_at(tf, x) - 1.0; }, prev_w, w);
            // Phase margin relative to -180: PM = 180 + phase(wc), using the
            // continuous phase at the bracket.
            const double ph_wc = phase_at(tf, wc) + unwrap;
            double pm = 180.0 + ph_wc * 180.0 / std::numbers::pi;
            // Fold to the conventional (-180, 180] band.
            while (pm > 180.0) pm -= 360.0;
            while (pm <= -180.0) pm += 360.0;
            m.phase.push_back({pm, wc});
        }

        prev_w = w;
        prev_phase = cont;
        prev_mag = mag;
    }
    return m;
}

// --- feedback and root locus ------------------------------------------------

RationalTF feedback_unity(const RationalTF& g, double k) {
    // T = K num / (den + K num).
    RationalTF t;
    t.num.assign(g.den.size(), 0.0);
    t.den = g.den;
    for (std::size_t i = 0; i < g.num.size(); ++i) {
        t.num[i] = k * g.num[i];
        t.den[i] += k * g.num[i];
    }
    trim_leading(t.num);
    trim_leading(t.den);
    if (near_zero(t.den.back())) {
        throw SysError("the closed loop degenerates (denominator vanishes)");
    }
    const double lead = t.den.back();
    for (double& c : t.den) c /= lead;
    for (double& c : t.num) c /= lead;
    return t;
}

std::vector<std::vector<cd>> root_locus(const RationalTF& g,
                                        const std::vector<double>& gains) {
    std::vector<std::vector<cd>> out;
    out.reserve(gains.size());
    for (const double k : gains) {
        std::vector<double> poly = g.den;
        if (g.num.size() > poly.size()) {
            poly.resize(g.num.size(), 0.0);
        }
        for (std::size_t i = 0; i < g.num.size(); ++i) {
            poly[i] += k * g.num[i];
        }
        out.push_back(poly_roots(poly));
    }
    return out;
}

// --- time simulation --------------------------------------------------------

TimeSim simulate(const RationalTF& tf, double horizon, int points) {
    const std::size_t n = tf.den.size() - 1; // state dimension
    TimeSim out;
    if (n == 0) {
        // Pure gain: step = k, impulse = 0 (plus k*delta).
        out.biproper = true;
        for (int i = 0; i < points; ++i) {
            const double t = horizon * i / (points - 1);
            out.t.push_back(t);
            out.step.push_back(tf.num.front() / tf.den.front());
            out.impulse.push_back(0.0);
        }
        return out;
    }
    // Controllable canonical form with monic denominator (den.back() == 1):
    //   xdot_i = x_{i+1};  xdot_{n-1} = -sum(den[k] x_k) + u
    //   y = sum((num[k] - num[n]*den[k]) x_k) + num[n]*u
    std::vector<double> b(n + 1, 0.0);
    for (std::size_t k = 0; k < tf.num.size(); ++k) {
        b[k] = tf.num[k];
    }
    const double D = b[n];
    out.biproper = !near_zero(D);
    std::vector<double> c(n);
    for (std::size_t k = 0; k < n; ++k) {
        c[k] = b[k] - D * tf.den[k];
    }

    const auto deriv = [&](const std::vector<double>& x, double u,
                           std::vector<double>& dx) {
        for (std::size_t k = 0; k + 1 < n; ++k) {
            dx[k] = x[k + 1];
        }
        double acc = u;
        for (std::size_t k = 0; k < n; ++k) {
            acc -= tf.den[k] * x[k];
        }
        dx[n - 1] = acc;
    };

    // RK4 with substeps for stiffness: keep dt*|fastest pole| modest.
    double fastest = 1.0;
    for (const cd& p : poly_roots(tf.den)) {
        fastest = std::max(fastest, std::abs(p));
    }
    const double dt_out = horizon / (points - 1);
    const int substeps = std::max(1, static_cast<int>(std::ceil(dt_out * fastest / 0.8)));
    const double h = dt_out / substeps;

    const auto rk4 = [&](std::vector<double>& x, double u) {
        std::vector<double> k1(n), k2(n), k3(n), k4(n), tmp(n);
        deriv(x, u, k1);
        for (std::size_t j = 0; j < n; ++j) tmp[j] = x[j] + h / 2 * k1[j];
        deriv(tmp, u, k2);
        for (std::size_t j = 0; j < n; ++j) tmp[j] = x[j] + h / 2 * k2[j];
        deriv(tmp, u, k3);
        for (std::size_t j = 0; j < n; ++j) tmp[j] = x[j] + h * k3[j];
        deriv(tmp, u, k4);
        for (std::size_t j = 0; j < n; ++j) {
            x[j] += h / 6 * (k1[j] + 2 * k2[j] + 2 * k3[j] + k4[j]);
        }
    };
    const auto output = [&](const std::vector<double>& x, double u) {
        double y = D * u;
        for (std::size_t k = 0; k < n; ++k) {
            y += c[k] * x[k];
        }
        return y;
    };

    std::vector<double> xs(n, 0.0); // step state
    std::vector<double> xi(n, 0.0); // impulse state: x0 = B = e_n
    xi[n - 1] = 1.0;
    for (int i = 0; i < points; ++i) {
        out.t.push_back(dt_out * i);
        out.step.push_back(output(xs, 1.0));
        out.impulse.push_back(output(xi, 0.0));
        for (int s = 0; s < substeps; ++s) {
            rk4(xs, 1.0);
            rk4(xi, 0.0);
        }
    }
    return out;
}

// --- discrete-time ----------------------------------------------------------

DiscreteSim simulate_discrete(const RationalTF& tf, int points) {
    // Positive-power coefficients -> difference-equation taps. With den monic
    // of degree N, multiply through by z^-N:
    //   b[i] = num[N-i]  (num zero-padded),  a[i] = den[N-i],  a[0] = 1.
    const std::size_t N = tf.den.size() - 1;
    std::vector<double> b(N + 1, 0.0);
    std::vector<double> a(N + 1, 0.0);
    for (std::size_t i = 0; i <= N; ++i) {
        a[i] = tf.den[N - i];
        const std::size_t src = N - i;
        b[i] = src < tf.num.size() ? tf.num[src] : 0.0;
    }
    const double a0 = a[0] == 0.0 ? 1.0 : a[0];

    DiscreteSim sim;
    const auto run = [&](bool step) {
        std::vector<double> x(N + 1, 0.0);
        std::vector<double> y(N + 1, 0.0);
        std::vector<double> out;
        for (int n = 0; n < points; ++n) {
            // Shift histories.
            for (std::size_t k = N; k > 0; --k) {
                x[k] = x[k - 1];
                y[k] = y[k - 1];
            }
            x[0] = step ? 1.0 : (n == 0 ? 1.0 : 0.0);
            double acc = 0.0;
            for (std::size_t i = 0; i <= N; ++i) {
                acc += b[i] * x[i];
            }
            for (std::size_t i = 1; i <= N; ++i) {
                acc -= a[i] * y[i];
            }
            y[0] = acc / a0;
            out.push_back(y[0]);
        }
        return out;
    };
    sim.impulse = run(false);
    sim.step = run(true);
    for (int n = 0; n < points; ++n) {
        sim.n.push_back(static_cast<double>(n));
    }
    return sim;
}

cd tfz_eval(const RationalTF& tf, double f, double fs) {
    const cd z = std::polar(1.0, 2.0 * std::numbers::pi * f / fs);
    return tf_eval(tf, z);
}

} // namespace mathsolver::plugins::sys
