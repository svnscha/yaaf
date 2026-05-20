# MCP Stdio Server With Variables

This example runs the repository hello-world stdio fixture and demonstrates `${workspaceFolder}`, `envFile`, and inline `env` overrides:

```json
{
  "servers": {
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

Call the fixture tool through normal yaaf tool selection:

```powershell
yaaf ask --tool hello.hello "Say hello to Sven."
yaaf agent --name react --tool hello.repeat "Use the repeat tool to repeat hi twice."
```
