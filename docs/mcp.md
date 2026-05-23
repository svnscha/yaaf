# MCP Tools

Yaaf loads MCP tools only when a config file path is supplied. Use `--mcp <path>` with `ask`, `chat`, `agent`, or `run`, or set `YAAF_MCP_FILE` in the process environment or `.env` file.

The file must use the VS Code MCP configuration shape unchanged. Yaaf does not auto-discover `.vscode/mcp.json`, and it does not introduce yaaf-specific MCP config keys. If you want to reuse a workspace `.vscode/mcp.json`, point to it explicitly:

```powershell
yaaf ask --mcp ./.vscode/mcp.json --tool docs.lookup "Look up the install steps."
```

## Supported Config Shape

Supported top-level fields:

- `servers`: required object keyed by server id.

Supported server types:

- `http`: JSON-RPC over HTTP POST.
- `sse`: accepted as an HTTP-style server entry; `text/event-stream` responses are parsed for `data:` payloads.
- `stdio`: JSON-RPC over newline-delimited stdio on Windows, macOS, and Linux builds.

Supported server fields:

- `url` for `http` and `sse` servers.
- `headers` for `http` and `sse` servers.
- `command` and `args` for `stdio` servers.
- `env` and `envFile` for `stdio` process environment overrides.

Minimal HTTP server:

```json
{
  "servers": {
    "docs": {
      "type": "http",
      "url": "http://127.0.0.1:3000/mcp"
    }
  }
}
```

Minimal stdio server:

```json
{
  "servers": {
    "hello": {
      "type": "stdio",
      "command": "uv",
      "args": ["--directory", "${workspaceFolder}/mcp-servers", "run", "python", "hello_stdio.py"]
    }
  }
}
```

For the repository fixture servers, Linux uses the same `uv`-based stdio setup as macOS. There are no extra Linux-only stdio fixture prerequisites beyond having `uv` available.

## Variable Substitution

Yaaf expands variables recursively in string values inside each server object.

Supported variables:

- `${workspaceFolder}`: replaced with the current workspace root using forward slashes.
- `${env:NAME}`: replaced with the process environment variable value, or an empty string if it is not set.

Example with workspace and environment variables:

```json
{
  "servers": {
    "docs": {
      "type": "http",
      "url": "${env:DOCS_MCP_URL}",
      "headers": {
        "Authorization": "Bearer ${env:DOCS_MCP_TOKEN}"
      }
    },
    "hello": {
      "type": "stdio",
      "command": "uv",
      "args": ["--directory", "${workspaceFolder}/mcp-servers", "run", "python", "hello_stdio.py"],
      "envFile": "${workspaceFolder}/.env",
      "env": {
        "YAAF_HELLO_MODE": "local"
      }
    }
  }
}
```

`envFile` lines use `NAME=value`. Empty names, comment lines starting with `#`, and lines without `=` are ignored. Inline `env` values override values loaded from `envFile`.

Values under `headers` and `env` are redacted from `doctor` output.

## Tool Names

MCP tools are exposed as `<server>.<tool>`. A server named `docs` with a remote tool named `lookup` becomes `docs.lookup`:

```powershell
yaaf doctor --format json --pretty
yaaf ask --tool docs.lookup "Look up the install steps."
yaaf chat --tool docs.lookup "What does this API do?"
yaaf agent --name react --tool docs.lookup "Summarize the docs for this feature."
```

## Runtime Behavior

Implemented JSON-RPC lifecycle:

- `initialize` with the latest generated supported protocol version.
- Negotiated `protocolVersion` validation.
- `notifications/initialized` after successful initialization.
- Per-server session caching.

Implemented MCP methods:

- `tools/list`, including `nextCursor` pagination.
- `tools/call`.

HTTP behavior:

- Sends `Accept: application/json, text/event-stream`.
- Sends `MCP-Protocol-Version` after negotiation.
- Stores and re-sends `Mcp-Session-Id` when returned by the server.
- Parses both JSON responses and SSE-style `data:` response payloads.

Tool call results are normalized into yaaf's tool result shape. Text content blocks are joined with newlines, non-text content blocks are preserved as JSON text, and `structuredContent` is used as JSON text when there are no content blocks.

## Schema Support

Protocol version metadata is generated from the official versioned MCP JSON schemas in `modelcontextprotocol/modelcontextprotocol`.

Current generated protocol versions:

- `2024-11-05`
- `2025-03-26`
- `2025-06-18`
- `2025-11-25`

Refresh generated schema files with:

```powershell
./scripts/UpdateMcpSchemas.ps1
```

Generated files include one backend implementation per protocol version under `libyaaf/mcp/schema/schema-<version>.cpp`, plus the registry/factory used by the native client.

## Fixture Servers

Real hello-world MCP fixture servers live in [mcp-servers](https://github.com/svnscha/yaaf/tree/main/mcp-servers). They expose:

- `hello(name?: string)`: returns `Hello, <name>!`.
- `repeat(text: string, count?: integer)`: repeats text a bounded number of times.

The Docker Compose mitmproxy stack also starts the HTTP and SSE fixtures:

```powershell
docker compose -f docker-compose.mitmproxy.yml up
```

Fixture URLs used by the Docker Compose stack:

- HTTP: `http://127.0.0.1:39231/mcp`
- HTTP through mitmproxy visibility: `http://host.docker.internal:39231/mcp`
- SSE: `http://127.0.0.1:39232/mcp`

## Not Implemented Yet

Yaaf currently does not implement native client APIs for prompts, resources, resource subscriptions, roots, sampling, elicitation, logging level control, task APIs, progress or cancellation handling, OAuth flows, JSON-RPC batching, or long-lived SSE event streams beyond extracting response payloads from HTTP responses.

The implementation-level support matrix is maintained in [libyaaf/mcp/README.md](https://github.com/svnscha/yaaf/blob/main/libyaaf/mcp/README.md).
