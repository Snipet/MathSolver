#pragma once

// MathSolver MCP server: exposes the computer-algebra engine and the plugin
// registry as Model Context Protocol tools over JSON-RPC 2.0.
//
// The protocol handling is a pure function of the request text so it can be
// unit-tested without any I/O: feed a single JSON-RPC request line to
// handle(), get back the response line (or nullopt for a notification, which
// takes no reply). mcp/main.cpp wraps this in the newline-delimited stdio
// loop that MCP's stdio transport specifies.

#include <optional>
#include <string>
#include <string_view>

namespace mathsolver::mcp {

class Server {
  public:
    /// Handle one JSON-RPC request. Returns the response line to write, or
    /// nullopt for a notification (a request without an id) which the
    /// protocol says must not be answered. Never throws.
    std::optional<std::string> handle(std::string_view request_line);
};

} // namespace mathsolver::mcp
