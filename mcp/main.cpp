// MathSolver MCP server executable.
//
// Speaks the Model Context Protocol over stdio: newline-delimited JSON-RPC
// 2.0, one message per line (the MCP stdio transport). Each request line is
// handed to Server::handle(); any response line is written to stdout and the
// stream is flushed so the client sees it immediately. All computation is
// local — the server holds no network sockets and links the same CAS and
// plugin code as the CLI and the WebAssembly build.
//
// Configure an MCP client to launch this binary; see docs/MCP.md.

#include <iostream>
#include <string>

#include "server.hpp"

int main() {
    // MCP frames messages by newline, so line buffering is exactly right;
    // untie from cin since we drive the loop ourselves.
    std::ios::sync_with_stdio(false);

    mathsolver::mcp::Server server;
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto response = server.handle(line);
        if (response) {
            std::cout << *response << '\n' << std::flush;
        }
    }
    return 0;
}
