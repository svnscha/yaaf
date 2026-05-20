# tool

`tool` is a built-in module that exposes the tool registry for local, script-registered, and MCP-discovered tools.

For built-in tool names, schemas, and a custom tool authoring guide, see [Tool Reference](../tools/index.md).

Load it with:

```lua
local tool = require("tool")
```

## Functions

- `tool.register(tool)`: registers a custom tool table for the current Lua runtime.
- `tool.names()`: returns available tool names, including MCP tools from the active config.
- `tool.providers()`: returns provider metadata keyed by tool name.
- `tool.create_many(tool_names)`: validates and loads selected tools.
- `tool.specs(tool_names)`: returns Ollama-compatible tool specs.
- `tool.descriptions(tool_names)`: returns text used in the ReAct system prompt.
- `tool.execute(tool_names, tool_name, arguments)`: executes a selected tool and returns a normalized result.

## Tool Shape

A tool module or registered tool table returns:

- `spec.name`
- `spec.description`
- `spec.parameters`
- `execute(arguments)` returning `{ tool_name, content, success, metadata }`

Custom tools registered with `tool.register(tool)` are available in the same runtime as built-in and MCP tools. MCP tools are exposed through the same registry with local names in the `<server>.<tool>` form.

```lua
tool.register({
    spec = {
        name = "hello",
        description = "Returns a greeting.",
        parameters = { type = "object" },
    },
    execute = function(arguments)
        return {
            tool_name = "hello",
            content = "hello " .. (arguments.name or "there"),
            success = true,
            metadata = {},
        }
    end,
})
```
