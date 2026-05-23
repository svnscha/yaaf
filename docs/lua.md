# Lua Runtime

Yaaf is a native C++ host for a Lua intelligence runtime. The native layer owns process startup, environment loading, low-level HTTP, LLM calls, MCP transports, and JSON conversion. Lua owns the user-facing extension surface: commands, agents, tools, and direct scripts.

Every built-in yaaf command is implemented as a Lua module under `lua/cli/`. At startup, the native CLI reads each module's `yaaf.command({ ... })` metadata and exposes it as a normal command such as `ask`, `chat`, `agent`, `embed`, or `doctor`. The same runtime can also execute a standalone Lua file directly through the native `run` subcommand.

## Direct Script Runs

Run a script from the repository root:

```powershell
yaaf run ./examples/example.lua one two three
```

Pass an MCP config path with the `run` subcommand:

```powershell
yaaf run --mcp ./configs/tools.mcp.json ./examples/example.lua
```

Legacy compatibility also remains available:

```powershell
yaaf --mcp ./configs/tools.mcp.json ./examples/example.lua
```

The sample script in [examples/example.lua](https://github.com/svnscha/yaaf/blob/main/examples/example.lua) prints runtime defaults and positional arguments. [examples/weather_agent.lua](https://github.com/svnscha/yaaf/blob/main/examples/weather_agent.lua) shows how to register an additional tool and run the included ReAct agent from a standalone script.

## Command Modules

Lua command modules return `yaaf.command({ ... })` metadata. The native CLI reads that metadata and exposes each command as a normal subcommand.

The built-in Lua commands are:

- `ask`: one-shot generate/chat flow, streaming, JSON output, thinking output, and tool calls.
- `chat`: interactive chat flow, streaming, and tool calls.
- `agent`: native agent entry point with Lua tool registry integration.
- `embed`: embedding requests.
- `doctor`: environment, registry, and MCP diagnostics.

`ask`, `chat`, `agent`, and `run` each accept `--mcp`. The root CLI also accepts `--mcp` for legacy direct script runs.

The command implementation lives in Lua, but the expensive or stateful operations stay native. For example, `ask.lua` parses command options and decides whether tools are enabled, then calls the `llm` and `json` modules. `doctor.lua` is also Lua; it gathers runtime defaults, registries, and MCP config state through public modules.

## Runtime Modules

See [Lua API Reference](modules/index.md) for the full runtime module surface. In short:

`require("yaaf")` exposes:

- `yaaf.args`: direct script arguments.
- `yaaf.options`: parsed command options for command modules.
- `yaaf.positionals`: parsed command positionals for command modules.
- `yaaf.defaults.endpoint`: endpoint resolved from `.env` or the built-in default.
- `yaaf.defaults.model`: command default model.
- `yaaf.command(metadata)`: command metadata wrapper.
- `yaaf.read_line()`, `yaaf.write(text)`, and `yaaf.flush()`.

`require("json")` exposes native JSON encode/decode helpers.

`require("http")` exposes the native low-level HTTP bridge for direct requests and future Lua-side provider implementations.

`require("llm")` exposes the provider-neutral LLM registry used by the built-in commands. It ships with built-in `ollama` and `echo` providers and lets Lua scripts register custom provider callback tables. The built-in `echo` provider is deterministic and intended for smoke tests and script-level wiring checks.

`require("agent")` exposes the native agent registry plus lower-level agent authoring primitives such as `complete_chat`, `emit`, and `strip_think_tags`.

`require("tool")` exposes the native tool registry. It includes the built-in `echo` tool, custom tools registered by scripts, and MCP tools discovered from the active MCP config. MCP tools use the local name `<server>.<tool>`.

See [Tool Reference](tools/index.md) for built-in tool names, schemas, and custom tool authoring.

`require("mcp")` exposes the native MCP bridge:

```lua
local mcp = require("mcp")

local config = mcp.config()
local servers = mcp.servers()
local tools = mcp.list_tools("docs")
local result = mcp.call_tool("docs", "lookup", { query = "install" })
```

The MCP bridge is intentionally thin. Config loading, transports, protocol negotiation, tool listing, and tool calls stay in native C++.
