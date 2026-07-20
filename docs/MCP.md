# MathSolver MCP server

MathSolver ships an [Model Context Protocol](https://modelcontextprotocol.io)
server (`mathsolver_mcp`) that exposes the computer-algebra engine and the
computation plugins as MCP tools. An MCP-capable client (Claude Desktop, an
IDE agent, a custom host) can then ask MathSolver to simplify, differentiate,
integrate, solve, take limits and transforms, and run the DSP / control /
linear-algebra / PDE / FEM plugins — all computed locally, with no network
access and no dependencies beyond the C++ standard library.

The server is written in C++ and links the same core and plugin code as the
CLI and the WebAssembly build, so results are identical across all three
front ends.

## Building

```sh
cmake -B build
cmake --build build -j --target mathsolver_mcp
# binary: build/mathsolver_mcp
```

## Transport

The server speaks JSON-RPC 2.0 over **stdio**, newline-delimited (one JSON
message per line — the MCP stdio transport). It reads requests on stdin and
writes responses on stdout, flushing each line. Notifications (requests with
no `id`, e.g. `notifications/initialized`) are processed and produce no reply,
per the spec.

Supported methods: `initialize`, `tools/list`, `tools/call`, and `ping`.

## Tools

| Tool | Arguments | Result |
|---|---|---|
| `simplify` | `expression` | canonical form (plain + LaTeX) |
| `expand` | `expression` | products/powers multiplied out |
| `factor` | `expression` | factored over the rationals |
| `to_latex` | `expression` | LaTeX rendering, unsimplified |
| `differentiate` | `expression`, `variable?` | symbolic derivative |
| `integrate` | `expression`, `variable?`, `from?`, `to?` | antiderivative, or a definite integral with both bounds |
| `solve` | `equation`, `variable?` | exact or numeric solutions |
| `evaluate` | `expression`, `bindings?` | numeric value; `bindings` is `{"x":3,"y":0.5}` or `"x=3,y=0.5"` |
| `series` | `expression`, `variable?`, `center?`, `order?` | Taylor series (center `inf` for expansion at infinity) |
| `limit` | `expression`, `variable`, `point`, `direction?` | limit at a point or `inf`/`-inf` |
| `laplace` | `expression`, `variable?` | Laplace transform f(t) → F(s) |
| `inverse_laplace` | `expression`, `variable?` | inverse transform F(s) → f(t) |
| `list_plugins` | — | catalog of plugins, commands, and example arguments |
| `plugin_command` | `plugin`, `command`, `args` | run a plugin command; `args` is an array of strings or one comma-joined string |

Expressions use MathSolver's grammar (see the main README): LaTeX (`\frac`,
`\sin`) or plain ASCII (`1/2`, `sin(x)`), with implicit multiplication (`2x`,
`(x+1)(x-2)`). When `variable` is omitted the single free symbol is inferred;
an expression with several free symbols returns a clear error asking for the
variable.

Computation failures (an integral with no elementary form, a parse error, a
plugin usage error) come back as normal tool results with `isError: true` and
an explanatory message, so the model sees them — they are not JSON-RPC
protocol errors. Only malformed JSON-RPC and unknown methods use the
JSON-RPC `error` channel.

## Configuring a client

Point your MCP client at the built binary. For a Claude Desktop-style
`mcpServers` config:

```json
{
  "mcpServers": {
    "mathsolver": {
      "command": "/absolute/path/to/build/mathsolver_mcp"
    }
  }
}
```

The server takes no arguments and reads no configuration files or environment
variables.

## Example session

Bytes on stdin (each line one JSON-RPC message), responses on stdout:

```jsonc
// → initialize
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{}}}
// ← {"jsonrpc":"2.0","id":0,"result":{"protocolVersion":"2025-06-18","capabilities":{"tools":{}},"serverInfo":{"name":"mathsolver","version":"0.5.0"}}}

// → (notification: no reply)
{"jsonrpc":"2.0","method":"notifications/initialized"}

// → solve x^2 = 4
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"solve","arguments":{"equation":"x^2 = 4","variable":"x"}}}
// ← {"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"x = -2\nx = 2\nmethod: quadratic formula"}],"isError":false}}

// → an eigendecomposition through the linalg plugin
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"plugin_command","arguments":{"plugin":"linalg","command":"eig","args":["[2 1; 1 2]"]}}}
// ← {"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"eigendecomposition (2x2, exact)\nEigenpairs:\n  # | lambda | eigenvector(s)\n  1 | 1 | (1, -1)\n  2 | 3 | (1, 1)\n  ..."}],"isError":false}}
```

You can drive it by hand for a quick check:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"simplify","arguments":{"expression":"2x + 3x"}}}' \
  | build/mathsolver_mcp
```

## Implementation

- `mcp/json.{hpp,cpp}` — a small dependency-free JSON value, parser, and
  serializer (the rest of MathSolver only writes JSON; the transport also has
  to read it).
- `mcp/server.{hpp,cpp}` — the protocol dispatch and tool handlers, exposed as
  a pure `Server::handle(request) → optional<response>` so the whole surface
  is unit-tested (`tests/test_mcp.cpp`) without spawning a process.
- `mcp/main.cpp` — the newline-delimited stdio loop.
