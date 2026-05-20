# yaaf docs

Yaaf is a C++20 command-line runtime for building intelligent workflows in Lua. The native layer owns the fast pieces: process startup, configuration, HTTP, JSON conversion, MCP transports, and schema-backed protocol handling. Lua owns the product surface: commands, agents, LLM provider implementations, tool registration, examples, and the scripting API developers use day to day.

That split is intentional. You can run `yaaf ask ...` as a normal CLI, but `ask` itself is a Lua command module. The same runtime can load a standalone Lua script, register local tools, connect MCP tools, and run an agent loop.

## Start Here

- [Usage](usage.md): build output, environment variables, command reference, embeddings, and proxy setup.
- [Lua Runtime](lua.md): how direct scripts and Lua command modules are discovered and executed.
- [Lua API Reference](modules/index.md): per-module runtime API reference.
- [Tool Reference](tools/index.md): built-in tools and custom tool authoring guide.
- [MCP Tools](mcp.md): explicit MCP config paths, VS Code-compatible `mcp.json`, transports, variable substitution, tool naming, diagnostics, and schema support.
- [Examples](examples/index.md): a step-by-step guide for building a Lua weather agent, plus copyable CLI and MCP examples.

## What You Can Build

- CLI commands implemented as Lua modules under `lua/cli/`.
- ReAct-style agents using the native `agent` and `tool` modules.
- Script-local tools registered at runtime with `tool.register(tool)`.
- MCP-backed tools loaded from explicit VS Code-shaped MCP config files.
- Direct Lua scripts that use native `yaaf`, `json`, `http`, `llm`, `mcp`, `agent`, and `tool` bindings.

Implementation-level MCP protocol support details remain in the repository under `libyaaf/mcp/README.md`.
