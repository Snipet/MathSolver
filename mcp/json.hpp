#pragma once

// A minimal, dependency-free JSON value with a recursive-descent parser and
// a compact serializer — just enough to speak JSON-RPC 2.0 for the MCP
// server. The rest of MathSolver only ever *writes* JSON (hand-rolled
// strings); the MCP transport also has to *read* it, which is what this adds.
//
// Design notes:
//   - Objects preserve insertion order (a vector of pairs, linear lookup):
//     JSON-RPC messages are small, and stable key order keeps output
//     deterministic and easy to test.
//   - Numbers are stored as double. That is exact for every integer a
//     JSON-RPC id or an MCP argument realistically carries; on output an
//     integral value prints without a decimal point so ids round-trip
//     cleanly (1 -> "1", not "1.0").

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace mathsolver::mcp {

class Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::vector<std::pair<std::string, Json>>;

class Json {
  public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool b) : value_(b) {}
    Json(double n) : value_(n) {}
    Json(int n) : value_(static_cast<double>(n)) {}
    Json(long long n) : value_(static_cast<double>(n)) {}
    Json(std::size_t n) : value_(static_cast<double>(n)) {}
    Json(const char* s) : value_(std::string(s)) {}
    Json(std::string s) : value_(std::move(s)) {}
    Json(JsonArray a) : value_(std::move(a)) {}
    Json(JsonObject o) : value_(std::move(o)) {}

    Type type() const;
    bool is_null() const { return type() == Type::Null; }
    bool is_bool() const { return type() == Type::Bool; }
    bool is_number() const { return type() == Type::Number; }
    bool is_string() const { return type() == Type::String; }
    bool is_array() const { return type() == Type::Array; }
    bool is_object() const { return type() == Type::Object; }

    /// Typed accessors with a fallback for the wrong type (never throw).
    bool as_bool(bool fallback = false) const;
    double as_number(double fallback = 0.0) const;
    const std::string& as_string() const;               ///< "" if not a string.
    std::string as_string(std::string fallback) const;  ///< copy with fallback.
    const JsonArray& as_array() const;                   ///< empty if not array.
    const JsonObject& as_object() const;                 ///< empty if not object.

    /// Object member lookup (returns a null Json when absent / not an object).
    const Json& operator[](std::string_view key) const;
    bool contains(std::string_view key) const;

    /// Object builder helpers (turn this into an object if it is not one).
    Json& set(std::string key, Json value);

    /// Serialize to compact JSON (no whitespace).
    std::string dump() const;

    /// Parse `text`. Returns std::nullopt and sets `error` on malformed input;
    /// trailing non-whitespace after one value is an error.
    static std::optional<Json> parse(std::string_view text, std::string& error);

  private:
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray,
                 JsonObject>
        value_;
};

} // namespace mathsolver::mcp
