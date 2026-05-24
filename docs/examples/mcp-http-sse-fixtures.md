# MCP HTTP And SSE Fixtures

Start the local Docker test stack with `httpbin`, mitmproxy, and the HTTP/SSE MCP fixtures:

```powershell
docker compose -f docker-compose.test-stack.yml up
```

Put fixture URLs in `.env` for repeated local runs:

```text
YAAF_PROXY=http://127.0.0.1:18080
YAAF_HTTPBIN_URL=http://127.0.0.1:18082
YAAF_MCP_FILE=./configs/hello-http.mcp.json
```

Use the proxied URL when you want MCP HTTP requests to show up in mitmproxy:

```json
{
  "servers": {
    "hello": {
      "type": "http",
      "url": "http://host.docker.internal:39231/mcp"
    }
  }
}
```

Use SSE-style response parsing:

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
