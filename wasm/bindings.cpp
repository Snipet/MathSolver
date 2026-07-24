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
    // Top-level commas only: commas nested in (), [], {} stay inside their
    // argument (matrix literals like "[1,2;3,4]" arrive intact).
    std::vector<std::string> out;
    std::size_t start = 0;
    int depth = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i < s.size() && (s[i] == '(' || s[i] == '[' || s[i] == '{')) ++depth;
        if (i < s.size() && (s[i] == ')' || s[i] == ']' || s[i] == '}')) --depth;
        if (i == s.size() || (s[i] == ',' && depth <= 0)) {
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
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(input);
        // A bare integer factors into primes (2^3 · 3^2 · 5) rather than
        // echoing itself, matching FactorInteger / factorint.
        const Expr s = simplify(e);
        if (s->kind() == Kind::Number && s->number().is_integer()) {
            const long long n = s->number().num();
            if (n == 0 || n == 1 || n == -1) {
                const std::string t = std::to_string(n);
                return std::format("{{\"ok\":true,\"plain\":{},\"latex\":{}}}",
                                   jstr(t), jstr(t));
            }
            const auto f = factorize(n);
            const std::string sign = n < 0 ? "-" : "";
            return std::format(
                "{{\"ok\":true,\"plain\":{},\"latex\":{}}}",
                jstr(sign + format_factorization(f, " * ")),
                jstr(sign + format_factorization_latex(f)));
        }
        return std::format("{{\"ok\":true,{}}}", rendered_fields(factor(e)));
    });
}

// ---- Number theory: gcd/lcm, primality, factorization, divisors, totient.
// Each returns a uniform {ok, plain, latex, notes} envelope the console renders
// like any transform result.

/// Parse a single number-theory argument to an integer, or nullopt.
std::optional<long long> nt_int(const std::string& tok) {
    const Expr e = simplify(parse_expression(tok));
    if (e->kind() != Kind::Number || !e->number().is_integer()) return std::nullopt;
    return e->number().num();
}

std::string nt_json(std::string_view plain, std::string_view latex,
                    const std::vector<std::string>& notes = {}) {
    return std::format("{{\"ok\":true,\"plain\":{},\"latex\":{},\"notes\":{}}}",
                       jstr(plain), jstr(latex), jarr_str(notes));
}

/// gcd/lcm of a comma-separated integer list, folded pairwise.
std::string ms_gcd(std::string list) {
    return guarded([&]() -> std::string {
        long long acc = 0;
        int count = 0;
        for (const std::string& t : split_csv(list)) {
            const auto v = nt_int(t);
            if (!v) return err_json(std::format("gcd: expected an integer, got '{}'", trim(t)));
            acc = int_gcd(acc, *v);
            ++count;
        }
        if (count == 0) return err_json("gcd: expected at least one integer");
        const std::string s = std::to_string(acc);
        return nt_json(s, s);
    });
}
std::string ms_lcm(std::string list) {
    return guarded([&]() -> std::string {
        long long acc = 1;
        int count = 0;
        for (const std::string& t : split_csv(list)) {
            const auto v = nt_int(t);
            if (!v) return err_json(std::format("lcm: expected an integer, got '{}'", trim(t)));
            acc = int_lcm(acc, *v);
            ++count;
        }
        if (count == 0) return err_json("lcm: expected at least one integer");
        const std::string s = std::to_string(acc);
        return nt_json(s, s);
    });
}
std::string ms_isprime(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("isprime: expected an integer, got '{}'", trim(arg)));
        const bool p = is_prime(*n);
        return nt_json(p ? "true" : "false", p ? "\\text{true}" : "\\text{false}",
                       {std::format("{} is {}", *n, p ? "prime" : "composite")});
    });
}
std::string ms_nextprime(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("nextprime: expected an integer, got '{}'", trim(arg)));
        const std::string s = std::to_string(next_prime(*n));
        return nt_json(s, s);
    });
}
std::string ms_totient(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("totient: expected an integer, got '{}'", trim(arg)));
        if (*n < 1) return err_json("totient is defined for positive integers");
        const std::string s = std::to_string(euler_totient(*n));
        return nt_json(s, s, {std::format("phi({})", *n)});
    });
}
std::string ms_sigma(std::string arg, std::string kArg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("sigma: expected an integer, got '{}'", trim(arg)));
        if (*n < 1) return err_json("sigma is defined for positive integers");
        int k = 1;
        if (!trim(kArg).empty()) {
            const auto kk = nt_int(kArg);
            if (!kk) return err_json(std::format("sigma: exponent must be an integer, got '{}'",
                                                 trim(kArg)));
            if (*kk < 0) return err_json("sigma exponent must be non-negative");
            k = static_cast<int>(*kk);
        }
        const std::string s = std::to_string(divisor_sigma(*n, k));
        return nt_json(s, s, {std::format("sigma_{}({})", k, *n)});
    });
}
std::string ms_mobius(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("mobius: expected an integer, got '{}'", trim(arg)));
        if (*n < 1) return err_json("mobius is defined for positive integers");
        const std::string s = std::to_string(mobius(*n));
        return nt_json(s, s, {std::format("mu({})", *n)});
    });
}
std::string ms_partitions(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("partitions: expected an integer, got '{}'", trim(arg)));
        if (*n < 0) return err_json("partitions is defined for n >= 0");
        const std::string s = std::to_string(partition_count(*n));
        return nt_json(s, s, {std::format("p({})", *n)});
    });
}
std::string ms_catalan(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("catalan: expected an integer, got '{}'", trim(arg)));
        if (*n < 0) return err_json("catalan is defined for n >= 0");
        const std::string s = std::to_string(catalan_number(*n));
        return nt_json(s, s, {std::format("C({})", *n)});
    });
}
std::string ms_stirling2(std::string nArg, std::string kArg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(nArg);
        const auto k = nt_int(kArg);
        if (!n) return err_json(std::format("stirling2: expected an integer n, got '{}'", trim(nArg)));
        if (!k) return err_json(std::format("stirling2: expected an integer k, got '{}'", trim(kArg)));
        if (*n < 0 || *k < 0) return err_json("stirling2 is defined for n, k >= 0");
        const std::string s = std::to_string(stirling_second(*n, *k));
        return nt_json(s, s, {std::format("S({}, {})", *n, *k)});
    });
}
std::string ms_bell(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("bell: expected an integer, got '{}'", trim(arg)));
        if (*n < 0) return err_json("bell is defined for n >= 0");
        const std::string s = std::to_string(bell_number(*n));
        return nt_json(s, s, {std::format("B({})", *n)});
    });
}
std::string ms_derangement(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("derangement: expected an integer, got '{}'", trim(arg)));
        if (*n < 0) return err_json("derangement is defined for n >= 0");
        const std::string s = std::to_string(derangement_count(*n));
        return nt_json(s, s, {std::format("!{}", *n)});
    });
}
std::string ms_bernoulli(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("bernoulli: expected an integer, got '{}'", trim(arg)));
        if (*n < 0 || *n > 20) return err_json("bernoulli: the index must be in [0, 20]");
        const Rational b = bernoulli_number(static_cast<int>(*n));
        // Render the exact rational (fractions like 1/6, -1/30) plain and LaTeX.
        return std::format("{{\"ok\":true,{},\"notes\":{}}}",
                           rendered_fields(make_num(b)), jarr_str({std::format("B_{}", *n)}));
    });
}
std::string ms_divisors(std::string arg) {
    return guarded([&]() -> std::string {
        const auto n = nt_int(arg);
        if (!n) return err_json(std::format("divisors: expected an integer, got '{}'", trim(arg)));
        if (*n == 0) return err_json("divisors of 0 is undefined");
        const std::vector<long long> ds = divisors(*n);
        std::string plain;
        std::string latex;
        for (std::size_t i = 0; i < ds.size(); ++i) {
            if (i > 0) {
                plain += ", ";
                latex += ", ";
            }
            plain += std::to_string(ds[i]);
            latex += std::to_string(ds[i]);
        }
        return nt_json(plain, "\\{" + latex + "\\}", {std::format("{} divisors", ds.size())});
    });
}

/// cfrac(value): continued fraction of a rational, sqrt(n), or real, with its
/// convergents. Returns {ok, plain, latex, notes} like a transform result.
std::string ms_cfrac(std::string input) {
    return guarded([&]() -> std::string {
        const Expr s = simplify(parse_expression(input));
        CFrac cf;
        if (s->kind() == Kind::Number) {
            const Rational r = s->number();
            cf = cf_rational(r.num(), r.den());
        } else if (s->kind() == Kind::Pow && s->arg(0)->kind() == Kind::Number &&
                   s->arg(0)->number().is_integer() && s->arg(0)->number().num() >= 1 &&
                   s->arg(1)->kind() == Kind::Number &&
                   s->arg(1)->number() == Rational(1, 2)) {
            cf = cf_sqrt(s->arg(0)->number().num());
        } else {
            cf = cf_numeric(evaluate(s, Bindings{}));
        }
        std::vector<std::string> notes;
        if (!cf.convergents.empty()) {
            std::string cs = "convergents: ";
            for (std::size_t i = 0; i < cf.convergents.size(); ++i) {
                const auto& [p, q] = cf.convergents[i];
                if (i > 0) cs += ", ";
                cs += q == 1 ? std::to_string(p)
                             : std::to_string(p) + "/" + std::to_string(q);
            }
            notes.push_back(cs);
        }
        if (!cf.exact) notes.push_back("numeric expansion — later terms are approximate");
        return std::format("{{\"ok\":true,\"plain\":{},\"latex\":{},\"notes\":{}}}",
                           jstr(format_cfrac(cf)), jstr(format_cfrac_latex(cf)),
                           jarr_str(notes));
    });
}

/// Parse a CSV of integers; nullopt if any token isn't an integer.
std::optional<std::vector<long long>> nt_int_list(const std::string& list) {
    std::vector<long long> out;
    for (const std::string& t : split_csv(list)) {
        const auto v = nt_int(t);
        if (!v) return std::nullopt;
        out.push_back(*v);
    }
    return out;
}

/// Modular arithmetic: mod(a,m), powmod(b,e,m), modinv(a,m). One CSV argument.
std::string ms_mod(std::string list) {
    return guarded([&]() -> std::string {
        const auto v = nt_int_list(list);
        if (!v || v->size() != 2) return err_json("usage: mod <a>, <m>");
        const std::string s = std::to_string(int_mod((*v)[0], (*v)[1]));
        return nt_json(s, s);
    });
}
std::string ms_powmod(std::string list) {
    return guarded([&]() -> std::string {
        const auto v = nt_int_list(list);
        if (!v || v->size() != 3) return err_json("usage: powmod <base>, <exponent>, <modulus>");
        const std::string s = std::to_string(pow_mod((*v)[0], (*v)[1], (*v)[2]));
        return nt_json(s, s, {std::format("{}^{} mod {}", (*v)[0], (*v)[1], (*v)[2])});
    });
}
std::string ms_modinv(std::string list) {
    return guarded([&]() -> std::string {
        const auto v = nt_int_list(list);
        if (!v || v->size() != 2) return err_json("usage: modinv <a>, <m>");
        const std::string s = std::to_string(mod_inverse((*v)[0], (*v)[1]));
        return nt_json(s, s, {std::format("{}^(-1) mod {}", (*v)[0], (*v)[1])});
    });
}

/// discriminant(poly, var): the discriminant of a degree 2–4 polynomial, with
/// the variable inferred when it's the only symbol.
std::string ms_discriminant(std::string poly, std::string var) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(poly);
        std::string v = trim(var);
        if (v.empty()) {
            const std::set<std::string> syms = free_symbols(e);
            if (syms.size() == 1) v = *syms.begin();
            else return err_json("discriminant: name the variable, e.g. "
                                 "discriminant a*x^2 + b*x + c, x");
        }
        const DiscriminantResult r = discriminant(e, v);
        if (r.status != DiscriminantResult::Status::Ok) return err_json(r.message);
        std::vector<std::string> notes;
        if (!r.root_nature.empty()) notes.push_back("roots: " + r.root_nature);
        return std::format("{{\"ok\":true,{},\"notes\":{}}}",
                           rendered_fields(r.value), jarr_str(notes));
    });
}

/// polydiv(dividend, divisor, var): polynomial long division. The quotient is
/// the rendered result; the remainder rides along as a note.
std::string ms_polydiv(std::string dividend, std::string divisor, std::string var) {
    return guarded([&]() -> std::string {
        const Expr n = parse_expression(dividend);
        const Expr d = parse_expression(divisor);
        std::string v = trim(var);
        if (v.empty()) {
            std::set<std::string> syms = free_symbols(n);
            for (const std::string& s : free_symbols(d)) syms.insert(s);
            if (syms.size() == 1) v = *syms.begin();
            else return err_json("polydiv: name the variable, e.g. "
                                 "polydiv x^3 - 1, x - 1, x");
        }
        const PolyDivResult r = polynomial_divide(n, d, v);
        if (r.status != PolyDivResult::Status::Ok) return err_json(r.message);
        const std::vector<std::string> notes = {
            "remainder: " + to_string(r.remainder, PrintStyle::Plain)};
        return std::format("{{\"ok\":true,{},\"notes\":{}}}",
                           rendered_fields(r.quotient), jarr_str(notes));
    });
}

/// Shared body for polygcd / polylcm: infer the variable when it is the only
/// symbol, then render the monic gcd or lcm.
std::string ms_poly_gcd_lcm(const std::string& a, const std::string& b,
                            const std::string& var, bool lcm) {
    return guarded([&]() -> std::string {
        const Expr ea = parse_expression(a);
        const Expr eb = parse_expression(b);
        std::string v = trim(var);
        if (v.empty()) {
            std::set<std::string> syms = free_symbols(ea);
            for (const std::string& s : free_symbols(eb)) syms.insert(s);
            if (syms.size() == 1) v = *syms.begin();
            else return err_json(std::format(
                "{}: name the variable, e.g. {} x^2 - 1, x^3 - 1, x",
                lcm ? "polylcm" : "polygcd", lcm ? "polylcm" : "polygcd"));
        }
        const PolyGcdResult r = lcm ? polynomial_lcm(ea, eb, v) : polynomial_gcd(ea, eb, v);
        if (r.status != PolyGcdResult::Status::Ok) return err_json(r.message);
        return std::format("{{\"ok\":true,{}}}", rendered_fields(r.value));
    });
}
std::string ms_polygcd(std::string a, std::string b, std::string var) {
    return ms_poly_gcd_lcm(a, b, var, false);
}
std::string ms_polylcm(std::string a, std::string b, std::string var) {
    return ms_poly_gcd_lcm(a, b, var, true);
}

/// resultant(a, b, var): the resultant of two polynomials.
std::string ms_resultant(std::string a, std::string b, std::string var) {
    return guarded([&]() -> std::string {
        const Expr ea = parse_expression(a);
        const Expr eb = parse_expression(b);
        std::string v = trim(var);
        if (v.empty()) {
            std::set<std::string> syms = free_symbols(ea);
            for (const std::string& s : free_symbols(eb)) syms.insert(s);
            if (syms.size() == 1) v = *syms.begin();
            else return err_json("resultant: name the variable, e.g. "
                                 "resultant x^2 - 1, x - 2, x");
        }
        const PolyGcdResult r = polynomial_resultant(ea, eb, v);
        if (r.status != PolyGcdResult::Status::Ok) return err_json(r.message);
        return std::format("{{\"ok\":true,{}}}", rendered_fields(r.value));
    });
}

/// bezout(a, b, var): extended gcd. The monic gcd is the rendered result; the
/// Bézout cofactors s, t (with s·a + t·b = gcd) ride along as notes.
std::string ms_bezout(std::string a, std::string b, std::string var) {
    return guarded([&]() -> std::string {
        const Expr ea = parse_expression(a);
        const Expr eb = parse_expression(b);
        std::string v = trim(var);
        if (v.empty()) {
            std::set<std::string> syms = free_symbols(ea);
            for (const std::string& s : free_symbols(eb)) syms.insert(s);
            if (syms.size() == 1) v = *syms.begin();
            else return err_json("bezout: name the variable, e.g. "
                                 "bezout x^2 - 1, x^3 - 1, x");
        }
        const PolyBezoutResult r = polynomial_bezout(ea, eb, v);
        if (r.status != PolyBezoutResult::Status::Ok) return err_json(r.message);
        const std::vector<std::string> notes = {
            "s = " + to_string(r.s, PrintStyle::Plain),
            "t = " + to_string(r.t, PrintStyle::Plain)};
        return std::format("{{\"ok\":true,{},\"notes\":{}}}",
                           rendered_fields(r.gcd), jarr_str(notes));
    });
}

/// vandermonde(nodes): the square Vandermonde matrix of a comma-separated node
/// list (row i = 1, x_i, x_i², …). Returns it rendered plain and LaTeX.
std::string ms_vandermonde(std::string nodes) {
    return guarded([&]() -> std::string {
        // Split on top-level commas/semicolons (respecting brackets) so a node
        // may be any scalar expression.
        std::vector<Expr> xs;
        std::string cur;
        int depth = 0;
        auto flush = [&]() {
            const std::string t = trim(cur);
            if (!t.empty()) xs.push_back(parse_expression(t));
            cur.clear();
        };
        for (char ch : nodes) {
            if (ch == '(' || ch == '[' || ch == '{') depth++;
            else if (ch == ')' || ch == ']' || ch == '}') depth = depth > 0 ? depth - 1 : 0;
            if ((ch == ',' || ch == ';') && depth == 0) flush();
            else cur.push_back(ch);
        }
        flush();
        const VandermondeResult r = vandermonde_matrix(xs);
        if (r.status != VandermondeResult::Status::Ok) return err_json(r.message);
        const int n = static_cast<int>(r.matrix.size());
        const std::vector<std::string> notes = {
            std::format("{}×{} Vandermonde matrix; det = ∏(x_j − x_i), the "
                        "interpolation system's coefficient matrix",
                        n, n)};
        return std::format("{{\"ok\":true,\"plain\":{},\"latex\":{},\"notes\":{}}}",
                           jstr(mat_to_string(r.matrix, PrintStyle::Plain)),
                           jstr(mat_to_string(r.matrix, PrintStyle::LaTeX)),
                           jarr_str(notes));
    });
}

/// companion(input, variable): the companion matrix of a univariate polynomial
/// (MATLAB `compan` orientation), whose eigenvalues are the polynomial's roots.
/// Returns the matrix rendered plain ("[a, b; c, d]") and LaTeX (pmatrix).
std::string ms_companion(std::string input, std::string variable) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(input);
        std::string v = trim(variable);
        if (v.empty()) {
            const std::set<std::string> syms = free_symbols(e);
            if (syms.size() == 1) v = *syms.begin();
            else return err_json("companion: name the variable, e.g. "
                                 "companion x^2 - 3x + 2, x");
        }
        const CompanionResult r = companion_matrix(e, v);
        if (r.status != CompanionResult::Status::Ok) return err_json(r.message);
        const int n = static_cast<int>(r.matrix.size());
        const std::vector<std::string> notes = {
            std::format("{}×{} companion matrix; its eigenvalues are the roots "
                        "of the polynomial",
                        n, n)};
        return std::format("{{\"ok\":true,\"plain\":{},\"latex\":{},\"notes\":{}}}",
                           jstr(mat_to_string(r.matrix, PrintStyle::Plain)),
                           jstr(mat_to_string(r.matrix, PrintStyle::LaTeX)),
                           jarr_str(notes));
    });
}

/// solveIneq(lhs, rhs, op, var): solve the inequality `lhs <op> rhs` for its
/// variable (op is one of "<", "<=", ">", ">="; var may be empty to infer).
std::string ms_solve_ineq(std::string lhs, std::string rhs, std::string op,
                          std::string var) {
    return guarded([&]() -> std::string {
        IneqOp o;
        if (op == "<") o = IneqOp::Lt;
        else if (op == "<=") o = IneqOp::Le;
        else if (op == ">") o = IneqOp::Gt;
        else if (op == ">=") o = IneqOp::Ge;
        else return err_json("unknown relational operator '" + op + "'");
        const IneqResult r = solve_inequality(parse_expression(lhs),
                                              parse_expression(rhs), o, trim(var));
        if (r.status == IneqResult::Status::Unsolved) {
            return err_json(r.message.empty() ? "unable to solve inequality"
                                              : r.message);
        }
        return std::format("{{\"ok\":true,\"plain\":{},\"latex\":{},\"notes\":{}}}",
                           jstr(format_solution_set(r, false)),
                           jstr(format_solution_set(r, true)), jarr_str(r.warnings));
    });
}

/// crt(system): "r1, r2, …; m1, m2, …" — residues before ';', moduli after.
std::string ms_crt(std::string system) {
    return guarded([&]() -> std::string {
        const auto semi = system.find(';');
        if (semi == std::string::npos) {
            return err_json("usage: crt <r1, r2, …; m1, m2, …>  (residues ; moduli)");
        }
        const auto residues = nt_int_list(system.substr(0, semi));
        const auto moduli = nt_int_list(system.substr(semi + 1));
        if (!residues || !moduli) return err_json("crt: residues and moduli must be integers");
        const Crt r = crt_solve(*residues, *moduli);
        const std::string plain = std::format("{} (mod {})", r.residue, r.modulus);
        const std::string latex = std::format("{} \\pmod{{{}}}", r.residue, r.modulus);
        return nt_json(plain, latex);
    });
}
std::string ms_trigexpand(std::string input) {
    return transform_json(input, [](const Expr& e) { return trig_expand(e); });
}
std::string ms_trigreduce(std::string input) {
    return transform_json(input, [](const Expr& e) { return trig_reduce(e); });
}
std::string ms_logexpand(std::string input) {
    return transform_json(input, [](const Expr& e) { return log_expand(e); });
}
std::string ms_logcombine(std::string input) {
    return transform_json(input, [](const Expr& e) { return log_combine(e); });
}
std::string ms_cancel(std::string input) {
    return transform_json(input, [](const Expr& e) { return cancel(e); });
}
std::string ms_together(std::string input) {
    return transform_json(input, [](const Expr& e) { return together(e); });
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

/// fit(data, model, degree): least-squares regression of "x,y; x,y; ..." data.
/// The polynomial models are solved exactly over the rationals; exp/power/log
/// are numeric. Returns the fitted expression (plottable in x) plus the model
/// label, exact flag, R², and point count.
std::string ms_fit(std::string data, std::string model, std::string degree) {
    return guarded([&]() -> std::string {
        auto [xs, ys] = parse_point_data(data);
        std::string name = trim(model);
        if (name.empty()) name = "linear";
        const auto spec = parse_fit_model(name);
        if (!spec) return err_json("unknown fit model '" + name + "'");
        auto [fam, deg] = *spec;
        if (fam == FitModel::Poly && deg < 0) { // generic "poly": read the degree
            deg = 2;
            const std::string dt = trim(degree);
            if (!dt.empty()) {
                try {
                    deg = std::stoi(dt);
                } catch (...) {
                    return err_json("polynomial degree must be an integer");
                }
            }
        }
        const FitResult r = fit(xs, ys, fam, deg, "x");
        if (r.status != FitResult::Status::Ok) return err_json(r.message);
        return std::format(
            "{{\"ok\":true,{},\"model\":{},\"exact\":{},\"r2\":{},\"n\":{}}}",
            rendered_fields(r.expr), jstr(r.model), r.exact ? "true" : "false",
            jnum(r.r2), r.n);
    });
}

/// interp(data): exact polynomial interpolation of "x,y; x,y; ..." data — the
/// unique polynomial through the points, exact over the rationals.
std::string ms_interp(std::string data) {
    return guarded([&]() -> std::string {
        auto [xs, ys] = parse_point_data(data);
        const InterpResult r = interp(xs, ys, "x");
        if (r.status != InterpResult::Status::Ok) return err_json(r.message);
        return std::format(
            "{{\"ok\":true,{},\"exact\":{},\"degree\":{},\"n\":{}}}",
            rendered_fields(r.expr), r.exact ? "true" : "false", r.degree, r.n);
    });
}

/// interpForm(data, form): the interpolating polynomial through the "x,y; …"
/// points, kept in its factored Newton or Lagrange construction (form is
/// "newton" or "lagrange"). Returns it rendered plain and LaTeX with the
/// constant coefficients (divided differences / weights) as notes.
std::string ms_interp_form(std::string data, std::string form) {
    return guarded([&]() -> std::string {
        const std::string f = trim(form);
        if (f != "newton" && f != "lagrange")
            return err_json("interp form must be 'newton' or 'lagrange'");
        auto [xs, ys] = parse_point_data(data);
        const InterpFormResult r = interp_form(
            xs, ys, "x", f == "newton" ? InterpForm::Newton : InterpForm::Lagrange);
        if (r.status != InterpFormResult::Status::Ok) return err_json(r.message);
        return std::format("{{\"ok\":true,{},\"notes\":{}}}", rendered_fields(r.expr),
                           jarr_str(r.notes));
    });
}

/// orthopoly(family, n, variable): the exact degree-n orthogonal polynomial of
/// the family ("chebyshev"/"chebyshevu"/"legendre"/"hermite"/"laguerre") in
/// `variable`. Returns the rendered polynomial plus its family label and degree.
std::string ms_orthopoly(std::string family, int n, std::string variable) {
    return guarded([&]() -> std::string {
        const auto fam = parse_ortho_family(trim(family));
        if (!fam) return err_json(std::format("unknown polynomial family '{}'", family));
        std::string var = trim(variable);
        if (var.empty()) var = "x";
        const OrthoPolyResult r = ortho_poly(*fam, n, var);
        if (r.status != OrthoPolyResult::Status::Ok) return err_json(r.message);
        return std::format("{{\"ok\":true,{},\"family\":{},\"degree\":{}}}",
                           rendered_fields(r.expr), jstr(r.family), r.degree);
    });
}

/// stats(data): exact summary statistics of a data list. Returns an ordered
/// `items` array of {label, plain, latex} plus the exact flag and count.
std::string ms_stats(std::string data) {
    return guarded([&]() -> std::string {
        const StatsResult r = compute_stats(parse_stat_data(data));
        if (r.status != StatsResult::Status::Ok) return err_json(r.message);
        std::string items;
        for (const StatItem& s : r.items) {
            if (!items.empty()) items += ",";
            items += std::format("{{\"label\":{},{}}}", jstr(s.label), rendered_fields(s.value));
        }
        return std::format("{{\"ok\":true,\"exact\":{},\"n\":{},\"items\":[{}]}}",
                           r.exact ? "true" : "false", r.n, items);
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
        if ((op == "grad" || op == "laplacian" || op == "hessian") &&
            field.size() != 1) {
            return err_json(op + " takes a single scalar field, not " +
                            std::to_string(field.size()) +
                            " ';'-separated components");
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
        // Unknown symbols would silently blank every sample; reject upfront.
        std::set<std::string> extras = free_symbols(u);
        const std::set<std::string> vsyms = free_symbols(v);
        extras.insert(vsyms.begin(), vsyms.end());
        extras.erase(xv);
        extras.erase(yv);
        if (!extras.empty()) {
            std::string list;
            for (const std::string& s : extras) {
                if (!list.empty()) list += ", ";
                list += s;
            }
            return err_json("the field contains symbols other than " + xv +
                            " and " + yv + ": " + list);
        }
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

/// sampleGrid(expr, xVar, yVar, x0, x1, nx, y0, y1, ny): evaluate a scalar
/// field g(x, y) on a rectangular nx×ny grid (row-major, y outer). Returns a
/// flat array `g` (null where non-finite). Used for implicit-curve contouring
/// and inequality shading in the graphing calculator.
std::string ms_sample_grid(std::string expr, std::string xvar, std::string yvar,
                           double x0, double x1, int nx, double y0, double y1,
                           int ny) {
    return guarded([&]() -> std::string {
        if (nx < 2 || ny < 2 || nx > 400 || ny > 400) {
            return err_json("grid dimensions must be in [2, 400]");
        }
        const Expr g = parse_expression(expr);
        const std::string xv = trim(xvar).empty() ? "x" : trim(xvar);
        const std::string yv = trim(yvar).empty() ? "y" : trim(yvar);
        std::set<std::string> extras = free_symbols(g);
        extras.erase(xv);
        extras.erase(yv);
        if (!extras.empty()) {
            std::string list;
            for (const std::string& s : extras) {
                if (!list.empty()) list += ", ";
                list += s;
            }
            return err_json("the relation contains symbols other than " + xv +
                            " and " + yv + ": " + list);
        }
        std::string out = std::format("{{\"ok\":true,\"nx\":{},\"ny\":{},\"g\":[", nx, ny);
        bool first = true;
        for (int j = 0; j < ny; ++j) {
            const double y = y0 + (y1 - y0) * j / (ny - 1);
            for (int i = 0; i < nx; ++i) {
                const double x = x0 + (x1 - x0) * i / (nx - 1);
                double val = std::numeric_limits<double>::quiet_NaN();
                try {
                    val = evaluate(g, Bindings{{xv, x}, {yv, y}});
                } catch (const Error&) {
                }
                if (!first) out += ",";
                first = false;
                out += jnum(val);
            }
        }
        out += "]}";
        return out;
    });
}

std::string sum_result_json(const SumResult& r) {
    const char* status = r.status == SumResult::Status::Exact ? "exact"
                         : r.status == SumResult::Status::Diverges
                             ? "diverges"
                             : "unsolved";
    std::string out = std::format("{{\"ok\":true,\"status\":{}", jstr(status));
    if (r.status == SumResult::Status::Exact) {
        out += "," + rendered_fields(r.value);
    }
    out += std::format(",\"method\":{},\"warnings\":{}}}", jstr(r.method),
                       jarr_str(r.warnings));
    return out;
}

/// sum(term, var, lo, hi): closed-form summation; hi accepts "inf".
std::string ms_sum(std::string term, std::string var, std::string lo,
                   std::string hi) {
    return guarded([&]() -> std::string {
        const Expr t = parse_expression(term);
        const Expr l = parse_expression(lo);
        const std::string h = trim(hi);
        if (h == "inf" || h == "oo") {
            return sum_result_json(sum_infinite(t, trim(var), l));
        }
        return sum_result_json(sum_finite(t, trim(var), l, parse_expression(h)));
    });
}

/// product(term, var, lo, hi): closed-form product (numeric or geometric).
std::string ms_product(std::string term, std::string var, std::string lo,
                       std::string hi) {
    return guarded([&]() -> std::string {
        return sum_result_json(product_finite(parse_expression(term), trim(var),
                                              parse_expression(lo),
                                              parse_expression(hi)));
    });
}

/// rsolve(recurrence, conditionsCsv): closed form of a linear recurrence.
std::string ms_rsolve(std::string recurrence, std::string conditions_csv) {
    return guarded([&]() -> std::string {
        const RsolveResult r = rsolve(recurrence, split_csv(conditions_csv));
        return std::format(
            "{{\"ok\":true,{},\"order\":{},\"method\":{},\"warnings\":{}}}",
            rendered_fields(r.solution), r.order, jstr(r.method),
            jarr_str(r.warnings));
    });
}

/// limit(expr, variable, point, direction): direction is "left", "right",
/// or "" (two-sided); point accepts inf/-inf/oo. Returns status plus the
/// rendered value where one exists.
std::string ms_limit(std::string input, std::string variable, std::string point,
                     std::string direction) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(input);
        const std::string var = trim(variable);
        const std::string pt = trim(point);
        const std::string dir_text = trim(direction);
        const int dir = dir_text == "left" ? -1 : dir_text == "right" ? +1 : 0;
        LimitResult r;
        if (pt == "inf" || pt == "+inf" || pt == "oo") {
            r = limit_at_infinity(e, var, true);
        } else if (pt == "-inf" || pt == "-oo") {
            r = limit_at_infinity(e, var, false);
        } else {
            r = limit(e, var, parse_expression(pt), dir);
        }
        const char* status =
            r.status == LimitResult::Status::Exact        ? "exact"
            : r.status == LimitResult::Status::Numeric    ? "numeric"
            : r.status == LimitResult::Status::Diverges   ? "diverges"
            : r.status == LimitResult::Status::DoesNotExist ? "doesNotExist"
                                                            : "unsolved";
        std::string out = std::format("{{\"ok\":true,\"status\":{},\"sign\":{}",
                                      jstr(status), r.sign);
        if (r.status == LimitResult::Status::Exact) {
            out += "," + rendered_fields(r.value);
        } else if (r.status == LimitResult::Status::Numeric) {
            out += std::format(",\"approx\":{}",
                               jnum(evaluate(r.value, Bindings{})));
        }
        out += std::format(",\"method\":{},\"warnings\":{}}}", jstr(r.method),
                           jarr_str(r.warnings));
        return out;
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
        const std::string ct = trim(center);
        if (ct == "inf" || ct == "oo") {
            return std::format(
                "{{\"ok\":true,{}}}",
                rendered_fields(series_at_infinity(e, var, order)));
        }
        const Expr c = ct.empty() ? make_num(0) : parse_expression(ct);
        return std::format("{{\"ok\":true,{}}}",
                           rendered_fields(series(e, var, c, order)));
    });
}

/// pade(input, variable, m, n): the [m/n] Padé approximant P(x)/Q(x) matching
/// the Maclaurin series of the input through order m + n.
std::string ms_pade(std::string input, std::string variable, int m, int n) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(input);
        std::string var = trim(variable);
        if (var.empty()) {
            const std::set<std::string> syms = free_symbols(e);
            if (syms.size() != 1) {
                return err_json(
                    syms.empty()
                        ? "cannot infer the variable: the input has no free symbols"
                        : "give the Padé variable explicitly: pade <expr>, <m>, "
                          "<n>, <var>");
            }
            var = *syms.begin();
        }
        return std::format("{{\"ok\":true,{}}}",
                           rendered_fields(pade(e, var, m, n).approximant));
    });
}

/// Reduce `lhs = rhs` to `(lhs) - (rhs)` so the root verbs accept an equation.
std::string reduce_equation(const std::string& input) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '=') continue;
        const char p = i > 0 ? input[i - 1] : '\0';
        const char n = i + 1 < input.size() ? input[i + 1] : '\0';
        if (p != '<' && p != '>' && p != '!' && p != '=' && n != '=') {
            return "(" + input.substr(0, i) + ") - (" + input.substr(i + 1) + ")";
        }
    }
    return input;
}

/// Infer the single free variable of `e`, or `""` with an error JSON written to
/// `errOut` when it is ambiguous.
std::string infer_var(const Expr& e, std::string variable, std::string& errOut,
                      const char* usage) {
    std::string var = trim(variable);
    if (var.empty()) {
        const std::set<std::string> syms = free_symbols(e);
        if (syms.size() != 1) {
            errOut = err_json(syms.empty()
                                  ? "cannot infer the variable: no free symbols"
                                  : usage);
            return "";
        }
        var = *syms.begin();
    }
    return var;
}

/// rootcount(input, variable, lo, hi): the number of distinct real roots of a
/// rational-coefficient polynomial (Sturm), over all of R or (lo, hi].
std::string ms_rootcount(std::string input, std::string variable, std::string lo,
                         std::string hi) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(reduce_equation(input));
        std::string err;
        const std::string var = infer_var(
            e, variable, err, "give the variable explicitly: rootcount <poly>, <var>");
        if (var.empty()) return err;
        std::optional<Rational> a, b;
        const std::string lt = trim(lo), ht = trim(hi);
        if (!lt.empty() || !ht.empty()) {
            if (lt.empty() || ht.empty()) return err_json("give both interval bounds");
            const Expr la = simplify(parse_expression(lt));
            const Expr hb = simplify(parse_expression(ht));
            if (la->kind() != Kind::Number || hb->kind() != Kind::Number) {
                return err_json("interval bounds must be rational numbers");
            }
            a = la->number();
            b = hb->number();
        }
        const int count = sturm_root_count(e, var, a, b);
        std::string out = std::format("{{\"ok\":true,\"count\":{}", count);
        if (a && b) {
            out += std::format(",\"lo\":{},\"hi\":{}", jstr(a->to_string()),
                               jstr(b->to_string()));
        }
        return out + "}";
    });
}

/// isolate(input, variable): a rational interval around every distinct real
/// root, exact rationals reported exactly.
std::string ms_isolate(std::string input, std::string variable) {
    return guarded([&]() -> std::string {
        const Expr e = parse_expression(reduce_equation(input));
        std::string err;
        const std::string var = infer_var(
            e, variable, err, "give the variable explicitly: isolate <poly>, <var>");
        if (var.empty()) return err;
        const std::vector<RootInterval> roots = sturm_isolate_roots(e, var);
        std::string arr = "[";
        for (std::size_t i = 0; i < roots.size(); ++i) {
            const RootInterval& r = roots[i];
            if (i) arr += ",";
            arr += std::format(
                "{{\"exact\":{},\"value\":{},\"lo\":{},\"hi\":{},\"approx\":{}}}",
                r.exact ? "true" : "false",
                r.exact ? jstr(r.lo.to_string()) : "null", jnum(r.lo.to_double()),
                jnum(r.hi.to_double()), jnum(r.approx));
        }
        arr += "]";
        return std::format("{{\"ok\":true,\"count\":{},\"roots\":{}}}", roots.size(),
                           arr);
    });
}

/// stirling(variable, terms): the Stirling asymptotic series for
/// ln Gamma(variable) with exact Bernoulli coefficients; the lgamma
/// accuracy check rides along as warnings.
std::string ms_stirling(std::string variable, int terms) {
    return guarded([&]() -> std::string {
        std::string var = trim(variable);
        if (var.empty()) {
            var = "x";
        }
        const Expr ve = parse_expression(var);
        if (ve->kind() != Kind::Symbol) {
            return err_json("stirling: the variable must be a symbol name");
        }
        const StirlingResult r =
            stirling_series(to_string(ve, PrintStyle::Plain), terms);
        return std::format("{{\"ok\":true,{},\"notes\":{}}}",
                           rendered_fields(r.series), jarr_str(r.checks));
    });
}

/// seq(termsCsv): recognize the pattern behind a list of exact terms.
std::string ms_seq(std::string terms_csv) {
    return guarded([&]() -> std::string {
        std::vector<Rational> terms;
        for (const std::string& t : split_csv(terms_csv)) {
            const Expr e = simplify(parse_expression(t));
            if (e->kind() != Kind::Number) {
                return err_json(std::format(
                    "seq terms must be exact numbers, got '{}'", t));
            }
            terms.push_back(e->number());
        }
        const SeqResult r = recognize_sequence(terms);
        const char* kind =
            r.kind == SeqResult::Kind::Arithmetic   ? "arithmetic"
            : r.kind == SeqResult::Kind::Geometric  ? "geometric"
            : r.kind == SeqResult::Kind::Polynomial ? "polynomial"
            : r.kind == SeqResult::Kind::Recurrence ? "recurrence"
                                                    : "unknown";
        std::string out = std::format(
            "{{\"ok\":true,\"kind\":{},\"description\":{}", jstr(kind),
            jstr(r.description));
        if (r.formula) {
            out += "," + rendered_fields(r.formula);
        }
        if (!r.recurrence.empty()) {
            out += std::format(",\"recurrence\":{}", jstr(r.recurrence));
        }
        std::vector<std::string> next;
        for (const Rational& v : r.next) {
            next.push_back(v.to_string());
        }
        out += std::format(",\"next\":{},\"warnings\":{}}}", jarr_str(next),
                           jarr_str(r.warnings));
        return out;
    });
}

/// mlimit(expr, xVar, a, yVar, b): two-variable limit by path sampling.
std::string ms_mlimit(std::string input, std::string xvar, std::string a,
                      std::string yvar, std::string b) {
    return guarded([&]() -> std::string {
        const LimitResult r =
            limit_multi(parse_expression(input), trim(xvar),
                        parse_expression(a), trim(yvar), parse_expression(b));
        const char* status =
            r.status == LimitResult::Status::Exact          ? "exact"
            : r.status == LimitResult::Status::Numeric      ? "numeric"
            : r.status == LimitResult::Status::Diverges     ? "diverges"
            : r.status == LimitResult::Status::DoesNotExist ? "doesNotExist"
                                                            : "unsolved";
        std::string out = std::format("{{\"ok\":true,\"status\":{},\"sign\":{}",
                                      jstr(status), r.sign);
        if (r.status == LimitResult::Status::Exact) {
            out += "," + rendered_fields(r.value);
        } else if (r.status == LimitResult::Status::Numeric) {
            out += std::format(",\"approx\":{}",
                               jnum(evaluate(r.value, Bindings{})));
        }
        out += std::format(",\"method\":{},\"warnings\":{}}}", jstr(r.method),
                           jarr_str(r.warnings));
        return out;
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
        // A ';' selects the first-order-system path; the components render
        // as one aligned block through the ordinary dsolve result card.
        std::vector<std::string> sys_eqs;
        {
            std::size_t start = 0;
            for (std::size_t i = 0; i <= ode.size(); ++i) {
                if (i == ode.size() || ode[i] == ';') {
                    const std::string piece = trim(ode.substr(start, i - start));
                    if (!piece.empty()) sys_eqs.push_back(piece);
                    start = i + 1;
                }
            }
        }
        if (sys_eqs.size() > 1) {
            const DsolveSystemResult r = dsolve_system(sys_eqs, conditions);
            std::string plain;
            std::string latex = "\\begin{aligned}";
            for (std::size_t i = 0; i < r.names.size(); ++i) {
                if (i > 0) {
                    plain += "\n";
                    latex += " \\\\ ";
                }
                plain += r.names[i] + "(t) = " +
                         to_string(r.solutions[i], PrintStyle::Plain);
                latex += r.names[i] + "(t) &= " +
                         to_string(r.solutions[i], PrintStyle::LaTeX);
            }
            latex += "\\end{aligned}";
            return std::format(
                "{{\"ok\":true,\"plain\":{},\"latex\":{},"
                "\"transformPlain\":\"\",\"transformLatex\":\"\","
                "\"implicit\":false,\"method\":{},\"warnings\":{}}}",
                jstr(plain), jstr(latex), jstr(r.method), jarr_str(r.warnings));
        }
        const DsolveResult r = dsolve(ode, conditions);
        return std::format(
            "{{\"ok\":true,{},\"transformPlain\":{},\"transformLatex\":{},"
            "\"implicit\":{},\"method\":{},\"warnings\":{}}}",
            rendered_fields(r.solution),
            jstr(r.transform ? to_string(r.transform, PrintStyle::Plain) : ""),
            jstr(r.transform ? to_string(r.transform, PrintStyle::LaTeX) : ""),
            r.implicit ? "true" : "false", jstr(r.method),
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
                out += std::format(
                    "{{\"name\":{},\"summary\":{},\"usage\":{},\"example\":{}}}",
                    jstr(c.name), jstr(c.summary), jstr(c.usage),
                    jstr(c.example));
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
    emscripten::function("trigexpand", &ms_trigexpand);
    emscripten::function("trigreduce", &ms_trigreduce);
    emscripten::function("logexpand", &ms_logexpand);
    emscripten::function("logcombine", &ms_logcombine);
    emscripten::function("cancel", &ms_cancel);
    emscripten::function("together", &ms_together);
    emscripten::function("latex", &ms_latex);
    emscripten::function("subs", &ms_subs);
    emscripten::function("collect", &ms_collect);
    emscripten::function("derivative", &ms_derivative);
    emscripten::function("apart", &ms_apart);
    emscripten::function("fit", &ms_fit);
    emscripten::function("interp", &ms_interp);
    emscripten::function("interpForm", &ms_interp_form);
    emscripten::function("orthopoly", &ms_orthopoly);
    emscripten::function("stats", &ms_stats);
    emscripten::function("dsolve", &ms_dsolve);
    emscripten::function("series", &ms_series);
    emscripten::function("pade", &ms_pade);
    emscripten::function("rootcount", &ms_rootcount);
    emscripten::function("isolate", &ms_isolate);
    emscripten::function("vectorOp", &ms_vector_op);
    emscripten::function("limit", &ms_limit);
    emscripten::function("mlimit", &ms_mlimit);
    emscripten::function("stirling", &ms_stirling);
    emscripten::function("seq", &ms_seq);
    emscripten::function("gcd", &ms_gcd);
    emscripten::function("lcm", &ms_lcm);
    emscripten::function("isprime", &ms_isprime);
    emscripten::function("nextprime", &ms_nextprime);
    emscripten::function("divisors", &ms_divisors);
    emscripten::function("totient", &ms_totient);
    emscripten::function("sigma", &ms_sigma);
    emscripten::function("mobius", &ms_mobius);
    emscripten::function("partitions", &ms_partitions);
    emscripten::function("catalan", &ms_catalan);
    emscripten::function("bernoulli", &ms_bernoulli);
    emscripten::function("stirling2", &ms_stirling2);
    emscripten::function("bell", &ms_bell);
    emscripten::function("derangement", &ms_derangement);
    emscripten::function("cfrac", &ms_cfrac);
    emscripten::function("discriminant", &ms_discriminant);
    emscripten::function("polydiv", &ms_polydiv);
    emscripten::function("polygcd", &ms_polygcd);
    emscripten::function("polylcm", &ms_polylcm);
    emscripten::function("resultant", &ms_resultant);
    emscripten::function("bezout", &ms_bezout);
    emscripten::function("companion", &ms_companion);
    emscripten::function("vandermonde", &ms_vandermonde);
    emscripten::function("solveIneq", &ms_solve_ineq);
    emscripten::function("mod", &ms_mod);
    emscripten::function("powmod", &ms_powmod);
    emscripten::function("modinv", &ms_modinv);
    emscripten::function("crt", &ms_crt);
    emscripten::function("sum", &ms_sum);
    emscripten::function("product", &ms_product);
    emscripten::function("rsolve", &ms_rsolve);
    emscripten::function("sampleField", &ms_sample_field);
    emscripten::function("sampleGrid", &ms_sample_grid);
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
