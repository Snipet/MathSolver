#include "mathsolver/parser.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mathsolver/errors.hpp"
#include "mathsolver/expr.hpp"
#include "mathsolver/rational.hpp"

namespace mathsolver {
namespace {

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

enum class Tok {
    Number,
    Symbol,
    Constant,
    Func, // canonical spelling in `text`: sin, arcsin, sec, exp, ln, log, sqrt, abs, ...
    Frac,
    Left,
    Right,
    Plus,
    Minus,
    Star,
    Slash,
    Caret,
    Equals,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Underscore,
    Pipe,
    End,
};

struct Token {
    Tok kind = Tok::End;
    std::size_t begin = 0;
    std::size_t end = 0;
    Rational value{};                     // Tok::Number
    std::string text;                     // Tok::Symbol / Tok::Func
    ConstantId constant = ConstantId::Pi; // Tok::Constant
};

bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alnum(char c) { return is_letter(c) || is_digit(c); }

// ---------------------------------------------------------------------------
// Known names for identifier-run segmentation (DESIGN.md §4).
// ---------------------------------------------------------------------------

enum class NameKind { Function, Greek, Pi, Euler, Imaginary };

struct KnownName {
    std::string_view name;
    NameKind kind;
};

constexpr std::array<KnownName, 52> kKnownNames{{
    {"factorial", NameKind::Function}, {"fibonacci", NameKind::Function},
    {"binomial", NameKind::Function}, {"harmonic", NameKind::Function},
    {"fib", NameKind::Function},
    // Complex accessors (complex-domain Phase 3): Re/Im are capitalized so the
    // lowercase products r*e and i*m keep their existing meaning.
    {"conj", NameKind::Function},   {"Re", NameKind::Function},
    {"Im", NameKind::Function},     {"arg", NameKind::Function},
    {"digamma", NameKind::Function}, {"erfc", NameKind::Function},
    {"erf", NameKind::Function},     {"psi", NameKind::Greek},
    {"arcsinh", NameKind::Function}, {"arccosh", NameKind::Function},
    {"arctanh", NameKind::Function}, {"asinh", NameKind::Function},
    {"acosh", NameKind::Function},  {"atanh", NameKind::Function},
    {"arcsin", NameKind::Function}, {"arccos", NameKind::Function},
    {"arctan", NameKind::Function}, {"asin", NameKind::Function},
    {"acos", NameKind::Function},   {"atan", NameKind::Function},
    {"sinh", NameKind::Function},   {"cosh", NameKind::Function},
    {"tanh", NameKind::Function},   {"sqrt", NameKind::Function},
    {"sin", NameKind::Function},    {"cos", NameKind::Function},
    {"tan", NameKind::Function},    {"sec", NameKind::Function},
    {"csc", NameKind::Function},    {"cot", NameKind::Function},
    {"exp", NameKind::Function},    {"abs", NameKind::Function},
    {"log", NameKind::Function},    {"ln", NameKind::Function},
    {"epsilon", NameKind::Greek},   {"lambda", NameKind::Greek},
    {"alpha", NameKind::Greek},     {"gamma", NameKind::Greek},
    {"delta", NameKind::Greek},     {"theta", NameKind::Greek},
    {"omega", NameKind::Greek},     {"beta", NameKind::Greek},
    {"phi", NameKind::Greek},       {"mu", NameKind::Greek},
    {"pi", NameKind::Pi},           {"e", NameKind::Euler},
    {"i", NameKind::Imaginary},
}};

const KnownName* longest_known_prefix(std::string_view run) {
    const KnownName* best = nullptr;
    for (const auto& k : kKnownNames) {
        if (run.starts_with(k.name) && (best == nullptr || k.name.size() > best->name.size())) {
            best = &k;
        }
    }
    return best;
}

// Backslash function commands accepted per DESIGN.md §4 (note: no \asin/\abs).
constexpr std::array<std::string_view, 16> kBackslashFunctions{
    "sin", "cos", "tan", "arcsin", "arccos", "arctan", "sinh", "cosh",
    "tanh", "sec", "csc", "cot",   "exp",    "ln",     "log",  "sqrt",
};

constexpr std::array<std::string_view, 10> kGreekNames{
    "alpha", "beta", "gamma", "delta", "epsilon", "theta", "lambda", "mu", "phi", "omega",
};

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

class Lexer {
public:
    explicit Lexer(std::string_view src) : src_(src) {}

    std::vector<Token> lex() {
        while (true) {
            skip_whitespace();
            if (pos_ >= src_.size()) break;
            char c = src_[pos_];
            if (is_digit(c)) {
                lex_number();
            } else if (is_letter(c)) {
                lex_identifier_run();
            } else if (c == '\\') {
                lex_command();
            } else if (c == '_') {
                lex_subscript();
            } else {
                lex_operator();
            }
        }
        Token end;
        end.kind = Tok::End;
        end.begin = end.end = src_.size();
        tokens_.push_back(std::move(end));
        return std::move(tokens_);
    }

private:
    void skip_whitespace() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    void push(Tok kind, std::size_t begin, std::size_t end) {
        Token t;
        t.kind = kind;
        t.begin = begin;
        t.end = end;
        tokens_.push_back(std::move(t));
    }

    void push_symbol(std::string name, std::size_t begin, std::size_t end) {
        Token t;
        t.kind = Tok::Symbol;
        t.begin = begin;
        t.end = end;
        t.text = std::move(name);
        tokens_.push_back(std::move(t));
    }

    void push_func(std::string name, std::size_t begin, std::size_t end) {
        Token t;
        t.kind = Tok::Func;
        t.begin = begin;
        t.end = end;
        t.text = std::move(name);
        tokens_.push_back(std::move(t));
    }

    void push_constant(ConstantId id, std::size_t begin, std::size_t end) {
        Token t;
        t.kind = Tok::Constant;
        t.begin = begin;
        t.end = end;
        t.constant = id;
        tokens_.push_back(std::move(t));
    }

    void lex_number() {
        std::size_t begin = pos_;
        while (pos_ < src_.size() && is_digit(src_[pos_])) ++pos_;
        if (pos_ < src_.size() && src_[pos_] == '.') {
            ++pos_;
            if (pos_ >= src_.size() || !is_digit(src_[pos_])) {
                throw ParseError(
                    std::format("malformed number literal '{}'", src_.substr(begin, pos_ - begin)),
                    begin, pos_);
            }
            while (pos_ < src_.size() && is_digit(src_[pos_])) ++pos_;
        }
        const std::size_t mantissa_end = pos_;

        // Scientific notation (v0.4): e/E + optional sign + digits. Only
        // consumed when digits actually follow, so "2e" stays 2*Euler and
        // "2e x" stays 2*e*x.
        long long exp10 = 0;
        bool has_exponent = false;
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            std::size_t p = pos_ + 1;
            bool neg = false;
            if (p < src_.size() && (src_[p] == '+' || src_[p] == '-')) {
                neg = src_[p] == '-';
                ++p;
            }
            if (p < src_.size() && is_digit(src_[p])) {
                std::size_t digits_begin = p;
                while (p < src_.size() && is_digit(src_[p])) ++p;
                has_exponent = true;
                for (std::size_t i = digits_begin; i < p; ++i) {
                    exp10 = exp10 * 10 + (src_[i] - '0');
                    if (exp10 > 1000) break; // clamp; overflow-checked below anyway
                }
                if (neg) exp10 = -exp10;
                pos_ = p;
            }
        }

        Rational value;
        try {
            value = Rational::from_decimal_string(src_.substr(begin, mantissa_end - begin));
            if (has_exponent) {
                value = value * Rational(10).pow(exp10);
            }
        } catch (const ParseError&) {
            // Re-anchor the span into the original input.
            throw ParseError(
                std::format("malformed number literal '{}'", src_.substr(begin, pos_ - begin)),
                begin, pos_);
        } catch (const OverflowError&) {
            throw ParseError(std::format("number '{}' does not fit the exact 64-bit "
                                         "rational range",
                                         src_.substr(begin, pos_ - begin)),
                             begin, pos_);
        }
        Token t;
        t.kind = Tok::Number;
        t.begin = begin;
        t.end = pos_;
        t.value = value;
        tokens_.push_back(std::move(t));
    }

    // Segment a maximal [A-Za-z]+ run greedily, longest known name first at
    // each position; unknown letters become single-letter symbols.
    void lex_identifier_run() {
        const std::size_t run_begin = pos_;
        std::size_t run_end = pos_;
        while (run_end < src_.size() && is_letter(src_[run_end])) ++run_end;

        // Word-variable guard (v0.4, DESIGN.md §4): a run whose greedy
        // segmentation contains 3+ single-letter symbols is almost certainly
        // an intended word ("speed" -> s*p*e*e*d with e as Euler's number) —
        // reject it with a helpful error instead of silently producing wrong
        // math. Runs dominated by known names ("sinx", "pie", "exy") keep
        // the greedy segmentation.
        if (run_end - run_begin >= 3) {
            int soup = 0;
            for (std::size_t p = run_begin; p < run_end;) {
                if (const KnownName* k = longest_known_prefix(src_.substr(p, run_end - p))) {
                    p += k->name.size();
                } else {
                    ++soup;
                    ++p;
                }
            }
            if (soup >= 3) {
                const std::string_view word = src_.substr(run_begin, run_end - run_begin);
                throw ParseError(
                    std::format("unknown name '{}': variables are single letters (x, y), "
                                "greek names (alpha), or subscripted (x_1); for a product, "
                                "write it explicitly ({})",
                                word, [&] {
                                    std::string prod;
                                    for (const char c : word) {
                                        if (!prod.empty()) prod += '*';
                                        prod += c;
                                    }
                                    return prod;
                                }()),
                    run_begin, run_end);
            }
        }

        while (pos_ < run_end) {
            std::string_view rest = src_.substr(pos_, run_end - pos_);
            if (const KnownName* k = longest_known_prefix(rest)) {
                std::size_t begin = pos_;
                pos_ += k->name.size();
                switch (k->kind) {
                case NameKind::Function: push_func(std::string(k->name), begin, pos_); break;
                case NameKind::Greek:
                    // A greek name applied to a parenthesized argument is a
                    // function call when one exists: gamma(x) and psi(x) are
                    // the gamma/digamma functions, bare gamma/psi stay
                    // symbols.
                    if (pos_ == run_end && pos_ < src_.size() && src_[pos_] == '(' &&
                        function_from_name(k->name)) {
                        push_func(std::string(k->name), begin, pos_);
                    } else {
                        push_symbol(std::string(k->name), begin, pos_);
                    }
                    break;
                case NameKind::Pi: push_constant(ConstantId::Pi, begin, pos_); break;
                case NameKind::Euler: push_constant(ConstantId::E, begin, pos_); break;
                case NameKind::Imaginary:
                    push_constant(ConstantId::I, begin, pos_);
                    break;
                }
            } else {
                push_symbol(std::string(1, src_[pos_]), pos_, pos_ + 1);
                ++pos_;
            }
        }
    }

    // Unicode math input (v0.4, DESIGN.md §4): the characters that phone
    // keyboards and copy-pasted text produce. Each match consumes the whole
    // UTF-8 sequence and pushes ordinary tokens carrying its span, so caret
    // diagnostics keep working. Returns false when the sequence is not one
    // of ours (caller emits the standard unexpected-character error).
    bool lex_unicode(std::size_t begin) {
        const auto starts = [&](std::string_view seq) {
            return src_.compare(pos_, seq.size(), seq) == 0;
        };
        const auto emit = [&](Tok kind, std::size_t len) {
            push(kind, begin, pos_ += len);
            return true;
        };

        if (starts("×") || starts("⋅") || starts("·")) { // × ⋅ ·
            return emit(Tok::Star, starts("×") || starts("·") ? 2 : 3);
        }
        if (starts("÷")) return emit(Tok::Slash, 2);  // ÷
        if (starts("−")) return emit(Tok::Minus, 3);  // − (minus sign)
        if (starts("√")) {                            // √ behaves like sqrt
            pos_ += 3;
            push_func("sqrt", begin, pos_);
            return true;
        }
        if (starts("π")) { // π
            pos_ += 2;
            push_constant(ConstantId::Pi, begin, pos_);
            return true;
        }
        // Greek letters with the same names as the §4 backslash/ASCII table.
        static constexpr std::pair<std::string_view, std::string_view> k_greek[] = {
            {"α", "alpha"}, {"β", "beta"},   {"γ", "gamma"},
            {"δ", "delta"}, {"ε", "epsilon"}, {"θ", "theta"},
            {"λ", "lambda"}, {"μ", "mu"},     {"φ", "phi"},
            {"ω", "omega"},
        };
        for (const auto& [seq, name] : k_greek) {
            if (starts(seq)) {
                pos_ += seq.size();
                push_symbol(std::string(name), begin, pos_);
                return true;
            }
        }
        // Superscript run (optionally signed) becomes ^ [-] digits.
        static constexpr std::pair<std::string_view, char> k_super[] = {
            {"⁰", '0'}, {"¹", '1'}, {"²", '2'}, {"³", '3'},
            {"⁴", '4'}, {"⁵", '5'}, {"⁶", '6'}, {"⁷", '7'},
            {"⁸", '8'}, {"⁹", '9'},
        };
        const auto super_digit = [&]() -> std::optional<char> {
            for (const auto& [seq, d] : k_super) {
                if (starts(seq)) {
                    pos_ += seq.size();
                    return d;
                }
            }
            return std::nullopt;
        };
        const bool super_minus = starts("⁻"); // ⁻
        if (super_minus) pos_ += 3;
        if (std::optional<char> first = super_digit()) {
            std::string digits(1, *first);
            while (std::optional<char> d = super_digit()) digits.push_back(*d);
            push(Tok::Caret, begin, begin);
            if (super_minus) push(Tok::Minus, begin, begin);
            Token t;
            t.kind = Tok::Number;
            t.begin = begin;
            t.end = pos_;
            t.value = Rational::from_decimal_string(digits);
            tokens_.push_back(std::move(t));
            return true;
        }
        if (super_minus) pos_ -= 3; // lone ⁻ with no digits: not ours

        if (starts("°")) { // ° = *(pi/180), as synthesized tokens
            pos_ += 2;
            push(Tok::Star, begin, pos_);
            push(Tok::LParen, begin, pos_);
            push_constant(ConstantId::Pi, begin, pos_);
            push(Tok::Slash, begin, pos_);
            Token t;
            t.kind = Tok::Number;
            t.begin = begin;
            t.end = pos_;
            t.value = Rational(180);
            tokens_.push_back(std::move(t));
            push(Tok::RParen, begin, pos_);
            return true;
        }
        if (starts("≤") || starts("≥") || starts("≠")) { // ≤ ≥ ≠
            throw ParseError("inequalities belong to 'solve' (e.g. solve x^2 < 4), "
                             "not a bare expression",
                             begin, pos_ + 3);
        }
        return false;
    }

    void lex_command() {
        std::size_t begin = pos_;
        ++pos_; // backslash
        // Spacing commands \, \; \! \: are backslash + punctuation.
        if (pos_ < src_.size() &&
            (src_[pos_] == ',' || src_[pos_] == ';' || src_[pos_] == '!' || src_[pos_] == ':')) {
            ++pos_;
            return;
        }
        std::size_t name_begin = pos_;
        while (pos_ < src_.size() && is_letter(src_[pos_])) ++pos_;
        std::string_view name = src_.substr(name_begin, pos_ - name_begin);
        if (name.empty()) {
            std::size_t end = pos_ < src_.size() ? pos_ + 1 : pos_;
            throw ParseError(std::format("unknown command '{}'", src_.substr(begin, end - begin)),
                             begin, end);
        }
        if (name == "quad" || name == "qquad") return; // spacing, ignored
        if (name == "frac") {
            push(Tok::Frac, begin, pos_);
            return;
        }
        if (name == "cdot" || name == "times") {
            push(Tok::Star, begin, pos_);
            return;
        }
        if (name == "div") {
            push(Tok::Slash, begin, pos_);
            return;
        }
        if (name == "left") {
            push(Tok::Left, begin, pos_);
            return;
        }
        if (name == "right") {
            push(Tok::Right, begin, pos_);
            return;
        }
        if (name == "pi") {
            push_constant(ConstantId::Pi, begin, pos_);
            return;
        }
        for (std::string_view g : kGreekNames) {
            if (name == g) {
                push_symbol(std::string(name), begin, pos_);
                return;
            }
        }
        for (std::string_view f : kBackslashFunctions) {
            if (name == f) {
                push_func(std::string(name), begin, pos_);
                return;
            }
        }
        throw ParseError(std::format("unknown command '{}'", src_.substr(begin, pos_ - begin)),
                         begin, pos_);
    }

    // `_` folds a subscript into the preceding Symbol token (x_1, x_{12},
    // x_a); after \log it becomes an Underscore token for the parser's
    // explicit-base handling; anywhere else it is an error.
    void lex_subscript() {
        std::size_t us = pos_;
        Token* prev = tokens_.empty() ? nullptr : &tokens_.back();
        if (prev != nullptr && prev->kind == Tok::Func && prev->text == "log") {
            push(Tok::Underscore, us, us + 1);
            ++pos_;
            return;
        }
        if (prev == nullptr || prev->kind != Tok::Symbol) {
            throw ParseError("subscript '_' is only allowed on a symbol", us, us + 1);
        }
        ++pos_;
        std::string sub;
        if (pos_ < src_.size() && src_[pos_] == '{') {
            ++pos_;
            while (pos_ < src_.size() && is_alnum(src_[pos_])) sub += src_[pos_++];
            if (pos_ >= src_.size() || src_[pos_] != '}') {
                throw ParseError("missing '}' in subscript", us, pos_);
            }
            if (sub.empty()) {
                throw ParseError("empty subscript", us, pos_ + 1);
            }
            ++pos_;
        } else if (pos_ < src_.size() && is_digit(src_[pos_])) {
            // Unbraced digit subscript: one maximal digit run (x_12 == x_{12}).
            while (pos_ < src_.size() && is_digit(src_[pos_])) sub += src_[pos_++];
        } else if (pos_ < src_.size() && is_letter(src_[pos_])) {
            // Unbraced letter subscript: exactly one letter (x_ab == x_a * b).
            sub = src_[pos_++];
        } else {
            throw ParseError("missing subscript after '_'", us, us + 1);
        }
        prev->text += '_';
        prev->text += sub;
        prev->end = pos_;
    }

    void lex_operator() {
        char c = src_[pos_];
        std::size_t begin = pos_;
        switch (c) {
        case '+': push(Tok::Plus, begin, ++pos_); return;
        case '-': push(Tok::Minus, begin, ++pos_); return;
        case '*':
            ++pos_;
            if (pos_ < src_.size() && src_[pos_] == '*') {
                ++pos_;
                push(Tok::Caret, begin, pos_); // ** is a synonym for ^
            } else {
                push(Tok::Star, begin, pos_);
            }
            return;
        case '/': push(Tok::Slash, begin, ++pos_); return;
        case '^': push(Tok::Caret, begin, ++pos_); return;
        case '=': push(Tok::Equals, begin, ++pos_); return;
        case '(': push(Tok::LParen, begin, ++pos_); return;
        case ')': push(Tok::RParen, begin, ++pos_); return;
        case '{': push(Tok::LBrace, begin, ++pos_); return;
        case '}': push(Tok::RBrace, begin, ++pos_); return;
        case '[': push(Tok::LBracket, begin, ++pos_); return;
        case ']': push(Tok::RBracket, begin, ++pos_); return;
        case ',': push(Tok::Comma, begin, ++pos_); return;
        case '|': push(Tok::Pipe, begin, ++pos_); return;
        default:
            if (lex_unicode(begin)) {
                return;
            }
            throw unexpected_character(begin);
        }
    }

    // Build the diagnostic for an unexpected byte. Consumes the whole UTF-8
    // sequence (lead byte + continuation bytes 0x80-0xBF) so the span covers
    // the entire character rather than a single byte, and renders non-ASCII
    // bytes as \xNN escapes so the message is always valid UTF-8 on stderr
    // (a lone 0xCE byte from e.g. an unhandled greek letter would not be).
    ParseError unexpected_character(std::size_t begin) {
        const auto lead = static_cast<unsigned char>(src_[begin]);
        std::size_t expected = 1;
        if (lead >= 0xF0 && lead <= 0xF7) {
            expected = 4;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            expected = 3;
        } else if (lead >= 0xC0 && lead <= 0xDF) {
            expected = 2;
        }
        std::size_t end = begin + 1;
        while (end < src_.size() && end < begin + expected &&
               (static_cast<unsigned char>(src_[end]) & 0xC0) == 0x80) {
            ++end;
        }
        std::string shown;
        for (std::size_t i = begin; i < end; ++i) {
            const auto b = static_cast<unsigned char>(src_[i]);
            if (b >= 0x20 && b < 0x7F) {
                shown += static_cast<char>(b);
            } else {
                shown += std::format("\\x{:02X}", static_cast<unsigned>(b));
            }
        }
        return ParseError(std::format("unexpected character '{}'", shown), begin, end);
    }

    std::string_view src_;
    std::size_t pos_ = 0;
    std::vector<Token> tokens_;
};

// ---------------------------------------------------------------------------
// Recursive-descent parser (grammar in DESIGN.md §4)
// ---------------------------------------------------------------------------

// Maximum recursive-descent nesting depth. Input nested deeper than this
// throws a ParseError instead of overflowing the stack (DESIGN.md §10: a
// clean exit-1 diagnostic, and the REPL session survives). Far below the
// point at which the native call stack overflows; no hand-written
// expression nests anywhere near this deep.
constexpr std::size_t k_max_parse_depth = 256;

// RAII nesting-depth counter for the recursive descent. Incremented on entry
// to each recursion level and decremented on exit; throws once the fixed
// limit is exceeded so the deep AST is never built (which also protects the
// downstream simplify/print recursion).
class DepthGuard {
public:
    DepthGuard(std::size_t& depth, const Token& tok) : depth_(depth) {
        if (++depth_ > k_max_parse_depth) {
            --depth_;
            throw ParseError("expression too deeply nested", tok.begin, tok.end);
        }
    }
    ~DepthGuard() { --depth_; }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;

private:
    std::size_t& depth_;
};

class Parser {
public:
    explicit Parser(std::string_view src) : src_(src), tokens_(Lexer(src).lex()) {}

    const Token& peek() const { return tokens_[idx_]; }

    const Token& advance() {
        const Token& t = tokens_[idx_];
        if (t.kind != Tok::End) ++idx_;
        return t;
    }

    std::string_view slice(const Token& t) const { return src_.substr(t.begin, t.end - t.begin); }

    [[noreturn]] void fail_unexpected(const Token& t) const {
        if (t.kind == Tok::End) {
            throw ParseError("unexpected end of input", t.begin, t.end);
        }
        throw ParseError(std::format("unexpected '{}'", slice(t)), t.begin, t.end);
    }

    void expect_end() const {
        const Token& t = peek();
        if (t.kind != Tok::End) fail_unexpected(t);
    }

    // expr := term (('+' | '-') term)*
    Expr parse_expr() {
        Expr cur = parse_term();
        while (true) {
            if (peek().kind == Tok::Plus) {
                advance();
                cur = make_add({cur, parse_term()});
            } else if (peek().kind == Tok::Minus) {
                advance();
                cur = make_sub(cur, parse_term());
            } else {
                break;
            }
        }
        return cur;
    }

private:
    static bool starts_atom(Tok k) {
        switch (k) {
        case Tok::Number:
        case Tok::Symbol:
        case Tok::Constant:
        case Tok::Func:
        case Tok::Frac:
        case Tok::LParen:
        case Tok::LBrace:
        case Tok::LBracket:
        case Tok::Pipe: // bare |x| absolute value opens in operand position
        case Tok::Left: return true;
        default: return false;
        }
    }

    // term := unary (('*' | '/' | implicit) unary)*
    Expr parse_term() {
        Expr cur = parse_unary();
        while (true) {
            if (peek().kind == Tok::Star) {
                advance();
                cur = make_mul({cur, parse_unary()});
            } else if (peek().kind == Tok::Slash) {
                advance();
                cur = make_div(cur, parse_unary());
            } else if (starts_atom(peek().kind) &&
                       !(peek().kind == Tok::Pipe && bar_depth_ > 0)) {
                // Implicit multiplication — but inside an open |...| group a
                // '|' closes the group instead of starting a new one.
                cur = make_mul({cur, parse_unary()});
            } else {
                break;
            }
        }
        return cur;
    }

    // unary := ('-' | '+') unary | postfix     (below ^: -x^2 == -(x^2))
    Expr parse_unary() {
        // Every recursion cycle (paren/group nesting, unary-sign chains, and
        // right-associative power towers) passes through parse_unary exactly
        // once per level, so guarding here bounds the whole descent.
        DepthGuard guard(depth_, peek());
        if (peek().kind == Tok::Minus) {
            advance();
            return make_neg(parse_unary());
        }
        if (peek().kind == Tok::Plus) {
            advance();
            return parse_unary();
        }
        return parse_postfix();
    }

    // postfix := atom ('^' unary)?             (right-assoc via the recursion)
    Expr parse_postfix() {
        Expr base = parse_atom();
        if (peek().kind == Tok::Caret) {
            advance();
            return make_pow(std::move(base), parse_unary());
        }
        return base;
    }

    Expr parse_atom() {
        const Token& t = peek();
        switch (t.kind) {
        case Tok::Number: return make_num(advance().value);
        case Tok::Symbol: return make_sym(advance().text);
        case Tok::Constant: return make_const(advance().constant);
        case Tok::LParen: return parse_group(Tok::RParen, ')');
        case Tok::LBrace: return parse_group(Tok::RBrace, '}');
        case Tok::LBracket: return parse_group(Tok::RBracket, ']');
        case Tok::Left: return parse_left_right();
        case Tok::Pipe: return parse_bare_abs();
        case Tok::Frac: return parse_frac();
        case Tok::Func: return parse_function_application();
        default: fail_unexpected(t);
        }
    }

    // Bare |expr| absolute value. A '|' in operand position opens; the next
    // top-level '|' closes. Nested bars work when the inner pair is also in
    // operand position (|x - |y||); genuinely ambiguous forms need abs() or
    // parentheses (documented in DESIGN.md §4). While a bar group is open,
    // '|' never starts an implicit-multiplication atom (see starts_atom's
    // caller) — it closes.
    Expr parse_bare_abs() {
        Token open = advance();
        ++bar_depth_;
        Expr e = parse_expr();
        --bar_depth_;
        const Token& t = peek();
        if (t.kind != Tok::Pipe) {
            if (t.kind == Tok::End) {
                throw ParseError("missing closing '|'", open.begin, open.end);
            }
            throw ParseError(std::format("expected '|', found '{}'", slice(t)), t.begin, t.end);
        }
        advance();
        return make_fn(FunctionId::Abs, std::move(e));
    }

    // '(' expr ')' and the {} / [] grouping forms.
    Expr parse_group(Tok close, char close_char) {
        Token open = advance();
        Expr e = parse_expr();
        const Token& t = peek();
        if (t.kind != close) {
            if (t.kind == Tok::End) {
                throw ParseError(std::format("missing '{}'", close_char), open.begin, open.end);
            }
            throw ParseError(std::format("expected '{}', found '{}'", close_char, slice(t)),
                             t.begin, t.end);
        }
        advance();
        return e;
    }

    Expr parse_left_right() {
        Token left = advance(); // \left
        Tok close = Tok::End;
        char close_char = ')';
        bool is_abs = false;
        switch (peek().kind) {
        case Tok::LParen: close = Tok::RParen, close_char = ')'; break;
        case Tok::LBracket: close = Tok::RBracket, close_char = ']'; break;
        case Tok::LBrace: close = Tok::RBrace, close_char = '}'; break;
        case Tok::Pipe: close = Tok::Pipe, close_char = '|', is_abs = true; break;
        default:
            throw ParseError("expected a delimiter after '\\left'",
                             peek().kind == Tok::End ? left.begin : peek().begin,
                             peek().kind == Tok::End ? left.end : peek().end);
        }
        Token open = advance();
        Expr e = parse_expr();
        if (is_abs) {
            // \left| u \right| is absolute value (the LaTeX printer's form).
            e = make_fn(FunctionId::Abs, std::move(e));
        }
        if (peek().kind != Tok::Right) {
            if (peek().kind == Tok::End) {
                throw ParseError("missing '\\right'", left.begin, open.end);
            }
            fail_unexpected(peek());
        }
        Token right = advance(); // \right
        const Token& d = peek();
        if (d.kind != close) {
            if (d.kind == Tok::End) {
                throw ParseError(std::format("missing '{}' after '\\right'", close_char),
                                 right.begin, right.end);
            }
            throw ParseError(
                std::format("mismatched '\\right' delimiter: expected '{}', found '{}'",
                            close_char, slice(d)),
                d.begin, d.end);
        }
        advance();
        return e;
    }

    Expr parse_frac() {
        Token frac = advance();
        expect_brace_open(frac, "after '\\frac'");
        Expr numerator = parse_expr();
        expect_brace_close("after '\\frac' numerator");
        expect_brace_open(frac, "before '\\frac' denominator");
        Expr denominator = parse_expr();
        expect_brace_close("after '\\frac' denominator");
        return make_div(std::move(numerator), std::move(denominator));
    }

    void expect_brace_open(const Token& anchor, std::string_view where) {
        const Token& t = peek();
        if (t.kind != Tok::LBrace) {
            if (t.kind == Tok::End) {
                throw ParseError(std::format("expected '{{' {}", where), anchor.begin, anchor.end);
            }
            throw ParseError(std::format("expected '{{' {}", where), t.begin, t.end);
        }
        advance();
    }

    void expect_brace_close(std::string_view where) {
        const Token& t = peek();
        if (t.kind != Tok::RBrace) {
            throw ParseError(std::format("missing '}}' {}", where), t.begin, t.end);
        }
        advance();
    }

    static bool is_leaf(Tok k) {
        return k == Tok::Number || k == Tok::Symbol || k == Tok::Constant;
    }

    Expr leaf(const Token& t) {
        switch (t.kind) {
        case Tok::Number: return make_num(t.value);
        case Tok::Symbol: return make_sym(t.text);
        default: return make_const(t.constant);
        }
    }

    Expr parse_function_application() {
        Token fn = advance(); // Tok::Func
        std::string name = fn.text;

        // binomial(n, k) is the one two-argument function; it rewrites at
        // parse time to gamma(n+1) / (gamma(k+1) gamma(n-k+1)), so integer
        // arguments fold exactly and symbolic ones stay meaningful.
        if (name == "binomial") {
            const Token& open = peek();
            if (open.kind != Tok::LParen) {
                throw ParseError("binomial needs two parenthesized arguments: "
                                 "binomial(n, k)",
                                 fn.begin, fn.end);
            }
            advance();
            Expr n = parse_expr();
            const Token& comma = peek();
            if (comma.kind != Tok::Comma) {
                throw ParseError("binomial needs two arguments: binomial(n, k)",
                                 comma.begin, comma.end);
            }
            advance();
            Expr k = parse_expr();
            const Token& close = peek();
            if (close.kind != Tok::RParen) {
                throw ParseError("missing ')' after binomial(n, k)",
                                 close.begin, close.end);
            }
            advance();
            const Expr n1 = make_add({n, make_num(1)});
            const Expr k1 = make_add({k, make_num(1)});
            const Expr nk1 = make_add({n, make_neg(k), make_num(1)});
            return make_div(make_fn(FunctionId::Gamma, n1),
                            make_mul({make_fn(FunctionId::Gamma, k1),
                                      make_fn(FunctionId::Gamma, nk1)}));
        }

        // \sqrt[n]{x} root index.
        Expr index;
        if (name == "sqrt" && peek().kind == Tok::LBracket) {
            Token open = advance();
            index = parse_expr();
            const Token& t = peek();
            if (t.kind != Tok::RBracket) {
                if (t.kind == Tok::End) {
                    throw ParseError("missing ']' after '\\sqrt' index", open.begin, open.end);
                }
                throw ParseError(std::format("expected ']', found '{}'", slice(t)), t.begin,
                                 t.end);
            }
            advance();
        }

        // \log_b / \log_{b} explicit base.
        Expr log_base;
        if (name == "log" && peek().kind == Tok::Underscore) {
            Token us = advance();
            if (peek().kind == Tok::LBrace) {
                log_base = parse_group(Tok::RBrace, '}');
            } else if (is_leaf(peek().kind)) {
                log_base = leaf(advance());
            } else {
                const Token& t = peek();
                throw ParseError("expected a base after '_' in 'log'",
                                 t.kind == Tok::End ? us.begin : t.begin,
                                 t.kind == Tok::End ? us.end : t.end);
            }
        }

        // \sin^{n} notation; \sin^{-1} means arcsin (likewise cos/tan).
        Expr exponent;
        if (peek().kind == Tok::Caret) {
            advance();
            exponent = parse_unary();
            if (exponent->kind() == Kind::Number && exponent->number() == Rational(-1) &&
                (name == "sin" || name == "cos" || name == "tan")) {
                name.insert(0, "a");
                exponent = nullptr;
            }
        }

        Expr arg = parse_function_argument(fn);
        Expr result = apply_function(name, std::move(arg), std::move(index), std::move(log_base));
        if (exponent) result = make_pow(std::move(result), std::move(exponent));
        return result;
    }

    // Bare-form argument: a single group if the next token opens one, else a
    // tight factor sequence of leaf atoms (each with an optional power).
    Expr parse_function_argument(const Token& fn) {
        switch (peek().kind) {
        case Tok::LParen: return parse_group(Tok::RParen, ')');
        case Tok::LBrace: return parse_group(Tok::RBrace, '}');
        case Tok::Left: return parse_left_right(); // \sin\left(x\right) — §5 LaTeX output
        case Tok::Frac: return parse_frac();
        case Tok::Func:
            if (peek().text == "sqrt") return parse_function_application();
            break;
        default: break;
        }
        std::vector<Expr> factors;
        while (is_leaf(peek().kind)) {
            Expr f = leaf(advance());
            if (peek().kind == Tok::Caret) {
                advance();
                f = make_pow(std::move(f), parse_unary());
            }
            factors.push_back(std::move(f));
        }
        if (factors.empty()) {
            throw ParseError(std::format("function '{}' has no argument", fn.text), fn.begin,
                             fn.end);
        }
        return make_mul(std::move(factors));
    }

    // Parse-time rewrites (DESIGN.md §2): no Sqrt/Exp/Log/sec/csc/cot nodes.
    static Expr apply_function(const std::string& name, Expr arg, Expr index, Expr log_base) {
        if (auto fid = function_from_name(name)) return make_fn(*fid, std::move(arg));
        if (name == "sec") return make_pow(make_fn(FunctionId::Cos, std::move(arg)), make_num(-1));
        if (name == "csc") return make_pow(make_fn(FunctionId::Sin, std::move(arg)), make_num(-1));
        if (name == "cot") return make_pow(make_fn(FunctionId::Tan, std::move(arg)), make_num(-1));
        if (name == "exp") return make_exp(std::move(arg));
        if (name == "factorial") {
            // x! = gamma(x + 1); integer arguments fold exactly downstream.
            return make_fn(FunctionId::Gamma,
                           make_add({std::move(arg), make_num(1)}));
        }
        if (name == "sqrt") {
            if (index) return make_pow(std::move(arg), make_div(make_num(1), std::move(index)));
            return make_sqrt(std::move(arg));
        }
        // log: base 10 unless an explicit base was given.
        Expr base = log_base ? std::move(log_base) : make_num(10);
        return make_div(make_fn(FunctionId::Ln, std::move(arg)),
                        make_fn(FunctionId::Ln, std::move(base)));
    }

    std::string_view src_;
    std::vector<Token> tokens_;
    std::size_t idx_ = 0;
    std::size_t depth_ = 0;
    int bar_depth_ = 0; // open bare |...| groups (v0.4 absolute-value bars)
};

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::variant<Expr, Equation> parse_input(std::string_view src) {
    Parser p(src);
    Expr lhs = p.parse_expr();
    if (p.peek().kind == Tok::Equals) {
        p.advance();
        Expr rhs = p.parse_expr();
        p.expect_end(); // a second top-level '=' fails here
        return Equation{std::move(lhs), std::move(rhs)};
    }
    p.expect_end();
    return lhs;
}

Expr parse_expression(std::string_view src) {
    Parser p(src);
    Expr e = p.parse_expr();
    p.expect_end(); // an '=' fails here
    return e;
}

Equation parse_equation(std::string_view src) {
    Parser p(src);
    Expr lhs = p.parse_expr();
    const Token& t = p.peek();
    if (t.kind != Tok::Equals) {
        if (t.kind == Tok::End) {
            throw ParseError("expected '=' in equation", t.begin, t.end);
        }
        throw ParseError(std::format("expected '=', found '{}'", p.slice(t)), t.begin, t.end);
    }
    p.advance();
    Expr rhs = p.parse_expr();
    p.expect_end();
    return Equation{std::move(lhs), std::move(rhs)};
}

} // namespace mathsolver
