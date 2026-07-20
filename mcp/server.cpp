// MathSolver MCP server (server.hpp): JSON-RPC 2.0 dispatch + tool handlers.

#include "server.hpp"

#include <cctype>
#include <cmath>
#include <format>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "json.hpp"
#include "mathsolver/mathsolver.hpp"
#include "mathsolver/plugin.hpp"

namespace mathsolver::mcp {

namespace {

using namespace mathsolver;

constexpr std::string_view k_server_name = "mathsolver";
constexpr std::string_view k_protocol_version = "2024-11-05";

// --- JSON-RPC envelope helpers ---------------------------------------------

Json rpc_result(const Json& id, Json result) {
    Json out;
    out.set("jsonrpc", "2.0").set("id", id).set("result", std::move(result));
    return out;
}

Json rpc_error(const Json& id, int code, std::string message) {
    Json err;
    err.set("code", code).set("message", std::move(message));
    Json out;
    out.set("jsonrpc", "2.0").set("id", id).set("error", std::move(err));
    return out;
}

// --- tool result ------------------------------------------------------------

struct ToolResult {
    std::string text;
    bool is_error = false;
};

ToolResult ok_text(std::string text) { return {std::move(text), false}; }
ToolResult err_text(std::string text) { return {std::move(text), true}; }

/// An MCP tools/call result: a single text content block plus isError.
Json tool_result_json(const ToolResult& r) {
    Json block;
    block.set("type", "text").set("text", r.text);
    Json content{JsonArray{block}};
    Json out;
    out.set("content", std::move(content)).set("isError", r.is_error);
    return out;
}

// --- shared engine helpers --------------------------------------------------

/// plain + LaTeX of an expression, formatted for a text content block.
std::string render_expr(const Expr& e) {
    return std::format("{}\n\nLaTeX: {}", to_string(e, PrintStyle::Plain),
                       to_string(e, PrintStyle::LaTeX));
}

/// Infer the single free variable, or throw a helpful Error.
std::string infer_variable(const Expr& e, std::string_view what) {
    const std::set<std::string> syms = free_symbols(e);
    if (syms.size() == 1) {
        return *syms.begin();
    }
    if (syms.empty()) {
        throw Error(std::format(
            "cannot infer the {} variable: the expression has no free "
            "symbols; pass \"variable\" explicitly",
            what));
    }
    std::string list;
    for (const auto& s : syms) {
        if (!list.empty()) list += ", ";
        list += s;
    }
    throw Error(std::format(
        "cannot infer the {} variable: candidates are {}; pass \"variable\" "
        "explicitly",
        what, list));
}

/// A required string argument.
std::string require_string(const Json& args, std::string_view key) {
    const Json& v = args[key];
    if (!v.is_string() || v.as_string().empty()) {
        throw Error(std::format("missing required string argument \"{}\"", key));
    }
    return v.as_string();
}

/// An optional string argument ("" when absent).
std::string opt_string(const Json& args, std::string_view key) {
    const Json& v = args[key];
    return v.is_string() ? v.as_string() : std::string{};
}

Equation equation_from(const std::string& src) {
    const auto parsed = parse_input(src);
    if (std::holds_alternative<Equation>(parsed)) {
        return std::get<Equation>(parsed);
    }
    return Equation{std::get<Expr>(parsed), make_num(0)};
}

// --- tool handlers ----------------------------------------------------------

ToolResult tool_simplify(const Json& a) {
    return ok_text(render_expr(simplify(parse_expression(require_string(a, "expression")))));
}
ToolResult tool_expand(const Json& a) {
    return ok_text(render_expr(expand(parse_expression(require_string(a, "expression")))));
}
ToolResult tool_factor(const Json& a) {
    return ok_text(render_expr(factor(parse_expression(require_string(a, "expression")))));
}
ToolResult tool_to_latex(const Json& a) {
    const Expr e = parse_expression(require_string(a, "expression"));
    return ok_text(to_string(e, PrintStyle::LaTeX));
}

ToolResult tool_differentiate(const Json& a) {
    const Expr e = parse_expression(require_string(a, "expression"));
    std::string var = opt_string(a, "variable");
    if (var.empty()) {
        var = infer_variable(e, "differentiation");
    }
    return ok_text(render_expr(differentiate(e, var)));
}

ToolResult tool_integrate(const Json& a) {
    const Expr e = parse_expression(require_string(a, "expression"));
    std::string var = opt_string(a, "variable");
    if (var.empty()) {
        var = infer_variable(e, "integration");
    }
    const std::string from = opt_string(a, "from");
    const std::string to = opt_string(a, "to");
    if (from.empty() != to.empty()) {
        return err_text("\"from\" and \"to\" must be given together for a "
                        "definite integral");
    }
    if (from.empty()) {
        const IntegrateResult r = integrate(e, var);
        if (r.status != IntegrateResult::Status::Integrated) {
            std::string msg = "unable to integrate: no applicable rule";
            if (!r.warnings.empty()) msg += " (" + r.warnings.front() + ")";
            return err_text(msg);
        }
        return ok_text(std::format("{} + C\n\nmethod: {}",
                                   to_string(r.antiderivative, PrintStyle::Plain),
                                   r.method));
    }
    const DefiniteIntegralResult r = integrate_definite(
        e, var, parse_expression(from), parse_expression(to));
    switch (r.status) {
        case DefiniteIntegralResult::Status::Exact:
            return ok_text(std::format("{}\n\nmethod: {}",
                                       to_string(r.value, PrintStyle::Plain),
                                       r.method));
        case DefiniteIntegralResult::Status::Numeric:
            return ok_text(std::format("{}\n\nmethod: {}",
                                       to_string(r.value, PrintStyle::Plain),
                                       r.method));
        case DefiniteIntegralResult::Status::Unsolved: {
            std::string msg = "unable to evaluate the definite integral";
            if (!r.warnings.empty()) msg += " (" + r.warnings.front() + ")";
            return err_text(msg);
        }
    }
    return err_text("unable to integrate");
}

ToolResult tool_solve(const Json& a) {
    const std::string input = require_string(a, "equation");
    const Equation eq = equation_from(input);
    std::string var = opt_string(a, "variable");
    if (var.empty()) {
        std::set<std::string> syms = free_symbols(eq.lhs);
        syms.merge(free_symbols(eq.rhs));
        if (syms.size() != 1) {
            return err_text(
                "cannot infer the variable to solve for; pass \"variable\"");
        }
        var = *syms.begin();
    }
    const SolveResult r = solve(eq, var);
    std::string out;
    switch (r.status) {
        case SolveResult::Status::Solved:
        case SolveResult::Status::SolvedComplex:
        case SolveResult::Status::NumericOnly:
            for (const Solution& s : r.solutions) {
                out += std::format("{} = {}", var,
                                   to_string(s.value, PrintStyle::Plain));
                if (!s.exact) out += "  (numeric)";
                if (!s.note.empty()) out += "  [" + s.note + "]";
                out += "\n";
            }
            out += "method: " + r.method;
            return ok_text(out);
        case SolveResult::Status::NoRealSolution:
            return ok_text("no real solution");
        case SolveResult::Status::AllReals:
            return ok_text("true for all " + var + " (identity)");
        case SolveResult::Status::Unsolved: {
            std::string msg = "unable to solve";
            if (!r.warnings.empty()) msg += " (" + r.warnings.front() + ")";
            return err_text(msg);
        }
    }
    return err_text("unable to solve");
}

ToolResult tool_evaluate(const Json& a) {
    const Expr e = parse_expression(require_string(a, "expression"));
    Bindings bindings;
    const Json& b = a["bindings"];
    if (b.is_object()) {
        for (const auto& [name, value] : b.as_object()) {
            if (!value.is_number()) {
                return err_text(std::format(
                    "binding \"{}\" must be a number", name));
            }
            bindings[name] = value.as_number();
        }
    } else if (b.is_string() && !b.as_string().empty()) {
        // Also accept "x=1, y=2.5".
        std::string_view s = b.as_string();
        std::size_t start = 0;
        for (std::size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || s[i] == ',') {
                const std::string_view part = s.substr(start, i - start);
                const auto eq = part.find('=');
                if (eq != std::string_view::npos) {
                    std::string name(part.substr(0, eq));
                    // trim
                    while (!name.empty() && std::isspace((unsigned char)name.front())) name.erase(name.begin());
                    while (!name.empty() && std::isspace((unsigned char)name.back())) name.pop_back();
                    try {
                        bindings[name] =
                            std::stod(std::string(part.substr(eq + 1)));
                    } catch (const std::exception&) {
                        return err_text(std::format(
                            "malformed binding \"{}\"", part));
                    }
                }
                start = i + 1;
            }
        }
    }
    const double v = evaluate(e, bindings);
    if (!std::isfinite(v)) {
        return err_text("the expression did not evaluate to a finite number "
                        "(check for undefined values or missing bindings)");
    }
    return ok_text(std::format("{}", v));
}

ToolResult tool_series(const Json& a) {
    const Expr e = parse_expression(require_string(a, "expression"));
    std::string var = opt_string(a, "variable");
    if (var.empty()) {
        var = infer_variable(e, "series");
    }
    const int order = a.contains("order")
                          ? static_cast<int>(a["order"].as_number(6))
                          : 6;
    const std::string center = opt_string(a, "center");
    if (center == "inf" || center == "oo" || center == "infinity") {
        return ok_text(render_expr(series_at_infinity(e, var, order)));
    }
    const Expr c = center.empty() ? make_num(0) : parse_expression(center);
    return ok_text(render_expr(series(e, var, c, order)));
}

ToolResult tool_limit(const Json& a) {
    const Expr e = parse_expression(require_string(a, "expression"));
    const std::string var = require_string(a, "variable");
    const std::string point = require_string(a, "point");
    const std::string dir = opt_string(a, "direction");
    const int direction = dir == "left" ? -1 : dir == "right" ? 1 : 0;

    LimitResult r;
    if (point == "inf" || point == "oo" || point == "+inf") {
        r = limit_at_infinity(e, var, true);
    } else if (point == "-inf" || point == "-oo") {
        r = limit_at_infinity(e, var, false);
    } else {
        r = limit(e, var, parse_expression(point), direction);
    }
    switch (r.status) {
        case LimitResult::Status::Exact:
            return ok_text(std::format(
                "limit = {}\nmethod: {}",
                to_string(r.value, PrintStyle::Plain), r.method));
        case LimitResult::Status::Numeric:
            return ok_text(std::format(
                "limit ~= {}\nmethod: {}",
                to_string(r.value, PrintStyle::Plain), r.method));
        case LimitResult::Status::Diverges:
            return ok_text(std::format(
                "the limit diverges ({})",
                r.sign > 0 ? "+infinity" : r.sign < 0 ? "-infinity"
                                                      : "unsigned"));
        case LimitResult::Status::DoesNotExist: {
            std::string msg = "the limit does not exist";
            for (const std::string& w : r.warnings) msg += "\n  " + w;
            return ok_text(msg);
        }
        case LimitResult::Status::Unsolved:
            return err_text("unable to determine the limit");
    }
    return err_text("unable to determine the limit");
}

ToolResult tool_laplace(const Json& a) {
    const Expr e = parse_expression(require_string(a, "expression"));
    const std::string var = opt_string(a, "variable");
    return ok_text(render_expr(var.empty() ? laplace(e) : laplace(e, var)));
}
ToolResult tool_inverse_laplace(const Json& a) {
    const Expr e = parse_expression(require_string(a, "expression"));
    const std::string var = opt_string(a, "variable");
    return ok_text(render_expr(var.empty() ? inverse_laplace(e)
                                           : inverse_laplace(e, var)));
}

/// Render a plugin's JSON block envelope (see docs/PLUGINS.md) as plain text
/// an LLM can read: kv pairs and tables become lines; charts are summarized.
std::string render_plugin_blocks(const Json& env) {
    std::string out;
    if (env.contains("title")) {
        out += env["title"].as_string() + "\n";
    }
    for (const Json& block : env["blocks"].as_array()) {
        const std::string type = block["type"].as_string();
        if (type == "kv") {
            for (const Json& item : block["items"].as_array()) {
                const JsonArray& pair = item.as_array();
                if (pair.size() == 2) {
                    out += std::format("  {}: {}\n", pair[0].as_string(),
                                       pair[1].as_string());
                }
            }
        } else if (type == "table") {
            if (block.contains("title")) {
                out += block["title"].as_string() + ":\n";
            }
            std::string header;
            for (const Json& c : block["columns"].as_array()) {
                if (!header.empty()) header += " | ";
                header += c.as_string();
            }
            out += "  " + header + "\n";
            const JsonArray& rows = block["rows"].as_array();
            for (std::size_t i = 0; i < rows.size() && i < 20; ++i) {
                std::string line;
                for (const Json& cell : rows[i].as_array()) {
                    if (!line.empty()) line += " | ";
                    line += cell.is_string()
                                ? cell.as_string()
                                : std::format("{}", cell.as_number());
                }
                out += "  " + line + "\n";
            }
            if (rows.size() > 20) {
                out += std::format("  ... ({} rows total)\n", rows.size());
            }
        } else if (type == "series") {
            const std::size_t n = block["series"].as_array().size();
            const std::size_t pts = block["x"].as_array().size();
            out += std::format("  [chart: {} — {} series over {} points]\n",
                               block["title"].as_string(), n, pts);
        } else if (type == "text") {
            for (const Json& line : block["lines"].as_array()) {
                out += "  " + line.as_string() + "\n";
            }
        }
    }
    return out;
}

ToolResult tool_list_plugins(const Json&) {
    plugins::register_builtin_plugins();
    std::string out = "Available plugins (call with plugin_command):\n";
    for (const auto& plugin : plugins::registry()) {
        out += std::format("\n{} {} — {}\n", plugin->name(),
                           plugin->version(), plugin->summary());
        for (const auto& cmd : plugin->commands()) {
            out += std::format("  {}.{}: {}\n    example: {}\n", plugin->name(),
                               cmd.name, cmd.summary, cmd.example);
        }
    }
    return ok_text(out);
}

ToolResult tool_plugin_command(const Json& a) {
    plugins::register_builtin_plugins();
    const std::string name = require_string(a, "plugin");
    const std::string command = require_string(a, "command");
    const plugins::Plugin* plugin = plugins::find(name);
    if (plugin == nullptr) {
        return err_text(std::format(
            "no plugin named \"{}\" (call list_plugins to see the catalog)",
            name));
    }
    std::vector<std::string> args;
    const Json& av = a["args"];
    if (av.is_array()) {
        for (const Json& item : av.as_array()) {
            args.push_back(item.is_string()
                               ? item.as_string()
                               : (item.is_number()
                                      ? std::format("{}", item.as_number())
                                      : item.dump()));
        }
    } else if (av.is_string() && !av.as_string().empty()) {
        // A single comma-joined string, split at top-level commas (matrix
        // literals with nested commas stay intact).
        std::string_view s = av.as_string();
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i <= s.size(); ++i) {
            if (i < s.size() && (s[i] == '(' || s[i] == '[' || s[i] == '{')) ++depth;
            if (i < s.size() && (s[i] == ')' || s[i] == ']' || s[i] == '}')) --depth;
            if (i == s.size() || (s[i] == ',' && depth <= 0)) {
                std::string item(s.substr(start, i - start));
                while (!item.empty() && std::isspace((unsigned char)item.front())) item.erase(item.begin());
                while (!item.empty() && std::isspace((unsigned char)item.back())) item.pop_back();
                if (!item.empty()) args.push_back(item);
                start = i + 1;
            }
        }
    }
    const std::string json = plugin->invoke(command, args);
    std::string parse_err;
    const auto env = Json::parse(json, parse_err);
    if (!env) {
        return ok_text(json); // fall back to the raw envelope
    }
    if (!(*env)["ok"].as_bool()) {
        return err_text((*env)["error"].as_string("plugin error"));
    }
    return ok_text(render_plugin_blocks(*env));
}

// --- tool registry ----------------------------------------------------------

struct Tool {
    std::string name;
    std::string description;
    Json input_schema;
    std::function<ToolResult(const Json&)> handler;
};

/// A JSON Schema object with the given string/number properties.
Json schema(std::vector<std::pair<std::string, std::string>> string_props,
            std::vector<std::string> required,
            std::vector<std::pair<std::string, std::string>> extra = {}) {
    Json props;
    for (const auto& [name, desc] : string_props) {
        Json p;
        p.set("type", "string").set("description", desc);
        props.set(name, std::move(p));
    }
    for (const auto& [name, desc] : extra) {
        Json p;
        // "number:" and "object:" prefixes select the JSON type.
        std::string type = "string";
        std::string d = desc;
        if (desc.rfind("number:", 0) == 0) { type = "number"; d = desc.substr(7); }
        else if (desc.rfind("object:", 0) == 0) { type = "object"; d = desc.substr(7); }
        else if (desc.rfind("array:", 0) == 0) { type = "array"; d = desc.substr(6); }
        p.set("type", type).set("description", d);
        props.set(name, std::move(p));
    }
    JsonArray reqarr;
    for (const auto& r : required) reqarr.emplace_back(Json(r));
    Json out;
    out.set("type", "object").set("properties", std::move(props))
       .set("required", Json(std::move(reqarr)));
    return out;
}

const std::vector<Tool>& tools() {
    static const std::vector<Tool> registry = [] {
        std::vector<Tool> t;
        t.push_back({"simplify",
                     "Simplify a mathematical expression to a canonical form. "
                     "Accepts LaTeX-style or plain ASCII (e.g. 'sin(x)^2 + "
                     "cos(x)^2', '\\frac{1}{2} + 2x'). Returns the simplified "
                     "expression in plain text and LaTeX.",
                     schema({{"expression", "The expression to simplify."}},
                            {"expression"}),
                     tool_simplify});
        t.push_back({"expand",
                     "Expand products and powers in an expression (e.g. "
                     "(x+1)^3 -> x^3 + 3x^2 + 3x + 1).",
                     schema({{"expression", "The expression to expand."}},
                            {"expression"}),
                     tool_expand});
        t.push_back({"factor",
                     "Factor a polynomial expression over the rationals where "
                     "possible (e.g. x^2 - 5x + 6 -> (x-3)(x-2)).",
                     schema({{"expression", "The expression to factor."}},
                            {"expression"}),
                     tool_factor});
        t.push_back({"to_latex",
                     "Render an expression as LaTeX without simplifying it.",
                     schema({{"expression", "The expression to render."}},
                            {"expression"}),
                     tool_to_latex});
        t.push_back({"differentiate",
                     "Differentiate an expression symbolically. If 'variable' "
                     "is omitted and the expression has one free symbol, that "
                     "symbol is used.",
                     schema({{"expression", "The expression to differentiate."},
                             {"variable", "The variable to differentiate with "
                                          "respect to (optional)."}},
                            {"expression"}),
                     tool_differentiate});
        t.push_back(
            {"integrate",
             "Integrate an expression symbolically. Omit 'from'/'to' for an "
             "indefinite integral (antiderivative); pass both for a definite "
             "integral. Unsolvable integrals are reported honestly rather "
             "than guessed.",
             schema({{"expression", "The integrand."},
                     {"variable", "The integration variable (optional; "
                                  "inferred if there is one free symbol)."},
                     {"from", "Lower bound for a definite integral (optional)."},
                     {"to", "Upper bound for a definite integral (optional)."}},
                    {"expression"}),
             tool_integrate});
        t.push_back(
            {"solve",
             "Solve an equation for a variable. Accepts 'lhs = rhs' or a bare "
             "expression (treated as = 0). Returns exact solutions where "
             "possible, otherwise numeric roots.",
             schema({{"equation", "The equation to solve, e.g. 'x^2 = 4'."},
                     {"variable", "The variable to solve for (optional if "
                                  "there is one free symbol)."}},
                    {"equation"}),
             tool_solve});
        t.push_back(
            {"evaluate",
             "Numerically evaluate an expression, optionally with variable "
             "bindings.",
             schema({{"expression", "The expression to evaluate."}},
                    {"expression"},
                    {{"bindings", "object:Variable values as a JSON object, "
                                  "e.g. {\"x\": 3, \"y\": 0.5}."}}),
             tool_evaluate});
        t.push_back(
            {"series",
             "Taylor-series expansion of an expression about a center (default "
             "0) to an order (default 6). Use center 'inf' for an asymptotic "
             "expansion at infinity.",
             schema({{"expression", "The expression to expand."},
                     {"variable", "The expansion variable (optional)."},
                     {"center", "The expansion center (default 0; 'inf' for "
                                "expansion at infinity)."}},
                    {"expression"},
                    {{"order", "number:The highest order term to keep "
                               "(default 6)."}}),
             tool_series});
        t.push_back(
            {"limit",
             "Compute the limit of an expression as a variable approaches a "
             "point. The point may be a number or 'inf'/'-inf'. Direction may "
             "be 'left' or 'right' for one-sided limits.",
             schema({{"expression", "The expression."},
                     {"variable", "The variable."},
                     {"point", "The point approached (number, 'inf', or "
                               "'-inf')."},
                     {"direction", "'left', 'right', or omit for two-sided."}},
                    {"expression", "variable", "point"}),
             tool_limit});
        t.push_back(
            {"laplace",
             "Laplace transform of a time-domain expression f(t) -> F(s).",
             schema({{"expression", "The time-domain expression."},
                     {"variable", "The time variable (default t)."}},
                    {"expression"}),
             tool_laplace});
        t.push_back(
            {"inverse_laplace",
             "Inverse Laplace transform F(s) -> f(t).",
             schema({{"expression", "The frequency-domain expression."},
                     {"variable", "The frequency variable (default s)."}},
                    {"expression"}),
             tool_inverse_laplace});
        t.push_back(
            {"list_plugins",
             "List the compiled-in computation plugins (DSP filter design, "
             "control systems, linear algebra, PDEs, integral equations, "
             "hybrid systems, finite elements) and their commands.",
             schema({}, {}),
             tool_list_plugins});
        t.push_back(
            {"plugin_command",
             "Run a plugin command. Call list_plugins first to see plugins, "
             "their commands, and example arguments. Arguments are the "
             "command's comma-separated positional arguments.",
             schema({{"plugin", "The plugin name, e.g. 'dsp' or 'linalg'."},
                     {"command", "The command name, e.g. 'butter' or 'eig'."}},
                    {"plugin", "command"},
                    {{"args", "array:The positional arguments as an array of "
                              "strings (or one comma-joined string)."}}),
             tool_plugin_command});
        return t;
    }();
    return registry;
}

Json tools_list_json() {
    JsonArray arr;
    for (const Tool& t : tools()) {
        Json j;
        j.set("name", t.name)
            .set("description", t.description)
            .set("inputSchema", t.input_schema);
        arr.emplace_back(std::move(j));
    }
    Json out;
    out.set("tools", Json(std::move(arr)));
    return out;
}

Json call_tool(const Json& params) {
    const std::string name = params["name"].as_string();
    const Json& args = params["arguments"];
    for (const Tool& t : tools()) {
        if (t.name == name) {
            ToolResult r;
            try {
                r = t.handler(args);
            } catch (const ParseError& e) {
                r = err_text(std::string("parse error: ") + e.what());
            } catch (const Error& e) {
                r = err_text(e.what());
            } catch (const std::exception& e) {
                r = err_text(std::string("internal error: ") + e.what());
            }
            return tool_result_json(r);
        }
    }
    // Unknown tool: surface as an error result (not a protocol error) so the
    // model sees it.
    return tool_result_json(err_text(
        std::format("unknown tool \"{}\"; call tools/list for the catalog",
                    name)));
}

Json initialize_result(const Json& params) {
    // Echo the client's protocol version when it sent one; otherwise use ours.
    std::string version(k_protocol_version);
    if (params["protocolVersion"].is_string()) {
        version = params["protocolVersion"].as_string();
    }
    Json caps;
    caps.set("tools", Json(JsonObject{}));
    Json info;
    info.set("name", std::string(k_server_name))
        .set("version", std::string(k_version));
    Json out;
    out.set("protocolVersion", version)
        .set("capabilities", std::move(caps))
        .set("serverInfo", std::move(info));
    return out;
}

} // namespace

std::optional<std::string> Server::handle(std::string_view request_line) {
    // Blank lines (keep-alives) produce no response.
    bool only_ws = true;
    for (const char c : request_line) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            only_ws = false;
            break;
        }
    }
    if (only_ws) {
        return std::nullopt;
    }

    std::string parse_err;
    const auto req = Json::parse(request_line, parse_err);
    if (!req || !req->is_object()) {
        // Parse error: JSON-RPC says respond with a null-id error.
        return rpc_error(Json(nullptr), -32700,
                         "Parse error: " + parse_err).dump();
    }

    const Json& id = (*req)["id"];
    const bool is_notification = !req->contains("id") || id.is_null();
    const std::string method = (*req)["method"].as_string();
    const Json& params = (*req)["params"];

    // Notifications (no id) get no response, whatever the method.
    if (is_notification) {
        return std::nullopt;
    }

    try {
        if (method == "initialize") {
            return rpc_result(id, initialize_result(params)).dump();
        }
        if (method == "tools/list") {
            return rpc_result(id, tools_list_json()).dump();
        }
        if (method == "tools/call") {
            return rpc_result(id, call_tool(params)).dump();
        }
        if (method == "ping") {
            return rpc_result(id, Json(JsonObject{})).dump();
        }
        return rpc_error(id, -32601, "Method not found: " + method).dump();
    } catch (const std::exception& e) {
        return rpc_error(id, -32603,
                         std::string("Internal error: ") + e.what()).dump();
    }
}

} // namespace mathsolver::mcp
