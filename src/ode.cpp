// Linear constant-coefficient ODE solver (ode.hpp): parse the y-side, take
// Laplace transforms with the initial conditions folded in, decompose Y(s)
// by apart(), and invert term by term.

#include "mathsolver/ode.hpp"

#include <cctype>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mathsolver/apart.hpp"
#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/simplify.hpp"
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

} // namespace

DsolveResult dsolve(std::string_view ode,
                    const std::vector<std::string>& conditions) {
    const std::string text{ode};
    const std::size_t eq_pos = text.find('=');
    if (eq_pos == std::string::npos || text.find('=', eq_pos + 1) != std::string::npos) {
        throw Error("dsolve: the ODE needs exactly one '=', e.g. "
                    "y'' + 3y' + 2y = e^(-t)");
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
    return result;
}

} // namespace mathsolver
