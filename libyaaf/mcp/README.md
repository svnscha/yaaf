# MCP Protocol Support

This directory contains yaaf's native Model Context Protocol client. The implementation intentionally keeps protocol work in C++ and exposes only a small Lua bridge for tool registry integration.

For user-facing MCP setup and examples, see [docs/mcp.md](../../docs/mcp.md) and [docs/examples.md](../../docs/examples.md).

## Configuration

Yaaf reads the VS Code MCP configuration shape unchanged only when a config file path is supplied. CLI and Lua script runs can point at any file containing that same shape with `--mcp <path>`, or `YAAF_MCP_FILE` can provide the same path from the process environment or `.env`. The path selection changes only where the file is loaded from; it does not add yaaf-specific fields to the MCP JSON format.

Supported top-level fields:

- `servers`: required object keyed by server id.

Supported server types:

- `http`: JSON-RPC over HTTP POST.
- `sse`: accepted as an HTTP-style server entry; responses with `text/event-stream` are parsed for `data:` payloads.
- `stdio`: JSON-RPC over newline-delimited stdio on Windows and macOS builds.

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

Run them with `uv --directory ./mcp-servers run python <server>.py` plus the HTTP/SSE port arguments documented in `mcp-servers/README.md`. They expose `hello` and `repeat` tools and back the real transport tests in `tests/integration/mcp_client_tests.cpp`.

The stdio tests write a VS Code-shaped MCP JSON file and pass it explicitly so the yaaf runtime starts the server from that config, matching the user-facing flow. HTTP and SSE tests expect the fixture servers to be prestarted, usually through `docker compose -f docker-compose.mitmproxy.yml up`, and read fixture endpoint overrides from test-only `.env`/environment variables.

## Lua And CLI Integration

The native client is exposed to Lua through `require("mcp")` with:

- `mcp.config()`
- `mcp.servers()`
- `mcp.list_tools(server_id)`
- `mcp.call_tool(server_id, tool_name, arguments)`

The Lua tool registry exposes MCP tools as `<server>.<tool>`, so configured MCP tools work with existing `ask`, `chat`, and `agent` `--tool` flows. `doctor` includes MCP config, server diagnostics, and generated protocol metadata.

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
