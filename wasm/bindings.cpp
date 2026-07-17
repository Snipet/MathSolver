// WebAssembly bindings for the MathSolver core (embind).
//
// Conventions:
//   - Inputs are plain strings (expressions in the DESIGN.md §4 grammar;
//     variable lists comma-separated; eval bindings as "x=1,y=2.5";
//     subs assignments as "x=y+1,z=2" (values parsed as expressions);
//     systems as top-level-';'-separated equations).
//   - Every function returns a JSON string. Success envelopes carry
//     ok:true; failures carry ok:false with error text and, for parse
//     errors, the [begin,end) byte span into the input for caret UI.
//   - No C++ exception ever crosses the JS boundary.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <emscripten/bind.h>

#include "mathsolver/mathsolver.hpp"
#include "mathsolver/plugin.hpp"

namespace {

using namespace mathsolver;

// ---------------------------------------------------------------------------
// Minimal JSON writing (strings escaped; doubles finite-checked).
// ---------------------------------------------------------------------------

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string jstr(std::string_view s) {
    return "\"" + json_escape(s) + "\"";
}

std::string jnum(double v) {
    if (!std::isfinite(v)) {
        return "null";
    }
    return std::format("{}", v);
}

std::string jarr_str(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ",";
        out += jstr(items[i]);
    }
    return out + "]";
}

std::string err_json(std::string_view message) {
    return std::format("{{\"ok\":false,\"error\":{}}}", jstr(message));
}

std::string parse_err_json(const ParseError& e) {
    return std::format("{{\"ok\":false,\"error\":{},\"begin\":{},\"end\":{}}}",
                       jstr(e.what()), e.begin(), e.end());
}

/// Run `fn` (returning a complete JSON object string) with the standard
/// error envelope around it.
template <typename Fn>
std::string guarded(Fn&& fn) {
    try {
        return fn();
    } catch (const ParseError& e) {
        return parse_err_json(e);
    } catch (const Error& e) {
        return err_json(e.what());
    } catch (const std::exception& e) {
        return err_json(std::string("internal error: ") + e.what());
    } catch (...) {
        return err_json("internal error");
    }
}

/// plain + latex renderings of an expression as JSON fields (no braces).
std::string rendered_fields(const Expr& e) {
    return std::format("\"plain\":{},\"latex\":{}", jstr(to_string(e, PrintStyle::Plain)),
                       jstr(to_string(e, PrintStyle::LaTeX)));
}

// ---------------------------------------------------------------------------
// Input helpers (mirror the CLI conventions).
// ---------------------------------------------------------------------------

/// Split at top-level ';' (outside parens/braces/brackets).
std::vector<std::string> split_equations(std::string_view src) {
    std::vector<std::string> parts;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        if (c == '(' || c == '{' || c == '[') ++depth;
        if (c == ')' || c == '}' || c == ']') --depth;
        if (c == ';' && depth == 0) {
            parts.emplace_back(src.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.emplace_back(src.substr(start));
    return parts;
}

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) --e;
    return std::string(s.substr(b, e - b));
}

std::vector<std::string> split_csv(std::string_view s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ',') {
            const std::string item = trim(s.substr(start, i - start));
            if (!item.empty()) out.push_back(item);
            start = i + 1;
        }
    }
    return out;
}

/// Solve-style input: "lhs = rhs", or a bare expression meaning expr = 0.
Equation equation_from(const std::string& src) {
    const auto parsed = parse_input(src);
    if (std::holds_alternative<Equation>(parsed)) {
        return std::get<Equation>(parsed);
    }
    return Equation{std::get<Expr>(parsed), make_num(0)};
}

// ---------------------------------------------------------------------------
// Bound functions.
// ---------------------------------------------------------------------------

std::string ms_version() {
    return std::format("{{\"ok\":true,\"version\":{}}}", jstr(k_version));
}

/// Inspect input: expression / equation / system, its free symbols, and the
/// parsed form rendered both ways (for live "as parsed" preview).
std::string ms_analyze(std::string input) {
    return guarded([&]() -> std::string {
        const auto pieces = split_equations(input);
        if (pieces.size() > 1) {
            std::set<std::string> syms;
            for (const auto& p : pieces) {
                const Equation eq = parse_equation(trim(p));
                syms.merge(free_symbols(eq.lhs));
                syms.merge(free_symbols(eq.rhs));
            }
            const std::vector<std::string> list(syms.begin(), syms.end());
            return std::format("{{\"ok\":true,\"kind\":\"system\",\"symbols\":{}}}",
                               jarr_str(list));
        }
        const auto parsed = parse_input(input);
        if (std::holds_alternative<Equation>(parsed)) {
            const Equation& eq = std::get<Equation>(parsed);
            std::set<std::string> syms = free_symbols(eq.lhs);
            syms.merge(free_symbols(eq.rhs));
            const std::vector<std::string> list(syms.begin(), syms.end());
            return std::format(
                "{{\"ok\":true,\"kind\":\"equation\",\"symbols\":{},\"plain\":{},\"latex\":{}}}",
                jarr_str(list), jstr(to_string(eq, PrintStyle::Plain)),
                jstr(to_string(eq, PrintStyle::LaTeX)));
        }
        const Expr& e = std::get<Expr>(parsed);
        const std::set<std::string> syms = free_symbols(e);
        const std::vector<std::string> list(syms.begin(), syms.end());
        return std::format("{{\"ok\":true,\"kind\":\"expression\",\"symbols\":{},{}}}",
                           jarr_str(list), rendered_fields(e));
    });
}

std::string transform_json(const std::string& input, Expr (*op)(const Expr&)) {
    return guarded([&]() -> std::string {
        const Expr e = op(parse_expression(input));
        return std::format("{{\"ok\":true,{}}}", rendered_fields(e));
    });
}

Expr identity_op(const Expr& e) { return e; }

std::string ms_simplify(std::string input) {
    return transform_json(input, [](const Expr& e) { return simplify(e); });
}
std::string ms_expand(std::string input) {
    return transform_json(input, [](const Expr& e) { return expand(e); });
}
std::string ms_factor(std::string input) {
    return transform_json(input, [](const Expr& e) { return factor(e); });
}
std::string ms_latex(std::string input) {
    return transform_json(input, identity_op);
}

/// Substitute expressions for symbols: assignments as "x=y+1,z=2" (split
/// like eval bindings, but the value side is parsed as an expression),
/// applied left to right, then simplified.
/// subs(input, assignments, simplifyResult): sequential substitution over an
/// expression or an equation (both sides), mirroring the CLI verb. With
/// simplifyResult=false the substituted form is returned un-simplified — the
/// web resolver path uses it so "computed from" shows the resolved input as
/// resolved, not as simplified (spec §8). Embind arity is exact, so the
/// parameter is required at the ABI; types.ts mirrors it.
std::string ms_subs(std::string input, std::string assignments, bool simplify_result) {
    return guarded([&]() -> std::string {
        std::vector<std::pair<std::string, Expr>> subs;
        for (const auto& part : split_csv(assignments)) {
            const std::size_t eq = part.find('=');
            const std::string name =
                eq == std::string::npos ? std::string{} : trim(part.substr(0, eq));
            const std::string value =
                eq == std::string::npos ? std::string{} : trim(part.substr(eq + 1));
            if (name.empty() || value.empty()) {
                return err_json(std::format(
                    "malformed substitution '{}': expected name=expression", part));
            }
            subs.emplace_back(name, parse_expression(value));
        }
        if (subs.empty()) {
            return err_json("subs needs at least one name=expression assignment");
        }
        // Equations substitute on both sides, mirroring the CLI verb.
        const auto parsed = parse_input(input);
        if (std::holds_alternative<Equation>(parsed)) {
            Equation eq = std::get<Equation>(parsed);
            for (const auto& [name, replacement] : subs) {
                eq.lhs = substitute(eq.lhs, name, replacement);
                eq.rhs = substitute(eq.rhs, name, replacement);
            }
            const Equation out = simplify_result ? simplify(eq) : eq;
            return std::format("{{\"ok\":true,\"plain\":{},\"latex\":{}}}",
                               jstr(to_string(out, PrintStyle::Plain)),
                               jstr(to_string(out, PrintStyle::LaTeX)));
        }
        Expr e = std::get<Expr>(parsed);
        for (const auto& [name, replacement] : subs) {
            e = substitute(e, name, replacement);
        }
        return std::format("{{\"ok\":true,{}}}",
                           rendered_fields(simplify_result ? simplify(e) : e));
    });
}

/// collect(expr, symbol): regroup as a polynomial in `variable`. An empty
/// variable infers the single free symbol (an error when there are 0 or
/// several, mirroring the CLI).
std::string ms_collect(std::string input, std::string variable) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(input);
        std::string var = trim(variable);
        if (var.empty()) {
            const std::set<std::string> syms = free_symbols(e);
            if (syms.size() != 1) {
                std::string list;
                for (const auto& s : syms) {
                    if (!list.empty()) list += ", ";
                    list += s;
                }
                return err_json(
                    syms.empty()
                        ? "cannot infer the variable: the input has no free symbols"
                        : "cannot infer the variable: candidates are " + list);
            }
            var = *syms.begin();
        }
        return std::format("{{\"ok\":true,{}}}", rendered_fields(collect(e, var)));
    });
}

std::string ms_derivative(std::string input, std::string var) {
    return guarded([&]() -> std::string {
        const Expr d = differentiate(parse_expression(input), var);
        return std::format("{{\"ok\":true,{}}}", rendered_fields(d));
    });
}

/// apart(expr, variable): partial-fraction expansion. An empty variable
/// infers the single free symbol, mirroring collect.
std::string ms_apart(std::string input, std::string variable) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(input);
        std::string var = trim(variable);
        if (var.empty()) {
            const std::set<std::string> syms = free_symbols(e);
            if (syms.size() != 1) {
                std::string list;
                for (const auto& s : syms) {
                    if (!list.empty()) list += ", ";
                    list += s;
                }
                return err_json(
                    syms.empty()
                        ? "cannot infer the variable: the input has no free symbols"
                        : "cannot infer the variable: candidates are " + list);
            }
            var = *syms.begin();
        }
        return std::format("{{\"ok\":true,{}}}", rendered_fields(apart(e, var)));
    });
}

/// Split on ';' (vector-field component separator), trimming blanks.
std::vector<std::string> split_semi(std::string_view s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ';') {
            const std::string item = trim(s.substr(start, i - start));
            if (!item.empty()) out.push_back(item);
            start = i + 1;
        }
    }
    return out;
}

/// vectorOp(op, fieldSemi, varsCsv): grad/div/curl/laplacian/jacobian/hessian.
/// `fieldSemi` is a ';'-separated field (a single expression for the scalar
/// operators); `varsCsv` is the comma-separated variable list. Returns the
/// result rendered plain and LaTeX.
std::string ms_vector_op(std::string op, std::string field_semi,
                         std::string vars_csv) {
    return guarded([&]() -> std::string {
        const std::vector<std::string> vars = split_csv(vars_csv);
        ExprVec field;
        for (const std::string& c : split_semi(field_semi)) {
            field.push_back(parse_expression(c));
        }
        if (field.empty()) {
            return err_json("no field expression given");
        }
        const Expr scalar = field.front();
        auto render_vec = [](const ExprVec& v) {
            return std::format("\"plain\":{},\"latex\":{}",
                               jstr(vec_to_string(v, PrintStyle::Plain)),
                               jstr(vec_to_string(v, PrintStyle::LaTeX)));
        };
        auto render_mat = [](const ExprMat& m) {
            return std::format("\"plain\":{},\"latex\":{}",
                               jstr(mat_to_string(m, PrintStyle::Plain)),
                               jstr(mat_to_string(m, PrintStyle::LaTeX)));
        };
        std::string fields;
        if (op == "grad") {
            fields = render_vec(gradient(scalar, vars));
        } else if (op == "div") {
            fields = rendered_fields(divergence(field, vars));
        } else if (op == "curl") {
            if (field.size() == 2 && vars.size() == 2) {
                fields = rendered_fields(curl2d(field, vars));
            } else {
                fields = render_vec(curl(field, vars));
            }
        } else if (op == "laplacian") {
            fields = rendered_fields(laplacian(scalar, vars));
        } else if (op == "jacobian") {
            fields = render_mat(jacobian(field, vars));
        } else if (op == "hessian") {
            fields = render_mat(hessian(scalar, vars));
        } else {
            return err_json("unknown vector operator '" + op + "'");
        }
        return std::format("{{\"ok\":true,{}}}", fields);
    });
}

/// sampleField(fx, fy, xVar, yVar, xlo, xhi, ylo, yhi, n): evaluate a planar
/// vector field on an n×n grid for a quiver plot. Returns flat x/y position
/// arrays and u/v component arrays (null where a sample is non-finite).
std::string ms_sample_field(std::string fx, std::string fy, std::string xvar,
                            std::string yvar, double xlo, double xhi, double ylo,
                            double yhi, int n) {
    return guarded([&]() -> std::string {
        if (n < 2 || n > 40) {
            return err_json("grid size must be in [2, 40]");
        }
        const Expr u = parse_expression(fx);
        const Expr v = parse_expression(fy);
        const std::string xv = trim(xvar).empty() ? "x" : trim(xvar);
        const std::string yv = trim(yvar).empty() ? "y" : trim(yvar);
        std::vector<double> xs, ys, us, vs;
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                const double x = xlo + (xhi - xlo) * i / (n - 1);
                const double y = ylo + (yhi - ylo) * j / (n - 1);
                double uu = std::numeric_limits<double>::quiet_NaN();
                double vv = std::numeric_limits<double>::quiet_NaN();
                try {
                    uu = evaluate(u, Bindings{{xv, x}, {yv, y}});
                    vv = evaluate(v, Bindings{{xv, x}, {yv, y}});
                } catch (const Error&) {
                }
                xs.push_back(x);
                ys.push_back(y);
                us.push_back(uu);
                vs.push_back(vv);
            }
        }
        auto arr = [](const std::vector<double>& a) {
            std::string out = "[";
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (i > 0) out += ",";
                out += jnum(a[i]);
            }
            return out + "]";
        };
        return std::format(
            "{{\"ok\":true,\"n\":{},\"x\":{},\"y\":{},\"u\":{},\"v\":{}}}", n,
            arr(xs), arr(ys), arr(us), arr(vs));
    });
}

/// series(expr, variable, center, order): Taylor expansion. Empty variable
/// infers the single free symbol; empty center means 0.
std::string ms_series(std::string input, std::string variable,
                      std::string center, int order) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(input);
        std::string var = trim(variable);
        if (var.empty()) {
            const std::set<std::string> syms = free_symbols(e);
            if (syms.size() != 1) {
                return err_json(
                    syms.empty()
                        ? "cannot infer the variable: the input has no free symbols"
                        : "give the series variable explicitly: series <expr>, "
                          "<var>[, <center>[, <order>]]");
            }
            var = *syms.begin();
        }
        const Expr c = trim(center).empty() ? make_num(0)
                                            : parse_expression(trim(center));
        return std::format("{{\"ok\":true,{}}}",
                           rendered_fields(series(e, var, c, order)));
    });
}

/// dsolve(ode, conditionsCsv): solve a linear constant-coefficient IVP.
/// Returns the solution as plain/latex plus the partial-fraction Y(s) and
/// any assumed-zero-IC warnings.
std::string ms_dsolve(std::string ode, std::string conditions_csv) {
    return guarded([&]() -> std::string {
        std::vector<std::string> conditions;
        std::size_t start = 0;
        while (start <= conditions_csv.size()) {
            const std::size_t comma = conditions_csv.find(',', start);
            const std::string part = trim(conditions_csv.substr(
                start, comma == std::string::npos ? std::string::npos
                                                  : comma - start));
            if (!part.empty()) {
                conditions.push_back(part);
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        const DsolveResult r = dsolve(ode, conditions);
        return std::format(
            "{{\"ok\":true,{},\"transformPlain\":{},\"transformLatex\":{},"
            "\"warnings\":{}}}",
            rendered_fields(r.solution),
            jstr(to_string(r.transform, PrintStyle::Plain)),
            jstr(to_string(r.transform, PrintStyle::LaTeX)),
            jarr_str(r.warnings));
    });
}

/// laplace(expr, timeVar): f(t) -> F(s). Empty timeVar defaults to t.
std::string ms_laplace(std::string input, std::string time) {
    return guarded([&]() -> std::string {
        const std::string t = trim(time).empty() ? "t" : trim(time);
        const Expr F = laplace(parse_expression(input), t);
        return std::format("{{\"ok\":true,{}}}", rendered_fields(F));
    });
}

/// ilaplace(expr, freqVar): F(s) -> f(t). Empty freqVar defaults to s.
std::string ms_ilaplace(std::string input, std::string svar) {
    return guarded([&]() -> std::string {
        const std::string s = trim(svar).empty() ? "s" : trim(svar);
        const Expr f = inverse_laplace(parse_expression(input), s);
        return std::format("{{\"ok\":true,{}}}", rendered_fields(f));
    });
}

std::string ms_integrate(std::string input, std::string var) {
    return guarded([&]() -> std::string {
        const IntegrateResult r = integrate(parse_expression(input), var);
        const bool solved = r.status == IntegrateResult::Status::Integrated;
        std::string out = std::format("{{\"ok\":true,\"solved\":{}", solved ? "true" : "false");
        if (solved) {
            out += "," + rendered_fields(r.antiderivative);
        }
        out += std::format(",\"method\":{},\"warnings\":{}}}", jstr(r.method),
                           jarr_str(r.warnings));
        return out;
    });
}

std::string ms_integrate_definite(std::string input, std::string var, std::string lo,
                                  std::string hi) {
    return guarded([&]() -> std::string {
        const DefiniteIntegralResult r = integrate_definite(
            parse_expression(input), var, parse_expression(lo), parse_expression(hi));
        const char* status = r.status == DefiniteIntegralResult::Status::Exact ? "exact"
                             : r.status == DefiniteIntegralResult::Status::Numeric
                                 ? "numeric"
                                 : "unsolved";
        std::string out = std::format("{{\"ok\":true,\"status\":{}", jstr(status));
        if (r.status != DefiniteIntegralResult::Status::Unsolved) {
            out += "," + rendered_fields(r.value);
            const auto v = [&]() -> std::optional<double> {
                try {
                    return evaluate(r.value);
                } catch (const Error&) {
                    return std::nullopt;
                }
            }();
            if (v) {
                out += std::format(",\"approx\":{}", jnum(*v));
            }
        }
        out += std::format(",\"method\":{},\"warnings\":{}}}", jstr(r.method),
                           jarr_str(r.warnings));
        return out;
    });
}

std::string solve_status_name(SolveResult::Status s) {
    switch (s) {
        case SolveResult::Status::Solved: return "solved";
        case SolveResult::Status::SolvedComplex: return "complex";
        case SolveResult::Status::NumericOnly: return "numeric";
        case SolveResult::Status::NoRealSolution: return "noRealSolution";
        case SolveResult::Status::AllReals: return "allReals";
        case SolveResult::Status::Unsolved: return "unsolved";
    }
    return "unsolved";
}

std::string ms_solve(std::string input, std::string var, double lo, double hi,
                     bool use_range) {
    return guarded([&]() -> std::string {
        const Equation eq = equation_from(input);
        NumericOptions opts;
        if (use_range) {
            opts.lo = lo;
            opts.hi = hi;
        }
        const SolveResult r = solve(eq, var, opts);
        std::string sols = "[";
        for (std::size_t i = 0; i < r.solutions.size(); ++i) {
            const Solution& s = r.solutions[i];
            if (i > 0) sols += ",";
            const auto v = [&]() -> std::optional<double> {
                try {
                    return evaluate(s.value);
                } catch (const Error&) {
                    return std::nullopt;
                }
            }();
            sols += std::format("{{{},\"exact\":{},\"note\":{},\"approx\":{}}}",
                                rendered_fields(s.value), s.exact ? "true" : "false",
                                jstr(s.note), v ? jnum(*v) : "null");
        }
        sols += "]";
        return std::format(
            "{{\"ok\":true,\"status\":{},\"method\":{},\"warnings\":{},\"solutions\":{}}}",
            jstr(solve_status_name(r.status)), jstr(r.method), jarr_str(r.warnings), sols);
    });
}

std::string ms_solve_system(std::string input, std::string vars_csv) {
    return guarded([&]() -> std::string {
        std::vector<Equation> eqs;
        for (const auto& piece : split_equations(input)) {
            eqs.push_back(parse_equation(trim(piece)));
        }
        std::vector<std::string> vars = split_csv(vars_csv);
        if (vars.empty()) {
            std::set<std::string> syms;
            for (const Equation& eq : eqs) {
                syms.merge(free_symbols(eq.lhs));
                syms.merge(free_symbols(eq.rhs));
            }
            if (syms.size() > eqs.size()) {
                const std::vector<std::string> list(syms.begin(), syms.end());
                return err_json("cannot infer the variables: candidates are " +
                                [&list] {
                                    std::string j;
                                    for (const auto& s : list) {
                                        if (!j.empty()) j += ", ";
                                        j += s;
                                    }
                                    return j;
                                }());
            }
            vars.assign(syms.begin(), syms.end());
        }
        const SystemSolveResult r = solve_system(eqs, vars);
        const char* status =
            r.status == SystemSolveResult::Status::Solved            ? "solved"
            : r.status == SystemSolveResult::Status::NoSolution      ? "noSolution"
            : r.status == SystemSolveResult::Status::Underdetermined ? "underdetermined"
                                                                     : "unsolved";
        std::string values = "[";
        bool first = true;
        for (const std::string& v : vars) {
            const auto it = r.values.find(v);
            if (it == r.values.end()) continue;
            if (!first) values += ",";
            first = false;
            values += std::format("{{\"symbol\":{},{}}}", jstr(v), rendered_fields(it->second));
        }
        values += "]";
        return std::format(
            "{{\"ok\":true,\"status\":{},\"values\":{},\"free\":{},\"method\":{},"
            "\"warnings\":{}}}",
            jstr(status), values, jarr_str(r.free_variables), jstr(r.method),
            jarr_str(r.warnings));
    });
}

std::string ms_evaluate(std::string input, std::string bindings_str) {
    return guarded([&]() -> std::string {
        Bindings b;
        for (const auto& part : split_csv(bindings_str)) {
            const std::size_t eq = part.find('=');
            if (eq == std::string::npos) {
                return err_json(std::format("malformed binding '{}': expected name=value", part));
            }
            const std::string name = trim(part.substr(0, eq));
            const std::string value = trim(part.substr(eq + 1));
            try {
                b[name] = std::stod(value);
            } catch (const std::exception&) {
                return err_json(std::format("malformed binding '{}': '{}' is not a number",
                                            part, value));
            }
        }
        const double v = evaluate(parse_expression(input), b);
        return std::format("{{\"ok\":true,\"value\":{}}}", jnum(v));
    });
}

/// Sample the expression on a uniform grid for plotting. Domain errors and
/// non-finite values become null (the plot breaks the line there).
std::string ms_sample(std::string input, std::string var, double lo, double hi, int n) {
    return guarded([&]() -> std::string {
        if (!(std::isfinite(lo) && std::isfinite(hi)) || !(lo < hi)) {
            return err_json("sample bounds must be finite with lo < hi");
        }
        n = std::max(2, std::min(n, 4096));
        const Expr e = parse_expression(input);
        std::string ys = "[";
        for (int i = 0; i < n; ++i) {
            if (i > 0) ys += ",";
            const double x = lo + (hi - lo) * i / (n - 1);
            try {
                ys += jnum(evaluate(e, Bindings{{var, x}}));
            } catch (const Error&) {
                ys += "null";
            }
        }
        ys += "]";
        return std::format("{{\"ok\":true,\"ys\":{}}}", ys);
    });
}

// ---------------------------------------------------------------------------
// Plugins (mathsolver/plugin.hpp, docs/PLUGINS.md).
// ---------------------------------------------------------------------------

/// Catalog of the compiled-in plugins and their commands.
std::string ms_plugins() {
    return guarded([&]() -> std::string {
        plugins::register_builtin_plugins();
        std::string out = "{\"ok\":true,\"plugins\":[";
        bool first_plugin = true;
        for (const auto& p : plugins::registry()) {
            if (!first_plugin) out += ",";
            first_plugin = false;
            out += std::format("{{\"name\":{},\"version\":{},\"summary\":{},\"commands\":[",
                               jstr(p->name()), jstr(p->version()), jstr(p->summary()));
            bool first_cmd = true;
            for (const auto& c : p->commands()) {
                if (!first_cmd) out += ",";
                first_cmd = false;
                out += std::format("{{\"name\":{},\"summary\":{},\"usage\":{}}}",
                                   jstr(c.name), jstr(c.summary), jstr(c.usage));
            }
            out += "]}";
        }
        return out + "]}";
    });
}

/// Invoke `plugin.command` with comma-separated arguments (same top-level
/// splitting convention as the console grammar). The plugin's JSON result is
/// passed through verbatim.
std::string ms_plugin_call(std::string plugin, std::string command, std::string args_csv) {
    return guarded([&]() -> std::string {
        plugins::register_builtin_plugins();
        const plugins::Plugin* p = plugins::find(plugin);
        if (p == nullptr) {
            return err_json(std::format("no plugin named '{}'", plugin));
        }
        return p->invoke(command, split_csv(args_csv));
    });
}

} // namespace

EMSCRIPTEN_BINDINGS(mathsolver) {
    emscripten::function("version", &ms_version);
    emscripten::function("analyze", &ms_analyze);
    emscripten::function("simplify", &ms_simplify);
    emscripten::function("expand", &ms_expand);
    emscripten::function("factor", &ms_factor);
    emscripten::function("latex", &ms_latex);
    emscripten::function("subs", &ms_subs);
    emscripten::function("collect", &ms_collect);
    emscripten::function("derivative", &ms_derivative);
    emscripten::function("apart", &ms_apart);
    emscripten::function("dsolve", &ms_dsolve);
    emscripten::function("series", &ms_series);
    emscripten::function("vectorOp", &ms_vector_op);
    emscripten::function("sampleField", &ms_sample_field);
    emscripten::function("laplace", &ms_laplace);
    emscripten::function("ilaplace", &ms_ilaplace);
    emscripten::function("integrate", &ms_integrate);
    emscripten::function("integrateDefinite", &ms_integrate_definite);
    emscripten::function("solve", &ms_solve);
    emscripten::function("solveSystem", &ms_solve_system);
    emscripten::function("evaluate", &ms_evaluate);
    emscripten::function("sample", &ms_sample);
    emscripten::function("plugins", &ms_plugins);
    emscripten::function("pluginCall", &ms_plugin_call);
}
