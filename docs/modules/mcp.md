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
