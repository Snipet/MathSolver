// MathSolver command-line interface and REPL (DESIGN.md §10).
//
// One-shot subcommands (simplify/expand/factor/solve/diff/eval/latex) print
// plain style by default; --latex switches to LaTeX. Errors go to stderr with
// caret diagnostics rendered from ParseError spans. Exit codes: 0 success,
// 1 parse/math error, 2 usage error. With no arguments an interactive REPL
// starts (">>> " prompt, plain std::getline — behaves identically when stdin
// is a pipe).

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

/// "x=3" -> binding; throws UsageError when malformed.
void add_binding(Bindings& bindings, const std::string& arg) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string::npos) {
        throw UsageError{std::format(
            "malformed binding '{}': expected name=value (e.g. x=3)", arg)};
    }
    const std::string name = trim(arg.substr(0, eq));
    const std::string value_text = trim(arg.substr(eq + 1));

    // Reject names that can never lex as a single bound variable. A bindable
    // name parses to exactly one Symbol equal to itself — a single letter, a
    // greek name, or a subscripted form (DESIGN §4). Constants (pi, e) parse to
    // a Constant and multi-letter runs (`foo` -> f*o*o) to a Mul; neither can
    // be bound, and they get distinct diagnostics.
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

void run_eval(const std::string& input, const Bindings& bindings) {
    const Expr e = parse_expression_diag(input);
    std::println("{}", evaluate(e, bindings));
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
               "MathSolver {} — parse, simplify, differentiate, and solve "
               "LaTeX-style math\n"
               "\n"
               "usage:\n"
               "  mathsolver simplify \"2x + 3x\"\n"
               "  mathsolver expand   \"(x+1)^3\"\n"
               "  mathsolver factor   \"x^2 - 5x + 6\"\n"
               "  mathsolver solve    \"x^2 = 4\" [x] [--range LO HI]\n"
               "  mathsolver diff     \"sin(x^2)\" [x]\n"
               "  mathsolver eval     \"x^2 + y\" x=3 y=0.5\n"
               "  mathsolver latex    \"sqrt(x)/2\"\n"
               "  mathsolver --help | --version\n"
               "  mathsolver          (no arguments: interactive REPL)\n"
               "\n"
               "options:\n"
               "  --latex        render output in LaTeX instead of plain text\n"
               "  --range LO HI  numeric root-search interval for solve "
               "(default: -100 100)\n"
               "\n"
               "The solve/diff variable may be omitted when the input has exactly\n"
               "one free symbol. Exit codes: 0 success, 1 parse/math error,\n"
               "2 usage error; error diagnostics go to stderr.\n",
               k_version);
}

bool is_known_subcommand(std::string_view s) {
    return s == "simplify" || s == "expand" || s == "factor" || s == "solve" ||
           s == "diff" || s == "eval" || s == "latex";
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

        if (sub == "solve" || sub == "diff") {
            if (positionals.size() > 2) {
                throw UsageError{std::format(
                    "unexpected argument '{}' (usage: mathsolver {} \"<input>\" [var])",
                    positionals[2], sub)};
            }
            const std::string var = positionals.size() > 1 ? positionals[1] : "";
            if (sub == "solve") {
                run_solve(input, var, opts, style);
            } else {
                run_diff(input, var, style);
            }
        } else if (sub == "eval") {
            Bindings bindings;
            for (std::size_t i = 1; i < positionals.size(); ++i) {
                add_binding(bindings, positionals[i]);
            }
            run_eval(input, bindings);
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
        "  diff <expression>[, <variable>]\n"
        "  eval <expression>, x=1[, y=2 ...]\n"
        "  simplify <expression>      expand <expression>\n"
        "  factor <expression>        latex <expression>\n"
        "  debug <expression>         (s-expression dump)\n"
        "  help                       quit / exit\n");
}

/// Split at commas that are not nested inside (), {}, or [].
std::vector<std::string> split_top_level_commas(const std::string& s) {
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (const char c : s) {
        if (c == '(' || c == '{' || c == '[') {
            ++depth;
        } else if (c == ')' || c == '}' || c == ']') {
            --depth;
        }
        if (c == ',' && depth <= 0) {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(trim(current));
    return parts;
}

bool is_repl_command(std::string_view word) {
    return word == "simplify" || word == "expand" || word == "factor" ||
           word == "solve" || word == "diff" || word == "eval" ||
           word == "latex" || word == "debug";
}

void repl_command(const std::string& command, const std::string& rest) {
    const std::vector<std::string> parts = split_top_level_commas(rest);
    if (parts.empty() || parts[0].empty()) {
        throw UsageError{std::format("usage: {} <input>{}", command,
                                     command == "solve" || command == "diff"
                                         ? "[, <variable>]"
                                         : "")};
    }
    const std::string& input = parts[0];

    if (command == "solve" || command == "diff") {
        if (parts.size() > 2) {
            throw UsageError{std::format(
                "too many arguments: usage: {} <input>[, <variable>]", command)};
        }
        const std::string var = parts.size() > 1 ? parts[1] : "";
        if (command == "solve") {
            run_solve(input, var, NumericOptions{}, PrintStyle::Plain);
        } else {
            run_diff(input, var, PrintStyle::Plain);
        }
    } else if (command == "eval") {
        Bindings bindings;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            add_binding(bindings, parts[i]);
        }
        run_eval(input, bindings);
    } else if (command == "simplify") {
        run_simplify(input, PrintStyle::Plain);
    } else if (command == "expand") {
        run_expand(input, PrintStyle::Plain);
    } else if (command == "factor") {
        run_factor(input, PrintStyle::Plain);
    } else if (command == "latex") {
        run_latex(input);
    } else {  // debug
        run_debug(input);
    }
}

void repl_line(const std::string& line) {
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
    if (word_terminated && is_repl_command(word)) {
        repl_command(word, trim(line.substr(word_end)));
        return;
    }

    const auto parsed = parse_input_diag(line);
    if (std::holds_alternative<Expr>(parsed)) {
        std::println("{}", to_string(simplify(std::get<Expr>(parsed))));
        return;
    }
    const Equation& eq = std::get<Equation>(parsed);
    std::set<std::string> syms = equation_symbols(eq);
    if (syms.size() != 1) {
        std::string list;
        for (const std::string& s : syms) {
            if (!list.empty()) {
                list += ", ";
            }
            list += s;
        }
        throw UsageError{std::format(
            "the equation has {} free symbols{}; use: solve <equation>, <variable>",
            syms.size(), syms.empty() ? std::string{} : " (" + list + ")")};
    }
    const std::string var = *syms.begin();
    print_solve_result(solve(eq, var), var, PrintStyle::Plain);
}

int run_repl() {
    std::println("MathSolver {} — type \"help\" for commands, \"quit\" to exit",
                 k_version);
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
            repl_line(input);
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
