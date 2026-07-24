#pragma once

// The script session environment: `name := value` bindings.
// Normative spec: docs/proposals/variable-assignment.md; contract condensed
// in DESIGN.md §10. Layering rationale: DESIGN.md §13.
//
// LAYERING. This library is the lowest layer that knows environments exist.
// Nothing beneath it does: no header under include/mathsolver/ outside this
// directory, and no translation unit under src/ outside src/script/,
// mentions a binding. The dependency runs one way — mathsolver_script links
// mathsolver_core and never the reverse — so the engine-purity invariant is
// a link error to violate rather than a convention to remember.
//
// Values are stored as parsed ASTs and resolve lazily at each use (spec §5);
// the dependency graph over defined names is kept acyclic at definition time
// (§5.2), so resolution never needs a runtime cycle check to terminate.

#include <cstddef>
#include <exception>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mathsolver/script/value.hpp"

namespace mathsolver::script {

/// A usage problem in a script statement: a malformed assignment target, an
/// ambiguous variable, a bad argument. Deliberately NOT derived from
/// mathsolver::Error, so a host can map it to its own "usage" exit status
/// without also catching genuine math errors.
class UsageError : public std::exception {
public:
    explicit UsageError(std::string message) : message_(std::move(message)) {}

    const char* what() const noexcept override { return message_.c_str(); }
    const std::string& message() const noexcept { return message_; }

private:
    std::string message_;
};

struct Binding {
    std::string name;
    Value value;
};

/// Canonical plain print of a binding: `a := 2`, `E_1 := x + y = 3`. The name
/// goes through the printer so it echoes in its re-parseable spelling
/// (`x_{max}`, not the bare symbol name `x_max`).
std::string binding_text(const Binding& b);

/// The session's `:=` bindings, in definition order.
///
/// Insertion-ordered because `vars` lists in definition order; lookups are
/// linear over a handful of entries, which is the right trade at this size.
class Environment {
public:
    const Binding* find(std::string_view name) const;
    bool contains(std::string_view name) const { return find(name) != nullptr; }

    bool empty() const noexcept { return bindings_.empty(); }
    std::size_t size() const noexcept { return bindings_.size(); }
    auto begin() const noexcept { return bindings_.begin(); }
    auto end() const noexcept { return bindings_.end(); }

    /// Bind `name` to `value` (spec §5.2). Redefinition replaces in place, so
    /// the definition order `vars` shows survives a redefinition. Throws
    /// UsageError when the value refers to `name` itself or would close a
    /// dependency cycle. Returns the stored binding, for echoing.
    const Binding& define(std::string name, Value value);

    /// Remove a binding; false when the name was not defined.
    bool erase(std::string_view name);
    void clear() noexcept;

    /// The spec §5 resolve(): substitute every binding reachable from the
    /// input through bound values, transitively, minus `excluded`. Bindings
    /// are applied parents-first — a binding precedes every binding its value
    /// references — so one sequential substitute() pass fully resolves chains
    /// and the result does not depend on definition order.
    ///
    /// Throws UsageError when a name bound to an equation is reached from
    /// inside an expression: that is the §4 placement error.
    Expr resolve(const Expr& e, const std::set<std::string>& excluded = {}) const;
    Equation resolve(const Equation& eq,
                     const std::set<std::string>& excluded = {}) const;
    Value resolve(const Value& v,
                  const std::set<std::string>& excluded = {}) const;

private:
    /// Reachable-and-ordered bindings for `roots`; see resolve().
    std::vector<const Binding*> active(const std::set<std::string>& roots,
                                       const std::set<std::string>& excluded) const;

    std::vector<Binding> bindings_;
};

/// The shared prefix of every bad-assignment-target diagnostic.
inline constexpr std::string_view k_assign_target_error =
    "assignment target must be a single variable name (e.g. x, alpha, E_1)";

/// Names the lexer reads as functions (docs/GRAMMAR.md): never assignable.
bool is_function_word(std::string_view s);

/// `x_{max}` and `\alpha` lex to the symbols x_max / alpha; normalize a typed
/// name so it can be compared against the parsed symbol's name. Also the
/// spelling `unset` accepts, so a name copied out of `vars` works.
std::string normalize_name(std::string_view s);

/// Validate the left side of `name := value` (spec §2.3) and return the
/// canonical symbol name it binds. Throws UsageError with targeted guidance
/// (function name, constant, `E1` reads as `E*1`, word-guard) otherwise.
std::string validate_assignment_target(const std::string& target);

} // namespace mathsolver::script
