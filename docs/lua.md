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

The sample script in [examples/example.lua](https://github.com/svnscha/yaaf/blob/main/examples/example.lua) prints runtime defaults and positional arguments. [examples/weather_agent.lua](https://github.com/svnscha/yaaf/blob/main/examples/weather_agent.lua) shows how to register an additional tool and run the included ReAct agent from a standalone script.

## Command Modules

Lua command modules return `yaaf.command({ ... })` metadata. The native CLI reads that metadata and exposes each command as a normal subcommand.

The built-in Lua commands are:

- `ask`: one-shot generate/chat flow, streaming, JSON output, thinking output, and tool calls.
- `chat`: interactive chat flow, streaming, and tool calls.
- `agent`: native agent entry point with Lua tool registry integration.
- `embed`: embedding requests.
- `doctor`: environment, registry, active MCP connectivity, and tool-discovery diagnostics.

`ask`, `chat`, `agent`, and `run` each accept `--mcp`. The root CLI also accepts `--mcp` as a global option.

The command implementation lives in Lua, but the expensive or stateful operations stay native. For example, `ask.lua` parses command options and decides whether tools are enabled, then calls the `llm` and `json` modules. `doctor.lua` is also Lua; it gathers runtime defaults, registries, MCP config state, and active MCP diagnostics through public modules.

## Runtime Modules

See [Lua API Reference](modules/index.md) for the full runtime module surface. In short:

`require("yaaf")` exposes:

- `yaaf.args`: direct script arguments.
- `yaaf.platform`: current platform name: `"windows"`, `"linux"`, or `"osx"`.
- `yaaf.options`: parsed command options for command modules.
- `yaaf.positionals`: parsed command positionals for command modules.
- `yaaf.defaults.endpoint`: endpoint resolved from `.env` or the built-in default.
- `yaaf.defaults.model`: command default model.
- `yaaf.command(metadata)`: command metadata wrapper.
- `yaaf.read_line()`, `yaaf.write(text)`, and `yaaf.flush()`.

`require("process")` exposes process spawning and inter-process communication. Start child processes with configurable command, args, working directory, and environment variables. Read and write to stdin/stdout, check process status, and gracefully shut down processes:

```lua
local process = require("process")

local handle = process.start({
    command = "echo",
    args = { "Hello" },
})

local line, err = handle:read(5000)
handle:close()
```

`require("json")` exposes native JSON encode/decode helpers.

`require("http")` exposes the native low-level, request-based HTTP bridge for direct requests and future Lua-side provider implementations.

`require("llm")` exposes the provider-neutral LLM registry used by the built-in commands. It ships with built-in `ollama`, `openai`, and `echo` providers and lets Lua scripts register custom provider callback tables. The built-in `echo` provider is deterministic and intended for smoke tests and script-level wiring checks. The built-in `openai` provider speaks the OpenAI-compatible `/v1/chat/completions` and `/v1/embeddings` HTTP surface.

`require("agent")` exposes the native agent registry plus lower-level agent authoring primitives such as `complete_chat`, `emit`, and `strip_think_tags`.

`require("tool")` exposes the native tool registry. It includes the built-in `echo` tool, custom tools registered by scripts, and MCP tools discovered from the active MCP config. MCP tools use the local name `<server>.<tool>`.

See [Tool Reference](tools/index.md) for built-in tool names, schemas, and custom tool authoring.

`require("mcp")` exposes the native MCP bridge:

```lua
local mcp = require("mcp")

local config = mcp.config()
local servers = mcp.servers()
local diagnostics = mcp.diagnostics()
local tools = mcp.list_tools("docs")
local result = mcp.call_tool("docs", "lookup", { query = "install" })
```

The MCP bridge is intentionally thin. Config loading, transports, protocol negotiation, tool listing, and tool calls stay in native C++.

## Authoring MCP Servers with Lua

Yaaf scripts can host MCP servers, allowing local tools and prompts to be consumed by any MCP client (Claude, VS Code, etc.). This is the reverse of the default MCP client mode: instead of yaaf consuming remote servers, MCP clients consume yaaf.

### Entry Point

Host an MCP server directly:

```bash
yaaf run ./examples/mcp_host_example.lua
```

The script blocks until the MCP client disconnects. JSON-RPC messages flow over stdin/stdout.

### Authoring Workflow

A typical MCP host script follows this pattern:

```lua
-- 1. Load required modules
local tool = require("tool")
local mcp = require("mcp")

-- 2. Register custom tools
tool.register({
  spec = {
    name = "calculate",
    description = "Simple calculator",
    parameters = {
      type = "object",
      properties = {
        expression = {
          type = "string",
          description = "Math expression to evaluate"
        }
      },
      required = { "expression" }
    }
  },
  execute = function(args)
    -- Tool execution logic
    local result = load("return " .. args.expression)()
    return {
      tool_name = "calculate",
      content = tostring(result),
      success = true,
      metadata = {}
    }
  end
})

-- 3. Register prompts (optional)
mcp.register_prompt({
  name = "system_role",
  description = "System role prompt for the assistant",
  arguments = {
    { name = "style", description = "Response style: formal or casual" }
  },
  handler = function(args)
    local style = args.style or "formal"
    return {
      messages = {
        {
          role = "user",
          content = "You are a helpful assistant. Use a " .. style .. " tone."
        }
      }
    }
  end
})

-- 4. Start the server
mcp.host_stdio({
  tools = { "calculate", "echo" },
  prompts = { "system_role" }
})
```

### Available Tools

Hosted tools can come from three sources:

1. **Built-in tools:** The `echo` tool shipped with yaaf
2. **Custom tools:** Registered via `tool.register()` in the same script
3. **Remote MCP tools:** Tools from configured MCP servers, available via `mcp.servers()`

Use `tool.names()` to list all available tools:

```lua
local tool = require("tool")

local available = tool.names()
-- Example output: { "echo", "reverse", "server1.tool1", "server1.tool2" }
```

### Prompt Specification

Prompts are script-local and must be registered before calling `mcp.host_stdio()`. Each prompt:

- Has a unique `name` and optional `description`
- Optionally accepts templating arguments (e.g., tone, style, detail level)
- Returns a table with a `messages` array
- Each message has `role` (`"user"` or `"assistant"`) and `content` (string)

Prompts allow clients to request system instructions or conversation starters alongside tools.

### Selective Exposure

Use the `{tools?, prompts?}` parameters to expose a subset of registered items:

```lua
-- Expose only "reverse" and "echo" tools, hide remote MCP tools
mcp.host_stdio({
  tools = { "reverse", "echo" },
  prompts = { "system_role", "greeting" }
})
```

If omitted, all tools and prompts are exposed.

### Use Cases

- **Wrap local scripts:** Expose shell commands, local APIs, or file operations as MCP tools
- **Composite servers:** Use remote MCP tools and augment them with custom logic
- **Prompt libraries:** Provide system instructions and conversation starters
- **Local tool testing:** Develop and test tools in isolation before shipping
