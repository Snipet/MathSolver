#include "mathsolver/printer.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mathsolver/expr.hpp"
#include "mathsolver/rational.hpp"

namespace mathsolver {
namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

constexpr std::array<std::string_view, 10> kGreekNames{
    "alpha", "beta", "gamma", "delta", "epsilon", "theta", "lambda", "mu", "phi", "omega",
};

bool is_greek(std::string_view name) {
    return std::ranges::find(kGreekNames, name) != kGreekNames.end();
}

bool is_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_ascii_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_number(const Expr& e) {
    return e->kind() == Kind::Number;
}

bool is_euler(const Expr& e) {
    return e->kind() == Kind::Constant && e->constant() == ConstantId::E;
}

/// True when -r is representable. With arbitrary-precision numerators this is
/// always the case (the old LLONG_MIN corner is gone), kept for call-site
/// clarity.
bool is_negatable(const Rational& /*r*/) {
    return true;
}

std::string render(const Expr& e, PrintStyle style);

std::string wrap(const std::string& s, PrintStyle style) {
    if (style == PrintStyle::LaTeX) {
        return "\\left(" + s + "\\right)";
    }
    return "(" + s + ")";
}

// ---------------------------------------------------------------------------
// Leaves
// ---------------------------------------------------------------------------

std::string render_number(const Rational& r, PrintStyle style) {
    if (style == PrintStyle::Plain || r.is_integer()) {
        return r.to_string();
    }
    std::string num = std::format("{}", r.num());
    std::string sign;
    if (num.front() == '-') {
        sign = "-";
        num.erase(num.begin());
    }
    return std::format("{}\\frac{{{}}}{{{}}}", sign, num, r.den());
}

std::string render_symbol(const std::string& name, PrintStyle style) {
    const std::size_t us = name.find('_');
    if (style == PrintStyle::LaTeX) {
        std::string base = us == std::string::npos ? name : name.substr(0, us);
        std::string out = is_greek(base) ? "\\" + base : base;
        if (us != std::string::npos && us + 1 < name.size()) {
            out += "_{" + name.substr(us + 1) + "}";
        }
        return out;
    }
    // Plain: unbraced when the subscript re-lexes as a single token (one
    // maximal digit run or exactly one letter), braced otherwise (x_{max}).
    if (us == std::string::npos || us + 1 >= name.size()) {
        return name;
    }
    const std::string sub = name.substr(us + 1);
    const bool unbraced = std::ranges::all_of(sub, is_ascii_digit) ||
                          (sub.size() == 1 && is_ascii_letter(sub.front()));
    if (unbraced) {
        return name;
    }
    return name.substr(0, us) + "_{" + sub + "}";
}

std::string render_constant(ConstantId id, PrintStyle style) {
    if (id == ConstantId::Pi) {
        return style == PrintStyle::LaTeX ? "\\pi" : "pi";
    }
    if (id == ConstantId::I) {
        return "i";
    }
    return "e";
}

// ---------------------------------------------------------------------------
// Pow (operator form b^e, sqrt reconstruction is handled by the callers)
// ---------------------------------------------------------------------------

bool pow_base_needs_parens(const Expr& base) {
    switch (base->kind()) {
        case Kind::Add:
        case Kind::Mul:
        case Kind::Pow:
            return true;
        case Kind::Number:
            return base->number().is_negative() || !base->number().is_integer();
        case Kind::Symbol:
        case Kind::Constant:
        case Kind::Function:
            return false;
    }
    throw std::logic_error("to_string: invalid Kind");
}

bool plain_exponent_needs_parens(const Expr& exponent) {
    switch (exponent->kind()) {
        case Kind::Symbol:
        case Kind::Constant:
            return false;
        case Kind::Number:
            return exponent->number().is_negative() || !exponent->number().is_integer();
        default:
            return true;
    }
}

std::string render_pow_operator(const Expr& base, const Expr& exponent, PrintStyle style) {
    std::string base_str = render(base, style);
    if (pow_base_needs_parens(base)) {
        base_str = wrap(base_str, style);
    }
    if (style == PrintStyle::LaTeX) {
        return base_str + "^{" + render(exponent, style) + "}";
    }
    std::string exp_str = render(exponent, style);
    if (plain_exponent_needs_parens(exponent)) {
        exp_str = "(" + exp_str + ")";
    }
    return base_str + "^" + exp_str;
}

std::string render_sqrt(const Expr& u, PrintStyle style) {
    if (style == PrintStyle::LaTeX) {
        return "\\sqrt{" + render(u, style) + "}";
    }
    return "sqrt(" + render(u, style) + ")";
}

// ---------------------------------------------------------------------------
// Products and division reconstruction (DESIGN.md section 5)
// ---------------------------------------------------------------------------

/// One factor moved below the fraction bar: base raised to a *positive*
/// rational exponent.
struct DenomItem {
    Expr base;
    Rational exponent;
};

/// Renders a denominator item; sets *is_compound when the rendering is a bare
/// sum/product that cannot stand alone after '/' in Plain style.
std::string render_denom_item(const DenomItem& item, PrintStyle style, bool& is_compound) {
    if (item.exponent == Rational(1)) {
        is_compound = item.base->kind() == Kind::Add || item.base->kind() == Kind::Mul;
        return render(item.base, style);
    }
    is_compound = false;
    if (item.exponent == Rational(1, 2)) {
        return render_sqrt(item.base, style);
    }
    return render_pow_operator(item.base, make_num(item.exponent), style);
}

/// LaTeX juxtaposition with the digit-boundary \cdot rule, plus a protective
/// space at letter boundaries so identifier runs do not merge back into known
/// names ("l n" must not re-lex as "ln"). Plain joins with '*'.
std::string join_factors(const std::vector<std::string>& parts, PrintStyle style) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            if (style == PrintStyle::Plain) {
                out += "*";
            } else {
                const char prev = out.back();
                const char next = parts[i].front();
                if (is_ascii_digit(prev) && is_ascii_digit(next)) {
                    out += " \\cdot ";
                } else if (is_ascii_letter(prev) &&
                           (is_ascii_letter(next) || is_ascii_digit(next))) {
                    out += " ";
                }
            }
        }
        out += parts[i];
    }
    return out;
}

/// Renders coeff * factors with coeff >= 0 (the caller extracts the sign).
/// Negative-Number-exponent factors and the coefficient denominator collect
/// into one denominator. Only the FIRST such factor moves below the bar:
/// a denominator holding two or more distinct symbolic factors would re-parse
/// as Pow(Mul(...), -1) instead of the original factors (the §2 make_pow
/// folds only recover a single one), so later ones stay in the numerator as
/// explicit negative powers. Pow(e, ...) never moves (the e^x rule wins).
std::string render_product(Rational coeff, const std::vector<Expr>& factors, PrintStyle style) {
    std::optional<DenomItem> denom;
    struct NumerFactor {
        Expr expr;
        bool explicit_pow;
    };
    std::vector<NumerFactor> numer;
    for (const Expr& f : factors) {
        const bool moves_down = f->kind() == Kind::Pow && is_number(f->arg(1)) &&
                                f->arg(1)->number().is_negative() &&
                                is_negatable(f->arg(1)->number()) && !is_euler(f->arg(0));
        if (moves_down && !denom) {
            denom = DenomItem{f->arg(0), -f->arg(1)->number()};
        } else if (moves_down) {
            numer.push_back({f, true});
        } else {
            numer.push_back({f, false});
        }
    }

    const BigInt& q = coeff.den();
    const bool has_denominator = q != 1 || denom.has_value();

    std::vector<std::string> parts;
    if (coeff.num() != 1 || numer.empty()) {
        parts.push_back(std::format("{}", coeff.num()));
    }
    const std::size_t total_parts = parts.size() + numer.size();
    for (const NumerFactor& nf : numer) {
        if (nf.explicit_pow) {
            parts.push_back(render_pow_operator(nf.expr->arg(0), nf.expr->arg(1), style));
        } else if (nf.expr->kind() == Kind::Add &&
                   (style == PrintStyle::Plain || total_parts > 1 || !has_denominator)) {
            parts.push_back(wrap(render(nf.expr, style), style));
        } else {
            parts.push_back(render(nf.expr, style));
        }
    }
    std::string numerator = join_factors(parts, style);
    if (!has_denominator) {
        return numerator;
    }

    bool item_compound = false;
    std::string item_str;
    if (denom) {
        item_str = render_denom_item(*denom, style, item_compound);
    }
    if (style == PrintStyle::LaTeX) {
        std::vector<std::string> den_parts;
        if (q != 1) {
            den_parts.push_back(std::format("{}", q));
        }
        if (denom) {
            // A sum juxtaposed with the coefficient denominator needs parens
            // (\frac{1}{2x + 2} would re-parse wrong); alone, braces suffice.
            if (q != 1 && denom->base->kind() == Kind::Add) {
                item_str = wrap(item_str, style);
            }
            den_parts.push_back(std::move(item_str));
        }
        return "\\frac{" + numerator + "}{" + join_factors(den_parts, style) + "}";
    }
    std::string den_str;
    if (q != 1 && denom) {
        if (denom->base->kind() == Kind::Add) {
            item_str = "(" + item_str + ")";
        }
        den_str = "(" + std::format("{}", q) + "*" + item_str + ")";
    } else if (q != 1) {
        den_str = std::format("{}", q);
    } else {
        den_str = item_compound ? "(" + item_str + ")" : item_str;
    }
    return numerator + "/" + den_str;
}

/// Splits a term into (is-negative, magnitude rendering) for the subtraction
/// rendering inside sums and the leading '-' of standalone products.
std::pair<bool, std::string> render_signed(const Expr& e, PrintStyle style) {
    if (e->kind() == Kind::Number) {
        const Rational& r = e->number();
        if (r.is_negative() && is_negatable(r)) {
            return {true, render_number(-r, style)};
        }
        return {false, render_number(r, style)};
    }
    if (e->kind() == Kind::Mul) {
        Rational coeff(1);
        auto it = e->args().begin();
        if (is_number(*it)) {
            coeff = (*it)->number();
            ++it;
        }
        const std::vector<Expr> factors(it, e->args().end());
        const bool negative = coeff.is_negative() && is_negatable(coeff);
        if (negative) {
            coeff = -coeff;
        }
        return {negative, render_product(coeff, factors, style)};
    }
    return {false, render(e, style)};
}

std::string render_mul(const Expr& e, PrintStyle style) {
    auto [negative, magnitude] = render_signed(e, style);
    return negative ? "-" + magnitude : magnitude;
}

std::string render_pow(const Expr& e, PrintStyle style) {
    const Expr& base = e->arg(0);
    const Expr& exponent = e->arg(1);
    if (is_euler(base)) {
        return render_pow_operator(base, exponent, style); // e^x wins over sqrt/division
    }
    if (is_number(exponent)) {
        const Rational& r = exponent->number();
        if (r == Rational(1, 2)) {
            return render_sqrt(base, style);
        }
        if (r.is_negative() && is_negatable(r)) {
            return render_product(Rational(1), {e}, style); // 1/x, 1/x^2, 1/sqrt(x)
        }
    }
    return render_pow_operator(base, exponent, style);
}

// ---------------------------------------------------------------------------
// Sums: display ordering by descending total degree (print-time only)
// ---------------------------------------------------------------------------

double factor_degree(const Expr& f) {
    if (f->kind() == Kind::Number) {
        return 0.0;
    }
    if (f->kind() == Kind::Pow && is_number(f->arg(1))) {
        return f->arg(1)->number().to_double();
    }
    return 1.0;
}

double display_degree(const Expr& term) {
    if (term->kind() == Kind::Mul) {
        double degree = 0.0;
        for (const Expr& f : term->args()) {
            degree += factor_degree(f);
        }
        return degree;
    }
    return factor_degree(term);
}

std::string render_add(const Expr& e, PrintStyle style) {
    std::vector<Expr> terms = e->args();
    // Ties keep the canonical arg order: §5's own subtraction example pins
    // Add(x, Mul(-2, y)) -> "x - 2*y", which a literal reverse-compare_expr
    // tie-break (Mul ranks above Symbol) would print as "-2*y + x".
    std::ranges::sort(terms, [](const Expr& a, const Expr& b) {
        const double da = display_degree(a);
        const double db = display_degree(b);
        if (da != db) {
            return da > db;
        }
        return compare_expr(a, b) < 0;
    });
    std::string out;
    for (std::size_t i = 0; i < terms.size(); ++i) {
        auto [negative, magnitude] = render_signed(terms[i], style);
        if (i == 0) {
            out += negative ? "-" + magnitude : magnitude;
        } else {
            out += negative ? " - " : " + ";
            out += magnitude;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

std::string render_function(const Expr& e, PrintStyle style) {
    const std::string arg = render(e->arg(0), style);
    if (style == PrintStyle::Plain) {
        return std::string(function_name(e->function())) + "(" + arg + ")";
    }
    if (e->function() == FunctionId::Abs) {
        // Standard LaTeX has no \abs command; bars are what renderers
        // (KaTeX/MathJax) and humans expect, and the parser accepts them.
        return "\\left|" + arg + "\\right|";
    }
    switch (e->function()) {
        case FunctionId::Gamma:
            return "\\Gamma\\left(" + arg + "\\right)";
        case FunctionId::Digamma:
            return "\\psi\\left(" + arg + "\\right)";
        case FunctionId::Conj:
            // Conjugate is drawn as an overline: conj(z) -> \overline{z}.
            return "\\overline{" + arg + "}";
        case FunctionId::Arg:
            return "\\arg\\left(" + arg + "\\right)";
        case FunctionId::Re:
        case FunctionId::Im:
        case FunctionId::Asinh:
        case FunctionId::Acosh:
        case FunctionId::Atanh:
        case FunctionId::Erf:
        case FunctionId::Erfc:
        case FunctionId::Fib:
        case FunctionId::Harmonic:
            // No dedicated LaTeX commands; \operatorname keeps KaTeX happy.
            return "\\operatorname{" + std::string(function_name(e->function())) +
                   "}\\left(" + arg + "\\right)";
        default: break;
    }
    std::string_view name;
    switch (e->function()) {
        case FunctionId::Asin: name = "arcsin"; break;
        case FunctionId::Acos: name = "arccos"; break;
        case FunctionId::Atan: name = "arctan"; break;
        default: name = function_name(e->function()); break;
    }
    return "\\" + std::string(name) + "\\left(" + arg + "\\right)";
}

std::string render(const Expr& e, PrintStyle style) {
    switch (e->kind()) {
        case Kind::Number: return render_number(e->number(), style);
        case Kind::Symbol: return render_symbol(e->symbol_name(), style);
        case Kind::Constant: return render_constant(e->constant(), style);
        case Kind::Add: return render_add(e, style);
        case Kind::Mul: return render_mul(e, style);
        case Kind::Pow: return render_pow(e, style);
        case Kind::Function: return render_function(e, style);
    }
    throw std::logic_error("to_string: invalid Kind");
}

} // namespace

std::string to_string(const Expr& e, PrintStyle style) {
    return render(e, style);
}

std::string to_string(const Equation& eq, PrintStyle style) {
    return to_string(eq.lhs, style) + " = " + to_string(eq.rhs, style);
}

} // namespace mathsolver
