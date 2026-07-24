#include "mathsolver/script/environment.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>

#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"

namespace mathsolver::script {
namespace {

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
        const Binding* b = env.find(cur);
        if (b == nullptr) {
            return false;
        }
        path.push_back(cur);
        for (const std::string& next : b->value.free_symbols()) {
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

} // namespace

std::string binding_text(const Binding& b) {
    const std::string name = to_string(make_sym(b.name), PrintStyle::Plain);
    return std::format("{} := {}", name, b.value.to_plain());
}

const Binding* Environment::find(std::string_view name) const {
    for (const Binding& b : bindings_) {
        if (b.name == name) {
            return &b;
        }
    }
    return nullptr;
}

const Binding& Environment::define(std::string name, Value value) {
    const std::set<std::string> value_syms = value.free_symbols();
    if (value_syms.contains(name)) {
        throw UsageError{
            std::format("'{}' cannot be defined in terms of itself", name)};
    }
    const std::vector<std::string> cycle = find_cycle_path(name, value_syms, *this);
    if (!cycle.empty()) {
        std::string msg = std::format("assignment would create a cycle: {}", name);
        for (const std::string& n : cycle) {
            msg += " -> " + n;
        }
        throw UsageError{msg};
    }

    // Redefinition replaces in place (keeps definition order for `vars`).
    const auto it = std::ranges::find(bindings_, name, &Binding::name);
    if (it != bindings_.end()) {
        *it = Binding{std::move(name), std::move(value)};
        return *it;
    }
    bindings_.push_back(Binding{std::move(name), std::move(value)});
    return bindings_.back();
}

bool Environment::erase(std::string_view name) {
    const auto it = std::ranges::find(bindings_, name, &Binding::name);
    if (it == bindings_.end()) {
        return false;
    }
    bindings_.erase(it);
    return true;
}

void Environment::clear() noexcept { bindings_.clear(); }

std::vector<const Binding*> Environment::active(
    const std::set<std::string>& roots,
    const std::set<std::string>& excluded) const {
    std::vector<const Binding*> active;
    std::set<std::string> seen;
    std::vector<std::string> frontier(roots.begin(), roots.end());
    while (!frontier.empty()) {
        const std::string name = std::move(frontier.back());
        frontier.pop_back();
        if (excluded.contains(name) || !seen.insert(name).second) {
            continue;
        }
        const Binding* b = find(name);
        if (b == nullptr) {
            continue;
        }
        if (b->value.is_equation()) {
            throw UsageError{std::format(
                "'{}' names an equation and cannot be used inside an expression",
                name)};
        }
        active.push_back(b);
        for (const std::string& s : free_symbols(b->value.expr())) {
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
            if (dep != b && contains_symbol(b->value.expr(), dep->name)) {
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

Expr Environment::resolve(const Expr& e,
                          const std::set<std::string>& excluded) const {
    Expr r = e;
    for (const Binding* b : active(free_symbols(e), excluded)) {
        r = substitute(r, b->name, b->value.expr());
    }
    return r;
}

Equation Environment::resolve(const Equation& eq,
                              const std::set<std::string>& excluded) const {
    Equation r = eq;
    for (const Binding* b : active(equation_symbols(eq), excluded)) {
        const Expr& v = b->value.expr();
        r.lhs = substitute(r.lhs, b->name, v);
        r.rhs = substitute(r.rhs, b->name, v);
    }
    return r;
}

Value Environment::resolve(const Value& v,
                           const std::set<std::string>& excluded) const {
    if (const Expr* e = v.if_expr()) {
        return Value{resolve(*e, excluded)};
    }
    return Value{resolve(v.equation(), excluded)};
}

bool is_function_word(std::string_view s) {
    static constexpr std::string_view names[] = {
        "sin",  "cos",  "tan", "asin", "acos", "atan", "arcsin",
        "arccos", "arctan", "sinh", "cosh", "tanh", "sec", "csc",
        "cot",  "exp",  "ln",  "log",  "sqrt", "abs"};
    return std::ranges::find(names, s) != std::ranges::end(names);
}

std::string normalize_name(std::string_view s) {
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
    const std::string normalized = normalize_name(target);
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

} // namespace mathsolver::script
