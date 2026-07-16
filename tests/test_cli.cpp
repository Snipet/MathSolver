// End-to-end tests for the mathsolver CLI/REPL (DESIGN.md §10).
//
// Each test runs the real binary (path injected as MATHSOLVER_CLI_PATH via
// CMake) through popen(), capturing stdout/stderr and the exit code.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>

#include <sys/wait.h>

namespace {

struct RunResult {
    std::string output;
    int exit_code = -1;
};

RunResult run_shell(const std::string& command) {
    std::FILE* pipe = ::popen(command.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::string output;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, pipe)) > 0) {
        output.append(buffer, n);
    }
    const int status = ::pclose(pipe);
    RunResult result{std::move(output), -1};
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
    return result;
}

std::string shell_quote(const std::string& s) {
    std::string quoted = "'";
    for (const char c : s) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += '\'';
    return quoted;
}

/// Run `mathsolver <args...>` with the given stream redirection
/// ("2>&1" both streams, "2>/dev/null" stdout only, "2>&1 1>/dev/null"
/// stderr only).
RunResult run_cli(std::initializer_list<std::string> args,
                  const std::string& redirect = "2>&1") {
    std::string command = shell_quote(MATHSOLVER_CLI_PATH);
    for (const std::string& a : args) {
        command += ' ';
        command += shell_quote(a);
    }
    command += ' ';
    command += redirect;
    return run_shell(command);
}

/// Pipe `input` (may contain newlines) into the REPL, capturing both streams.
RunResult run_repl(const std::string& input) {
    return run_shell("printf %s " + shell_quote(input) + " | " +
                     shell_quote(MATHSOLVER_CLI_PATH) + " 2>&1");
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

/// Extract the number following `prefix` (e.g. "x ≈ ") in `text`.
double number_after(const std::string& text, const std::string& prefix) {
    const std::size_t pos = text.find(prefix);
    REQUIRE(pos != std::string::npos);
    return std::strtod(text.c_str() + pos + prefix.size(), nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// One-shot subcommands
// ---------------------------------------------------------------------------

TEST_CASE("cli: simplify") {
    const RunResult r = run_cli({"simplify", "2x + 3x"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "5*x\n");
}

TEST_CASE("cli: simplify accepts an equation and simplifies both sides") {
    const RunResult r = run_cli({"simplify", "x + x = 4"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "2*x = 4\n");
}

TEST_CASE("cli: expand") {
    const RunResult r = run_cli({"expand", "(x+1)^3"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "x^3 + 3*x^2 + 3*x + 1\n");
}

TEST_CASE("cli: factor") {
    const RunResult r = run_cli({"factor", "x^2 - 5x + 6"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x - 2"));
    CHECK(contains(r.output, "x - 3"));
    CHECK(contains(r.output, ")*("));
}

TEST_CASE("cli: solve exact quadratic") {
    const RunResult r = run_cli({"solve", "x^2 = 4", "x"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = -2\n"));
    CHECK(contains(r.output, "x = 2\n"));
    CHECK(contains(r.output, "method: quadratic formula"));
}

TEST_CASE("cli: solve infers a unique variable") {
    const RunResult r = run_cli({"solve", "2y + 3 = 7"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "y = 2\n"));
    CHECK(contains(r.output, "method: linear"));
}

TEST_CASE("cli: solve numeric fallback with --range") {
    const RunResult r = run_cli({"solve", "cos(x) = x", "x", "--range", "0", "2"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    REQUIRE(contains(r.output, "x ≈ "));
    const double root = number_after(r.output, "x ≈ ");
    CHECK(std::abs(root - 0.7390851332151607) < 1e-9);
    CHECK(contains(r.output, "method: numeric (Newton/bisection)"));
    CHECK(contains(r.output, "[0, 2]")); // interval warning reflects --range
}

TEST_CASE("cli: solve reports identities and contradictions") {
    const RunResult identity = run_cli({"solve", "x + 1 = x + 1", "x"});
    INFO(identity.output);
    CHECK(identity.exit_code == 0);
    CHECK(contains(identity.output, "true for all x"));

    const RunResult contradiction = run_cli({"solve", "x + 1 = x", "x"});
    INFO(contradiction.output);
    CHECK(contradiction.exit_code == 0);
    CHECK(contains(contradiction.output, "no real solutions"));
}

TEST_CASE("cli: solve trig equation prints the periodicity note") {
    const RunResult r = run_cli({"solve", "sin(x) = 1/2", "x"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "x = pi/6"));
    CHECK(contains(r.output, "principal solution"));
    CHECK(contains(r.output, "2*pi*n"));
}

TEST_CASE("cli: diff with explicit and inferred variable") {
    const RunResult explicit_var = run_cli({"diff", "sin(x^2)", "x"});
    INFO(explicit_var.output);
    CHECK(explicit_var.exit_code == 0);
    CHECK(explicit_var.output == "2*x*cos(x^2)\n");

    const RunResult inferred = run_cli({"diff", "sin(x^2)"});
    CHECK(inferred.exit_code == 0);
    CHECK(inferred.output == "2*x*cos(x^2)\n");
}

TEST_CASE("cli: eval with bindings") {
    const RunResult r = run_cli({"eval", "x^2 + y", "x=3", "y=0.5"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "9.5\n");
}

TEST_CASE("cli: eval without bindings evaluates constants") {
    const RunResult r = run_cli({"eval", "e^2"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(std::abs(std::strtod(r.output.c_str(), nullptr) - 7.38905609893065) < 1e-9);
}

TEST_CASE("cli: latex subcommand") {
    const RunResult r = run_cli({"latex", "sqrt(x)/2"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(r.output == "\\frac{\\sqrt{x}}{2}\n");
}

TEST_CASE("cli: --latex flag switches output style") {
    const RunResult r = run_cli({"diff", "sin(x)", "x", "--latex"});
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "\\cos\\left(x\\right)"));
}

TEST_CASE("cli: --version and --help") {
    const RunResult version = run_cli({"--version"});
    INFO(version.output);
    CHECK(version.exit_code == 0);
    CHECK(contains(version.output, "0.1.0"));

    const RunResult help = run_cli({"--help"});
    CHECK(help.exit_code == 0);
    CHECK(contains(help.output, "usage"));
    CHECK(contains(help.output, "--range"));
}

// ---------------------------------------------------------------------------
// Error handling and exit codes
// ---------------------------------------------------------------------------

TEST_CASE("cli: parse error exits 1 with a caret diagnostic on stderr") {
    const RunResult stderr_only = run_cli({"simplify", "(x + 1"}, "2>&1 1>/dev/null");
    INFO(stderr_only.output);
    CHECK(stderr_only.exit_code == 1);
    CHECK(contains(stderr_only.output, "error:"));
    CHECK(contains(stderr_only.output, "    (x + 1\n"));
    CHECK(contains(stderr_only.output, "\n    ^")); // caret under the '('

    const RunResult stdout_only = run_cli({"simplify", "(x + 1"}, "2>/dev/null");
    CHECK(stdout_only.exit_code == 1);
    CHECK(stdout_only.output.empty()); // nothing on stdout
}

TEST_CASE("cli: caret span underlines the offending token") {
    const RunResult r = run_cli({"simplify", "\\fraq{1}{2} + x"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 1);
    CHECK(contains(r.output, "\\fraq"));
    CHECK(contains(r.output, "\n    ^~~~~")); // 5-byte span of '\fraq'
}

TEST_CASE("cli: math errors exit 1") {
    const RunResult overflow = run_cli({"simplify", "2^200"});
    INFO(overflow.output);
    CHECK(overflow.exit_code == 1);
    CHECK(contains(overflow.output, "error:"));

    const RunResult unbound = run_cli({"eval", "x^2"});
    INFO(unbound.output);
    CHECK(unbound.exit_code == 1);
    CHECK(contains(unbound.output, "x")); // EvalError names the symbol

    const RunResult div_zero = run_cli({"eval", "1/x", "x=0"});
    CHECK(div_zero.exit_code == 1);
}

TEST_CASE("cli: usage errors exit 2") {
    const RunResult unknown = run_cli({"frobnicate", "x"});
    INFO(unknown.output);
    CHECK(unknown.exit_code == 2);
    CHECK(contains(unknown.output, "frobnicate"));

    const RunResult missing_range = run_cli({"solve", "x = 1", "x", "--range", "5"});
    INFO(missing_range.output);
    CHECK(missing_range.exit_code == 2);

    const RunResult bad_binding = run_cli({"eval", "x", "x=abc"});
    INFO(bad_binding.output);
    CHECK(bad_binding.exit_code == 2);

    const RunResult missing_expr = run_cli({"simplify"});
    CHECK(missing_expr.exit_code == 2);
}

TEST_CASE("cli: ambiguous variable exits 2 and lists the symbols") {
    const RunResult r = run_cli({"solve", "x + y = 1"}, "2>&1 1>/dev/null");
    INFO(r.output);
    CHECK(r.exit_code == 2);
    CHECK(contains(r.output, "x"));
    CHECK(contains(r.output, "y"));

    const RunResult diff_r = run_cli({"diff", "x*y + z"}, "2>&1 1>/dev/null");
    INFO(diff_r.output);
    CHECK(diff_r.exit_code == 2);
    CHECK(contains(diff_r.output, "x"));
    CHECK(contains(diff_r.output, "y"));
    CHECK(contains(diff_r.output, "z"));
}

// ---------------------------------------------------------------------------
// REPL (piped stdin behaves exactly like a terminal session)
// ---------------------------------------------------------------------------

TEST_CASE("cli: piped REPL session with recovery from an error") {
    const RunResult r = run_repl("2x + 3x\n"
                                 "solve x^2 = 4, x\n"
                                 "1 +\n"
                                 "diff sin(x), x\n"
                                 "x^2 = 9\n"
                                 "eval x^2 + 1, x=3\n"
                                 "latex sqrt(x)\n"
                                 "debug 2 + 3x\n"
                                 "help\n"
                                 "quit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "MathSolver"));
    CHECK(contains(r.output, ">>> "));
    CHECK(contains(r.output, "5*x"));                  // bare expression -> simplify
    CHECK(contains(r.output, "x = -2"));               // solve command
    CHECK(contains(r.output, "x = 2"));
    CHECK(contains(r.output, "error:"));               // '1 +' diagnostic ...
    CHECK(contains(r.output, "cos(x)"));               // ... and the session kept going
    CHECK(contains(r.output, "x = 3"));                // bare equation -> auto-solve
    CHECK(contains(r.output, "10"));                   // eval command
    CHECK(contains(r.output, "\\sqrt{x}"));            // latex command
    CHECK(contains(r.output, "(add 2 (mul 3 x))"));    // debug command
    CHECK(contains(r.output, "solve <equation>"));     // help text
}

TEST_CASE("cli: REPL exits cleanly on end of input") {
    const RunResult r = run_repl("1 + 1\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "2"));
}

TEST_CASE("cli: REPL asks for a variable when the equation has several") {
    const RunResult r = run_repl("x + y = 1\nx + 4 = 6\nexit\n");
    INFO(r.output);
    CHECK(r.exit_code == 0);
    CHECK(contains(r.output, "solve <equation>, <variable>"));
    CHECK(contains(r.output, "x = 2")); // session stayed alive and solved
}
