// Linear constant-coefficient ODE solver (ode.hpp): parse the y-side, take
// Laplace transforms with the initial conditions folded in, decompose Y(s)
// by apart(), and invert term by term.

#include "mathsolver/ode.hpp"

#include <cctype>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mathsolver/apart.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/evaluator.hpp"
#include "mathsolver/integrate.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"
#include "mathsolver/solver.hpp"
#include "mathsolver/transform.hpp"

namespace mathsolver {

namespace {

std::string trim_ws(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
    return std::string(s.substr(b, e - b));
}

/// Split at top-level '+'/'-' (sign captured with each term).
std::vector<std::string> split_signed_terms(const std::string& side) {
    std::vector<std::string> terms;
    std::string current;
    int depth = 0;
    for (std::size_t i = 0; i < side.size(); ++i) {
        const char c = side[i];
        if (c == '(' || c == '[' || c == '{') ++depth;
        if (c == ')' || c == ']' || c == '}') --depth;
        if ((c == '+' || c == '-') && depth == 0 && !trim_ws(current).empty()) {
            terms.push_back(current);
            current.clear();
        }
        current += c;
    }
    if (!trim_ws(current).empty()) {
        terms.push_back(current);
    }
    return terms;
}

/// Parse one LHS term "[sign][coef]['*']y[primes][(t)]" into (order, coef).
std::pair<int, Rational> parse_y_term(const std::string& term_in) {
    const std::string term = trim_ws(term_in);
    const std::size_t ypos = term.find('y');
    if (ypos == std::string::npos) {
        throw Error(std::format(
            "dsolve: the left side must be a combination of y and its "
            "derivatives; move '{}' to the right side",
            term));
    }
    // Coefficient text before 'y' (strip a trailing '*').
    std::string coef_text = trim_ws(term.substr(0, ypos));
    if (!coef_text.empty() && coef_text.back() == '*') {
        coef_text = trim_ws(coef_text.substr(0, coef_text.size() - 1));
    }
    Rational coef{1};
    if (coef_text == "-") {
        coef = Rational{-1};
    } else if (coef_text == "+" || coef_text.empty()) {
        coef = Rational{1};
    } else {
        Expr c;
        try {
            c = simplify(parse_expression(coef_text));
        } catch (const Error&) {
            throw Error(std::format("dsolve: cannot read the coefficient '{}'",
                                    coef_text));
        }
        if (c->kind() != Kind::Number) {
            throw Error(std::format(
                "dsolve: the coefficient '{}' must be numeric", coef_text));
        }
        coef = c->number();
    }
    // Primes, then an optional "(t)".
    std::size_t i = ypos + 1;
    int order = 0;
    while (i < term.size() && term[i] == '\'') {
        ++order;
        ++i;
    }
    std::string rest = trim_ws(term.substr(i));
    if (rest == "(t)") {
        rest.clear();
    }
    if (!rest.empty()) {
        throw Error(std::format("dsolve: unexpected '{}' after y{}", rest,
                                std::string(static_cast<std::size_t>(order), '\'')));
    }
    return {order, coef};
}

/// Parse "y''(0) = value" into (order, value expression).
std::pair<int, Expr> parse_condition(const std::string& text) {
    const std::size_t eq = text.find('=');
    if (eq == std::string::npos) {
        throw Error(std::format(
            "dsolve: an initial condition needs '=', e.g. y(0)=1 (got '{}')",
            trim_ws(text)));
    }
    const std::string name = trim_ws(text.substr(0, eq));
    if (name.empty() || name[0] != 'y') {
        throw Error(std::format(
            "dsolve: initial conditions must start with y, e.g. y'(0)=0 "
            "(got '{}')",
            name));
    }
    std::size_t i = 1;
    int order = 0;
    while (i < name.size() && name[i] == '\'') {
        ++order;
        ++i;
    }
    if (trim_ws(name.substr(i)) != "(0)") {
        throw Error(std::format(
            "dsolve: initial conditions are given at t = 0, e.g. y{}(0)=..., "
            "got '{}'",
            std::string(static_cast<std::size_t>(order), '\''), name));
    }
    Expr value;
    try {
        value = simplify(parse_expression(trim_ws(text.substr(eq + 1))));
    } catch (const Error& e) {
        throw Error(std::format("dsolve: cannot read the initial value in "
                                "'{}': {}",
                                trim_ws(text), e.what()));
    }
    if (contains_symbol(value, "t") || contains_symbol(value, "y")) {
        throw Error("dsolve: initial values must be constants");
    }
    return {order, value};
}

// --- first-order methods: y' = f(t, y) --------------------------------------

bool expr_is_zero(const Expr& e) {
    return e->kind() == Kind::Number && e->number().is_zero();
}

/// Merge every e^(...) factor of a product into one exponential, so the
/// integrator sees e^{P+Q} instead of the uncombined e^P · e^Q.
Expr combine_exponentials(const Expr& e) {
    if (e->kind() != Kind::Mul) {
        return e;
    }
    std::vector<Expr> exponents;
    std::vector<Expr> rest;
    for (const Expr& f : e->args()) {
        if (f->kind() == Kind::Pow && f->arg(0)->kind() == Kind::Constant &&
            f->arg(0)->constant() == ConstantId::E) {
            exponents.push_back(f->arg(1));
        } else {
            rest.push_back(f);
        }
    }
    if (exponents.size() < 2) {
        return e;
    }
    rest.push_back(make_exp(simplify(make_add(std::move(exponents)))));
    return simplify(make_mul(std::move(rest)));
}

/// ∫ e dv, or throw with a message naming the method that needed it.
Expr integrate_or_throw(const Expr& e, const std::string& v,
                        const char* method) {
    const IntegrateResult r = integrate(e, v);
    if (r.status != IntegrateResult::Status::Integrated) {
        throw Error(std::format(
            "dsolve ({}): unable to integrate {} d{}", method,
            to_string(e, PrintStyle::Plain), v));
    }
    return r.antiderivative;
}

/// The single initial condition y(0) = y0 for the first-order path, or
/// nullopt for a general solution with symbolic C.
std::optional<Expr> first_order_ic(const std::vector<std::string>& conditions) {
    if (conditions.empty()) {
        return std::nullopt;
    }
    if (conditions.size() > 1) {
        throw Error("dsolve: a first-order equation takes one initial "
                    "condition y(0)=...");
    }
    const auto [k, value] = parse_condition(conditions[0]);
    if (k != 0) {
        throw Error("dsolve: a first-order equation's condition is on y(0)");
    }
    return value;
}

/// Linear  y' = -p(t) y + q(t):  y = e^{-P} (∫ e^{P} q dt + C), P = ∫p.
DsolveResult solve_linear_first_order(const Expr& p, const Expr& q,
                                      const std::optional<Expr>& y0,
                                      std::vector<std::string> warnings) {
    const Expr t = make_sym("t");
    const Expr P = simplify(integrate_or_throw(p, "t", "integrating factor"));
    const Expr mu = make_exp(P);
    const Expr F = expr_is_zero(q)
                       ? make_num(0)
                       : simplify(integrate_or_throw(
                             combine_exponentials(simplify(make_mul({mu, q}))),
                             "t", "integrating factor"));
    Expr C;
    if (y0) {
        // C = y0 e^{P(0)} - F(0).
        C = simplify(make_sub(
            make_mul({*y0, make_exp(simplify(substitute(P, "t", make_num(0))))}),
            simplify(substitute(F, "t", make_num(0)))));
    } else {
        C = make_sym("C");
        warnings.push_back("general solution: C is a free constant");
    }
    DsolveResult res;
    res.order = 1;
    res.method = "integrating factor";
    res.solution = simplify(make_mul(
        {make_exp(simplify(make_neg(P))), make_add({F, C})}));
    res.warnings = std::move(warnings);
    return res;
}

/// Separable  y' = g(t) h(y):  ∫ dy/h = ∫ g dt + C, inverted when possible.
DsolveResult solve_separable(const Expr& g, const Expr& h,
                             const std::optional<Expr>& y0,
                             std::vector<std::string> warnings) {
    const Expr H = simplify(integrate_or_throw(
        simplify(make_div(make_num(1), h)), "y", "separation"));
    const Expr G = simplify(integrate_or_throw(g, "t", "separation"));
    Expr C;
    if (y0) {
        C = simplify(make_sub(simplify(substitute(H, "y", *y0)),
                              simplify(substitute(G, "t", make_num(0)))));
    } else {
        C = make_sym("C");
        warnings.push_back("general solution: C is a free constant");
    }
    DsolveResult res;
    res.order = 1;
    res.method = "separation of variables";
    res.warnings = std::move(warnings);

    // Try to invert H(y) = G(t) + C for y.
    const Expr rhs = simplify(make_add({G, C}));
    const SolveResult inv = solve(Equation{H, rhs}, "y");
    if (inv.status == SolveResult::Status::Solved && !inv.solutions.empty()) {
        // With an IC, pick the branch through (0, y0); otherwise take the
        // first branch and say so when there are several.
        const Expr* chosen = &inv.solutions.front().value;
        if (y0 && inv.solutions.size() > 1) {
            double target = 0.0;
            bool have_target = false;
            try {
                target = evaluate(*y0, Bindings{});
                have_target = true;
            } catch (const Error&) {
            }
            if (have_target) {
                for (const Solution& s : inv.solutions) {
                    try {
                        const double at0 = evaluate(
                            simplify(substitute(s.value, "t", make_num(0))),
                            Bindings{});
                        if (std::abs(at0 - target) <
                            1e-9 * (1.0 + std::abs(target))) {
                            chosen = &s.value;
                            break;
                        }
                    } catch (const Error&) {
                    }
                }
            }
        } else if (!y0 && inv.solutions.size() > 1) {
            res.warnings.push_back(
                "several branches solve the relation; showing one");
        }
        res.solution = simplify(*chosen);
        return res;
    }

    // Honest implicit fallback: H(y) - G(t) - C = 0.
    res.implicit = true;
    res.solution = simplify(make_sub(H, rhs));
    res.warnings.push_back("implicit solution: the relation could not be "
                           "inverted for y");
    return res;
}

/// Route y' = f(t, y) through linear -> Bernoulli -> separable.
DsolveResult dsolve_first_order(const std::string& rhs_text,
                                const std::vector<std::string>& conditions) {
    Expr f;
    try {
        f = simplify(parse_expression(rhs_text));
    } catch (const Error& e) {
        throw Error(std::format("dsolve: cannot read the right side: {}",
                                e.what()));
    }
    if (contains_symbol(f, "s")) {
        throw Error("dsolve: the symbol 's' is reserved");
    }
    const std::optional<Expr> y0 = first_order_ic(conditions);
    std::vector<std::string> warnings;

    // Polynomial in y? Degree <= 1 is linear; two terms at degrees 1 and
    // m >= 2 (no constant) is Bernoulli.
    const auto yc = polynomial_coefficients(f, "y");
    if (yc) {
        const int deg = static_cast<int>(yc->size()) - 1;
        if (deg <= 1) {
            const Expr q = (*yc)[0];
            const Expr p = deg == 1 ? simplify(make_neg((*yc)[1])) : make_num(0);
            return solve_linear_first_order(p, q, y0, std::move(warnings));
        }
        bool bernoulli = expr_is_zero(simplify((*yc)[0]));
        for (int k = 2; k < deg && bernoulli; ++k) {
            bernoulli = expr_is_zero(simplify((*yc)[static_cast<std::size_t>(k)]));
        }
        if (bernoulli) {
            // y' = -p y + q y^m  ->  v = y^{1-m}:  v' = -(1-m) p v + (1-m) q.
            const long long m = deg;
            const Expr p = simplify(make_neg((*yc)[1]));
            const Expr q = (*yc)[static_cast<std::size_t>(deg)];
            const Expr one_minus_m = make_num(1 - m);
            std::optional<Expr> v0;
            if (y0) {
                if (expr_is_zero(*y0)) {
                    throw Error("dsolve (bernoulli): y(0) = 0 is singular for "
                                "the v = y^(1-m) substitution");
                }
                v0 = simplify(make_pow(*y0, one_minus_m));
            }
            DsolveResult vres = solve_linear_first_order(
                simplify(make_mul({one_minus_m, p})),
                simplify(make_mul({one_minus_m, q})), v0, {});
            DsolveResult res;
            res.order = 1;
            res.method = "bernoulli substitution + integrating factor";
            res.warnings = std::move(warnings);
            res.warnings.insert(res.warnings.end(), vres.warnings.begin(),
                                vres.warnings.end());
            res.solution = simplify(make_pow(
                vres.solution, simplify(make_div(make_num(1), one_minus_m))));
            return res;
        }
    }

    // Separable: every factor must depend on t only or y only.
    std::vector<Expr> tf;
    std::vector<Expr> yf;
    bool separable = true;
    const std::vector<Expr> fs =
        f->kind() == Kind::Mul ? f->args() : std::vector<Expr>{f};
    for (const Expr& factor : fs) {
        const bool has_t = contains_symbol(factor, "t");
        const bool has_y = contains_symbol(factor, "y");
        if (has_t && has_y) {
            separable = false;
            break;
        }
        (has_y ? yf : tf).push_back(factor);
    }
    if (separable) {
        const Expr g = tf.empty() ? make_num(1)
                                  : simplify(make_mul(std::move(tf)));
        const Expr h = yf.empty() ? make_num(1)
                                  : simplify(make_mul(std::move(yf)));
        if (expr_is_zero(h)) {
            throw Error("dsolve: y' = 0 · h(y) — the equation is trivial");
        }
        return solve_separable(g, h, y0, std::move(warnings));
    }

    throw Error(std::format(
        "dsolve: no method applies to y' = {} (supported first-order forms: "
        "linear in y, Bernoulli, separable)",
        to_string(f, PrintStyle::Plain)));
}

} // namespace

DsolveResult dsolve(std::string_view ode,
                    const std::vector<std::string>& conditions) {
    const std::string text{ode};
    const std::size_t eq_pos = text.find('=');
    if (eq_pos == std::string::npos || text.find('=', eq_pos + 1) != std::string::npos) {
        throw Error("dsolve: the ODE needs exactly one '=', e.g. "
                    "y'' + 3y' + 2y = e^(-t)");
    }

    // "y' = f(t, y)" routes to the first-order methods (which handle
    // variable coefficients and the supported nonlinear forms).
    if (trim_ws(text.substr(0, eq_pos)) == "y'") {
        return dsolve_first_order(trim_ws(text.substr(eq_pos + 1)), conditions);
    }

    // Left side: numeric coefficients per derivative order.
    std::map<int, Rational> coeffs;
    const std::vector<std::string> terms =
        split_signed_terms(text.substr(0, eq_pos));
    if (terms.empty()) {
        throw Error("dsolve: the left side of the ODE is empty");
    }
    int order = 0;
    for (const std::string& term : terms) {
        const auto [k, c] = parse_y_term(term);
        coeffs[k] = (coeffs.contains(k) ? coeffs[k] : Rational{0}) + c;
        order = std::max(order, k);
    }
    if (order == 0) {
        throw Error("dsolve: the equation has no derivative of y — use solve "
                    "for algebraic equations");
    }
    if (coeffs[order].is_zero()) {
        throw Error("dsolve: the leading derivative's coefficients cancel");
    }

    // Right side: the forcing term f(t).
    Expr forcing;
    try {
        forcing = simplify(parse_expression(trim_ws(text.substr(eq_pos + 1))));
    } catch (const Error& e) {
        throw Error(std::format("dsolve: cannot read the forcing term: {}",
                                e.what()));
    }
    if (contains_symbol(forcing, "y")) {
        throw Error("dsolve: the right side must not contain y (only linear "
                    "constant-coefficient equations are supported)");
    }
    if (contains_symbol(forcing, "s")) {
        throw Error("dsolve: the forcing term may not use the symbol 's' "
                    "(reserved for the transform domain)");
    }

    // Initial conditions y^(k)(0); omitted ones default to zero.
    std::map<int, Expr> y0;
    for (const std::string& cond : conditions) {
        const auto [k, value] = parse_condition(cond);
        if (k >= order) {
            throw Error(std::format(
                "dsolve: the condition on y^({}) exceeds the equation order {}",
                k, order));
        }
        if (y0.contains(k)) {
            throw Error(std::format("dsolve: duplicate condition for y{}(0)",
                                    std::string(static_cast<std::size_t>(k), '\'')));
        }
        y0[k] = value;
    }
    DsolveResult result;
    result.order = order;
    for (int k = 0; k < order; ++k) {
        if (!y0.contains(k)) {
            y0[k] = make_num(0);
            result.warnings.push_back(std::format(
                "assuming y{}(0) = 0",
                std::string(static_cast<std::size_t>(k), '\'')));
        }
    }

    // Characteristic polynomial A(s) and the initial-condition polynomial:
    //   L{y^(k)} = s^k Y - sum_{j<k} s^(k-1-j) y^(j)(0).
    const Expr s = make_sym("s");
    std::vector<Expr> a_terms;
    std::vector<Expr> ic_terms;
    for (const auto& [k, c] : coeffs) {
        if (c.is_zero()) {
            continue;
        }
        const Expr ck = make_num(c);
        a_terms.push_back(
            k == 0 ? ck : make_mul({ck, make_pow(s, make_num(k))}));
        for (int j = 0; j < k; ++j) {
            if (j >= order || (y0.contains(j) &&
                               y0.at(j)->kind() == Kind::Number &&
                               y0.at(j)->number().is_zero())) {
                continue;
            }
            const int p = k - 1 - j;
            Expr piece = make_mul(
                {ck, y0.at(j),
                 p == 0 ? make_num(1) : make_pow(s, make_num(p))});
            ic_terms.push_back(std::move(piece));
        }
    }
    const Expr charpoly = simplify(make_add(std::move(a_terms)));

    // F(s) for the forcing.
    Expr F;
    try {
        F = laplace(forcing, "t");
    } catch (const Error& e) {
        throw Error(std::format(
            "dsolve: the forcing term has no Laplace transform: {}", e.what()));
    }
    const Expr G = ic_terms.empty() ? make_num(0)
                                    : simplify(make_add(std::move(ic_terms)));

    // Y(s) = (F + G)/A -> partial fractions -> y(t).
    const Expr Y = simplify(expand(make_mul(
        {make_add({F, G}), make_pow(charpoly, make_num(-1))})));
    Expr Y_pf;
    try {
        Y_pf = apart(Y, "s");
    } catch (const Error& e) {
        throw Error(std::format("dsolve: cannot decompose Y(s) = {}: {}",
                                to_string(Y, PrintStyle::Plain), e.what()));
    }
    try {
        result.solution = simplify(inverse_laplace(Y_pf, "s"));
    } catch (const Error& e) {
        throw Error(std::format("dsolve: cannot invert Y(s) = {}: {}",
                                to_string(Y_pf, PrintStyle::Plain), e.what()));
    }
    result.transform = Y_pf;
    result.method = "laplace transform + partial fractions";
    return result;
}

} // namespace mathsolver
