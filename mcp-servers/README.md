# Yaaf MCP Fixture Servers

This directory contains tiny Python MCP servers used by yaaf integration tests. They intentionally avoid external APIs and keep the tool surface hello-world small.

Run them with `uv` from the repository root:

```powershell
uv --directory ./mcp-servers run python hello_stdio.py
uv --directory ./mcp-servers run python hello_http.py --host 127.0.0.1 --port 39231
uv --directory ./mcp-servers run python hello_sse.py --host 127.0.0.1 --port 39232
```

Linux uses the same `uv`-based stdio fixture flow as macOS. There are no extra Linux-only prerequisites for the stdio fixture tests beyond having `uv` available.

The optional local fixture stack includes `httpbin`, mitmproxy, and the HTTP/SSE MCP fixtures used for manual debugging, proxy inspection, and explicit smoke checks:

```powershell
docker compose -f docker-compose.fixture-stack.yml up
```

The native codebase still uses these fixture-only endpoint defaults and overrides when you intentionally point a manual run or smoke check at the prestarted stack:

- `YAAF_HTTPBIN_URL` (default `http://127.0.0.1:18082`)
- `YAAF_HTTPBIN_PROXIED_URL` (default `http://host.docker.internal:18082`)
- `YAAF_MCP_HELLO_HTTP_URL` (default `http://127.0.0.1:39231/mcp`)
- `YAAF_MCP_HELLO_HTTP_PROXIED_URL` (default `http://host.docker.internal:39231/mcp`)
- `YAAF_MCP_HELLO_SSE_URL` (default `http://127.0.0.1:39232/mcp`)

Those variables are not user-facing MCP configuration; normal yaaf runs select an MCP config file with `--mcp <path>` or `YAAF_MCP_FILE`, and fixture URLs belong inside that file. Proxied MCP smoke checks also require `YAAF_PROXY` so requests are visible in mitmproxy.

For tests, pass `--port 0 --ready-file <path>` to let the OS choose a free port and write the selected port as JSON.

All servers expose the same tools:

- `hello(name?: string)`: returns `Hello, <name>!`.
- `repeat(text: string, count?: integer)`: repeats text a bounded number of times.

The stdio server writes only JSON-RPC messages to stdout. Diagnostics must go to stderr so stdout remains a clean MCP transport.
