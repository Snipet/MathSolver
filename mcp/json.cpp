// Minimal JSON value, parser, and serializer (json.hpp).

#include "json.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <format>

namespace mathsolver::mcp {

namespace {

const std::string k_empty_string;
const JsonArray k_empty_array;
const JsonObject k_empty_object;
const Json k_null;

} // namespace

Json::Type Json::type() const {
    switch (value_.index()) {
        case 0: return Type::Null;
        case 1: return Type::Bool;
        case 2: return Type::Number;
        case 3: return Type::String;
        case 4: return Type::Array;
        default: return Type::Object;
    }
}

bool Json::as_bool(bool fallback) const {
    return is_bool() ? std::get<bool>(value_) : fallback;
}

double Json::as_number(double fallback) const {
    return is_number() ? std::get<double>(value_) : fallback;
}

const std::string& Json::as_string() const {
    return is_string() ? std::get<std::string>(value_) : k_empty_string;
}

std::string Json::as_string(std::string fallback) const {
    return is_string() ? std::get<std::string>(value_) : std::move(fallback);
}

const JsonArray& Json::as_array() const {
    return is_array() ? std::get<JsonArray>(value_) : k_empty_array;
}

const JsonObject& Json::as_object() const {
    return is_object() ? std::get<JsonObject>(value_) : k_empty_object;
}

const Json& Json::operator[](std::string_view key) const {
    if (is_object()) {
        for (const auto& [k, v] : std::get<JsonObject>(value_)) {
            if (k == key) {
                return v;
            }
        }
    }
    return k_null;
}

bool Json::contains(std::string_view key) const {
    if (!is_object()) {
        return false;
    }
    for (const auto& [k, v] : std::get<JsonObject>(value_)) {
        if (k == key) {
            return true;
        }
    }
    return false;
}

Json& Json::set(std::string key, Json value) {
    if (!is_object()) {
        value_ = JsonObject{};
    }
    auto& obj = std::get<JsonObject>(value_);
    for (auto& [k, v] : obj) {
        if (k == key) {
            v = std::move(value);
            return *this;
        }
    }
    obj.emplace_back(std::move(key), std::move(value));
    return *this;
}

// --- serialization ---------------------------------------------------------

namespace {

void dump_string(const std::string& s, std::string& out) {
    out += '"';
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
                    out += std::format("\\u{:04x}",
                                       static_cast<unsigned char>(c));
                } else {
                    out += c; // UTF-8 bytes pass through unescaped
                }
        }
    }
    out += '"';
}

void dump_number(double n, std::string& out) {
    if (!std::isfinite(n)) {
        out += "null"; // JSON has no inf/nan
        return;
    }
    // Integral values print without a decimal point so JSON-RPC ids and
    // counts round-trip as integers.
    if (n == std::floor(n) && std::abs(n) < 1e15) {
        out += std::format("{}", static_cast<long long>(n));
    } else {
        out += std::format("{}", n);
    }
}

void dump_value(const Json& v, std::string& out); // fwd

} // namespace

std::string Json::dump() const {
    std::string out;
    dump_value(*this, out);
    return out;
}

namespace {

void dump_value(const Json& v, std::string& out) {
    switch (v.type()) {
        case Json::Type::Null:
            out += "null";
            break;
        case Json::Type::Bool:
            out += v.as_bool() ? "true" : "false";
            break;
        case Json::Type::Number:
            dump_number(v.as_number(), out);
            break;
        case Json::Type::String:
            dump_string(v.as_string(), out);
            break;
        case Json::Type::Array: {
            out += '[';
            const JsonArray& a = v.as_array();
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (i > 0) out += ',';
                dump_value(a[i], out);
            }
            out += ']';
            break;
        }
        case Json::Type::Object: {
            out += '{';
            const JsonObject& o = v.as_object();
            for (std::size_t i = 0; i < o.size(); ++i) {
                if (i > 0) out += ',';
                dump_string(o[i].first, out);
                out += ':';
                dump_value(o[i].second, out);
            }
            out += '}';
            break;
        }
    }
}

// --- parsing ---------------------------------------------------------------

class Parser {
  public:
    Parser(std::string_view text, std::string& error)
        : s_(text), error_(error) {}

    std::optional<Json> run() {
        skip_ws();
        auto v = parse_value();
        if (!v) {
            return std::nullopt;
        }
        skip_ws();
        if (pos_ != s_.size()) {
            return fail("trailing characters after JSON value");
        }
        return v;
    }

  private:
    std::string_view s_;
    std::string& error_;
    std::size_t pos_ = 0;
    int depth_ = 0;
    static constexpr int k_max_depth = 200;

    std::nullopt_t fail(const std::string& msg) {
        if (error_.empty()) {
            error_ = std::format("{} at byte {}", msg, pos_);
        }
        return std::nullopt;
    }

    void skip_ws() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }

    std::optional<Json> parse_value() {
        if (pos_ >= s_.size()) {
            return fail("unexpected end of input");
        }
        switch (s_[pos_]) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': {
                auto str = parse_string();
                if (!str) return std::nullopt;
                return Json(std::move(*str));
            }
            case 't':
            case 'f': return parse_bool();
            case 'n': return parse_null();
            default: return parse_number();
        }
    }

    std::optional<Json> parse_null() {
        if (s_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return Json(nullptr);
        }
        return fail("invalid literal");
    }

    std::optional<Json> parse_bool() {
        if (s_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return Json(true);
        }
        if (s_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return Json(false);
        }
        return fail("invalid literal");
    }

    std::optional<Json> parse_number() {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') {
                ++pos_;
            } else {
                break;
            }
        }
        if (pos_ == start) {
            return fail("invalid value");
        }
        const std::string token(s_.substr(start, pos_ - start));
        try {
            std::size_t consumed = 0;
            const double v = std::stod(token, &consumed);
            if (consumed != token.size()) {
                return fail("invalid number");
            }
            return Json(v);
        } catch (const std::exception&) {
            return fail("invalid number");
        }
    }

    /// Parse a JSON string starting at the opening quote.
    std::optional<std::string> parse_string() {
        if (peek() != '"') {
            fail("expected string");
            return std::nullopt;
        }
        ++pos_;
        std::string out;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (pos_ >= s_.size()) break;
                const char e = s_[pos_++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        if (!parse_unicode(out)) {
                            return std::nullopt;
                        }
                        break;
                    }
                    default:
                        fail("invalid escape");
                        return std::nullopt;
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character in string");
                return std::nullopt;
            } else {
                out += c;
            }
        }
        fail("unterminated string");
        return std::nullopt;
    }

    /// A \uXXXX escape (already past the 'u'), UTF-16 surrogate pairs joined,
    /// encoded as UTF-8.
    bool parse_unicode(std::string& out) {
        auto hex4 = [&](std::uint32_t& cp) -> bool {
            if (pos_ + 4 > s_.size()) {
                fail("truncated \\u escape");
                return false;
            }
            cp = 0;
            for (int i = 0; i < 4; ++i) {
                const char h = s_[pos_++];
                cp <<= 4;
                if (h >= '0' && h <= '9') {
                    cp |= static_cast<std::uint32_t>(h - '0');
                } else if (h >= 'a' && h <= 'f') {
                    cp |= static_cast<std::uint32_t>(h - 'a' + 10);
                } else if (h >= 'A' && h <= 'F') {
                    cp |= static_cast<std::uint32_t>(h - 'A' + 10);
                } else {
                    fail("invalid \\u escape");
                    return false;
                }
            }
            return true;
        };
        std::uint32_t cp = 0;
        if (!hex4(cp)) {
            return false;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            // High surrogate: expect a following \uXXXX low surrogate.
            if (pos_ + 2 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                pos_ += 2;
                std::uint32_t lo = 0;
                if (!hex4(lo)) {
                    return false;
                }
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else {
                    cp = 0xFFFD; // unpaired: replacement character
                }
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD; // lone low surrogate
        }
        // Encode cp as UTF-8.
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return true;
    }

    std::optional<Json> parse_array() {
        if (++depth_ > k_max_depth) {
            return fail("nesting too deep");
        }
        ++pos_; // consume '['
        JsonArray out;
        skip_ws();
        if (peek() == ']') {
            ++pos_;
            --depth_;
            return Json(std::move(out));
        }
        while (true) {
            skip_ws();
            auto v = parse_value();
            if (!v) {
                return std::nullopt;
            }
            out.push_back(std::move(*v));
            skip_ws();
            const char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == ']') {
                ++pos_;
                --depth_;
                return Json(std::move(out));
            }
            return fail("expected ',' or ']'");
        }
    }

    std::optional<Json> parse_object() {
        if (++depth_ > k_max_depth) {
            return fail("nesting too deep");
        }
        ++pos_; // consume '{'
        JsonObject out;
        skip_ws();
        if (peek() == '}') {
            ++pos_;
            --depth_;
            return Json(std::move(out));
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                return fail("expected object key string");
            }
            auto key = parse_string();
            if (!key) {
                return std::nullopt;
            }
            skip_ws();
            if (peek() != ':') {
                return fail("expected ':' after object key");
            }
            ++pos_;
            skip_ws();
            auto v = parse_value();
            if (!v) {
                return std::nullopt;
            }
            out.emplace_back(std::move(*key), std::move(*v));
            skip_ws();
            const char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == '}') {
                ++pos_;
                --depth_;
                return Json(std::move(out));
            }
            return fail("expected ',' or '}'");
        }
    }
};

} // namespace

std::optional<Json> Json::parse(std::string_view text, std::string& error) {
    error.clear();
    Parser p(text, error);
    return p.run();
}

} // namespace mathsolver::mcp
