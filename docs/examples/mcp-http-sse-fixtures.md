# MCP HTTP And SSE Fixtures

These fixture configs point yaaf at local deterministic MCP servers. They match the HTTP and SSE scenarios covered by `libyaaf_tests` without requiring Docker, mitmproxy, or public-network services.

Use the HTTP fixture when you want a plain JSON-RPC-over-HTTP MCP server:

```json
{
  "servers": {
    "hello": {
      "type": "http",
      "url": "http://127.0.0.1:39231/mcp"
    }
  }
}
```

Use the SSE fixture when you want an SSE transport with the same `hello` tool surface:

```json
{
  "servers": {
    "helloSse": {
      "type": "sse",
      "url": "http://127.0.0.1:39232/mcp"
    }
  }
}
```

Pass the config explicitly when you run yaaf:

```powershell
yaaf.exe ask --mcp .\configs\hello-http.mcp.json "Use the hello tool to greet Sven."
```

These fixture endpoints are intended for local examples and tests. For real MCP servers, keep using the same config shape with your actual server URL or stdio command.
