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

void run_factor(const std::string& input, PrintStyle style) {
    const auto parsed = parse_input_diag(input);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(factor(std::get<Expr>(parsed)), style));
    } else {
        const Equation& eq = std::get<Equation>(parsed);
        std::println("{}", to_string(Equation{factor(eq.lhs), factor(eq.rhs)}, style));
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

void run_eval(const std::string& input, const Bindings& bindings) {
    const Expr e = parse_expression_diag(input);
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

void run_solve(const std::string& input, const std::string& explicit_var,
               const NumericOptions& opts, PrintStyle style) {
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
               "  mathsolver solve    \"x^2 = 4\" [x] [--range LO HI]\n"
               "  mathsolver solve    \"x + y = 3; x - y = 1\" [x y ...]\n"
               "  mathsolver diff     \"sin(x^2)\" [x]\n"
               "  mathsolver integrate \"x*sin(x)\" [x]\n"
               "  mathsolver integrate \"sin(x)\" [x] --from 0 --to pi\n"
               "  mathsolver apart    \"(3x+2)/((x+1)(x+2))\" [x]\n"
               "  mathsolver laplace  \"e^(-t) sin(2t)\" [t]\n"
               "  mathsolver ilaplace \"1/(s^2 + 2s + 5)\" [s]\n"
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
    return s == "simplify" || s == "expand" || s == "factor" || s == "solve" ||
           s == "diff" || s == "integrate" || s == "eval" || s == "latex" ||
           s == "subs" || s == "collect" || s == "laplace" || s == "ilaplace" ||
           s == "apart";
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
                   sub == "apart") {
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
            } else if (sub == "laplace") {
                run_laplace(input, var, style);
            } else if (sub == "ilaplace") {
                run_ilaplace(input, var, style);
            } else {
                run_integrate(input, var, from_text, to_text, style);
            }
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
        "  simplify <expression>      expand <expression>\n"
        "  factor <expression>        latex <expression>\n"
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
           word == "solve" || word == "diff" || word == "integrate" ||
           word == "eval" || word == "latex" || word == "debug" ||
           word == "subs" || word == "collect" || word == "laplace" ||
           word == "ilaplace" || word == "apart";
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
    } else if (command == "diff" || command == "collect" || command == "apart") {
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
