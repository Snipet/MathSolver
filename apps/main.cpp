// MathSolver command-line interface and REPL (DESIGN.md §10).
//
// One-shot subcommands (simplify/expand/factor/solve/diff/integrate/eval/
// subs/collect/latex) print plain style by default; --latex switches to LaTeX. Errors go to stderr with
// caret diagnostics rendered from ParseError spans. Exit codes: 0 success,
// 1 parse/math error, 2 usage error. With no arguments an interactive REPL
// starts (">>> " prompt, plain std::getline — behaves identically when stdin
// is a pipe).
//
// The REPL additionally keeps a session environment of `name := value`
// assignments (docs/proposals/variable-assignment.md; contract condensed in
// DESIGN.md §10). The environment is pure application state: the engine and
// the one-shot subcommands know nothing about it, and applying it means
// composing the existing substitute() primitive over each computing verb's
// input.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mathsolver/mathsolver.hpp"

namespace {

using namespace mathsolver;

constexpr int k_exit_ok = 0;
constexpr int k_exit_error = 1;  // parse/math errors
constexpr int k_exit_usage = 2;  // usage errors

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

/// Usage problem (bad flags, malformed bindings, ambiguous variable, ...).
struct UsageError {
    std::string message;
};

/// A ParseError together with the exact source string it points into, so the
/// caret diagnostic can be rendered at any catch site.
struct DiagnosedParseError {
    std::string source;
    ParseError error;
};

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

/// Render the §4 caret diagnostic:
///     error: unknown command '\fraq'
///         \fraq{1}{2} + x
///         ^~~~~
std::string caret_diagnostic(std::string_view src, const ParseError& err) {
    const std::size_t begin = std::min(err.begin(), src.size());
    const std::size_t end = std::min(std::max(err.end(), begin), src.size());

    // Render each source byte to a fixed display cell so that byte offsets in
    // the span map to display columns: a tab collapses to one space and a
    // newline/return/other control byte becomes a visible escape, all on a
    // single line. The caret padding is built from the same cells, so it lines
    // up with the offending region regardless of whitespace in the source.
    auto cell = [](unsigned char b) -> std::string {
        switch (b) {
        case '\t': return " ";
        case '\n': return "\\n";
        case '\r': return "\\r";
        case '\v': return "\\v";
        case '\f': return "\\f";
        default: return std::string(1, static_cast<char>(b));
        }
    };

    std::string echoed;
    std::string pad;               // spaces spanning the display width of [0, begin)
    std::size_t marker_width = 0;  // display width of the offending region [begin, end)
    for (std::size_t i = 0; i < src.size(); ++i) {
        const std::string c = cell(static_cast<unsigned char>(src[i]));
        echoed += c;
        if (i < begin) {
            pad.append(c.size(), ' ');
        } else if (i < end) {
            marker_width += c.size();
        }
    }

    std::string out = std::format("error: {}\n    {}\n    {}^", err.what(), echoed, pad);
    if (marker_width > 1) {
        out.append(marker_width - 1, '~');
    }
    return out;
}

Expr parse_expression_diag(const std::string& src) {
    try {
        return parse_expression(src);
    } catch (const ParseError& e) {
        throw DiagnosedParseError{src, e};
    }
}

Equation parse_equation_diag(const std::string& src) {
    try {
        return parse_equation(src);
    } catch (const ParseError& e) {
        throw DiagnosedParseError{src, e};
    }
}

std::variant<Expr, Equation> parse_input_diag(const std::string& src) {
    try {
        return parse_input(src);
    } catch (const ParseError& e) {
        throw DiagnosedParseError{src, e};
    }
}

bool is_symbol_name(std::string_view s) {
    if (s.empty() || std::isalpha(static_cast<unsigned char>(s[0])) == 0) {
        return false;
    }
    return std::ranges::all_of(s, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
}

/// Full-consumption double parse ("3", "0.5", "-1e3"); nullopt on failure.
std::optional<double> parse_double(const std::string& s) {
    if (s.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size() || errno == ERANGE) {
        return std::nullopt;
    }
    return v;
}

/// Reject names that can never lex as a single bound variable. A bindable
/// name parses to exactly one Symbol equal to itself — a single letter, a
/// greek name, or a subscripted form (DESIGN §4). Constants (pi, e) parse to
/// a Constant and anything else (function names, multi-letter runs) never
/// yields a lone matching Symbol; neither can be bound, and they get
/// distinct diagnostics. Shared by eval bindings and subs substitutions.
void require_bindable_name(const std::string& name) {
    Expr parsed_name;
    try {
        parsed_name = parse_expression(name);
    } catch (const ParseError&) {
        parsed_name = nullptr;
    }
    if (parsed_name && parsed_name->kind() == Kind::Constant) {
        throw UsageError{std::format("'{}' is a constant and cannot be bound", name)};
    }
    if (!parsed_name || parsed_name->kind() != Kind::Symbol ||
        parsed_name->symbol_name() != name) {
        throw UsageError{std::format(
            "'{}' is not a bindable variable (variables are single letters or greek "
            "names)",
            name)};
    }
}

/// "x=3" -> binding; throws UsageError when malformed.
void add_binding(Bindings& bindings, const std::string& arg) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string::npos) {
        throw UsageError{std::format(
            "malformed binding '{}': expected name=value (e.g. x=3)", arg)};
    }
    const std::string name = trim(arg.substr(0, eq));
    const std::string value_text = trim(arg.substr(eq + 1));
    require_bindable_name(name);

    const std::optional<double> value = parse_double(value_text);
    if (!value) {
        throw UsageError{std::format(
            "malformed binding '{}': '{}' is not a number", arg, value_text)};
    }
    bindings[name] = *value;
}

/// Pick the variable: explicit wins; otherwise the input must have exactly
/// one free symbol. Throws UsageError listing the symbols otherwise.
std::string choose_variable(const std::string& explicit_var,
                            const std::set<std::string>& symbols,
                            std::string_view what) {
    if (!explicit_var.empty()) {
        if (!is_symbol_name(explicit_var)) {
            throw UsageError{
                std::format("'{}' is not a valid variable name", explicit_var)};
        }
        return explicit_var;
    }
    if (symbols.size() == 1) {
        return *symbols.begin();
    }
    if (symbols.empty()) {
        throw UsageError{std::format(
            "cannot infer the variable for {}: the input has no free symbols; "
            "pass the variable explicitly",
            what)};
    }
    std::string list;
    for (const std::string& s : symbols) {
        if (!list.empty()) {
            list += ", ";
        }
        list += s;
    }
    throw UsageError{std::format(
        "cannot infer the variable for {}: the input has {} free symbols ({}); "
        "pass the variable explicitly",
        what, symbols.size(), list)};
}

std::set<std::string> equation_symbols(const Equation& eq) {
    std::set<std::string> syms = free_symbols(eq.lhs);
    syms.merge(free_symbols(eq.rhs));
    return syms;
}

// ---------------------------------------------------------------------------
// Command implementations (shared between one-shot mode and the REPL)
// ---------------------------------------------------------------------------

void run_simplify(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(simplify(std::get<Expr>(parsed)), style));
    } else {
        std::println("{}", to_string(simplify(std::get<Equation>(parsed)), style));
    }
}

void run_expand(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(expand(std::get<Expr>(parsed)), style));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::println("{}", to_string(Equation{expand(eq.lhs), expand(eq.rhs)}, style));
    }
}

/// `trigexpand`: expand trig of sums/multiples into single-angle products.
void run_trigexpand(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(trig_expand(std::get<Expr>(parsed)), style));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::println("{}", to_string(Equation{trig_expand(eq.lhs), trig_expand(eq.rhs)}, style));
    }
}

/// `trigreduce`: products/powers of sin & cos -> sums of multiple-angle trig.
void run_trigreduce(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(trig_reduce(std::get<Expr>(parsed)), style));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::println("{}", to_string(Equation{trig_reduce(eq.lhs), trig_reduce(eq.rhs)}, style));
    }
}

/// `logexpand`: ln of products/quotients/powers -> sums and multiples of ln.
void run_logexpand(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(log_expand(std::get<Expr>(parsed)), style));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::println("{}", to_string(Equation{log_expand(eq.lhs), log_expand(eq.rhs)}, style));
    }
}

/// `logcombine`: a sum of ln terms -> a single ln.
void run_logcombine(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(log_combine(std::get<Expr>(parsed)), style));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::println("{}", to_string(Equation{log_combine(eq.lhs), log_combine(eq.rhs)}, style));
    }
}

void run_factor(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        const Expr& e = std::get<Expr>(parsed);
        // A bare integer factors into primes (2^3 · 3^2 · 5) rather than
        // echoing itself, the way the polynomial factorer would.
        const Expr s = simplify(e);
        if (s->kind() == Kind::Number && s->number().is_integer()) {
            const long long n = s->number().num();
            if (n == 0 || n == 1 || n == -1) {
                std::println("{}", n);
            } else {
                const auto f = factorize(n);
                const std::string sign = n < 0 ? "-" : "";
                std::println("{}{}", sign,
                             style == PrintStyle::LaTeX
                                 ? format_factorization_latex(f)
                                 : format_factorization(f, " * "));
            }
            return;
        }
        std::println("{}", to_string(factor(e), style));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::println("{}", to_string(Equation{factor(eq.lhs), factor(eq.rhs)}, style));
    }
}

/// `cancel` removes the common polynomial factor of a rational expression's
/// numerator and denominator (univariate GCD over Q[x]). The optional
/// variable follows the diff/collect convention: accepted, validated (a name
/// not free in the input is a usage error), and forward-compatible with the
/// multivariate v2 — the v1 engine only ever cancels single-symbol inputs and
/// auto-selects that symbol. A no-op input prints the simplified input, exit 0.
void run_cancel(const std::string& input, const std::string& explicit_var,
                PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    const auto validate = [&](const std::set<std::string>& symbols) {
        if (explicit_var.empty()) return;
        if (!is_symbol_name(explicit_var)) {
            throw UsageError{
                std::format("'{}' is not a valid variable name", explicit_var)};
        }
        if (!symbols.contains(explicit_var)) {
            throw UsageError{std::format(
                "'{}' is not a free variable of the input", explicit_var)};
        }
    };
    if (std::holds_alternative<Expr>(parsed)) {
        const Expr& e = std::get<Expr>(parsed);
        validate(free_symbols(e));
        std::println("{}", to_string(cancel(e), style));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::set<std::string> symbols = free_symbols(eq.lhs);
        for (const std::string& s : free_symbols(eq.rhs)) symbols.insert(s);
        validate(symbols);
        std::println("{}", to_string(cancel(eq), style));
    }
}

/// `together` combines a sum of fractions over a common denominator
/// (`1/x + 1/y → (x + y)/(x*y)`). It is multivariate and takes no variable
/// argument. A no-op input prints the simplified input, exit 0.
void run_together(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(together(std::get<Expr>(parsed)), style));
    } else {
        std::println("{}", to_string(together(std::get<Equation>(parsed)), style));
    }
}

/// `latex` is a pure format conversion: it prints the parsed AST in LaTeX
/// without simplifying first.
void run_latex(const std::string& input) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(std::get<Expr>(parsed), PrintStyle::LaTeX));
    } else {
        std::println("{}", to_string(std::get<Equation>(parsed), PrintStyle::LaTeX));
    }
}

void run_diff(const std::string& input, const std::string& explicit_var,
              PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = choose_variable(explicit_var, free_symbols(e), "diff");
    std::println("{}", to_string(differentiate(e, var), style));
}

/// `laplace` maps f(t) -> F(s); the time variable defaults to t and may be
/// given explicitly (any name but s). Errors from the transform (e.g. no rule
/// for the input) propagate as normal Error diagnostics.
void run_laplace(const std::string& input, const std::string& explicit_var,
                 PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = explicit_var.empty() ? "t" : explicit_var;
    std::println("{}", to_string(laplace(e, var), style));
}

/// `ilaplace` maps F(s) -> f(t); the frequency variable defaults to s.
void run_ilaplace(const std::string& input, const std::string& explicit_var,
                  PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = explicit_var.empty() ? "s" : explicit_var;
    std::println("{}", to_string(inverse_laplace(e, var), style));
}

/// `collect` regroups the expression as a polynomial in the variable (§7
/// collect); the variable is inferred exactly like diff when omitted.
void run_collect(const std::string& input, const std::string& explicit_var,
                 PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = choose_variable(explicit_var, free_symbols(e), "collect");
    std::println("{}", to_string(collect(e, var), style));
}

/// `apart` expands a rational function into partial fractions; the variable
/// is inferred exactly like collect when omitted.
void run_apart(const std::string& input, const std::string& explicit_var,
               PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = choose_variable(explicit_var, free_symbols(e), "apart");
    std::println("{}", to_string(apart(e, var), style));
}

/// `fit`: least-squares regression of "x,y; x,y; ..." data. The model defaults
/// to linear; a named poly alias (quadratic/cubic/quartic) or "poly <degree>"
/// fits a polynomial exactly over the rationals; exp/power/log fit linearized
/// numeric models. Prints the fitted expression, then `model:` and `R^2:` notes.
void run_fit(const std::string& input, const std::string& model_text,
             const std::string& degree_text, PrintStyle style) {
    auto [xs, ys] = parse_point_data(input);
    const std::string name = model_text.empty() ? "linear" : model_text;
    const auto spec = parse_fit_model(name);
    if (!spec) {
        throw UsageError{std::format(
            "unknown fit model '{}' (linear, quadratic, cubic, quartic, poly, "
            "exp, power, log)",
            name)};
    }
    auto [model, degree] = *spec;
    if (model == FitModel::Poly && degree < 0) { // generic "poly": read degree
        degree = 2;
        if (!degree_text.empty()) {
            const auto d = parse_double(degree_text);
            if (!d || *d != std::floor(*d)) {
                throw UsageError{std::format(
                    "polynomial degree must be an integer, got '{}'", degree_text)};
            }
            degree = static_cast<int>(*d);
        }
    }
    const FitResult r = fit(xs, ys, model, degree, "x");
    if (r.status != FitResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", to_string(r.expr, style));
    std::println("model: {}{}", r.model, r.exact ? " (exact)" : "");
    std::println("R^2: {:.6g}", r.r2);
}

/// `interp`: exact polynomial interpolation of "x,y; x,y; ..." data — the
/// unique polynomial through the points (degree ≤ n−1), exact over the
/// rationals. Prints the polynomial, then a `degree:` note.
void run_interp(const std::string& input, PrintStyle style) {
    auto [xs, ys] = parse_point_data(input);
    const InterpResult r = interp(xs, ys, "x");
    if (r.status != InterpResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", to_string(r.expr, style));
    std::println("degree: {}{}", r.degree, r.exact ? " (exact)" : "");
}

/// `newton`/`lagrange`: the interpolating polynomial through the points, kept
/// in its factored construction form rather than expanded.
void run_interp_form(const std::string& input, InterpForm form, PrintStyle style) {
    auto [xs, ys] = parse_point_data(input);
    const InterpFormResult r = interp_form(xs, ys, "x", form);
    if (r.status != InterpFormResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", to_string(r.expr, style));
    for (const std::string& note : r.notes) std::println("  {}", note);
}

/// `chebyshev`/`legendre`/`hermite`/`laguerre`: the exact degree-n orthogonal
/// polynomial of the family in the given variable (default `x`).
void run_orthopoly(OrthoFamily fam, const std::string& n_text,
                   const std::string& var_text, PrintStyle style) {
    const std::string nt = trim(n_text);
    int n = 0;
    try {
        std::size_t pos = 0;
        n = std::stoi(nt, &pos);
        if (pos != nt.size()) throw std::invalid_argument("trailing");
    } catch (const std::exception&) {
        throw UsageError{std::format("expected an integer degree, got '{}'", n_text)};
    }
    const std::string var = var_text.empty() ? "x" : trim(var_text);
    const OrthoPolyResult r = ortho_poly(fam, n, var);
    if (r.status != OrthoPolyResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", to_string(r.expr, style));
    std::println("{}, degree {}", r.family, r.degree);
}

/// `stats`: exact summary statistics of a data list (mean, median, quartiles,
/// spread). Each statistic prints as `label = value`; values are exact
/// (fractions / radicals) when the data are rational.
void run_stats(const std::string& input, PrintStyle style) {
    const std::vector<std::string> data = parse_stat_data(input);
    const StatsResult r = compute_stats(data);
    if (r.status != StatsResult::Status::Ok) throw UsageError{r.message};
    for (const StatItem& s : r.items) {
        std::println("{} = {}", s.label, to_string(s.value, style));
    }
}

/// `vandermonde`: the square Vandermonde matrix of a comma-separated node list.
void run_vandermonde(const std::string& input, PrintStyle style) {
    // Split on top-level commas/semicolons (respecting parens) so a node may be
    // any scalar expression, e.g. `1/2, a + 1, x^2`.
    std::vector<Expr> nodes;
    std::string cur;
    int depth = 0;
    auto flush = [&]() {
        const std::string t = trim(cur);
        if (!t.empty()) nodes.push_back(parse_expression_diag(t));
        cur.clear();
    };
    for (char ch : input) {
        if (ch == '(' || ch == '[' || ch == '{') depth++;
        else if (ch == ')' || ch == ']' || ch == '}') depth = depth > 0 ? depth - 1 : 0;
        if ((ch == ',' || ch == ';') && depth == 0) flush();
        else cur.push_back(ch);
    }
    flush();
    const VandermondeResult r = vandermonde_matrix(nodes);
    if (r.status != VandermondeResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", mat_to_string(r.matrix, style));
}

/// `polydiv`: polynomial long division, printing the quotient and remainder.
void run_polydiv(const std::string& dividend, const std::string& divisor,
                 const std::string& explicit_var, PrintStyle style) {
    const Expr n = parse_expression_diag(dividend);
    const Expr d = parse_expression_diag(divisor);
    std::set<std::string> syms = free_symbols(n);
    for (const std::string& s : free_symbols(d)) syms.insert(s);
    const std::string var = choose_variable(explicit_var, syms, "polydiv");
    const PolyDivResult r = polynomial_divide(n, d, var);
    if (r.status != PolyDivResult::Status::Ok) throw UsageError{r.message};
    std::println("quotient: {}", to_string(r.quotient, style));
    std::println("remainder: {}", to_string(r.remainder, style));
}

/// `polygcd` / `polylcm`: the monic GCD or LCM of two polynomials.
void run_polygcd(const std::string& a, const std::string& b,
                 const std::string& explicit_var, bool lcm, PrintStyle style) {
    const Expr ea = parse_expression_diag(a);
    const Expr eb = parse_expression_diag(b);
    std::set<std::string> syms = free_symbols(ea);
    for (const std::string& s : free_symbols(eb)) syms.insert(s);
    const std::string var = choose_variable(explicit_var, syms, lcm ? "polylcm" : "polygcd");
    const PolyGcdResult r = lcm ? polynomial_lcm(ea, eb, var) : polynomial_gcd(ea, eb, var);
    if (r.status != PolyGcdResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", to_string(r.value, style));
}

/// `resultant`: the resultant of two polynomials (zero iff they share a root).
void run_resultant(const std::string& a, const std::string& b,
                   const std::string& explicit_var, PrintStyle style) {
    const Expr ea = parse_expression_diag(a);
    const Expr eb = parse_expression_diag(b);
    std::set<std::string> syms = free_symbols(ea);
    for (const std::string& s : free_symbols(eb)) syms.insert(s);
    const std::string var = choose_variable(explicit_var, syms, "resultant");
    const PolyGcdResult r = polynomial_resultant(ea, eb, var);
    if (r.status != PolyGcdResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", to_string(r.value, style));
}

/// `bezout`: the monic gcd of two polynomials plus the Bézout cofactors s, t
/// with s·a + t·b = gcd.
void run_bezout(const std::string& a, const std::string& b,
                const std::string& explicit_var, PrintStyle style) {
    const Expr ea = parse_expression_diag(a);
    const Expr eb = parse_expression_diag(b);
    std::set<std::string> syms = free_symbols(ea);
    for (const std::string& s : free_symbols(eb)) syms.insert(s);
    const std::string var = choose_variable(explicit_var, syms, "bezout");
    const PolyBezoutResult r = polynomial_bezout(ea, eb, var);
    if (r.status != PolyBezoutResult::Status::Ok) throw UsageError{r.message};
    std::println("gcd: {}", to_string(r.gcd, style));
    std::println("s: {}", to_string(r.s, style));
    std::println("t: {}", to_string(r.t, style));
}

/// `companion`: the companion matrix of a univariate polynomial (MATLAB
/// `compan` orientation), whose eigenvalues are the polynomial's roots.
void run_companion(const std::string& input, const std::string& explicit_var,
                   PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = choose_variable(explicit_var, free_symbols(e), "companion");
    const CompanionResult r = companion_matrix(e, var);
    if (r.status != CompanionResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", mat_to_string(r.matrix, style));
}

/// `discriminant`: the discriminant of a polynomial (degree 2–4), symbolic
/// coefficients kept symbolic; the variable is inferred like diff when omitted.
void run_discriminant(const std::string& input, const std::string& explicit_var,
                      PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = choose_variable(explicit_var, free_symbols(e), "discriminant");
    const DiscriminantResult r = discriminant(e, var);
    if (r.status != DiscriminantResult::Status::Ok) throw UsageError{r.message};
    std::println("{}", to_string(r.value, style));
    if (!r.root_nature.empty()) std::println("roots: {}", r.root_nature);
}

/// `series`: Taylor expansion about a center (default 0) to an order
/// (default 6); the variable is inferred like diff when omitted.
void run_series(const std::string& input, const std::string& explicit_var,
                const std::string& center_text, const std::string& order_text,
                PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = choose_variable(explicit_var, free_symbols(e), "series");
    int order = 6;
    if (!order_text.empty()) {
        const auto n = parse_double(order_text);
        if (!n || *n != std::floor(*n)) {
            throw UsageError{std::format("series order must be an integer, got '{}'",
                                         order_text)};
        }
        order = static_cast<int>(*n);
    }
    if (center_text == "inf" || center_text == "oo") {
        std::println("{}", to_string(series_at_infinity(e, var, order), style));
        return;
    }
    const Expr center =
        center_text.empty() ? make_num(0) : parse_expression_diag(center_text);
    std::println("{}", to_string(series(e, var, center, order), style));
}

/// `pade`: the [m/n] Padé approximant P(x)/Q(x) matching the Maclaurin series
/// of the input through order m + n. m and n are required non-negative
/// integers; the variable is inferred unless given.
void run_pade(const std::string& input, const std::string& m_text,
              const std::string& n_text, const std::string& explicit_var,
              PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    const std::string var = choose_variable(explicit_var, free_symbols(e), "pade");
    const auto order = [](const std::string& t, const char* what) -> int {
        const auto d = parse_double(t);
        if (!d || *d != std::floor(*d) || *d < 0) {
            throw UsageError{std::format(
                "pade {} must be a non-negative integer, got '{}'", what, t)};
        }
        return static_cast<int>(*d);
    };
    if (m_text.empty() || n_text.empty()) {
        throw UsageError{"usage: pade <expression>, <m>, <n>[, <variable>]"};
    }
    const int m = order(m_text, "numerator degree m");
    const int n = order(n_text, "denominator degree n");
    std::println("{}", to_string(pade(e, var, m, n).approximant, style));
}

/// Reduce `lhs = rhs` to the polynomial `(lhs) - (rhs)` so root verbs accept an
/// equation; a bare expression (roots of expr = 0) is returned unchanged.
std::string equation_to_poly(const std::string& input) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '=') continue;
        const char prev = i > 0 ? input[i - 1] : '\0';
        const char next = i + 1 < input.size() ? input[i + 1] : '\0';
        if (prev != '<' && prev != '>' && prev != '!' && prev != '=' && next != '=') {
            return "(" + input.substr(0, i) + ") - (" + input.substr(i + 1) + ")";
        }
    }
    return input;
}

Rational bound_rational(const std::string& text) {
    const Expr s = simplify(parse_expression_diag(text));
    if (s->kind() != Kind::Number) {
        throw UsageError{
            std::format("bound must be a rational number, got '{}'", text)};
    }
    return s->number();
}

/// `rootcount`: the number of distinct real roots of a rational-coefficient
/// polynomial (Sturm's theorem), over all of R or a given (lo, hi] interval.
void run_rootcount(const std::string& input, const std::string& explicit_var,
                   const std::string& lo_text, const std::string& hi_text) {
    const Expr e = parse_expression_diag(equation_to_poly(input));
    const std::string var = choose_variable(explicit_var, free_symbols(e), "rootcount");
    std::optional<Rational> lo, hi;
    if (!lo_text.empty() || !hi_text.empty()) {
        if (lo_text.empty() || hi_text.empty()) {
            throw UsageError{"give both interval bounds: rootcount <poly>, <var>, <lo>, <hi>"};
        }
        lo = bound_rational(lo_text);
        hi = bound_rational(hi_text);
    }
    const int n = sturm_root_count(e, var, lo, hi);
    const char* noun = n == 1 ? "root" : "roots";
    if (lo && hi) {
        std::println("{} distinct real {} in ({}, {}]", n, noun, lo->to_string(),
                     hi->to_string());
    } else {
        std::println("{} distinct real {}", n, noun);
    }
}

/// `isolate`: a disjoint rational interval around every distinct real root
/// (exact rationals reported exactly), with a numeric approximation.
void run_isolate(const std::string& input, const std::string& explicit_var,
                 PrintStyle style) {
    const Expr e = parse_expression_diag(equation_to_poly(input));
    const std::string var = choose_variable(explicit_var, free_symbols(e), "isolate");
    const std::vector<RootInterval> roots = sturm_isolate_roots(e, var);
    std::println("{} distinct real {}{}", roots.size(),
                 roots.size() == 1 ? "root" : "roots", roots.empty() ? "" : ":");
    for (const RootInterval& r : roots) {
        if (r.exact) {
            const std::string val = to_string(make_num(r.lo), style);
            if (r.lo.is_integer()) {
                std::println("  {} = {}", var, val);
            } else {
                std::println("  {} = {}  (≈ {:.10g})", var, val, r.approx);
            }
        } else {
            std::println("  {} ≈ {:.10g}   in ({:.10g}, {:.10g})", var, r.approx,
                         r.lo.to_double(), r.hi.to_double());
        }
    }
}

/// `stirling`: the Stirling asymptotic series for ln Gamma(var) with exact
/// Bernoulli coefficients; the lgamma accuracy check prints as notes.
void run_stirling(const std::string& var_text, const std::string& terms_text,
                  PrintStyle style) {
    std::string var = "x";
    if (!var_text.empty()) {
        const Expr ve = parse_expression_diag(var_text);
        if (ve->kind() != Kind::Symbol) {
            throw UsageError{std::format(
                "stirling: the first argument must be a variable name, got "
                "'{}'",
                var_text)};
        }
        var = to_string(ve, PrintStyle::Plain);
    }
    int terms = 3;
    if (!terms_text.empty()) {
        const auto n = parse_double(terms_text);
        if (!n || *n != std::floor(*n)) {
            throw UsageError{std::format(
                "stirling terms must be an integer, got '{}'", terms_text)};
        }
        terms = static_cast<int>(*n);
    }
    const StirlingResult r = stirling_series(var, terms);
    std::println("ln Gamma({}) ~ {}", var, to_string(r.series, style));
    for (const std::string& c : r.checks) {
        std::println("note: {}", c);
    }
    std::println("note: ln n! = ln Gamma(n + 1); the full series diverges "
                 "for fixed {} — truncation, not convergence",
                 var);
}

/// Parse one number-theory argument (a literal or an exact expression like
/// `2^4`) to a 64-bit integer, rejecting non-integers with a usage error.
long long parse_integer_arg(const std::string& text, std::string_view verb) {
    const Expr e = simplify(parse_expression_diag(text));
    if (e->kind() != Kind::Number || !e->number().is_integer()) {
        throw UsageError{
            std::format("{}: expected an integer, got '{}'", verb, trim(text))};
    }
    return e->number().num();
}

/// Split a number-theory argument blob on commas / semicolons / whitespace
/// and parse each token to an integer (at least one required).
std::vector<long long> parse_integer_list(const std::string& input,
                                          std::string_view verb) {
    std::vector<long long> out;
    for (const std::string& t : parse_stat_data(input)) {
        out.push_back(parse_integer_arg(t, verb));
    }
    if (out.empty()) {
        throw UsageError{std::format("{}: expected at least one integer", verb)};
    }
    return out;
}

/// `gcd` / `lcm`: greatest common divisor / least common multiple of a list of
/// integers, folded pairwise over the exact int64 routines.
void run_gcd(const std::string& input) {
    long long g = 0;
    for (long long x : parse_integer_list(input, "gcd")) g = int_gcd(g, x);
    std::println("{}", g);
}
void run_lcm(const std::string& input) {
    long long l = 1;
    for (long long x : parse_integer_list(input, "lcm")) l = int_lcm(l, x);
    std::println("{}", l);
}

/// `isprime`: deterministic primality of a single integer.
void run_isprime(const std::string& input) {
    const std::vector<long long> v = parse_integer_list(input, "isprime");
    if (v.size() != 1) throw UsageError{"isprime takes a single integer"};
    std::println("{} is {}", v[0], is_prime(v[0]) ? "prime" : "composite");
}

/// `nextprime`: the smallest prime greater than a single integer.
void run_nextprime(const std::string& input) {
    const std::vector<long long> v = parse_integer_list(input, "nextprime");
    if (v.size() != 1) throw UsageError{"nextprime takes a single integer"};
    std::println("{}", next_prime(v[0]));
}

/// `totient`: Euler's phi of a single positive integer.
void run_totient(const std::string& input) {
    const std::vector<long long> v = parse_integer_list(input, "totient");
    if (v.size() != 1) throw UsageError{"totient takes a single integer"};
    if (v[0] < 1) throw UsageError{"totient is defined for positive integers"};
    std::println("{}", euler_totient(v[0]));
}

/// `divisors`: all positive divisors of a single non-zero integer, ascending.
void run_divisors(const std::string& input) {
    const std::vector<long long> v = parse_integer_list(input, "divisors");
    if (v.size() != 1) throw UsageError{"divisors takes a single integer"};
    if (v[0] == 0) throw UsageError{"divisors of 0 is undefined"};
    std::string out;
    for (long long d : divisors(v[0])) {
        if (!out.empty()) out += ", ";
        out += std::to_string(d);
    }
    std::println("{}", out);
}

/// `mod` / `powmod` / `modinv`: modular arithmetic on integers. `mod a, m`,
/// `powmod b, e, m`, `modinv a, m`.
void run_mod(const std::string& input) {
    const std::vector<long long> v = parse_integer_list(input, "mod");
    if (v.size() != 2) throw UsageError{"usage: mod <a>, <m>"};
    std::println("{}", int_mod(v[0], v[1]));
}
void run_powmod(const std::string& input) {
    const std::vector<long long> v = parse_integer_list(input, "powmod");
    if (v.size() != 3) throw UsageError{"usage: powmod <base>, <exponent>, <modulus>"};
    std::println("{}", pow_mod(v[0], v[1], v[2]));
}
void run_modinv(const std::string& input) {
    const std::vector<long long> v = parse_integer_list(input, "modinv");
    if (v.size() != 2) throw UsageError{"usage: modinv <a>, <m>"};
    std::println("{}", mod_inverse(v[0], v[1]));
}

/// `crt`: Chinese remainder theorem. `crt <r1, r2, …; m1, m2, …>` — residues
/// before the ';', moduli after.
void run_crt(const std::string& input) {
    const auto semi = input.find(';');
    if (semi == std::string::npos) {
        throw UsageError{"usage: crt <r1, r2, …; m1, m2, …>  (residues ; moduli)"};
    }
    const std::vector<long long> residues =
        parse_integer_list(input.substr(0, semi), "crt");
    const std::vector<long long> moduli =
        parse_integer_list(input.substr(semi + 1), "crt");
    const Crt r = crt_solve(residues, moduli);
    std::println("{} (mod {})", r.residue, r.modulus);
}

/// Route an expression to the right continued-fraction routine: an exact
/// rational (finite), sqrt(n) for integer n (periodic), or anything else
/// (numeric, via double).
CFrac cfrac_of(const Expr& e) {
    const Expr s = simplify(e);
    if (s->kind() == Kind::Number) {
        const Rational r = s->number();
        return cf_rational(r.num(), r.den());
    }
    if (s->kind() == Kind::Pow) {
        const Expr& base = s->arg(0);
        const Expr& exp = s->arg(1);
        if (base->kind() == Kind::Number && base->number().is_integer() &&
            base->number().num() >= 1 && exp->kind() == Kind::Number &&
            exp->number() == Rational(1, 2)) {
            return cf_sqrt(base->number().num());
        }
    }
    return cf_numeric(evaluate(s, Bindings{}));
}

/// `cfrac`: continued fraction of a rational, sqrt(n), or real, with
/// convergents (the successive best rational approximations).
void run_cfrac(const std::string& input, PrintStyle style) {
    const CFrac cf = cfrac_of(parse_expression_diag(input));
    std::println("{}", style == PrintStyle::LaTeX ? format_cfrac_latex(cf)
                                                  : format_cfrac(cf));
    if (!cf.convergents.empty()) {
        std::string cs;
        for (const auto& [p, q] : cf.convergents) {
            if (!cs.empty()) cs += ", ";
            cs += q == 1 ? std::to_string(p)
                         : std::to_string(p) + "/" + std::to_string(q);
        }
        std::println("convergents: {}", cs);
    }
    if (!cf.exact) {
        std::println("note: numeric expansion (double precision) — later terms "
                     "and convergents are approximate");
    }
}

/// `seq`: recognize the pattern behind a list of exact terms.
void run_seq(const std::vector<std::string>& term_texts, PrintStyle style) {
    std::vector<Rational> terms;
    for (const std::string& t : term_texts) {
        const Expr e = simplify(parse_expression_diag(t));
        if (e->kind() != Kind::Number) {
            throw UsageError{std::format(
                "seq terms must be exact numbers, got '{}'", t)};
        }
        terms.push_back(e->number());
    }
    const SeqResult r = recognize_sequence(terms);
    std::println("pattern: {}", r.description);
    if (r.formula) {
        std::println("a(n) = {}   (n = 0, 1, 2, ...)",
                     to_string(r.formula, style));
    }
    if (!r.recurrence.empty()) {
        std::println("recurrence: {}", r.recurrence);
    }
    if (!r.next.empty()) {
        std::string nx;
        for (const Rational& v : r.next) {
            if (!nx.empty()) nx += ", ";
            nx += v.to_string();
        }
        std::println("next: {}", nx);
    }
    for (const std::string& w : r.warnings) {
        std::println("warning: {}", w);
    }
}

std::vector<std::string> split_top_level(const std::string& s, char delim);

void print_sum_result(const SumResult& r, const char* noun, PrintStyle style) {
    switch (r.status) {
        case SumResult::Status::Exact:
            std::println("{} = {}", noun, to_string(r.value, style));
            break;
        case SumResult::Status::Diverges:
            std::println("the {} diverges", noun);
            break;
        case SumResult::Status::Unsolved:
            std::println("unable to find a closed form");
            break;
    }
    if (!r.method.empty()) {
        std::println("method: {}", r.method);
    }
    for (const std::string& w : r.warnings) {
        std::println("warning: {}", w);
    }
}

/// `sum <term> <var> <lo> <hi>` (hi accepts inf) and `product` likewise.
void run_sum(const std::string& input, const std::vector<std::string>& args,
             bool is_product, PrintStyle style) {
    if (args.size() != 4) {
        throw UsageError{std::format(
            "usage: {} <term>, <variable>, <lo>, <hi>{}",
            is_product ? "product" : "sum", is_product ? "" : "   (hi may be inf)")};
    }
    const Expr term = parse_expression_diag(input);
    const std::string& var = args[1];
    const Expr lo = parse_expression_diag(args[2]);
    if (!is_product && (args[3] == "inf" || args[3] == "oo")) {
        print_sum_result(sum_infinite(term, var, lo), "sum", style);
        return;
    }
    const Expr hi = parse_expression_diag(args[3]);
    if (is_product) {
        print_sum_result(product_finite(term, var, lo, hi), "product", style);
    } else {
        print_sum_result(sum_finite(term, var, lo, hi), "sum", style);
    }
}

/// `rsolve <recurrence>[, a(0)=v, ...]` — mirrors dsolve's shape.
void run_rsolve(const std::string& input, PrintStyle style) {
    const std::vector<std::string> parts = split_top_level(input, ',');
    const RsolveResult res = rsolve(parts[0], {parts.begin() + 1, parts.end()});
    std::println("a(n) = {}", to_string(res.solution, style));
    std::println("method: {}", res.method);
    for (const std::string& w : res.warnings) {
        std::println("warning: {}", w);
    }
}

/// `limit <expr> <var> <point> [left|right]`. The point accepts inf/-inf/oo.
void run_limit(const std::string& input, const std::string& var,
               const std::string& point_text, const std::string& dir_text,
               PrintStyle style) {
    const Expr e = parse_expression_diag(input);
    if (var.empty() || point_text.empty()) {
        throw UsageError{
            "usage: limit <expression>, <variable>, <point>[, left|right]"};
    }
    int dir = 0;
    if (dir_text == "left") {
        dir = -1;
    } else if (dir_text == "right") {
        dir = +1;
    } else if (!dir_text.empty() && dir_text != "both") {
        throw UsageError{std::format(
            "the limit direction must be left, right, or both (got '{}')",
            dir_text)};
    }
    LimitResult r;
    if (point_text == "inf" || point_text == "+inf" || point_text == "oo") {
        r = limit_at_infinity(e, var, true);
    } else if (point_text == "-inf" || point_text == "-oo") {
        r = limit_at_infinity(e, var, false);
    } else {
        r = limit(e, var, parse_expression_diag(point_text), dir);
    }
    switch (r.status) {
        case LimitResult::Status::Exact:
            std::println("limit = {}", to_string(r.value, style));
            break;
        case LimitResult::Status::Numeric:
            std::println("limit ≈ {:.10g}", evaluate(r.value, Bindings{}));
            break;
        case LimitResult::Status::Diverges:
            std::println("limit = {}", r.sign > 0   ? "+inf"
                                       : r.sign < 0 ? "-inf"
                                                    : "inf (unsigned)");
            break;
        case LimitResult::Status::DoesNotExist:
            std::println("the limit does not exist");
            break;
        case LimitResult::Status::Unsolved:
            std::println("unable to determine the limit");
            break;
    }
    if (!r.method.empty()) {
        std::println("method: {}", r.method);
    }
    for (const std::string& w : r.warnings) {
        std::println("warning: {}", w);
    }
}

/// `mlimit <f> <x> <a> <y> <b>`: two-variable limit by path sampling.
void run_mlimit(const std::string& input, const std::vector<std::string>& args,
                PrintStyle style) {
    if (args.size() != 5) {
        throw UsageError{
            "usage: mlimit <expression>, <x var>, <a>, <y var>, <b>"};
    }
    const Expr e = parse_expression_diag(input);
    const LimitResult r =
        limit_multi(e, args[1], parse_expression_diag(args[2]), args[3],
                    parse_expression_diag(args[4]));
    switch (r.status) {
        case LimitResult::Status::Exact:
            std::println("limit = {}", to_string(r.value, style));
            break;
        case LimitResult::Status::Numeric:
            std::println("limit ≈ {:.10g}", evaluate(r.value, Bindings{}));
            break;
        case LimitResult::Status::Diverges:
            std::println("limit = {}", r.sign > 0   ? "+inf"
                                       : r.sign < 0 ? "-inf"
                                                    : "inf (unsigned)");
            break;
        case LimitResult::Status::DoesNotExist:
            std::println("the limit does not exist");
            break;
        case LimitResult::Status::Unsolved:
            std::println("unable to determine the limit");
            break;
    }
    if (!r.method.empty()) {
        std::println("method: {}", r.method);
    }
    for (const std::string& w : r.warnings) {
        std::println("warning: {}", w);
    }
}

/// Vector-calculus verbs (grad/div/curl/laplacian/jacobian/hessian). The
/// first positional is the scalar field or a ';'-separated vector field; the
/// remaining positionals are the variables (in order). Scalar operators
/// print a vector/matrix, div/curl2d/laplacian print a scalar.
void run_vector(const std::string& sub, const std::vector<std::string>& positionals,
                PrintStyle style) {
    if (positionals.size() < 2) {
        throw UsageError{std::format(
            "usage: mathsolver {} \"<field>\" <var> [<var> ...]   (a vector "
            "field is ';'-separated, e.g. \"x*y; y*z; z*x\")",
            sub)};
    }
    std::vector<std::string> vars(positionals.begin() + 1, positionals.end());
    ExprVec field;
    for (const std::string& comp : split_top_level(positionals[0], ';')) {
        field.push_back(parse_expression_diag(comp));
    }
    const bool wants_scalar =
        sub == "grad" || sub == "laplacian" || sub == "hessian";
    if (wants_scalar && field.size() != 1) {
        throw UsageError{std::format(
            "{} takes a single scalar field, not {} ';'-separated components",
            sub, field.size())};
    }
    const Expr scalar = field.front();
    if (sub == "grad") {
        std::println("{}", vec_to_string(gradient(scalar, vars), style));
    } else if (sub == "div") {
        std::println("{}", to_string(divergence(field, vars), style));
    } else if (sub == "curl") {
        if (field.size() == 2 && vars.size() == 2) {
            std::println("{}", to_string(curl2d(field, vars), style));
        } else {
            std::println("{}", vec_to_string(curl(field, vars), style));
        }
    } else if (sub == "laplacian") {
        std::println("{}", to_string(laplacian(scalar, vars), style));
    } else if (sub == "jacobian") {
        std::println("{}", mat_to_string(jacobian(field, vars), style));
    } else if (sub == "hessian") {
        std::println("{}", mat_to_string(hessian(scalar, vars), style));
    }
}

/// `integrate` (DESIGN.md §8b): indefinite prints `F(x) + C`, definite
/// (--from/--to, both or neither) prints `value = ...` / `value ≈ ...`;
/// Unsolved prints `unable to integrate` and still exits 0 — it is an answer.
void run_integrate(const std::string& input, const std::string& explicit_var,
                   const std::optional<std::string>& from_text,
                   const std::optional<std::string>& to_text, PrintStyle style) {
    if (from_text.has_value() != to_text.has_value()) {
        throw UsageError{"--from and --to must be given together"};
    }
    const Expr e = parse_expression_diag(input);
    const std::string var =
        choose_variable(explicit_var, free_symbols(e), "integrate");

    if (!from_text) {
        const IntegrateResult res = integrate(e, var);
        if (res.status == IntegrateResult::Status::Integrated) {
            std::println("{} + C", to_string(res.antiderivative, style));
        } else {
            std::println("unable to integrate");
        }
        if (!res.method.empty()) {
            std::println("method: {}", res.method);
        }
        for (const std::string& w : res.warnings) {
            std::println("warning: {}", w);
        }
        return;
    }

    Expr lo;
    Expr hi;
    try {
        lo = parse_expression_diag(*from_text);
        hi = parse_expression_diag(*to_text);
    } catch (const Error&) {
        // Constant folding during parsing can throw (e.g. --to "1/0"); the
        // §8b bounds contract answers Unsolved, not a hard error.  Syntax
        // errors throw DiagnosedParseError and keep their caret diagnostic.
        std::println("unable to integrate");
        std::println("warning: integration bounds must evaluate to finite numbers");
        return;
    }
    const DefiniteIntegralResult res = integrate_definite(e, var, lo, hi);
    switch (res.status) {
    case DefiniteIntegralResult::Status::Exact:
        std::println("value = {}", to_string(res.value, style));
        break;
    case DefiniteIntegralResult::Status::Numeric:
        std::println("value ≈ {}", res.value->number().to_double());
        break;
    case DefiniteIntegralResult::Status::Unsolved:
        std::println("unable to integrate");
        break;
    }
    if (!res.method.empty()) {
        std::println("method: {}", res.method);
    }
    for (const std::string& w : res.warnings) {
        std::println("warning: {}", w);
    }
}

/// Render a complex value as a + b*i, chopping rounding dust so Euler's
/// formula reads cleanly (e^(i*pi) -> -1, not -1 + 1.2e-16*i).
std::string format_complex(std::complex<double> z) {
    double re = z.real();
    double im = z.imag();
    const double scale = std::max({1.0, std::abs(re), std::abs(im)});
    if (std::abs(re) < 1e-12 * scale) re = 0.0;
    if (std::abs(im) < 1e-12 * scale) im = 0.0;
    if (im == 0.0) {
        return std::format("{}", re);
    }
    const auto imag_only = [](double v) {
        if (v == 1.0) return std::string("i");
        if (v == -1.0) return std::string("-i");
        return std::format("{}*i", v);
    };
    if (re == 0.0) {
        return imag_only(im);
    }
    const std::string sign = im < 0.0 ? " - " : " + ";
    const double mag = std::abs(im);
    const std::string term = mag == 1.0 ? "i" : std::format("{}*i", mag);
    return std::format("{}{}{}", re, sign, term);
}

void run_eval(const std::string& input, const Bindings& bindings) {
    const Expr e = parse_expression_diag(input);
    // A complex expression (contains the imaginary unit) evaluates over C; the
    // real path stays real-only and its verification guarantees intact.
    if (contains_constant(e, ConstantId::I)) {
        ComplexBindings cb;
        for (const auto& [name, value] : bindings) {
            cb.emplace(name, std::complex<double>(value, 0.0));
        }
        std::println("{}", format_complex(evaluate_complex(e, cb)));
        return;
    }
    std::println("{}", evaluate(e, bindings));
}

/// "x=y+1" -> (name, replacement); throws UsageError when malformed. Unlike
/// an eval binding the value side is parsed as a full expression.
std::pair<std::string, Expr> parse_substitution(const std::string& arg) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string::npos) {
        throw UsageError{std::format(
            "malformed substitution '{}': expected name=expression (e.g. x=y+1)",
            arg)};
    }
    const std::string name = trim(arg.substr(0, eq));
    const std::string value_text = trim(arg.substr(eq + 1));
    require_bindable_name(name);
    if (value_text.empty()) {
        throw UsageError{std::format(
            "malformed substitution '{}': expected name=expression (e.g. x=y+1)",
            arg)};
    }
    return {name, parse_expression_diag(value_text)};
}

/// `subs` substitutes an expression for each named symbol, left to right,
/// then simplifies. An equation input substitutes into both sides.
void run_subs(const std::string& input, const std::vector<std::string>& assignments,
              PrintStyle style) {
    if (assignments.empty()) {
        throw UsageError{
            "subs needs at least one name=expression argument (e.g. x=y+1)"};
    }
    std::vector<std::pair<std::string, Expr>> subs;
    subs.reserve(assignments.size());
    for (const std::string& a : assignments) {
        subs.push_back(parse_substitution(a));
    }
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        Expr e = std::get<Expr>(parsed);
        for (const auto& [name, replacement] : subs) {
            e = substitute(e, name, replacement);
        }
        std::println("{}", to_string(simplify(e), style));
    } else {
        Equation eq = std::get<Equation>(parsed);
        for (const auto& [name, replacement] : subs) {
            eq.lhs = substitute(eq.lhs, name, replacement);
            eq.rhs = substitute(eq.rhs, name, replacement);
        }
        std::println("{}", to_string(simplify(eq), style));
    }
}

void print_solve_result(const SolveResult& res, const std::string& var,
                        PrintStyle style) {
    switch (res.status) {
    case SolveResult::Status::Solved:
    case SolveResult::Status::NumericOnly:
        for (const Solution& s : res.solutions) {
            std::string line;
            if (!s.exact && s.value->kind() == Kind::Number) {
                line = std::format("{} ≈ {}", var, s.value->number().to_double());
            } else {
                line = std::format("{} = {}", var, to_string(s.value, style));
            }
            if (!s.note.empty()) {
                line += std::format("    ({})", s.note);
            }
            std::println("{}", line);
        }
        break;
    case SolveResult::Status::SolvedComplex:
        std::println("no real solutions; complex roots:");
        for (const Solution& s : res.solutions) {
            std::println("{} = {}", var, to_string(s.value, style));
        }
        break;
    case SolveResult::Status::NoRealSolution:
        std::println("no real solutions");
        break;
    case SolveResult::Status::AllReals:
        std::println("true for all {}", var);
        break;
    case SolveResult::Status::Unsolved:
        std::println("unable to solve for {}", var);
        break;
    }
    if (!res.method.empty()) {
        std::println("method: {}", res.method);
    }
    for (const std::string& w : res.warnings) {
        std::println("warning: {}", w);
    }
}

struct ParsedInequality {
    IneqOp op;
    std::string lhs;
    std::string rhs;
};

/// Find a top-level relational operator (`<`, `>`, `<=`, `>=`, or the Unicode
/// `≤`/`≥`) outside any brackets. The core parser rejects these bytes, so an
/// inequality has to be split at the text level before either side is parsed.
std::optional<ParsedInequality> find_inequality(const std::string& s) {
    int depth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (depth == 0) {
            // Unicode ≤ (E2 89 A4) / ≥ (E2 89 A5).
            if (static_cast<unsigned char>(c) == 0xE2 && i + 2 < s.size() &&
                static_cast<unsigned char>(s[i + 1]) == 0x89) {
                const unsigned char t = static_cast<unsigned char>(s[i + 2]);
                if (t == 0xA4) return ParsedInequality{IneqOp::Le, s.substr(0, i), s.substr(i + 3)};
                if (t == 0xA5) return ParsedInequality{IneqOp::Ge, s.substr(0, i), s.substr(i + 3)};
            }
            if (c == '<' || c == '>') {
                const bool eq = i + 1 < s.size() && s[i + 1] == '=';
                const IneqOp op = c == '<' ? (eq ? IneqOp::Le : IneqOp::Lt)
                                           : (eq ? IneqOp::Ge : IneqOp::Gt);
                return ParsedInequality{op, s.substr(0, i), s.substr(eq ? i + 2 : i + 1)};
            }
        }
    }
    return std::nullopt;
}

void run_solve(const std::string& input, const std::string& explicit_var,
               const NumericOptions& opts, PrintStyle style) {
    // An inequality (x^2 < 4) yields a solution set of intervals, not roots.
    if (const auto ineq = find_inequality(input)) {
        const Expr lhs = parse_expression_diag(ineq->lhs);
        const Expr rhs = parse_expression_diag(ineq->rhs);
        const IneqResult r = solve_inequality(lhs, rhs, ineq->op, explicit_var);
        if (r.status == IneqResult::Status::Unsolved) {
            throw UsageError{r.message.empty() ? "unable to solve inequality"
                                               : r.message};
        }
        std::println("{}", format_solution_set(r, style == PrintStyle::LaTeX));
        for (const std::string& w : r.warnings) std::println("note: {}", w);
        return;
    }
    const Equation eq = parse_equation_diag(input);
    const std::string var = choose_variable(explicit_var, equation_symbols(eq), "solve");
    print_solve_result(solve(eq, var, opts), var, style);
}

// ---------------------------------------------------------------------------
// Linear systems (DESIGN.md §9b): the solve subcommand routes here when the
// expression argument contains a top-level ';'.
// ---------------------------------------------------------------------------

/// Split at `delim` characters that are not nested inside (), {}, or [].
std::vector<std::string> split_top_level(const std::string& s, char delim) {
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (const char c : s) {
        if (c == '(' || c == '{' || c == '[') {
            ++depth;
        } else if (c == ')' || c == '}' || c == ']') {
            --depth;
        }
        if (c == delim && depth <= 0) {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(trim(current));
    return parts;
}

bool has_top_level_semicolon(const std::string& s) {
    return split_top_level(s, ';').size() > 1;
}

/// `dsolve`: the input's first top-level comma segment is the ODE, the rest
/// are initial conditions ("y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0").
/// Prints y(t), the partial-fraction Y(s), and assumed-zero-IC warnings.
void run_dsolve(const std::string& input, PrintStyle style) {
    const std::vector<std::string> parts = split_top_level(input, ',');
    // A ';' in the equation part selects the first-order-system path:
    //   dsolve x' = -2x + y; y' = x - 2y, x(0)=1, y(0)=0
    if (split_top_level(parts[0], ';').size() > 1) {
        const DsolveSystemResult res = dsolve_system(
            split_top_level(parts[0], ';'), {parts.begin() + 1, parts.end()});
        for (std::size_t i = 0; i < res.names.size(); ++i) {
            std::println("{}(t) = {}", res.names[i],
                         to_string(res.solutions[i], style));
        }
        std::println("method: {}", res.method);
        for (const std::string& w : res.warnings) {
            std::println("warning: {}", w);
        }
        return;
    }
    const DsolveResult res =
        dsolve(parts[0], {parts.begin() + 1, parts.end()});
    if (res.implicit) {
        std::println("implicit solution: {} = 0", to_string(res.solution, style));
    } else {
        std::println("y(t) = {}", to_string(res.solution, style));
    }
    if (res.transform) {
        std::println("Y(s) = {}", to_string(res.transform, style));
    }
    std::println("method: {}", res.method);
    for (const std::string& w : res.warnings) {
        std::println("warning: {}", w);
    }
}

void print_system_result(const SystemSolveResult& res,
                         const std::vector<std::string>& vars, PrintStyle style) {
    switch (res.status) {
    case SystemSolveResult::Status::Solved:
    case SystemSolveResult::Status::Underdetermined:
        for (const std::string& v : vars) {
            const auto it = res.values.find(v);
            if (it != res.values.end()) {
                std::println("{} = {}", v, to_string(it->second, style));
            }
        }
        if (!res.free_variables.empty()) {
            std::string list;
            for (const std::string& f : res.free_variables) {
                if (!list.empty()) {
                    list += ", ";
                }
                list += f;
            }
            std::println("free: {}", list);
        }
        break;
    case SystemSolveResult::Status::NoSolution:
        std::println("no solution (inconsistent system)");
        break;
    case SystemSolveResult::Status::Unsolved:
        std::println("unable to solve the system");
        break;
    }
    if (!res.method.empty()) {
        std::println("method: {}", res.method);
    }
    for (const std::string& w : res.warnings) {
        std::println("warning: {}", w);
    }
}

void run_solve_system(const std::string& input,
                      const std::vector<std::string>& explicit_vars,
                      PrintStyle style) {
    std::vector<Equation> eqs;
    for (const std::string& piece : split_top_level(input, ';')) {
        if (piece.empty()) {
            throw UsageError{"empty equation in the system (check the ';' separators)"};
        }
        eqs.push_back(parse_equation_diag(piece));
    }

    std::vector<std::string> vars;
    for (const std::string& v : explicit_vars) {
        if (!is_symbol_name(v)) {
            throw UsageError{std::format("'{}' is not a valid variable name", v)};
        }
        // Same doctrine as eval bindings: a system variable must parse to a
        // single Symbol equal to itself. Constants (pi, e) and function
        // names (sin) can never be solved for.
        Expr parsed_name;
        try {
            parsed_name = parse_expression(v);
        } catch (const ParseError&) {
            parsed_name = nullptr;
        }
        if (parsed_name && parsed_name->kind() == Kind::Constant) {
            throw UsageError{
                std::format("'{}' is a constant and cannot be a system variable", v)};
        }
        if (!parsed_name || parsed_name->kind() != Kind::Symbol ||
            parsed_name->symbol_name() != v) {
            throw UsageError{std::format("'{}' is not a valid variable name", v)};
        }
        if (std::find(vars.begin(), vars.end(), v) == vars.end()) {
            vars.push_back(v);
        }
    }
    if (vars.empty()) {
        // Default: the union of free symbols, but only when that cannot
        // exceed what the equations can determine.
        std::set<std::string> syms;
        for (const Equation& eq : eqs) {
            std::set<std::string> s = equation_symbols(eq);
            syms.merge(s);
        }
        if (syms.empty()) {
            throw UsageError{
                "cannot infer the variables for solve: the system has no free "
                "symbols; pass the variables explicitly"};
        }
        if (syms.size() > eqs.size()) {
            std::string list;
            for (const std::string& s : syms) {
                if (!list.empty()) {
                    list += ", ";
                }
                list += s;
            }
            throw UsageError{std::format(
                "cannot infer the variables for solve: the system has {} free "
                "symbols ({}) but only {} equation(s); pass the variables "
                "explicitly",
                syms.size(), list, eqs.size())};
        }
        vars.assign(syms.begin(), syms.end());
    }

    print_system_result(solve_system(eqs, vars), vars, style);
}

void run_debug(const std::string& input) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", debug_string(std::get<Expr>(parsed)));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::println("{} = {}", debug_string(eq.lhs), debug_string(eq.rhs));
    }
}

// ---------------------------------------------------------------------------
// One-shot command line
// ---------------------------------------------------------------------------

void print_usage(std::FILE* out) {
    std::print(out,
               "MathSolver {} — parse, simplify, differentiate, integrate, and "
               "solve LaTeX-style math\n"
               "\n"
               "usage:\n"
               "  mathsolver simplify \"2x + 3x\"\n"
               "  mathsolver expand   \"(x+1)^3\"\n"
               "  mathsolver factor   \"x^2 - 5x + 6\"\n"
               "  mathsolver trigexpand \"sin(a+b)\"     sin/cos of sums & multiples\n"
               "  mathsolver trigreduce \"sin(x)^2\"     products/powers -> multiples\n"
               "  mathsolver logexpand \"ln(x*y)\"       ln of products/powers -> sums\n"
               "  mathsolver cancel   \"(x^2 - 1)/(x - 1)\" [x]\n"
               "  mathsolver together \"1/x + 1/y\"\n"
               "  mathsolver solve    \"x^2 = 4\" [x] [--range LO HI]\n"
               "  mathsolver solve    \"x + y = 3; x - y = 1\" [x y ...]\n"
               "  mathsolver diff     \"sin(x^2)\" [x]\n"
               "  mathsolver integrate \"x*sin(x)\" [x]\n"
               "  mathsolver integrate \"sin(x)\" [x] --from 0 --to pi\n"
               "  mathsolver apart    \"(3x+2)/((x+1)(x+2))\" [x]\n"
               "  mathsolver laplace  \"e^(-t) sin(2t)\" [t]\n"
               "  mathsolver ilaplace \"1/(s^2 + 2s + 5)\" [s]\n"
               "  mathsolver dsolve   \"y'' + y = sin(t), y(0)=0, y'(0)=0\"\n"
               "  mathsolver series   \"sin(x)\" [x] [0] [5]\n"
               "  mathsolver pade     \"exp(x)\" 2 2\n"
               "  mathsolver isolate  \"x^3 - x - 1\"\n"
               "  mathsolver fit      \"0,0; 1,1; 2,4\" quadratic\n"
               "  mathsolver stats    \"1, 2, 3, 4, 5\"\n"
               "  mathsolver stirling x [3]\n"
               "  mathsolver seq      0 1 1 2 3 5 8\n"
               "  mathsolver factor   360                (integer -> primes)\n"
               "  mathsolver gcd      \"48, 36\"           lcm, gcd of a list\n"
               "  mathsolver isprime  97                 isprime/nextprime\n"
               "  mathsolver divisors 360                divisors, totient\n"
               "  mathsolver cfrac    \"355/113\"          continued fraction + convergents\n"
               "  mathsolver discriminant \"a*x^2+b*x+c\" x  polynomial discriminant\n"
               "  mathsolver polydiv  \"x^3-1\" \"x-1\"      polynomial long division\n"
               "  mathsolver polygcd  \"x^2-1\" \"x^3-1\"    polynomial gcd / lcm\n"
               "  mathsolver resultant \"x^2-1\" \"x-2\"     resultant of two polys\n"
               "  mathsolver powmod   \"7, 100, 13\"        modular pow/inverse/mod\n"
               "  mathsolver crt      \"2,3,2; 3,5,7\"      Chinese remainder theorem\n"
               "  mathsolver limit    \"sin(x)/x\" x 0\n"
               "  mathsolver mlimit   \"x*y/(x^2+y^2)\" x 0 y 0\n"
               "  mathsolver sum      \"k^2\" k 1 n\n"
               "  mathsolver rsolve   \"a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1\"\n"
               "  mathsolver grad     \"x^2 + y^2\" x y\n"
               "  mathsolver curl     \"x*y; y*z; z*x\" x y z\n"
               "  mathsolver eval     \"x^2 + y\" x=3 y=0.5\n"
               "  mathsolver latex    \"sqrt(x)/2\"\n"
               "  mathsolver --help | --version\n"
               "  mathsolver          (no arguments: interactive REPL)\n"
               "\n"
               "options:\n"
               "  --latex        render output in LaTeX instead of plain text\n"
               "  --range LO HI  numeric root-search interval for solve "
               "(default: -100 100)\n"
               "  --from A --to B  definite-integral bounds for integrate\n"
               "                 (both or neither; parsed as expressions)\n"
               "\n"
               "The solve/diff/integrate variable may be omitted when the input\n"
               "has exactly one free symbol. Exit codes: 0 success, 1 parse/math\n"
               "error, 2 usage error; error diagnostics go to stderr.\n",
               k_version);
}

bool is_known_subcommand(std::string_view s) {
    return s == "simplify" || s == "expand" || s == "factor" || s == "cancel" ||
           s == "together" ||
           s == "solve" || s == "diff" || s == "integrate" || s == "eval" ||
           s == "latex" || s == "subs" || s == "collect" || s == "laplace" ||
           s == "ilaplace" ||
           s == "apart" || s == "dsolve" || s == "series" || s == "pade" ||
           s == "rootcount" || s == "isolate" ||
           s == "grad" ||
           s == "div" || s == "curl" || s == "laplacian" || s == "jacobian" ||
           s == "hessian" || s == "limit" || s == "sum" || s == "product" ||
           s == "rsolve" || s == "mlimit" || s == "stirling" ||
           s == "seq" || s == "fit" || s == "regress" || s == "interp" ||
           s == "newton" || s == "lagrange" ||
           s == "vandermonde" || s == "stats" ||
           s == "chebyshev" || s == "chebyu" || s == "legendre" ||
           s == "hermite" || s == "laguerre" ||
           s == "gcd" || s == "lcm" || s == "isprime" || s == "nextprime" ||
           s == "divisors" || s == "totient" || s == "cfrac" ||
           s == "mod" || s == "powmod" || s == "modinv" || s == "crt" ||
           s == "discriminant" || s == "trigexpand" || s == "trigreduce" ||
           s == "polydiv" || s == "polygcd" || s == "polylcm" ||
           s == "resultant" || s == "bezout" || s == "companion" ||
           s == "logexpand" || s == "logcombine";
}

int run_one_shot(const std::vector<std::string>& args) {
    const std::string& sub = args[0];
    try {
        if (!is_known_subcommand(sub)) {
            throw UsageError{std::format(
                "unknown command '{}' (run 'mathsolver --help' for usage)", sub)};
        }

        bool latex = false;
        bool end_of_options = false;
        NumericOptions opts;
        std::optional<std::string> from_text;
        std::optional<std::string> to_text;
        std::vector<std::string> positionals;
        for (std::size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (!end_of_options && a == "--") {
                end_of_options = true;  // subsequent args are positionals ("--x")
            } else if (!end_of_options && a == "--latex") {
                latex = true;
            } else if (!end_of_options && a == "--help") {
                print_usage(stdout);
                return k_exit_ok;
            } else if (!end_of_options && a == "--range") {
                if (sub != "solve") {
                    throw UsageError{"--range is only valid for 'solve'"};
                }
                if (i + 2 >= args.size()) {
                    throw UsageError{"--range needs two numbers: --range LO HI"};
                }
                const std::optional<double> lo = parse_double(args[i + 1]);
                const std::optional<double> hi = parse_double(args[i + 2]);
                // strtod accepts "nan"/"inf"; treat NaN as "not a number" (its
                // own message, checked before the ordering test so it never
                // reports the misleading "LO must be less than HI").
                if (!lo || !hi || std::isnan(*lo) || std::isnan(*hi)) {
                    throw UsageError{std::format(
                        "--range arguments must be numbers (got '{}' '{}')",
                        args[i + 1], args[i + 2])};
                }
                // Reject infinite bounds and an interval whose width overflows
                // to infinity (e.g. -1e308 1e308): the scan would silently find
                // nothing and falsely report the interval as covered.
                if (!std::isfinite(*lo) || !std::isfinite(*hi) ||
                    !std::isfinite(*hi - *lo)) {
                    throw UsageError{std::format(
                        "--range bounds must be finite (got '{}' '{}')",
                        args[i + 1], args[i + 2])};
                }
                if (!(*lo < *hi)) {
                    throw UsageError{"--range LO must be less than HI"};
                }
                opts.lo = *lo;
                opts.hi = *hi;
                i += 2;
            } else if (!end_of_options && (a == "--from" || a == "--to")) {
                if (sub != "integrate") {
                    throw UsageError{
                        std::format("{} is only valid for 'integrate'", a)};
                }
                if (i + 1 >= args.size()) {
                    throw UsageError{
                        std::format("{} needs a bound expression", a)};
                }
                (a == "--from" ? from_text : to_text) = args[i + 1];
                ++i;
            } else if (!end_of_options && a.starts_with("--")) {
                throw UsageError{std::format("unknown option '{}'", a)};
            } else {
                positionals.push_back(a);
            }
        }

        if (positionals.empty()) {
            throw UsageError{std::format("'{}' needs an expression argument", sub)};
        }
        const std::string& input = positionals[0];
        const PrintStyle style = latex ? PrintStyle::LaTeX : PrintStyle::Plain;

        if (sub == "solve" && has_top_level_semicolon(input)) {
            // System of equations: the positional args after the expression
            // are the variables (optional).
            const std::vector<std::string> vars(positionals.begin() + 1,
                                                positionals.end());
            run_solve_system(input, vars, style);
        } else if (sub == "solve" || sub == "diff" || sub == "integrate" ||
                   sub == "collect" || sub == "laplace" || sub == "ilaplace" ||
                   sub == "apart" || sub == "cancel") {
            if (positionals.size() > 2) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver {} \"<input>\" [var])",
                    positionals[2], sub)};
            }
            const std::string var = positionals.size() > 1 ? positionals[1] : "";
            if (sub == "solve") {
                run_solve(input, var, opts, style);
            } else if (sub == "diff") {
                run_diff(input, var, style);
            } else if (sub == "collect") {
                run_collect(input, var, style);
            } else if (sub == "apart") {
                run_apart(input, var, style);
            } else if (sub == "cancel") {
                run_cancel(input, var, style);
            } else if (sub == "laplace") {
                run_laplace(input, var, style);
            } else if (sub == "ilaplace") {
                run_ilaplace(input, var, style);
            } else {
                run_integrate(input, var, from_text, to_text, style);
            }
        } else if (sub == "grad" || sub == "div" || sub == "curl" ||
                   sub == "laplacian" || sub == "jacobian" || sub == "hessian") {
            run_vector(sub, positionals, style);
        } else if (sub == "limit") {
            if (positionals.size() < 3 || positionals.size() > 4) {
                throw UsageError{
                    "usage: mathsolver limit \"<expr>\" <var> <point> "
                    "[left|right]"};
            }
            run_limit(input, positionals[1], positionals[2],
                      positionals.size() > 3 ? positionals[3] : "", style);
        } else if (sub == "mlimit") {
            run_mlimit(input, positionals, style);
        } else if (sub == "sum" || sub == "product") {
            run_sum(input, positionals, sub == "product", style);
        } else if (sub == "rsolve") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the recurrence and its "
                    "conditions in one quoted argument)",
                    positionals[1])};
            }
            run_rsolve(input, style);
        } else if (sub == "series") {
            if (positionals.size() > 4) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver series "
                    "\"<expr>\" [var] [center] [order])",
                    positionals[4])};
            }
            run_series(input, positionals.size() > 1 ? positionals[1] : "",
                       positionals.size() > 2 ? positionals[2] : "",
                       positionals.size() > 3 ? positionals[3] : "", style);
        } else if (sub == "pade") {
            if (positionals.size() > 4) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver pade "
                    "\"<expr>\" <m> <n> [var])",
                    positionals[4])};
            }
            run_pade(input, positionals.size() > 1 ? positionals[1] : "",
                     positionals.size() > 2 ? positionals[2] : "",
                     positionals.size() > 3 ? positionals[3] : "", style);
        } else if (sub == "rootcount") {
            if (positionals.size() > 4) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver rootcount "
                    "\"<poly>\" [var] [lo] [hi])",
                    positionals[4])};
            }
            run_rootcount(input, positionals.size() > 1 ? positionals[1] : "",
                          positionals.size() > 2 ? positionals[2] : "",
                          positionals.size() > 3 ? positionals[3] : "");
        } else if (sub == "isolate") {
            if (positionals.size() > 2) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver isolate "
                    "\"<poly>\" [var])",
                    positionals[2])};
            }
            run_isolate(input, positionals.size() > 1 ? positionals[1] : "", style);
        } else if (sub == "fit" || sub == "regress") {
            if (positionals.size() > 3) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver fit "
                    "\"x,y; x,y; ...\" [model] [degree])",
                    positionals[3])};
            }
            run_fit(input, positionals.size() > 1 ? positionals[1] : "",
                    positionals.size() > 2 ? positionals[2] : "", style);
        } else if (sub == "interp") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the data in one quoted "
                    "\"x,y; x,y; ...\" list)",
                    positionals[1])};
            }
            run_interp(input, style);
        } else if (sub == "newton" || sub == "lagrange") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the data in one quoted "
                    "\"x,y; x,y; ...\" list)",
                    positionals[1])};
            }
            run_interp_form(input, sub == "newton" ? InterpForm::Newton : InterpForm::Lagrange,
                            style);
        } else if (sub == "vandermonde") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the nodes in one quoted "
                    "argument: mathsolver vandermonde \"1, 2, 3\")",
                    positionals[1])};
            }
            run_vandermonde(input, style);
        } else if (sub == "chebyshev" || sub == "chebyu" || sub == "legendre" ||
                   sub == "hermite" || sub == "laguerre") {
            if (positionals.size() > 2) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver {} <n> [var])",
                    positionals[2], sub)};
            }
            const OrthoFamily fam = sub == "chebyshev" ? OrthoFamily::ChebyshevT
                                    : sub == "chebyu"    ? OrthoFamily::ChebyshevU
                                    : sub == "legendre"  ? OrthoFamily::Legendre
                                    : sub == "hermite"   ? OrthoFamily::Hermite
                                                         : OrthoFamily::Laguerre;
            run_orthopoly(fam, input, positionals.size() > 1 ? positionals[1] : "",
                          style);
        } else if (sub == "stats") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the data in one quoted "
                    "argument: mathsolver stats \"1, 2, 3, 4\")",
                    positionals[1])};
            }
            run_stats(input, style);
        } else if (sub == "seq") {
            run_seq(positionals, style);
        } else if (sub == "gcd" || sub == "lcm" || sub == "isprime" ||
                   sub == "nextprime" || sub == "divisors" || sub == "totient") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the integers in one quoted "
                    "argument: mathsolver {} \"48, 36\")",
                    positionals[1], sub)};
            }
            if (sub == "gcd") run_gcd(input);
            else if (sub == "lcm") run_lcm(input);
            else if (sub == "isprime") run_isprime(input);
            else if (sub == "nextprime") run_nextprime(input);
            else if (sub == "divisors") run_divisors(input);
            else run_totient(input);
        } else if (sub == "cfrac") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the value in one quoted "
                    "argument: mathsolver cfrac \"355/113\")",
                    positionals[1])};
            }
            run_cfrac(input, style);
        } else if (sub == "mod" || sub == "powmod" || sub == "modinv" ||
                   sub == "crt") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the arguments in one quoted "
                    "argument: mathsolver {} \"...\")",
                    positionals[1], sub)};
            }
            if (sub == "mod") run_mod(input);
            else if (sub == "powmod") run_powmod(input);
            else if (sub == "modinv") run_modinv(input);
            else run_crt(input);
        } else if (sub == "stirling") {
            if (positionals.size() > 2) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver stirling "
                    "<var> [terms])",
                    positionals[2])};
            }
            run_stirling(input, positionals.size() > 1 ? positionals[1] : "",
                         style);
        } else if (sub == "polydiv") {
            if (positionals.size() < 2 || positionals.size() > 3) {
                throw UsageError{"usage: mathsolver polydiv \"<dividend>\" "
                                 "\"<divisor>\" [var]"};
            }
            run_polydiv(positionals[0], positionals[1],
                        positionals.size() > 2 ? positionals[2] : "", style);
        } else if (sub == "polygcd" || sub == "polylcm") {
            if (positionals.size() < 2 || positionals.size() > 3) {
                throw UsageError{std::format("usage: mathsolver {} \"<a>\" "
                                             "\"<b>\" [var]",
                                             sub)};
            }
            run_polygcd(positionals[0], positionals[1],
                        positionals.size() > 2 ? positionals[2] : "",
                        sub == "polylcm", style);
        } else if (sub == "resultant") {
            if (positionals.size() < 2 || positionals.size() > 3) {
                throw UsageError{"usage: mathsolver resultant \"<a>\" \"<b>\" [var]"};
            }
            run_resultant(positionals[0], positionals[1],
                          positionals.size() > 2 ? positionals[2] : "", style);
        } else if (sub == "bezout") {
            if (positionals.size() < 2 || positionals.size() > 3) {
                throw UsageError{"usage: mathsolver bezout \"<a>\" \"<b>\" [var]"};
            }
            run_bezout(positionals[0], positionals[1],
                       positionals.size() > 2 ? positionals[2] : "", style);
        } else if (sub == "discriminant") {
            if (positionals.size() > 2) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver discriminant "
                    "\"<polynomial>\" [var])",
                    positionals[2])};
            }
            run_discriminant(input, positionals.size() > 1 ? positionals[1] : "",
                             style);
        } else if (sub == "companion") {
            if (positionals.size() > 2) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver companion "
                    "\"<polynomial>\" [var])",
                    positionals[2])};
            }
            run_companion(input, positionals.size() > 1 ? positionals[1] : "",
                          style);
        } else if (sub == "dsolve") {
            if (positionals.size() > 1) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (put the ODE and its conditions "
                    "in one quoted argument)",
                    positionals[1])};
            }
            run_dsolve(input, style);
        } else if (sub == "eval") {
            Bindings bindings;
            for (std::size_t i = 1; i < positionals.size(); ++i) {
                add_binding(bindings, positionals[i]);
            }
            run_eval(input, bindings);
        } else if (sub == "subs") {
            run_subs(input, {positionals.begin() + 1, positionals.end()}, style);
        } else {
            if (positionals.size() > 1) {
                throw UsageError{std::format("unexpected argument '{}'", positionals[1])};
            }
            if (sub == "simplify") {
                run_simplify(input, style);
            } else if (sub == "expand") {
                run_expand(input, style);
            } else if (sub == "factor") {
                run_factor(input, style);
            } else if (sub == "together") {
                run_together(input, style);
            } else if (sub == "trigexpand") {
                run_trigexpand(input, style);
            } else if (sub == "trigreduce") {
                run_trigreduce(input, style);
            } else if (sub == "logexpand") {
                run_logexpand(input, style);
            } else if (sub == "logcombine") {
                run_logcombine(input, style);
            } else {  // latex
                run_latex(input);
            }
        }
        return k_exit_ok;
    } catch (const UsageError& e) {
        std::fflush(stdout);
        std::println(stderr, "usage error: {}", e.message);
        return k_exit_usage;
    } catch (const DiagnosedParseError& e) {
        std::fflush(stdout);
        std::println(stderr, "{}", caret_diagnostic(e.source, e.error));
        return k_exit_error;
    } catch (const ParseError& e) {
        // ParseError raised outside a tracked parse call (defensive).
        std::fflush(stdout);
        std::println(stderr, "error: {}", e.what());
        return k_exit_error;
    } catch (const Error& e) {
        std::fflush(stdout);
        std::println(stderr, "error: {}", e.what());
        return k_exit_error;
    }
}

// ---------------------------------------------------------------------------
// REPL
// ---------------------------------------------------------------------------

void print_repl_help() {
    std::print(
        "Enter a bare expression to simplify it, or an equation to solve it.\n"
        "Commands (arguments separated by top-level commas):\n"
        "  solve <equation>[, <variable>]\n"
        "  solve <eq>; <eq>[; ...][, <var>, <var>, ...]   (linear system)\n"
        "  diff <expression>[, <variable>]\n"
        "  integrate <expression>[, <variable>[, <lo>, <hi>]]\n"
        "  eval <expression>, x=1[, y=2 ...]\n"
        "  subs <expression>, x=y+1[, z=2 ...]\n"
        "  collect <expression>[, <variable>]\n"
        "  apart <expression>[, <variable>]       partial fractions\n"
        "  laplace <expression>[, <time var>]     f(t) -> F(s)\n"
        "  ilaplace <expression>[, <freq var>]    F(s) -> f(t)\n"
        "  dsolve <ode>[, y(0)=v, y'(0)=v, ...]   solve an IVP, e.g.\n"
        "         dsolve y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0\n"
        "  series <expression>[, <var>[, <center>[, <order>]]]   Taylor\n"
        "  pade <expression>, <m>, <n>[, <var>]   [m/n] Padé approximant\n"
        "  rootcount <poly>[, <var>[, <lo>, <hi>]]  distinct real roots (Sturm)\n"
        "  isolate <poly>[, <var>]                isolate the real roots\n"
        "  discriminant <polynomial>[, <var>]     discriminant (degree 2–4)\n"
        "  polydiv <dividend>, <divisor>[, <var>] quotient + remainder\n"
        "  polygcd <a>, <b>[, <var>]   polylcm <a>, <b>[, <var>]   monic gcd/lcm\n"
        "  resultant <a>, <b>[, <var>]            0 iff a shared root\n"
        "  bezout <a>, <b>[, <var>]               gcd + cofactors s·a + t·b = gcd\n"
        "  companion <polynomial>[, <var>]        companion matrix (roots = eigenvalues)\n"
        "  fit <x,y; x,y; ...> [| <model> [<degree>]]  least-squares regression\n"
        "  interp <x,y; x,y; ...>                 exact polynomial through the points\n"
        "  newton <x,y; x,y; ...>                 interpolant in Newton divided-difference form\n"
        "  lagrange <x,y; x,y; ...>               interpolant in Lagrange basis form\n"
        "  vandermonde <x1, x2, x3, ...>          Vandermonde matrix of the nodes\n"
        "         (models: linear, quadratic, cubic, poly, exp, power, log)\n"
        "  stats <v1, v2, v3, ...>                exact summary statistics\n"
        "  chebyshev/chebyu/legendre/hermite/laguerre <n> [var]\n"
        "                                         exact orthogonal polynomials\n"
        "  stirling [<var>[, <terms>]]            ln Gamma asymptotics\n"
        "  seq <a0>, <a1>, <a2>, <a3>[, ...]      recognize the pattern\n"
        "  factor <n>                             integer -> prime factorization\n"
        "  gcd <a, b, ...>   lcm <a, b, ...>       exact over the integers\n"
        "  isprime <n>   nextprime <n>   divisors <n>   totient <n>\n"
        "  cfrac <rational | sqrt(n) | real>      continued fraction + convergents\n"
        "  mod <a, m>   powmod <b, e, m>   modinv <a, m>   modular arithmetic\n"
        "  crt <r1, r2, …; m1, m2, …>             Chinese remainder theorem\n"
        "  limit <expression>, <variable>, <point>[, left|right]\n"
        "         (point accepts numbers, inf, -inf)\n"
        "  mlimit <expr>, <x>, <a>, <y>, <b>      2-D limit by path sampling\n"
        "  sum <term>, <var>, <lo>, <hi>          closed forms (hi may be inf)\n"
        "  product <term>, <var>, <lo>, <hi>\n"
        "  rsolve <recurrence>[, a(0)=v, ...]     e.g.\n"
        "         rsolve a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1\n"
        "  grad <f>, <vars...>       div/curl <F1;F2;..>, <vars...>\n"
        "  laplacian <f>, <vars...>  jacobian <F1;..>, <vars...>  hessian <f>, <vars...>\n"
        "  simplify <expression>      expand <expression>\n"
        "  factor <expression>        latex <expression>\n"
        "  trigexpand <expression>    trig of sums/multiples -> single angles\n"
        "  trigreduce <expression>    products/powers of sin,cos -> multiples\n"
        "  logexpand <expression>     logcombine <expression>   log identities\n"
        "  cancel <expression>[, <variable>]      cancel a rational's GCD\n"
        "  together <expression>                  sum of fractions over one denom\n"
        "  debug <expression>         (s-expression dump)\n"
        "  help                       quit / exit\n"
        "Assignments (docs/GRAMMAR.md \"Assignments\"):\n"
        "  <name> := <value>          bind a variable to an expression or an\n"
        "                             equation (E_1 := x + y = 3); bindings\n"
        "                             apply lazily to every computing command\n"
        "  vars                       list bindings in definition order\n"
        "  unset <name>               remove one binding\n"
        "  clear                      remove all bindings\n");
}

/// Split at commas that are not nested inside (), {}, or [].
std::vector<std::string> split_top_level_commas(const std::string& s) {
    return split_top_level(s, ',');
}

bool is_repl_command(std::string_view word) {
    return word == "simplify" || word == "expand" || word == "factor" ||
           word == "trigexpand" || word == "trigreduce" ||
           word == "logexpand" || word == "logcombine" ||
           word == "cancel" || word == "together" || word == "solve" ||
           word == "diff" || word == "integrate" ||
           word == "eval" || word == "latex" || word == "debug" ||
           word == "subs" || word == "collect" || word == "laplace" ||
           word == "ilaplace" || word == "apart" || word == "dsolve" ||
           word == "series" || word == "pade" || word == "rootcount" ||
           word == "isolate" || word == "grad" || word == "div" ||
           word == "curl" || word == "laplacian" || word == "jacobian" ||
           word == "hessian" || word == "limit" || word == "sum" ||
           word == "product" || word == "rsolve" || word == "mlimit" ||
           word == "stirling" || word == "seq" || word == "fit" ||
           word == "regress" || word == "stats" || word == "vandermonde" || word == "gcd" ||
           word == "lcm" || word == "isprime" || word == "nextprime" ||
           word == "divisors" || word == "totient" || word == "cfrac" ||
           word == "mod" || word == "powmod" || word == "modinv" ||
           word == "crt" || word == "discriminant" || word == "polydiv" ||
           word == "polygcd" || word == "polylcm" || word == "resultant" ||
           word == "companion";
}

// ---------------------------------------------------------------------------
// Session environment (variable assignment): `name := value` bindings,
// REPL-only. Spec: docs/proposals/variable-assignment.md; condensed contract
// in DESIGN.md §10. Values are stored as parsed ASTs and resolve lazily at
// each use (§5 of the spec); the dependency graph over defined names is kept
// acyclic at definition time (§5.2).
// ---------------------------------------------------------------------------

struct Binding {
    std::string name;
    std::variant<Expr, Equation> value;
};

/// Insertion-ordered so `vars` lists in definition order; lookups are linear
/// over a handful of entries.
using Environment = std::vector<Binding>;

const Binding* find_binding(const Environment& env, std::string_view name) {
    for (const Binding& b : env) {
        if (b.name == name) {
            return &b;
        }
    }
    return nullptr;
}

std::set<std::string> binding_free_symbols(const Binding& b) {
    if (const Expr* e = std::get_if<Expr>(&b.value)) {
        return free_symbols(*e);
    }
    return equation_symbols(std::get<Equation>(b.value));
}

/// Canonical plain print of a binding: `a := 2`, `E_1 := x + y = 3`. The
/// name goes through the printer so it echoes in its re-parseable spelling
/// (`x_{max}`, not the bare symbol name `x_max`).
std::string binding_text(const Binding& b) {
    const std::string name = to_string(make_sym(b.name), PrintStyle::Plain);
    if (const Expr* e = std::get_if<Expr>(&b.value)) {
        return std::format("{} := {}", name, to_string(*e, PrintStyle::Plain));
    }
    return std::format("{} := {}", name,
                       to_string(std::get<Equation>(b.value), PrintStyle::Plain));
}

/// The §5 resolve() ordering: the environment entries reachable from `roots`
/// through bound values (transitively), minus `excluded`, ordered
/// parents-first — a binding precedes every binding its value references —
/// so one sequential substitute() pass fully resolves chains and the result
/// is independent of definition order. A name bound to an Equation can never
/// be substituted into an expression; reaching one here is the §4 placement
/// error.
std::vector<const Binding*> active_bindings(const std::set<std::string>& roots,
                                            const Environment& env,
                                            const std::set<std::string>& excluded) {
    std::vector<const Binding*> active;
    std::set<std::string> seen;
    std::vector<std::string> frontier(roots.begin(), roots.end());
    while (!frontier.empty()) {
        const std::string name = std::move(frontier.back());
        frontier.pop_back();
        if (excluded.contains(name) || !seen.insert(name).second) {
            continue;
        }
        const Binding* b = find_binding(env, name);
        if (b == nullptr) {
            continue;
        }
        if (std::holds_alternative<Equation>(b->value)) {
            throw UsageError{std::format(
                "'{}' names an equation and cannot be used inside an expression",
                name)};
        }
        active.push_back(b);
        for (const std::string& s : free_symbols(std::get<Expr>(b->value))) {
            frontier.push_back(s);
        }
    }

    // Parents-first topological order (DFS post-order, reversed). The
    // definition-time cycle check keeps the graph acyclic; the visiting set
    // and the depth bound are belt-and-braces only. The bound is the active
    // set's size — a simple path cannot visit more bindings than exist — so
    // it can never fire on legal acyclic input, however deep the chain
    // (a fixed constant here once misdiagnosed a 66-deep chain as a cycle).
    std::vector<const Binding*> ordered;
    std::set<std::string> done;
    std::set<std::string> visiting;
    const int max_depth = static_cast<int>(active.size());
    auto visit = [&](auto&& self, const Binding* b, int depth) -> void {
        if (depth > max_depth || visiting.contains(b->name)) {
            throw Error{"internal error: assignment cycle detected"};
        }
        if (done.contains(b->name)) {
            return;
        }
        visiting.insert(b->name);
        for (const Binding* dep : active) {
            if (dep != b && contains_symbol(std::get<Expr>(b->value), dep->name)) {
                self(self, dep, depth + 1);
            }
        }
        visiting.erase(b->name);
        done.insert(b->name);
        ordered.push_back(b);
    };
    for (const Binding* b : active) {
        visit(visit, b, 0);
    }
    std::reverse(ordered.begin(), ordered.end());
    return ordered;
}

Expr resolve_expr(const Expr& e, const Environment& env,
                  const std::set<std::string>& excluded = {}) {
    Expr r = e;
    for (const Binding* b : active_bindings(free_symbols(e), env, excluded)) {
        r = substitute(r, b->name, std::get<Expr>(b->value));
    }
    return r;
}

Equation resolve_equation(const Equation& eq, const Environment& env,
                          const std::set<std::string>& excluded = {}) {
    Equation r = eq;
    for (const Binding* b : active_bindings(equation_symbols(eq), env, excluded)) {
        const Expr& v = std::get<Expr>(b->value);
        r.lhs = substitute(r.lhs, b->name, v);
        r.rhs = substitute(r.rhs, b->name, v);
    }
    return r;
}

/// Resolve a whole parsed input to plain text (round-trip-guaranteed), ready
/// to feed an existing run_* verb.
std::string resolve_input_text(const std::string& input, const Environment& env,
                               const std::set<std::string>& excluded = {}) {
    const auto parsed = parse_input_diag(input);
    if (const Expr* e = std::get_if<Expr>(&parsed)) {
        return to_string(resolve_expr(*e, env, excluded), PrintStyle::Plain);
    }
    return to_string(resolve_equation(std::get<Equation>(parsed), env, excluded),
                     PrintStyle::Plain);
}

constexpr std::string_view k_assign_target_error =
    "assignment target must be a single variable name (e.g. x, alpha, E_1)";

/// Names the lexer reads as functions (docs/GRAMMAR.md): never assignable.
bool is_function_word(std::string_view s) {
    static constexpr std::string_view names[] = {
        "sin",  "cos",  "tan", "asin", "acos", "atan", "arcsin",
        "arccos", "arctan", "sinh", "cosh", "tanh", "sec", "csc",
        "cot",  "exp",  "ln",  "log",  "sqrt", "abs"};
    return std::ranges::find(names, s) != std::ranges::end(names);
}

/// `x_{max}` and `\alpha` lex to the symbols x_max / alpha; normalize the
/// typed target so it can be compared against the parsed symbol's name.
std::string normalize_target_text(std::string_view s) {
    std::string out{s};
    if (!out.empty() && out.front() == '\\') {
        out.erase(0, 1);
    }
    const std::size_t sub = out.find("_{");
    if (sub != std::string::npos && !out.empty() && out.back() == '}') {
        out = out.substr(0, sub + 1) + out.substr(sub + 2, out.size() - sub - 3);
    }
    return out;
}

/// Validate the left side of `name := value` (spec §2.3) and return the
/// canonical symbol name it binds.
std::string validate_assignment_target(const std::string& target) {
    if (is_function_word(target)) {
        throw UsageError{
            std::format("cannot assign to the function name '{}'", target)};
    }
    Expr parsed;
    try {
        parsed = parse_expression(target);
    } catch (const ParseError&) {
        parsed = nullptr;
    }
    const std::string normalized = normalize_target_text(target);
    if (parsed && parsed->kind() == Kind::Constant &&
        (normalized == "pi" || normalized == "e")) {
        throw UsageError{std::format("cannot assign to the constant '{}'", target)};
    }
    if (parsed && parsed->kind() == Kind::Symbol &&
        parsed->symbol_name() == normalized) {
        return normalized;
    }

    // 'E1' lexes as E*1 — suggest the subscript spelling.
    std::size_t letters_end = 0;
    while (letters_end < target.size() &&
           std::isalpha(static_cast<unsigned char>(target[letters_end])) != 0) {
        ++letters_end;
    }
    const std::string letters = target.substr(0, letters_end);
    const std::string digits = target.substr(letters_end);
    const bool all_digits =
        !digits.empty() && std::ranges::all_of(digits, [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
    if (!letters.empty() && all_digits) {
        Expr letter_expr;
        try {
            letter_expr = parse_expression(letters);
        } catch (const ParseError&) {
            letter_expr = nullptr;
        }
        if (letter_expr && letter_expr->kind() == Kind::Symbol &&
            letter_expr->symbol_name() == letters) {
            throw UsageError{std::format(
                "{} — '{}' reads as {}*{}; did you mean {}_{}?",
                k_assign_target_error, target, letters, digits, letters, digits)};
        }
    }
    if (!parsed) {
        // A multi-letter word (the v0.4 word guard) or any other lex error:
        // reuse the guard's rule text plus assignment-specific guidance
        // (spec §6).
        throw UsageError{std::format(
            "{} — variables are single letters (a-z), Greek names, or "
            "subscripted (v_1); assignment targets follow the same rule — try "
            "a subscripted name like s_max := 5",
            k_assign_target_error)};
    }
    throw UsageError{std::string{k_assign_target_error}};
}

/// Depth-first search for a dependency path from any of `starts` back to
/// `name` through the defined bindings; returns the path (start .. name) or
/// empty when none exists.
std::vector<std::string> find_cycle_path(const std::string& name,
                                         const std::set<std::string>& starts,
                                         const Environment& env) {
    std::vector<std::string> path;
    std::set<std::string> seen;
    auto dfs = [&](auto&& self, const std::string& cur) -> bool {
        if (cur == name) {
            path.push_back(cur);
            return true;
        }
        if (!seen.insert(cur).second) {
            return false;
        }
        const Binding* b = find_binding(env, cur);
        if (b == nullptr) {
            return false;
        }
        path.push_back(cur);
        for (const std::string& next : binding_free_symbols(*b)) {
            if (self(self, next)) {
                return true;
            }
        }
        path.pop_back();
        return false;
    };
    for (const std::string& s : starts) {
        if (dfs(dfs, s)) {
            return path;
        }
    }
    return {};
}

/// An input line whose first `:=` splits it into a non-empty left part is an
/// assignment; a ':' not followed by '=' falls through to the parser and
/// keeps its existing lex error.
bool is_assignment_line(const std::string& line) {
    const std::size_t pos = line.find(":=");
    return pos != std::string::npos && !trim(line.substr(0, pos)).empty();
}

/// `name := value` (spec §2): validate the target, parse the value now
/// (caret diagnostics at definition time), reject cycles (§5.2), store, and
/// echo the binding in canonical plain form.
void repl_assign(const std::string& line, Environment& env) {
    const std::size_t pos = line.find(":=");
    const std::string target = trim(line.substr(0, pos));
    const std::string value_text = trim(line.substr(pos + 2));
    const std::string name = validate_assignment_target(target);
    if (value_text.empty()) {
        throw UsageError{"assignment needs a value (e.g. x := 2)"};
    }
    Binding binding{name, parse_input_diag(value_text)};

    const std::set<std::string> value_syms = binding_free_symbols(binding);
    if (value_syms.contains(name)) {
        throw UsageError{
            std::format("'{}' cannot be defined in terms of itself", name)};
    }
    const std::vector<std::string> cycle = find_cycle_path(name, value_syms, env);
    if (!cycle.empty()) {
        std::string msg = std::format("assignment would create a cycle: {}", name);
        for (const std::string& n : cycle) {
            msg += " -> " + n;
        }
        throw UsageError{msg};
    }

    // Redefinition replaces in place (keeps definition order for `vars`).
    const auto it = std::ranges::find(env, name, &Binding::name);
    if (it != env.end()) {
        *it = std::move(binding);
        std::println("{}", binding_text(*it));
    } else {
        env.push_back(std::move(binding));
        std::println("{}", binding_text(env.back()));
    }
}

/// Spec §7: solving (or diff/integrate/collect) for an assigned variable
/// ignores the assignment, warn-and-proceed. Prints in the warnings position
/// (after the result lines and `method:`).
void warn_assigned_variable(const std::string& var, const Environment& env) {
    if (var.empty()) {
        return;
    }
    if (const Binding* b = find_binding(env, var)) {
        std::println(
            "warning: '{}' has an assigned value ({}), which is ignored while "
            "solving for it; 'unset {}' removes the assignment",
            var, binding_text(*b), var);
    }
}

/// Spec §7: explicit eval bindings / subs substitutions shadow the
/// environment for one command, with a note per shadowed name.
void print_override_notes(const std::vector<std::string>& args,
                          const Environment& env) {
    for (const std::string& arg : args) {
        const std::size_t eq = arg.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        if (const Binding* b = find_binding(env, trim(arg.substr(0, eq)))) {
            std::println(
                "note: binding {} overrides the assignment {} for this command",
                trim(arg), binding_text(*b));
        }
    }
}

/// REPL solve with the environment: an equation-valued name may stand as a
/// whole ';'-separated segment (spec §4); every requested variable is
/// excluded from resolution and warned about when assigned (§7).
void repl_solve(const std::string& input, std::vector<std::string> vars,
                const Environment& env) {
    // An inequality is a text-split path (the parser rejects `<`), so it skips
    // the equation-oriented environment resolution and goes straight to
    // run_solve, which builds the interval solution set.
    if (find_inequality(input)) {
        run_solve(input, vars.empty() ? std::string{} : vars[0], NumericOptions{},
                  PrintStyle::Plain);
        return;
    }
    const std::vector<std::string> segments = split_top_level(input, ';');
    const auto segment_equation = [&](const std::string& segment) -> Equation {
        // An equation-valued name may stand as a whole segment (spec §4).
        if (const Binding* b = find_binding(env, segment);
            b != nullptr && std::holds_alternative<Equation>(b->value)) {
            return std::get<Equation>(b->value);
        }
        return parse_equation_diag(segment);
    };

    // `solve <eq>` with no explicit variable: when the equation's only free
    // symbol is itself assigned, resolving first would consume the unknown
    // (x := 3; solve x^2 = 9 -> "no free symbols"). Infer the variable from
    // the unresolved input instead and warn-and-proceed exactly as if it had
    // been requested explicitly (§7 doctrine). With several symbols left
    // nothing is inferable and run_solve's error stands.
    if (vars.empty() && segments.size() == 1 && !segments[0].empty()) {
        const Equation eq = segment_equation(segments[0]);
        const std::set<std::string> raw = equation_symbols(eq);
        if (raw.size() == 1 &&
            equation_symbols(resolve_equation(eq, env)).empty()) {
            vars.push_back(*raw.begin());
        }
    }

    const std::set<std::string> excluded(vars.begin(), vars.end());
    std::string resolved;
    bool first = true;
    for (const std::string& segment : segments) {
        if (!first) {
            resolved += "; ";
        }
        first = false;
        if (segment.empty()) {
            continue;  // run_solve_system reports the empty-segment error
        }
        resolved += to_string(resolve_equation(segment_equation(segment), env, excluded),
                              PrintStyle::Plain);
    }
    if (segments.size() > 1) {
        run_solve_system(resolved, vars, PrintStyle::Plain);
    } else {
        if (vars.size() > 1) {
            throw UsageError{"too many arguments: usage: solve <input>[, <variable>]"};
        }
        run_solve(resolved, vars.empty() ? std::string{} : vars[0],
                  NumericOptions{}, PrintStyle::Plain);
    }
    for (const std::string& v : vars) {
        warn_assigned_variable(v, env);
    }
}

/// Bare equation with the environment (spec §7): resolve both sides fully;
/// one free symbol left → solve for it, none → evaluate the truth, several →
/// today's "use solve ..., var" prompt.
void repl_bare_equation(const Equation& eq, const Environment& env) {
    const Equation resolved = resolve_equation(eq, env);
    const std::set<std::string> syms = equation_symbols(resolved);
    if (syms.empty()) {
        const Expr difference = simplify(make_sub(resolved.lhs, resolved.rhs));
        if (difference->kind() == Kind::Number) {
            std::println("{}", difference->number().is_zero()
                                   ? "equation holds (identity)"
                                   : "equation is false (contradiction)");
            return;
        }
        // Symbol-free but not exactly foldable (e.g. sin(1)^2 + cos(1)^2 - 1).
        // "identity"/"contradiction" are exactness claims a float comparison
        // cannot make (sin(1e-7)^2 is nonzero but < 1e-12), so the numeric
        // path answers with an explicit caveat — the solver's standing
        // doctrine (§9 warnings) — instead of a false certainty.
        const double d = evaluate(difference);
        if (std::abs(d) < 1e-12) {
            std::println(
                "equation holds numerically (lhs - rhs ≈ {}; not verified "
                "exactly)",
                d);
        } else {
            std::println("equation is false numerically (lhs - rhs ≈ {})", d);
        }
        return;
    }
    if (syms.size() > 1) {
        std::string list;
        for (const std::string& s : syms) {
            if (!list.empty()) {
                list += ", ";
            }
            list += s;
        }
        throw UsageError{std::format(
            "the equation has {} free symbols ({}); use: solve <equation>, <variable>",
            syms.size(), list)};
    }
    const std::string var = *syms.begin();
    print_solve_result(solve(resolved, var), var, PrintStyle::Plain);
}

void repl_command(const std::string& command, const std::string& rest,
                  const Environment& env) {
    if (command == "grad" || command == "div" || command == "curl" ||
        command == "laplacian" || command == "jacobian" || command == "hessian") {
        // Field/scalar + variable list; resolve the session environment into
        // each field component and each variable is excluded from resolution.
        const std::vector<std::string> parts = split_top_level_commas(rest);
        if (parts.size() < 2) {
            throw UsageError{std::format(
                "usage: {} <field>, <var>[, <var> ...]   (a vector field is "
                "';'-separated, e.g. \"x*y; y*z; z*x\")",
                command)};
        }
        std::set<std::string> excluded(parts.begin() + 1, parts.end());
        std::vector<std::string> positionals;
        std::string resolved_field;
        for (const std::string& comp : split_top_level(parts[0], ';')) {
            if (!resolved_field.empty()) resolved_field += ";";
            resolved_field += to_string(
                resolve_expr(parse_expression_diag(comp), env, excluded),
                PrintStyle::Plain);
        }
        positionals.push_back(resolved_field);
        positionals.insert(positionals.end(), parts.begin() + 1, parts.end());
        run_vector(command, positionals, PrintStyle::Plain);
        return;
    }
    if (command == "rsolve") {
        if (trim(rest).empty()) {
            throw UsageError{
                "usage: rsolve <recurrence>[, a(0)=v, ...]   e.g. "
                "rsolve a(n+2) = a(n+1) + a(n), a(0)=0, a(1)=1"};
        }
        run_rsolve(rest, PrintStyle::Plain);
        return;
    }
    if (command == "sum" || command == "product") {
        const std::vector<std::string> parts = split_top_level_commas(rest);
        if (parts.size() != 4) {
            throw UsageError{std::format(
                "usage: {} <term>, <variable>, <lo>, <hi>", command)};
        }
        std::set<std::string> excluded{parts[1]};
        std::vector<std::string> args = parts;
        args[0] = to_string(
            resolve_expr(parse_expression_diag(parts[0]), env, excluded),
            PrintStyle::Plain);
        run_sum(args[0], args, command == "product", PrintStyle::Plain);
        warn_assigned_variable(parts[1], env);
        return;
    }
    if (command == "seq") {
        const std::vector<std::string> sparts = split_top_level_commas(rest);
        if (sparts.size() < 4) {
            throw UsageError{
                "usage: seq <a0>, <a1>, <a2>, <a3>[, ...]   (at least 4 terms)"};
        }
        run_seq(sparts, PrintStyle::Plain);
        return;
    }
    if (command == "stirling") {
        // No expression input: just an optional variable name and term count.
        const std::vector<std::string> sparts = split_top_level_commas(rest);
        if (sparts.size() > 2) {
            throw UsageError{"usage: stirling [<variable>[, <terms>]]"};
        }
        run_stirling(sparts.empty() ? "" : sparts[0],
                     sparts.size() > 1 ? sparts[1] : "", PrintStyle::Plain);
        return;
    }
    if (command == "dsolve") {
        // The ODE grammar (primes, y(0)=...) is handled by dsolve itself;
        // the session environment does not apply to ODE text.
        if (trim(rest).empty()) {
            throw UsageError{
                "usage: dsolve <ode>[, y(0)=v, y'(0)=v, ...]   e.g. "
                "dsolve y'' + 3y' + 2y = e^(-t), y(0)=1, y'(0)=0"};
        }
        run_dsolve(rest, PrintStyle::Plain);
        return;
    }
    if (command == "fit" || command == "regress") {
        // The data holds commas and semicolons, so the model/degree come after
        // a '|': fit 0,0; 1,1; 2,4 | quadratic
        std::string data = rest;
        std::string model;
        std::string degree;
        if (const auto bar = rest.rfind('|'); bar != std::string::npos) {
            data = rest.substr(0, bar);
            const std::string opts = trim(rest.substr(bar + 1));
            const auto sp = opts.find_first_of(" \t");
            if (sp == std::string::npos) {
                model = opts;
            } else {
                model = opts.substr(0, sp);
                degree = trim(opts.substr(sp + 1));
            }
        }
        if (trim(data).empty()) {
            throw UsageError{"usage: fit <x,y; x,y; ...> [| <model> [<degree>]]"};
        }
        run_fit(data, model, degree, PrintStyle::Plain);
        return;
    }
    if (command == "stats") {
        // The whole line is the data list (commas / semicolons / spaces).
        if (trim(rest).empty()) throw UsageError{"usage: stats <v1, v2, v3, ...>"};
        run_stats(rest, PrintStyle::Plain);
        return;
    }
    if (command == "vandermonde") {
        // The whole line is the node list (comma-separated).
        if (trim(rest).empty()) throw UsageError{"usage: vandermonde <x1, x2, x3, ...>"};
        run_vandermonde(rest, PrintStyle::Plain);
        return;
    }
    // Number theory over the integers: the whole line is the integer list (for
    // gcd/lcm) or a single integer (for the rest).
    if (command == "gcd" || command == "lcm" || command == "isprime" ||
        command == "nextprime" || command == "divisors" ||
        command == "totient") {
        if (trim(rest).empty()) {
            throw UsageError{std::format(
                "usage: {} <integer{}>", command,
                command == "gcd" || command == "lcm" ? ", integer, ..." : "")};
        }
        if (command == "gcd") run_gcd(rest);
        else if (command == "lcm") run_lcm(rest);
        else if (command == "isprime") run_isprime(rest);
        else if (command == "nextprime") run_nextprime(rest);
        else if (command == "divisors") run_divisors(rest);
        else run_totient(rest);
        return;
    }
    if (command == "cfrac") {
        if (trim(rest).empty()) {
            throw UsageError{"usage: cfrac <rational | sqrt(n) | real>"};
        }
        run_cfrac(rest, PrintStyle::Plain);
        return;
    }
    if (command == "mod" || command == "powmod" || command == "modinv" ||
        command == "crt") {
        if (trim(rest).empty()) {
            throw UsageError{std::format("usage: {} <arguments>", command)};
        }
        if (command == "mod") run_mod(rest);
        else if (command == "powmod") run_powmod(rest);
        else if (command == "modinv") run_modinv(rest);
        else run_crt(rest);
        return;
    }
    const std::vector<std::string> parts = split_top_level_commas(rest);
    if (parts.empty() || parts[0].empty()) {
        const std::string_view suffix =
            command == "solve" || command == "diff" || command == "collect"
                ? "[, <variable>]"
            : command == "subs" ? ", <name>=<expr>[, ...]"
                                : "";
        throw UsageError{std::format("usage: {} <input>{}", command, suffix)};
    }
    const std::string& input = parts[0];

    // The environment applies to the input of every computing verb, with the
    // verb's designated symbols excluded; display verbs (latex, debug) never
    // resolve — they must show the input as typed (spec §7).
    if (command == "solve") {
        // The first comma segment holds the equation(s) (';'-separated for a
        // system), the remaining segments are the variables.
        repl_solve(input, {parts.begin() + 1, parts.end()}, env);
    } else if (command == "diff" || command == "collect" || command == "apart" ||
               command == "cancel") {
        if (parts.size() > 2) {
            throw UsageError{std::format(
                "too many arguments: usage: {} <input>[, <variable>]", command)};
        }
        const std::string var = parts.size() > 1 ? parts[1] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(input), env, excluded),
            PrintStyle::Plain);
        if (command == "diff") {
            run_diff(resolved, var, PrintStyle::Plain);
        } else if (command == "apart") {
            run_apart(resolved, var, PrintStyle::Plain);
        } else if (command == "cancel") {
            run_cancel(resolved, var, PrintStyle::Plain);
        } else {
            run_collect(resolved, var, PrintStyle::Plain);
        }
        warn_assigned_variable(var, env);
    } else if (command == "integrate") {
        // integrate <expr>[, <var>[, <lo>, <hi>]] — bounds come as a pair.
        if (parts.size() == 3 || parts.size() > 4) {
            throw UsageError{
                "usage: integrate <expression>[, <variable>[, <lo>, <hi>]]"};
        }
        const std::string var = parts.size() >= 2 ? parts[1] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(input), env, excluded),
            PrintStyle::Plain);
        std::optional<std::string> from_text;
        std::optional<std::string> to_text;
        if (parts.size() == 4) {
            // Bounds are ordinary expressions: resolve them fully (spec §7).
            // A bound that blows up while folding keeps the §8b contract
            // (Unsolved + warning), exactly like run_integrate's own parse.
            try {
                from_text = to_string(
                    resolve_expr(parse_expression_diag(parts[2]), env),
                    PrintStyle::Plain);
                to_text = to_string(
                    resolve_expr(parse_expression_diag(parts[3]), env),
                    PrintStyle::Plain);
            } catch (const Error&) {
                std::println("unable to integrate");
                std::println(
                    "warning: integration bounds must evaluate to finite numbers");
                return;
            }
        }
        run_integrate(resolved, var, from_text, to_text, PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "mlimit") {
        const std::vector<std::string> parts2 = split_top_level_commas(rest);
        if (parts2.size() != 5) {
            throw UsageError{
                "usage: mlimit <expression>, <x var>, <a>, <y var>, <b>"};
        }
        std::set<std::string> excluded{parts2[1], parts2[3]};
        std::vector<std::string> args = parts2;
        args[0] = to_string(
            resolve_expr(parse_expression_diag(parts2[0]), env, excluded),
            PrintStyle::Plain);
        run_mlimit(args[0], args, PrintStyle::Plain);
    } else if (command == "limit") {
        // limit <expr>, <var>, <point>[, left|right]
        if (parts.size() < 3 || parts.size() > 4) {
            throw UsageError{
                "usage: limit <expression>, <variable>, <point>[, left|right]"};
        }
        std::set<std::string> excluded{parts[1]};
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(input), env, excluded),
            PrintStyle::Plain);
        run_limit(resolved, parts[1], parts[2],
                  parts.size() > 3 ? parts[3] : "", PrintStyle::Plain);
        warn_assigned_variable(parts[1], env);
    } else if (command == "series") {
        // series <expr>[, <var>[, <center>[, <order>]]]
        if (parts.size() > 4) {
            throw UsageError{
                "usage: series <expression>[, <variable>[, <center>[, <order>]]]"};
        }
        const std::string var = parts.size() > 1 ? parts[1] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(input), env, excluded),
            PrintStyle::Plain);
        run_series(resolved, var, parts.size() > 2 ? parts[2] : "",
                   parts.size() > 3 ? parts[3] : "", PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "pade") {
        // pade <expr>, <m>, <n>[, <var>]
        if (parts.size() < 3 || parts.size() > 4) {
            throw UsageError{"usage: pade <expression>, <m>, <n>[, <variable>]"};
        }
        const std::string var = parts.size() > 3 ? parts[3] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(input), env, excluded),
            PrintStyle::Plain);
        run_pade(resolved, parts[1], parts[2], var, PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "rootcount") {
        // rootcount <poly>[, <var>[, <lo>, <hi>]]
        if (parts.size() > 4) {
            throw UsageError{"usage: rootcount <poly>[, <variable>[, <lo>, <hi>]]"};
        }
        const std::string var = parts.size() > 1 ? parts[1] : "";
        std::set<std::string> excluded;
        if (!var.empty()) excluded.insert(var);
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(equation_to_poly(input)), env, excluded),
            PrintStyle::Plain);
        run_rootcount(resolved, var, parts.size() > 2 ? parts[2] : "",
                      parts.size() > 3 ? parts[3] : "");
        warn_assigned_variable(var, env);
    } else if (command == "isolate") {
        // isolate <poly>[, <var>]
        if (parts.size() > 2) {
            throw UsageError{"usage: isolate <poly>[, <variable>]"};
        }
        const std::string var = parts.size() > 1 ? parts[1] : "";
        std::set<std::string> excluded;
        if (!var.empty()) excluded.insert(var);
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(equation_to_poly(input)), env, excluded),
            PrintStyle::Plain);
        run_isolate(resolved, var, PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "discriminant") {
        // discriminant <polynomial>[, <var>]
        if (parts.size() > 2) {
            throw UsageError{"usage: discriminant <polynomial>[, <variable>]"};
        }
        const std::string var = parts.size() > 1 ? parts[1] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(input), env, excluded),
            PrintStyle::Plain);
        run_discriminant(resolved, var, PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "companion") {
        // companion <polynomial>[, <var>]
        if (parts.size() > 2) {
            throw UsageError{"usage: companion <polynomial>[, <variable>]"};
        }
        const std::string var = parts.size() > 1 ? parts[1] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(input), env, excluded),
            PrintStyle::Plain);
        run_companion(resolved, var, PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "polydiv") {
        // polydiv <dividend>, <divisor>[, <var>]
        if (parts.size() < 2 || parts.size() > 3) {
            throw UsageError{"usage: polydiv <dividend>, <divisor>[, <variable>]"};
        }
        const std::string var = parts.size() > 2 ? parts[2] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string dvd =
            to_string(resolve_expr(parse_expression_diag(parts[0]), env, excluded), PrintStyle::Plain);
        const std::string dvs =
            to_string(resolve_expr(parse_expression_diag(parts[1]), env, excluded), PrintStyle::Plain);
        run_polydiv(dvd, dvs, var, PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "polygcd" || command == "polylcm") {
        // polygcd <a>, <b>[, <var>]
        if (parts.size() < 2 || parts.size() > 3) {
            throw UsageError{std::format("usage: {} <a>, <b>[, <variable>]", command)};
        }
        const std::string var = parts.size() > 2 ? parts[2] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string ea =
            to_string(resolve_expr(parse_expression_diag(parts[0]), env, excluded), PrintStyle::Plain);
        const std::string eb =
            to_string(resolve_expr(parse_expression_diag(parts[1]), env, excluded), PrintStyle::Plain);
        run_polygcd(ea, eb, var, command == "polylcm", PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "resultant") {
        // resultant <a>, <b>[, <var>]
        if (parts.size() < 2 || parts.size() > 3) {
            throw UsageError{"usage: resultant <a>, <b>[, <variable>]"};
        }
        const std::string var = parts.size() > 2 ? parts[2] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string ea =
            to_string(resolve_expr(parse_expression_diag(parts[0]), env, excluded), PrintStyle::Plain);
        const std::string eb =
            to_string(resolve_expr(parse_expression_diag(parts[1]), env, excluded), PrintStyle::Plain);
        run_resultant(ea, eb, var, PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "bezout") {
        // bezout <a>, <b>[, <var>]
        if (parts.size() < 2 || parts.size() > 3) {
            throw UsageError{"usage: bezout <a>, <b>[, <variable>]"};
        }
        const std::string var = parts.size() > 2 ? parts[2] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string ea =
            to_string(resolve_expr(parse_expression_diag(parts[0]), env, excluded), PrintStyle::Plain);
        const std::string eb =
            to_string(resolve_expr(parse_expression_diag(parts[1]), env, excluded), PrintStyle::Plain);
        run_bezout(ea, eb, var, PrintStyle::Plain);
        warn_assigned_variable(var, env);
    } else if (command == "laplace" || command == "ilaplace") {
        if (parts.size() > 2) {
            throw UsageError{std::format(
                "too many arguments: usage: {} <input>[, <variable>]", command)};
        }
        const std::string var = parts.size() > 1 ? parts[1] : "";
        std::set<std::string> excluded;
        if (!var.empty()) {
            excluded.insert(var);
        }
        const std::string resolved = to_string(
            resolve_expr(parse_expression_diag(input), env, excluded),
            PrintStyle::Plain);
        if (command == "laplace") {
            run_laplace(resolved, var, PrintStyle::Plain);
        } else {
            run_ilaplace(resolved, var, PrintStyle::Plain);
        }
        warn_assigned_variable(var, env);
    } else if (command == "eval") {
        Bindings bindings;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            add_binding(bindings, parts[i]);
        }
        // Explicitly bound names shadow the environment for this command.
        std::set<std::string> excluded;
        for (const auto& [name, value] : bindings) {
            excluded.insert(name);
        }
        run_eval(to_string(resolve_expr(parse_expression_diag(input), env, excluded),
                           PrintStyle::Plain),
                 bindings);
        print_override_notes({parts.begin() + 1, parts.end()}, env);
    } else if (command == "subs") {
        const std::vector<std::string> args(parts.begin() + 1, parts.end());
        // Explicit substitution names shadow the environment; resolution runs
        // once, before the verb (expressions introduced by explicit values
        // are not re-resolved — spec §7 single-pass doctrine).
        std::set<std::string> excluded;
        for (const std::string& a : args) {
            if (const std::size_t eq = a.find('='); eq != std::string::npos) {
                excluded.insert(trim(a.substr(0, eq)));
            }
        }
        run_subs(resolve_input_text(input, env, excluded), args, PrintStyle::Plain);
        print_override_notes(args, env);
    } else if (command == "simplify") {
        run_simplify(resolve_input_text(input, env), PrintStyle::Plain);
    } else if (command == "expand") {
        run_expand(resolve_input_text(input, env), PrintStyle::Plain);
    } else if (command == "factor") {
        run_factor(resolve_input_text(input, env), PrintStyle::Plain);
    } else if (command == "together") {
        run_together(resolve_input_text(input, env), PrintStyle::Plain);
    } else if (command == "trigexpand") {
        run_trigexpand(resolve_input_text(input, env), PrintStyle::Plain);
    } else if (command == "trigreduce") {
        run_trigreduce(resolve_input_text(input, env), PrintStyle::Plain);
    } else if (command == "logexpand") {
        run_logexpand(resolve_input_text(input, env), PrintStyle::Plain);
    } else if (command == "logcombine") {
        run_logcombine(resolve_input_text(input, env), PrintStyle::Plain);
    } else if (command == "latex") {
        run_latex(input);
    } else {  // debug
        run_debug(input);
    }
}

void repl_line(const std::string& line, Environment& env) {
    // Assignment (`name := value`) is recognized at the input layer, before
    // the parser or command dispatch ever see the text (spec §2).
    if (is_assignment_line(line)) {
        repl_assign(line, env);
        return;
    }

    // A leading known command word (followed by whitespace or end of line)
    // selects command mode; anything else is a bare expression/equation.
    std::size_t word_end = 0;
    while (word_end < line.size() &&
           std::isalpha(static_cast<unsigned char>(line[word_end])) != 0) {
        ++word_end;
    }
    const std::string word = line.substr(0, word_end);
    const bool word_terminated =
        word_end == line.size() ||
        std::isspace(static_cast<unsigned char>(line[word_end])) != 0;

    // Environment management (spec §7.1). As bare inputs all three words are
    // word-guard parse errors today, so claiming them changes no working
    // input.
    if (line == "vars") {
        if (env.empty()) {
            std::println("no variables defined");
        }
        for (const Binding& b : env) {
            std::println("{}", binding_text(b));
        }
        return;
    }
    if (line == "clear") {
        std::println("cleared {} assignment(s)", env.size());
        env.clear();
        return;
    }
    if (word == "unset" && word_terminated) {
        const std::string typed = trim(line.substr(word_end));
        if (typed.empty()) {
            throw UsageError{"usage: unset <name>"};
        }
        // Accept both spellings of a name, exactly like the assignment side:
        // `vars` displays `x_{max}`, the stored symbol is `x_max` — copy/
        // pasting the displayed name must work.
        const std::string name = normalize_target_text(typed);
        const auto it = std::ranges::find(env, name, &Binding::name);
        if (it == env.end()) {
            std::println("note: '{}' is not defined", typed);
        } else {
            env.erase(it);
        }
        return;
    }

    if (word_terminated && is_repl_command(word)) {
        repl_command(word, trim(line.substr(word_end)), env);
        return;
    }

    const auto parsed = parse_input_diag(line);
    if (std::holds_alternative<Expr>(parsed)) {
        const Expr& e = std::get<Expr>(parsed);
        // A name bound to an equation may stand as the entire input line
        // (spec §4): it denotes the stored equation.
        if (e->kind() == Kind::Symbol) {
            if (const Binding* b = find_binding(env, e->symbol_name());
                b != nullptr && std::holds_alternative<Equation>(b->value)) {
                repl_bare_equation(std::get<Equation>(b->value), env);
                return;
            }
        }
        std::println("{}", to_string(simplify(resolve_expr(e, env))));
        return;
    }
    repl_bare_equation(std::get<Equation>(parsed), env);
}

int run_repl() {
    std::println("MathSolver {} — type \"help\" for commands, \"quit\" to exit",
                 k_version);
    Environment env;  // session `:=` assignments; lives and dies with the process
    std::string line;
    for (;;) {
        std::print(">>> ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) {
            std::println("");  // clean newline after Ctrl-D
            break;
        }
        const std::string input = trim(line);
        if (input.empty()) {
            continue;
        }
        if (input == "quit" || input == "exit") {
            break;
        }
        if (input == "help") {
            print_repl_help();
            continue;
        }
        try {
            repl_line(input, env);
        } catch (const UsageError& e) {
            std::fflush(stdout);
            std::println(stderr, "error: {}", e.message);
        } catch (const DiagnosedParseError& e) {
            std::fflush(stdout);
            std::println(stderr, "{}", caret_diagnostic(e.source, e.error));
        } catch (const Error& e) {
            std::fflush(stdout);
            std::println(stderr, "error: {}", e.what());
        }
    }
    return k_exit_ok;
}

}  // namespace

int main(int argc, char** argv) {
    // Line-buffer stdout so prompts/results interleave correctly with stderr
    // diagnostics when the output is piped (the e2e tests rely on this).
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);

    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        return run_repl();
    }
    if (args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        print_usage(stdout);
        return k_exit_ok;
    }
    if (args[0] == "--version") {
        std::println("MathSolver {}", k_version);
        return k_exit_ok;
    }
    return run_one_shot(args);
}
