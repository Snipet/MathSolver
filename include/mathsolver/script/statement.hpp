#pragma once

// Statement shapes: what one console input line *is*, before anything is
// parsed or executed (docs/proposals/console-language.md §1).
//
// Every surface re-derives this classification today — the CLI in
// apps/main.cpp, the web console in web/src/lib/notebook/run.ts, the terminal
// app in apps/ink/src/core/intent.ts — from three independent sets of string
// tests, and they drift. This header is the one description of the shape; the
// verb table itself stays with the host until the builtin registry lands
// (proposal §5, Phase 2), which is why classification takes an `is_command`
// predicate rather than owning a list of words.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace mathsolver::script {

/// Strip leading and trailing ASCII whitespace.
std::string trim(std::string_view s);

/// What shape a line has.
enum class StatementKind {
    Assignment,  ///< `name := value`
    Command,     ///< a known leading verb word, then its arguments
    Bare,        ///< anything else: an expression or an equation
};

struct Statement {
    StatementKind kind = StatementKind::Bare;
    /// Command: the verb word. Assignment: the target text as typed (the
    /// canonical symbol name comes from validate_assignment_target, which
    /// accepts both the `x_{max}` and `x_max` spellings). Bare: empty.
    std::string head;
    /// Command: the argument text. Assignment: the value text. Bare: the
    /// whole line. Always trimmed.
    std::string rest;
};

/// True when the first `:=` splits `line` into a non-empty left part. A ':'
/// not followed by '=', or a `:=` with nothing on its left, is not an
/// assignment and falls through to the parser to keep its existing lex error.
bool is_assignment_line(std::string_view line);

/// The leading word of a line and whether it stands alone.
struct LeadingWord {
    std::string word;        ///< empty when the line does not start with a letter
    bool terminated = false; ///< word ran to end of line, or the next byte is space
    std::size_t end = 0;     ///< byte offset just past the word
};

/// Lex the leading word: one letter followed by letters *and digits*, so
/// `stirling2` is a single word. '_' does not continue a word, so `x_max`
/// lexes to `x`. A line that does not start with a letter yields an empty
/// word with `terminated == false`, which is what keeps a numeric line from
/// ever being mistaken for a command.
LeadingWord leading_word(std::string_view line);

/// Classify an already-trimmed, non-empty line. `is_command` decides which
/// leading words select command mode.
Statement parse_statement(std::string_view line,
                          const std::function<bool(std::string_view)>& is_command);

} // namespace mathsolver::script
