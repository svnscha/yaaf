# mcp

`mcp` is a built-in module that exposes the MCP bridge.

Config loading, variable expansion, transport handling, protocol negotiation, and tool calls stay in the native runtime. Lua receives config reports, server summaries, tool descriptors, and normalized tool-call results.

Load it with:

```lua
local mcp = require("mcp")
```

## Functions

- `mcp.config()`: returns the active MCP config report.
- `mcp.servers()`: returns `{ id, type, supported, diagnostics }` entries.
- `mcp.diagnostics()`: returns per-server active initialize and tool-discovery results for doctor-style troubleshooting.
- `mcp.list_tools(server_id)`: returns tool descriptors for one supported server.
- `mcp.call_tool(server_id, tool_name, arguments)`: calls a tool and returns `{ tool_name, content, success, metadata }`.

Tool descriptors include `server_id`, `name`, `local_name`, `title`, `description`, `inputSchema`, `parameters`, `outputSchema`, and `annotations`.

```lua
for _, server in ipairs(mcp.servers()) do
    print(server.id .. ": " .. tostring(server.supported))
end
```

Use `mcp.diagnostics()` when you want a structured active connectivity check without invoking a tool. Use the higher-level [tool](tool.md) registry when you want MCP tools to appear beside local and script-registered tools.

## Hosting MCP Servers

Lua scripts can host MCP servers to expose tools and prompts to MCP clients (Claude, etc.) over stdio transport.

### `mcp.register_prompt(descriptor)`

Register a prompt for use when hosting an MCP server.

**Parameters:**

- `descriptor` (table): Prompt descriptor with the following structure:
  - `name` (string): Unique prompt identifier
  - `description` (string, optional): Human-readable description of the prompt
  - `arguments` (table, optional): Array of argument descriptors `{ {name, description?, required?}, ... }`
  - `handler` (function): Handler called when client requests the prompt. Signature: `function(arguments_table) -> {messages = {{role, content}, ...}}`

**Returns:** `true` on success

**Throws:** Lua error on invalid descriptor (missing name, missing handler, etc.)

**Message format:**

Each message table has:
- `role` (string): `"user"` or `"assistant"`
- `content` (string): Message text

**Example:**

```lua
local mcp = require("mcp")

mcp.register_prompt({
  name = "system_role",
  description = "System role for a helpful assistant",
  arguments = {
    { name = "tone", description = "Assistant tone: formal or casual", required = false },
  },
  handler = function(args)
    local tone = args.tone or "formal"
    local instruction = "You are a helpful assistant. Keep a " .. tone .. " tone."
    return {
      messages = {
        { role = "user", content = instruction }
      }
    }
  end
})
```

### `mcp.host_stdio(options)`

Start an MCP server listening on stdin/stdout.

**Parameters:**

- `options` (table, optional):
  - `tools` (table, optional): Array of tool names to expose. If omitted, all available tools are exposed.
  - `prompts` (table, optional): Array of prompt names to expose. If omitted, all registered prompts are exposed.

**Returns:** `boolean` (`true` on clean exit)

**Throws:** Lua error on fatal error (e.g., schema registry not available, JSON-RPC parse failure)

**Behavior:**

- Blocks until the client disconnects or stdin reaches EOF
- Handles all JSON-RPC protocol messages from the client
- Responds to `tools/list`, `tools/call`, `prompts/list`, and `prompts/get` requests
- Responds to `initialize` with supported protocol version

**Example:**

```lua
local tool = require("tool")
local mcp = require("mcp")

-- Register a custom tool
tool.register({
  spec = {
    name = "reverse",
    description = "Reverses a string",
    parameters = {
      type = "object",
      properties = {
        text = { type = "string", description = "Text to reverse" }
      },
      required = { "text" }
    }
  },
  execute = function(args)
    local text = args.text or ""
    local reversed = string.reverse(text)
    return {
      tool_name = "reverse",
      content = reversed,
      success = true,
      metadata = { input_length = #text }
    }
  end
})

-- Register a prompt
mcp.register_prompt({
  name = "greeting",
  description = "Greeting prompt",
  handler = function(args)
    return {
      messages = {
        { role = "user", content = "Hello! How can I help?" }
      }
    }
  end
})

-- Start the server, exposing reverse, echo (built-in), and greeting
mcp.host_stdio({
  tools = { "reverse", "echo" },
  prompts = { "greeting" }
})
```

### Integration with yaaf's Tool Ecosystem

Hosted tools can be:

- **Built-in tools:** The `echo` tool that ships with yaaf
- **Custom tools:** Registered in the script via `tool.register()`
- **Remote MCP tools:** Tools from configured MCP servers, accessible via `mcp.servers()` and `mcp.list_tools()`

Use `tool.names()` and `tool.specs()` to discover available tools before calling `mcp.host_stdio()`:

```lua
local tool = require("tool")
local mcp = require("mcp")

local available = tool.names()
print("Available tools: " .. table.concat(available, ", "))

-- Expose a selected subset
mcp.host_stdio({
  tools = { "echo", "reverse" }
})
```
