# Examples

These examples assume commands run from the repository root in a shell where the yaaf build output directory is on `PATH`.

Yaaf is a Lua runtime for intelligent workflows. The native executable provides startup, configuration, model clients, JSON conversion, and the MCP bridge. Lua scripts provide commands, agents, tools, and runnable examples.

## Start Here

- [Build A Weather Agent](weather-agent.md): register a script-local tool and run the included ReAct agent.
- [Built-In Echo Tool](echo-tool.md): check tool wiring with the deterministic `echo` tool.
- [Basic Direct Script](direct-script.md): run a minimal Lua file with direct script arguments.
- [Basic CLI](basic-cli.md): copyable `ask` and `chat` command examples.
- [MCP HTTP Server](mcp-http-server.md): use a VS Code-shaped MCP config with an HTTP server.
- [MCP Stdio Server With Variables](mcp-stdio-variables.md): use `${workspaceFolder}`, `envFile`, and inline `env` with the stdio fixture.
- [MCP HTTP And SSE Fixtures](mcp-http-sse-fixtures.md): point yaaf at local deterministic MCP HTTP and SSE fixtures without Docker or public-network dependencies.
- [Lua MCP Bridge](lua-mcp-bridge.md): call the MCP bridge directly from Lua.
