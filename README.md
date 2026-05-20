# yaaf

Yaaf is a command-line runtime for small AI workflows in Lua. You can use it as a normal CLI for prompts and chat, wire in local or MCP tools, or run a Lua script that registers its own tools and agents.

The native layer handles startup, HTTP, JSON, and MCP transports. Lua handles commands, tools, agents, and provider logic, so you can start with copyable commands and then grow into custom workflows without changing runtimes.

## What It Is Good At

- One-shot prompts from the terminal.
- Chat sessions with local or remote models.
- Tool-using agents with a small Lua surface area.
- MCP-backed tools loaded from a VS Code-style `mcp.json`.
- Direct Lua scripts that register local tools for a single run.

## Quick Start

These examples assume yaaf is already built and the build output directory is on `PATH`.

Ask a quick question:

```powershell
yaaf.exe ask "Explain RAII in one sentence."
```

Open a short chat:

```powershell
yaaf.exe chat "Reply with one short greeting."
```

Stream output as it arrives:

```powershell
yaaf.exe ask --stream "Write a haiku about C++."
```

Request JSON output for automation:

```powershell
yaaf.exe ask --format json --pretty "Return a JSON object with answer equal to 2."
```

Inspect your current runtime configuration:

```powershell
yaaf.exe doctor --format json --pretty
```

## Simple Real-World Examples

Use a local script to add a tiny custom tool and run an agent:

```powershell
yaaf.exe ./examples/weather_agent.lua "Use the weather tool to tell me the weather in Berlin."
```

Use the built-in echo tool to verify agent tool wiring before involving external services:

```powershell
yaaf.exe agent --name react --tool echo "Use the echo tool to repeat hello."
```

Point yaaf at an explicit MCP config and call a remote tool:

```powershell
yaaf.exe ask --mcp ./.vscode/mcp.json --tool docs.lookup "Look up the install steps."
```

Run a Lua script directly when you want full control over the workflow:

```powershell
yaaf.exe ./examples/example.lua one two three
```

## Start Here

- [Usage](docs/usage.md): build output, environment variables, command reference, embeddings, and proxy setup.
- [Examples](docs/examples/index.md): copyable CLI, Lua, ReAct, and MCP examples.
- [Lua Runtime](docs/lua.md): how command modules and direct scripts are discovered and run.
- [Lua API Reference](docs/modules/index.md): built-in runtime modules such as `llm`, `tool`, `agent`, and `mcp`.
- [MCP Tools](docs/mcp.md): explicit MCP config paths, supported config shape, and tool naming.
- [Tool Reference](docs/tools/index.md): built-in tools and custom tool authoring.

## Build And Detailed Setup

Build, environment setup, executable locations, and command reference live in [docs/usage.md](docs/usage.md). The full documentation index is at [docs/index.md](docs/index.md).

Serve the docs locally with MkDocs:

```powershell
python -m pip install -r requirements-docs.txt
python -m mkdocs serve
```

Build the static docs site with:

```powershell
python -m mkdocs build --strict
```

Implementation-level MCP protocol support details are maintained in [libyaaf/mcp/README.md](libyaaf/mcp/README.md).
