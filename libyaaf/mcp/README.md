# MCP Protocol Support

This directory contains yaaf's native Model Context Protocol client. The implementation intentionally keeps protocol work in C++ and exposes only a small Lua bridge for tool registry integration.

For user-facing MCP setup and examples, see [MCP Tools](https://svnscha.github.io/yaaf/mcp/) and [Examples](https://svnscha.github.io/yaaf/examples/).

## Configuration

Yaaf reads the VS Code MCP configuration shape unchanged. CLI and Lua script runs select the config path in this precedence order: explicit `--mcp <path>`, then `YAAF_MCP_FILE` from the process environment or `.env`, then auto-discovery of `.yaaf/mcp.json` in the current working directory. The path selection changes only where the file is loaded from; it does not add yaaf-specific fields to the MCP JSON format.

Supported top-level fields:

- `servers`: required object keyed by server id.

Supported server types:

- `http`: JSON-RPC over HTTP POST.
- `sse`: accepted as an HTTP-style server entry; responses with `text/event-stream` are parsed for `data:` payloads.
- `stdio`: JSON-RPC over newline-delimited stdio on Windows, macOS, and Linux builds.

Supported server fields:

- `url` for `http` and `sse` servers.
- `headers` for `http` and `sse` servers.
- `command` and `args` for `stdio` servers.
- `env` and `envFile` for `stdio` process environment overrides.

Variable expansion:

- `${workspaceFolder}` is expanded in string values.
- `${env:NAME}` is expanded from the process environment.

Doctor output redacts values under `headers` and `env`.

## Protocol Lifecycle

Implemented JSON-RPC lifecycle:

- `initialize` request with the latest generated supported protocol version.
- Negotiated `protocolVersion` validation against the generated schema registry.
- `notifications/initialized` notification after successful initialization.
- Session caching per configured server id.

HTTP transport behavior:

- Sends `Accept: application/json, text/event-stream`.
- Sends `MCP-Protocol-Version` after negotiation.
- Stores and re-sends `Mcp-Session-Id` when returned by the server.
- Parses normal JSON responses and SSE-style `data:` response payloads.

Stdio transport behavior:

- Starts the configured command through the native process API.
- Sends one JSON-RPC message per line.
- Reads line-delimited JSON responses.
- Waits for responses matching the request id.

## Tool Support

Implemented MCP methods:

- `tools/list`, including `nextCursor` pagination.
- `tools/call`.

Tool descriptors are mapped to yaaf tool descriptors with:

- server id
- remote tool name
- local tool name as `<server>.<tool>`
- title
- description
- input schema
- output schema
- annotations

Tool results are normalized to yaaf's existing tool result shape:

- `success` is false when MCP returns `isError: true` or when the native call throws.
- text content blocks are joined with newlines.
- non-text content blocks are preserved as JSON text.
- `structuredContent` is used as JSON text when no content blocks are present.
- raw MCP call results are kept in result metadata.

## Fixture Servers

The repository includes thin real MCP servers under `mcp-servers/` for integration testing without external APIs:

- `hello_stdio.py`: stdio JSON-RPC server.
- `hello_http.py`: HTTP POST JSON-RPC server.
- `hello_sse.py`: HTTP POST server returning `text/event-stream` `data:` payloads.

Run them with `uv --directory ./mcp-servers run python <server>.py` plus the HTTP/SSE port arguments documented in `mcp-servers/README.md`. They expose `hello` and `repeat` tools and back the real transport tests in `tests/integration/mcp_client_tests.cpp` plus the real CLI and Lua fixture flows in `tests/integration/mcp_fixture_tests.cpp`.

Linux uses the same `uv`-based stdio fixture flow as macOS. There are no extra Linux-only prerequisites for the stdio fixture tests beyond having `uv` available.

The stdio tests write a VS Code-shaped MCP JSON file and pass it explicitly so the yaaf runtime starts the server from that config, matching the user-facing flow. The optional HTTP and SSE fixture stack can still be prestarted through `docker compose -f docker-compose.fixture-stack.yml up` for manual transport debugging, proxy inspection, and smoke checks that intentionally exercise real local servers.

## Hosting MCP Servers (Host Bridge)

Yaaf can host MCP servers from Lua scripts using the stdio transport. This allows local scripts and tools to be exposed as standard MCP servers that any MCP client (Claude, etc.) can connect to.

### Entry Point

Host yaaf MCP servers through the `run` subcommand:

```bash
yaaf run ./examples/mcp_host_example.lua
```

The Lua script registers tools and prompts, then calls `mcp.host_stdio()` to start listening on stdin/stdout. The script blocks until the client disconnects.

### Hosted Methods Support Matrix

| MCP Method | Support | Notes |
| --- | --- | --- |
| `initialize` | ✓ Full | Protocol version negotiation, server info |
| `tools/list` | ✓ Full | Lists tools from yaaf registry (built-in, custom, and MCP) |
| `tools/call` | ✓ Full | Executes tools via `tool.execute()` |
| `prompts/list` | ✓ Full | Lists registered prompts |
| `prompts/get` | ✓ Full | Executes prompt handlers |
| `resources/*` | ✗ Not implemented | Out of scope for v1 |
| `sampling/*` | ✗ Not implemented | Out of scope for v1 |
| Other methods | ✗ Not implemented | Future enhancements |

Hosted tools can be:
- Built-in tools like `echo`
- Custom tools registered via `tool.register()`
- Remote MCP tools fetched from configured MCP servers and re-exposed

Use the optional `{tools?, prompts?}` parameters to `mcp.host_stdio()` to select which tools and prompts are exposed; if omitted, all registered tools and prompts are exposed.

### Compared to Client Mode

Yaaf has two MCP modes:

- **Client mode** (default): Yaaf consumes remote MCP servers configured in `mcp.json` and uses their tools locally
- **Host mode** (via `mcp.host_stdio()`): Yaaf becomes the server; a client (Claude or another tool) connects to yaaf and calls registered tools and prompts

## Lua And CLI Integration

The native client is exposed to Lua through `require("mcp")` with:

- `mcp.config()`
- `mcp.servers()`
- `mcp.diagnostics()`
- `mcp.list_tools(server_id)`
- `mcp.call_tool(server_id, tool_name, arguments)`
- `mcp.register_prompt(descriptor)` — register prompts for hosting
- `mcp.host_stdio(options)` — start stdio MCP server

The Lua tool registry exposes MCP tools as `<server>.<tool>`, so configured MCP tools work with existing `ask`, `chat`, and `agent` `--tool` flows. `doctor` now actively initializes each configured server, runs `tools/list`, and reports per-server initialize status plus discovered tool names alongside the MCP config report and generated protocol metadata.

## Schema Support

Protocol version metadata is generated from the official versioned MCP JSON schemas. The stable abstract contract is in `mcp_schema.h`; generated code provides a factory, registry, and one backend implementation per schema version under `schema/schema-<version>.cpp`.

Current generated protocol versions:

- `2024-11-05`
- `2025-03-26`
- `2025-06-18`
- `2025-11-25`

Refresh generated schema files with:

```powershell
./scripts/UpdateMcpSchemas.ps1
```

## Not Implemented Yet

Yaaf currently does not implement these MCP features as native client APIs:

- prompts
- resources
- resource subscriptions
- roots
- sampling
- elicitation
- logging level control
- task APIs
- progress or cancellation handling
- server-initiated notifications beyond accepting initialization flow
- JSON-RPC batching
- OAuth or other auth flows
- long-lived SSE event streams beyond extracting a response payload from an HTTP response

Some of these methods may appear in generated schema metadata because the schemas know about them. That metadata means the protocol version recognizes the method; it does not mean yaaf has a high-level feature integration for it.

## Maintenance Checklist

When MCP behavior changes:

1. Update this README's support matrix before changing higher-level docs.
2. Update `MCP_PLAN.md` or the root README only when user-facing behavior changes.
3. Add or update focused tests in `tests/plain/mcp_config_schema_tests.cpp`, `tests/mock/mcp_protocol_tests.cpp`, or `tests/integration/mcp_client_tests.cpp` for any newly supported method, transport behavior, or config field.
4. Prefer the real `mcp-servers/` fixtures for transport/tool-list/tool-call regression tests; keep mocked HTTP callbacks for narrow error and edge cases.
5. Rebuild `libyaaf_tests` and run `./build/tests/Debug/libyaaf_tests --gtest_filter=Mcp*Tests.*:Mcp*IntegrationTests.*`.
6. Run full tests and covdbg when native MCP behavior changes, especially transport, schema generation, or Lua bridge code.
