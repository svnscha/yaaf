# Yaaf MCP Fixture Servers

This directory contains tiny Python MCP servers used by yaaf integration tests. They intentionally avoid external APIs and keep the tool surface hello-world small.

Run them with `uv` from the repository root:

```powershell
uv --directory ./mcp-servers run python hello_stdio.py
uv --directory ./mcp-servers run python hello_http.py --host 127.0.0.1 --port 39231
uv --directory ./mcp-servers run python hello_sse.py --host 127.0.0.1 --port 39232
```

Linux uses the same `uv`-based stdio fixture flow as macOS. There are no extra Linux-only prerequisites for the stdio fixture tests beyond having `uv` available.

The HTTP and SSE fixtures are also included in the mitmproxy Docker Compose stack used by local integration tests:

```powershell
docker compose -f docker-compose.mitmproxy.yml up
```

The native integration tests have fixture-only endpoint overrides named `YAAF_MCP_HELLO_HTTP_URL`, `YAAF_MCP_HELLO_HTTP_PROXIED_URL`, and `YAAF_MCP_HELLO_SSE_URL`. Those variables are not user-facing MCP configuration; normal yaaf runs select an MCP config file with `--mcp <path>` or `YAAF_MCP_FILE`, and fixture URLs belong inside that file. The proxied HTTP client test also requires `YAAF_PROXY` so its MCP requests are visible in mitmproxy. The tests skip HTTP/SSE cases when the prestarted servers are not reachable.

For tests, pass `--port 0 --ready-file <path>` to let the OS choose a free port and write the selected port as JSON.

All servers expose the same tools:

- `hello(name?: string)`: returns `Hello, <name>!`.
- `repeat(text: string, count?: integer)`: repeats text a bounded number of times.

The stdio server writes only JSON-RPC messages to stdout. Diagnostics must go to stderr so stdout remains a clean MCP transport.
