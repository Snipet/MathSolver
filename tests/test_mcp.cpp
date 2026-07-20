// MCP server tests: the JSON library round-trips, the JSON-RPC handshake,
// tools/list, and each tool through Server::handle() (the pure request ->
// response function the stdio loop wraps).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <format>
#include <optional>
#include <string>

#include "../mcp/json.hpp"
#include "../mcp/server.hpp"

using namespace mathsolver::mcp;
using Catch::Matchers::ContainsSubstring;

namespace {

/// Parse a response line back into Json for structured assertions.
Json parse(const std::string& text) {
    std::string err;
    auto v = Json::parse(text, err);
    REQUIRE(err.empty());
    REQUIRE(v.has_value());
    return *v;
}

/// The text of the first content block of a tools/call result.
std::string call_text(Server& s, const std::string& name,
                      const std::string& arguments_json, bool& is_error) {
    const std::string req = std::format(
        R"mcp({{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{{"name":{},"arguments":{}}}}})mcp",
        Json(name).dump(), arguments_json);
    const auto resp = s.handle(req);
    REQUIRE(resp.has_value());
    const Json j = parse(*resp);
    const Json& result = j["result"];
    is_error = result["isError"].as_bool();
    return result["content"].as_array().at(0)["text"].as_string();
}

std::string call_ok(Server& s, const std::string& name,
                    const std::string& args) {
    bool is_error = false;
    const std::string text = call_text(s, name, args, is_error);
    INFO(name << " -> " << text);
    CHECK_FALSE(is_error);
    return text;
}

} // namespace

TEST_CASE("mcp json: round-trips values and escapes") {
    std::string err;
    const auto v = Json::parse(
        R"mcp({"a":1,"b":[true,null,-2.5],"s":"x\"y\né","n":{}})mcp", err);
    REQUIRE(err.empty());
    REQUIRE(v.has_value());
    CHECK(v->is_object());
    CHECK((*v)["a"].as_number() == 1.0);
    CHECK((*v)["b"].as_array().size() == 3);
    CHECK((*v)["b"].as_array()[0].as_bool());
    CHECK((*v)["b"].as_array()[1].is_null());
    CHECK((*v)["s"].as_string() == "x\"y\n\xc3\xa9"); // é as UTF-8
    // Integers serialize without a decimal point (ids round-trip).
    CHECK(Json(42).dump() == "42");
    CHECK(Json(-3).dump() == "-3");
    CHECK(Json("a\"b").dump() == "\"a\\\"b\"");
}

TEST_CASE("mcp json: rejects malformed input") {
    std::string err;
    CHECK_FALSE(Json::parse("{", err).has_value());
    CHECK_FALSE(err.empty());
    CHECK_FALSE(Json::parse("[1,2,]", err).has_value());
    CHECK_FALSE(Json::parse("nul", err).has_value());
    CHECK_FALSE(Json::parse("{\"a\":1} trailing", err).has_value());
    CHECK_FALSE(Json::parse("", err).has_value());
}

TEST_CASE("mcp: initialize handshake") {
    Server s;
    const auto resp = s.handle(
        R"mcp({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{}}})mcp");
    REQUIRE(resp.has_value());
    const Json j = parse(*resp);
    CHECK(j["jsonrpc"].as_string() == "2.0");
    CHECK(j["id"].as_number() == 0);
    const Json& r = j["result"];
    CHECK(r["protocolVersion"].as_string() == "2025-06-18"); // echoed
    CHECK(r["serverInfo"]["name"].as_string() == "mathsolver");
    CHECK(r["capabilities"].contains("tools"));
}

TEST_CASE("mcp: notifications get no response") {
    Server s;
    CHECK_FALSE(
        s.handle(R"mcp({"jsonrpc":"2.0","method":"notifications/initialized"})mcp")
            .has_value());
    // A blank keep-alive line is silent too.
    CHECK_FALSE(s.handle("   ").has_value());
}

TEST_CASE("mcp: unknown method is a JSON-RPC error") {
    Server s;
    const auto resp = s.handle(
        R"mcp({"jsonrpc":"2.0","id":7,"method":"no/such","params":{}})mcp");
    REQUIRE(resp.has_value());
    const Json j = parse(*resp);
    CHECK(j["id"].as_number() == 7);
    CHECK(j["error"]["code"].as_number() == -32601);
}

TEST_CASE("mcp: malformed request line is a parse error") {
    Server s;
    const auto resp = s.handle("{not json");
    REQUIRE(resp.has_value());
    const Json j = parse(*resp);
    CHECK(j["error"]["code"].as_number() == -32700);
    CHECK(j["id"].is_null());
}

TEST_CASE("mcp: tools/list advertises the catalog with schemas") {
    Server s;
    const auto resp =
        s.handle(R"mcp({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})mcp");
    REQUIRE(resp.has_value());
    const Json j = parse(*resp);
    const Json& tools = j["result"]["tools"];
    REQUIRE(tools.is_array());
    CHECK(tools.as_array().size() >= 13);
    bool found_solve = false;
    for (const Json& t : tools.as_array()) {
        CHECK(t["name"].is_string());
        CHECK(t["description"].is_string());
        CHECK(t["inputSchema"]["type"].as_string() == "object");
        if (t["name"].as_string() == "solve") {
            found_solve = true;
            CHECK(t["inputSchema"]["properties"].contains("equation"));
        }
    }
    CHECK(found_solve);
}

TEST_CASE("mcp: core CAS tools compute correctly") {
    Server s;
    CHECK_THAT(call_ok(s, "simplify", R"mcp({"expression":"2x + 3x"})mcp"),
               ContainsSubstring("5*x"));
    CHECK_THAT(call_ok(s, "expand", R"mcp({"expression":"(x+1)^2"})mcp"),
               ContainsSubstring("x^2 + 2*x + 1"));
    CHECK_THAT(call_ok(s, "factor", R"mcp({"expression":"x^2 - 5x + 6"})mcp"),
               ContainsSubstring("(x - 3)"));
    CHECK_THAT(call_ok(s, "to_latex", R"mcp({"expression":"sqrt(x)/2"})mcp"),
               ContainsSubstring("\\frac"));
    CHECK_THAT(call_ok(s, "differentiate", R"mcp({"expression":"sin(x^2)"})mcp"),
               ContainsSubstring("2*x*cos(x^2)"));
    CHECK_THAT(call_ok(s, "integrate", R"mcp({"expression":"x^3"})mcp"),
               ContainsSubstring("x^4/4 + C"));
    CHECK_THAT(
        call_ok(s, "integrate",
                R"mcp({"expression":"sin(x)","from":"0","to":"pi"})mcp"),
        ContainsSubstring("2"));
    CHECK_THAT(call_ok(s, "series", R"mcp({"expression":"sin(x)","order":5})mcp"),
               ContainsSubstring("x^5/120"));
    CHECK_THAT(call_ok(s, "limit",
                       R"mcp({"expression":"sin(x)/x","variable":"x","point":"0"})mcp"),
               ContainsSubstring("limit = 1"));
    CHECK_THAT(call_ok(s, "laplace", R"mcp({"expression":"e^(-t) sin(2t)"})mcp"),
               ContainsSubstring("(s + 1)^2 + 4"));
    CHECK_THAT(call_ok(s, "inverse_laplace",
                       R"mcp({"expression":"1/(s^2 + 2s + 5)"})mcp"),
               ContainsSubstring("e^(-t)*sin(2*t)/2"));
}

TEST_CASE("mcp: solve and evaluate") {
    Server s;
    const std::string sol =
        call_ok(s, "solve", R"mcp({"equation":"x^2 = 4","variable":"x"})mcp");
    CHECK_THAT(sol, ContainsSubstring("x = -2"));
    CHECK_THAT(sol, ContainsSubstring("x = 2"));

    CHECK_THAT(call_ok(s, "evaluate",
                       R"mcp({"expression":"x^2 + y","bindings":{"x":3,"y":0.5}})mcp"),
               ContainsSubstring("9.5"));
    // String-form bindings work too.
    CHECK_THAT(call_ok(s, "evaluate",
                       R"mcp({"expression":"2*a + 1","bindings":"a=5"})mcp"),
               ContainsSubstring("11"));
}

TEST_CASE("mcp: tool errors are result-level, not protocol errors") {
    Server s;
    bool is_error = false;
    // e^(x^2) has no elementary antiderivative: reported honestly.
    const std::string t =
        call_text(s, "integrate", R"mcp({"expression":"e^(x^2)"})mcp", is_error);
    CHECK(is_error);
    CHECK_THAT(t, ContainsSubstring("unable to integrate"));

    // A parse error inside a tool is also a result-level error.
    bool e2 = false;
    call_text(s, "simplify", R"mcp({"expression":"2 +"})mcp", e2);
    CHECK(e2);

    // Missing required argument.
    bool e3 = false;
    const std::string t3 = call_text(s, "simplify", R"mcp({})mcp", e3);
    CHECK(e3);
    CHECK_THAT(t3, ContainsSubstring("expression"));

    // Unknown tool -> result-level error, not a -32601.
    bool e4 = false;
    const std::string t4 = call_text(s, "nope", R"mcp({})mcp", e4);
    CHECK(e4);
    CHECK_THAT(t4, ContainsSubstring("unknown tool"));
}

TEST_CASE("mcp: plugin tools expose the registry") {
    Server s;
    const std::string list = call_ok(s, "list_plugins", R"mcp({})mcp");
    CHECK_THAT(list, ContainsSubstring("dsp"));
    CHECK_THAT(list, ContainsSubstring("linalg"));

    // A numeric linear solve through the plugin, args as an array.
    const std::string solve = call_ok(
        s, "plugin_command",
        R"mcp({"plugin":"linalg","command":"solve","args":["[2 1; 1 3]","[3 5]"]})mcp");
    CHECK_THAT(solve, ContainsSubstring("(0.8, 1.4)"));

    // Args as a single comma-joined string (matrix commas stay intact).
    const std::string det = call_ok(
        s, "plugin_command",
        R"mcp({"plugin":"linalg","command":"det","args":"[a b; c d]"})mcp");
    CHECK_THAT(det, ContainsSubstring("a*d"));

    // A plugin usage error surfaces as an isError result.
    bool is_error = false;
    call_text(s, "plugin_command",
              R"mcp({"plugin":"linalg","command":"solve","args":["[1 2; 2 4]","[1 1]"]})mcp",
              is_error);
    CHECK(is_error);

    bool unknown = false;
    const std::string u = call_text(
        s, "plugin_command", R"mcp({"plugin":"nope","command":"x"})mcp", unknown);
    CHECK(unknown);
    CHECK_THAT(u, ContainsSubstring("no plugin named"));
}

TEST_CASE("mcp: variable inference and its failure") {
    Server s;
    // One free symbol: inferred.
    CHECK_THAT(call_ok(s, "differentiate", R"mcp({"expression":"x^3"})mcp"),
               ContainsSubstring("3*x^2"));
    // Two free symbols with no variable: a clear error.
    bool is_error = false;
    const std::string t =
        call_text(s, "differentiate", R"mcp({"expression":"x*y"})mcp", is_error);
    CHECK(is_error);
    CHECK_THAT(t, ContainsSubstring("cannot infer"));
}
