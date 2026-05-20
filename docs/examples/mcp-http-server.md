# MCP HTTP Server

Create a VS Code-shaped MCP config file, for example `configs/docs.mcp.json`:

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

Use a discovered MCP tool named `lookup`:

```powershell
yaaf --mcp ./configs/docs.mcp.json doctor --format json --pretty
yaaf ask --mcp ./configs/docs.mcp.json --tool docs.lookup "Look up the install steps."
```

Or set the config path once in `.env`:

```text
YAAF_MCP_FILE=./configs/docs.mcp.json
```

Then run:

```powershell
yaaf ask --tool docs.lookup "Look up the install steps."
```
