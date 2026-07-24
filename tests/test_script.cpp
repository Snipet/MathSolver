#include <catch2/catch_test_macros.hpp>

#include <format>
#include <set>
#include <string>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/parser.hpp"
#include "mathsolver/printer.hpp"
#include "mathsolver/script/script.hpp"

using namespace mathsolver;
using namespace mathsolver::script;

namespace {

Expr ex(const std::string& s) { return parse_expression(s); }

Equation eq(const std::string& s) { return parse_equation(s); }

std::string plain(const Expr& e) { return to_string(e, PrintStyle::Plain); }

/// Resolve `input` against `env` and print it, the way a surface does before
/// handing text to a computing verb.
std::string resolved(const Environment& env, const std::string& input) {
    return env.resolve(Value{ex(input)}).to_plain();
}

/// The CLI's REPL verb set, standing in for any host's command table.
bool is_verb(std::string_view w) {
    return w == "solve" || w == "diff" || w == "simplify" || w == "stirling2";
}

std::vector<std::string> names(const Environment& env) {
    std::vector<std::string> out;
    for (const Binding& b : env) {
        out.push_back(b.name);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

TEST_CASE("script: a value is an expression or an equation", "[script][value]") {
    const Value v{ex("2*x + y")};
    REQUIRE(v.is_expr());
    REQUIRE_FALSE(v.is_equation());
    REQUIRE(v.if_equation() == nullptr);
    REQUIRE(v.free_symbols() == std::set<std::string>{"x", "y"});
    // Printed in the AST's canonical order (§2), not as typed.
    REQUIRE(v.to_plain() == "y + 2*x");

    const Value e{eq("a + b = 3")};
    REQUIRE(e.is_equation());
    REQUIRE(e.if_expr() == nullptr);
    REQUIRE(e.free_symbols() == std::set<std::string>{"a", "b"});
    REQUIRE(e.to_plain() == "a + b = 3");
}

TEST_CASE("script: equation_symbols spans both sides", "[script][value]") {
    REQUIRE(equation_symbols(eq("x = y")) == std::set<std::string>{"x", "y"});
    REQUIRE(equation_symbols(eq("2 = 2")).empty());
}

// ---------------------------------------------------------------------------
// Environment: define / find / erase / clear
// ---------------------------------------------------------------------------

TEST_CASE("script: define, find, erase, clear", "[script][env]") {
    Environment env;
    REQUIRE(env.empty());
    REQUIRE(env.find("a") == nullptr);
    REQUIRE_FALSE(env.contains("a"));

    const Binding& b = env.define("a", Value{ex("2")});
    REQUIRE(b.name == "a");
    REQUIRE(binding_text(b) == "a := 2");
    REQUIRE(env.size() == 1);
    REQUIRE(env.contains("a"));
    REQUIRE(env.find("a")->value.to_plain() == "2");

    env.define("b", Value{ex("a + 1")});
    REQUIRE(names(env) == std::vector<std::string>{"a", "b"});

    REQUIRE(env.erase("a"));
    REQUIRE_FALSE(env.erase("a"));
    REQUIRE(names(env) == std::vector<std::string>{"b"});

    env.clear();
    REQUIRE(env.empty());
}

TEST_CASE("script: redefinition replaces in place", "[script][env]") {
    // `vars` lists in definition order, so redefining must not reorder.
    Environment env;
    env.define("a", Value{ex("1")});
    env.define("b", Value{ex("2")});
    env.define("a", Value{ex("9")});
    REQUIRE(names(env) == std::vector<std::string>{"a", "b"});
    REQUIRE(env.find("a")->value.to_plain() == "9");
}

TEST_CASE("script: a binding echoes in its re-parseable spelling",
          "[script][env]") {
    // The stored name is the bare symbol `x_max`; the echo goes through the
    // printer so it comes back in the spelling that parses (`x_{max}`).
    Environment env;
    REQUIRE(binding_text(env.define("x_max", Value{ex("5")})) == "x_{max} := 5");
    REQUIRE(binding_text(env.define("alpha", Value{eq("y = 2")})) ==
            "alpha := y = 2");
}

// ---------------------------------------------------------------------------
// Environment: cycles (spec §5.2)
// ---------------------------------------------------------------------------

TEST_CASE("script: a binding cannot refer to itself", "[script][env]") {
    Environment env;
    REQUIRE_THROWS_AS(env.define("a", Value{ex("a + 1")}), UsageError);
    REQUIRE(env.empty());
}

TEST_CASE("script: a cycle is rejected at definition time", "[script][env]") {
    Environment env;
    env.define("a", Value{ex("b")});
    try {
        env.define("b", Value{ex("a")});
        FAIL("expected a cycle diagnostic");
    } catch (const UsageError& e) {
        REQUIRE(e.message() == "assignment would create a cycle: b -> a -> b");
    }
    REQUIRE(names(env) == std::vector<std::string>{"a"});
}

// ---------------------------------------------------------------------------
// Environment: resolve (spec §5)
// ---------------------------------------------------------------------------

TEST_CASE("script: resolve substitutes transitively", "[script][env]") {
    // resolve() only substitutes — it never simplifies — but the §2 factories
    // fold numeric operands as the substituted tree is rebuilt, so a
    // fully-numeric chain lands on a literal.
    Environment env;
    env.define("a", Value{ex("2")});
    env.define("b", Value{ex("a + 1")});
    env.define("c", Value{ex("a*b")});
    REQUIRE(resolved(env, "c + 1") == "7");
    REQUIRE(resolved(env, "c*x") == "6*x");
    // An unbound name is left exactly as it stands.
    REQUIRE(plain(env.resolve(ex("q"))) == "q");
}

TEST_CASE("script: resolve does not depend on definition order",
          "[script][env]") {
    // Defining the referring name first still resolves parents-first.
    Environment forward;
    forward.define("a", Value{ex("2")});
    forward.define("b", Value{ex("a + 1")});

    Environment backward;
    backward.define("b", Value{ex("a + 1")});
    backward.define("a", Value{ex("2")});

    REQUIRE(resolved(forward, "b") == resolved(backward, "b"));
    REQUIRE(resolved(forward, "b") == "3");
    REQUIRE(resolved(forward, "b*x") == resolved(backward, "b*x"));
    REQUIRE(resolved(forward, "b*x") == "3*x");
}

TEST_CASE("script: resolve handles a deep chain", "[script][env]") {
    // The traversal bound is the active set's size, not a constant: a chain
    // far deeper than any fixed limit must still resolve (a fixed bound here
    // once misdiagnosed a 66-deep chain as a cycle).
    Environment env;
    env.define("a_0", Value{ex("1")});
    for (int i = 1; i <= 80; ++i) {
        env.define(std::format("a_{}", i), Value{ex(std::format("a_{}", i - 1))});
    }
    REQUIRE(resolved(env, "a_80") == "1");
}

TEST_CASE("script: excluded names block their whole subtree", "[script][env]") {
    // The exclusion check happens before reachability, so an excluded name's
    // own dependencies are not traversed either.
    Environment env;
    env.define("a", Value{ex("2")});
    env.define("b", Value{ex("a + 1")});
    REQUIRE(plain(env.resolve(ex("b"), {"b"})) == "b");
    REQUIRE(plain(env.resolve(ex("b"), {"a"})) == "a + 1");
}

TEST_CASE("script: resolve reaches into both sides of an equation",
          "[script][env]") {
    Environment env;
    env.define("a", Value{ex("2")});
    REQUIRE(env.resolve(Value{eq("a*x = a")}).to_plain() == "2*x = 2");
}

TEST_CASE("script: an equation name inside an expression is a usage error",
          "[script][env]") {
    // Spec §4: an equation-valued name may stand alone, never as a subterm.
    Environment env;
    env.define("E_1", Value{eq("x + y = 3")});
    try {
        (void)env.resolve(ex("E_1 + 1"));
        FAIL("expected a placement diagnostic");
    } catch (const UsageError& e) {
        REQUIRE(e.message() ==
                "'E_1' names an equation and cannot be used inside an expression");
    }
}

TEST_CASE("script: UsageError is not a math Error", "[script][env]") {
    // Hosts map usage problems to their own exit status without also
    // catching genuine math failures, so the two hierarchies stay disjoint.
    Environment env;
    REQUIRE_THROWS_AS(env.define("a", Value{ex("a")}), UsageError);
    try {
        env.define("a", Value{ex("a")});
    } catch (const Error&) {
        FAIL("UsageError must not derive from mathsolver::Error");
    } catch (const UsageError&) {
    }
}

// ---------------------------------------------------------------------------
// Assignment targets (spec §2.3)
// ---------------------------------------------------------------------------

TEST_CASE("script: valid assignment targets normalize", "[script][target]") {
    REQUIRE(validate_assignment_target("x") == "x");
    REQUIRE(validate_assignment_target("x_{max}") == "x_max");
    REQUIRE(validate_assignment_target("\\alpha") == "alpha");
    REQUIRE(validate_assignment_target("alpha") == "alpha");
    REQUIRE(validate_assignment_target("E_1") == "E_1");
    // A target must be something the parser reads as one symbol, so the brace
    // spelling is the only way to write a word subscript — `x_max` is a lex
    // error on the assignment side even though `unset x_max` accepts it.
    REQUIRE_THROWS_AS(validate_assignment_target("x_max"), UsageError);
}

TEST_CASE("script: rejected assignment targets explain themselves",
          "[script][target]") {
    auto message = [](const std::string& target) {
        try {
            (void)validate_assignment_target(target);
        } catch (const UsageError& e) {
            return e.message();
        }
        return std::string{"<accepted>"};
    };

    REQUIRE(message("sin") == "cannot assign to the function name 'sin'");
    REQUIRE(message("pi") == "cannot assign to the constant 'pi'");
    REQUIRE(message("e") == "cannot assign to the constant 'e'");
    REQUIRE(message("E1").contains("'E1' reads as E*1; did you mean E_1?"));
    REQUIRE(message("speed").contains("try a subscripted name like s_max := 5"));
    REQUIRE(message("x + y").starts_with(k_assign_target_error));
}

TEST_CASE("script: is_function_word covers the lexer's function names",
          "[script][target]") {
    REQUIRE(is_function_word("sqrt"));
    REQUIRE(is_function_word("arctan"));
    REQUIRE_FALSE(is_function_word("gamma"));
    REQUIRE_FALSE(is_function_word(""));
}

TEST_CASE("script: normalize_name accepts both displayed spellings",
          "[script][target]") {
    // `vars` shows `x_{max}`; copy-pasting that into `unset` must work.
    REQUIRE(normalize_name("x_{max}") == "x_max");
    REQUIRE(normalize_name("x_max") == "x_max");
    REQUIRE(normalize_name("\\alpha") == "alpha");
    REQUIRE(normalize_name("") == "");
}

// ---------------------------------------------------------------------------
// Statement shapes
// ---------------------------------------------------------------------------

TEST_CASE("script: trim strips both ends", "[script][statement]") {
    REQUIRE(trim("  x + 1 \t") == "x + 1");
    REQUIRE(trim("   ").empty());
    REQUIRE(trim("").empty());
}

TEST_CASE("script: assignment lines need a non-empty target",
          "[script][statement]") {
    REQUIRE(is_assignment_line("a := 2"));
    REQUIRE(is_assignment_line("  a  :=  2"));
    REQUIRE_FALSE(is_assignment_line(":= 2"));
    REQUIRE_FALSE(is_assignment_line("   := 2"));
    // A ':' not followed by '=' keeps its existing lex error.
    REQUIRE_FALSE(is_assignment_line("a : 2"));
    REQUIRE_FALSE(is_assignment_line("solve x = 2"));
}

TEST_CASE("script: leading_word lexes letters then digits",
          "[script][statement]") {
    const LeadingWord w = leading_word("stirling2 5, 2");
    REQUIRE(w.word == "stirling2");
    REQUIRE(w.terminated);
    REQUIRE(w.end == 9);

    const LeadingWord alone = leading_word("vars");
    REQUIRE(alone.word == "vars");
    REQUIRE(alone.terminated);
    REQUIRE(alone.end == 4);

    // '_' does not continue a word, so `x_max` heads with `x`, unterminated.
    const LeadingWord sub = leading_word("x_max");
    REQUIRE(sub.word == "x");
    REQUIRE_FALSE(sub.terminated);

    // A line that does not start with a letter is never a command.
    const LeadingWord num = leading_word("2 + 2");
    REQUIRE(num.word.empty());
    REQUIRE_FALSE(num.terminated);
}

TEST_CASE("script: parse_statement classifies a line", "[script][statement]") {
    const Statement assign = parse_statement("  a := 2 + 2  ", is_verb);
    REQUIRE(assign.kind == StatementKind::Assignment);
    REQUIRE(assign.head == "a");
    REQUIRE(assign.rest == "2 + 2");

    const Statement command = parse_statement("solve  x^2 = 4, x", is_verb);
    REQUIRE(command.kind == StatementKind::Command);
    REQUIRE(command.head == "solve");
    REQUIRE(command.rest == "x^2 = 4, x");

    const Statement bare = parse_statement("2 + 2", is_verb);
    REQUIRE(bare.kind == StatementKind::Bare);
    REQUIRE(bare.head.empty());
    REQUIRE(bare.rest == "2 + 2");

    // An unknown leading word is a bare input, not a command.
    REQUIRE(parse_statement("wibble x", is_verb).kind == StatementKind::Bare);
    // A known word must stand alone: `solved` is not `solve`.
    REQUIRE(parse_statement("solved", is_verb).kind == StatementKind::Bare);
    // Assignment wins over command dispatch: `solve` never sees this line.
    REQUIRE(parse_statement("solve := 2", is_verb).kind ==
            StatementKind::Assignment);
}
