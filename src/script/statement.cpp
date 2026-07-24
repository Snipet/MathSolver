#include "mathsolver/script/statement.hpp"

#include <cctype>

namespace mathsolver::script {

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
        --e;
    }
    return std::string{s.substr(b, e - b)};
}

bool is_assignment_line(std::string_view line) {
    const std::size_t pos = line.find(":=");
    return pos != std::string_view::npos && !trim(line.substr(0, pos)).empty();
}

LeadingWord leading_word(std::string_view line) {
    LeadingWord w;
    if (!line.empty() && std::isalpha(static_cast<unsigned char>(line[0])) != 0) {
        w.end = 1;
        while (w.end < line.size() &&
               std::isalnum(static_cast<unsigned char>(line[w.end])) != 0) {
            ++w.end;
        }
    }
    w.word = std::string{line.substr(0, w.end)};
    w.terminated = w.end == line.size() ||
                   std::isspace(static_cast<unsigned char>(line[w.end])) != 0;
    return w;
}

Statement parse_statement(
    std::string_view line,
    const std::function<bool(std::string_view)>& is_command) {
    // Assignment is recognized at the input layer, before the parser or
    // command dispatch ever see the text (variable-assignment spec §2).
    if (is_assignment_line(line)) {
        const std::size_t pos = line.find(":=");
        return Statement{StatementKind::Assignment, trim(line.substr(0, pos)),
                         trim(line.substr(pos + 2))};
    }

    const LeadingWord w = leading_word(line);
    if (w.terminated && is_command(w.word)) {
        return Statement{StatementKind::Command, w.word, trim(line.substr(w.end))};
    }
    return Statement{StatementKind::Bare, "", trim(line)};
}

} // namespace mathsolver::script
