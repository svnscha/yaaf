# Tool Reference

Yaaf tools are callable capabilities exposed through the built-in [tool](../modules/tool.md) registry. The same registry contains included tools, script-registered custom tools, and MCP-discovered tools.

Use this page when you need tool names, input schemas, or the shape of a custom tool.

## Built-In Tools

| Tool | Source | Description |
| --- | --- | --- |
| `echo` | [tools.echo](../modules/tools-echo.md) | Returns the provided text unchanged. Useful for checking tool wiring without external dependencies. |

### `echo`

Input schema:

```json
{
  "type": "object",
  "properties": {
    "text": {
      "type": "string",
      "description": "Text to echo."
    }
  },
  "required": ["text"]
}
```

Example call from Lua:

```lua
local tool = require("tool")

local result = tool.execute({ "echo" }, "echo", { text = "hello" })
print(result.content)
```

Example call from the CLI:

```powershell
yaaf ask --tool echo "Echo hello."
```

## MCP Tools

MCP tools are discovered from the active explicit MCP config and exposed through the same registry. Their local names use `<server>.<tool>`, for example `docs.lookup` or `hello.repeat`.

```powershell
yaaf ask --mcp ./configs/docs.mcp.json --tool docs.lookup "Look up the install steps."
```

See [MCP Tools](../mcp.md) for config shape, transports, variable substitution, and fixture details.

## Custom Tool Guide

A custom tool is a Lua table with a `spec` and an `execute(arguments)` function. Register it with `tool.register(tool)` inside the current Lua runtime.

```lua
local tool = require("tool")

local hello_tool = {
    spec = {
        name = "hello",
        description = "Returns a friendly greeting.",
        parameters = {
            type = "object",
            properties = {
                name = {
                    type = "string",
                    description = "Name to greet.",
                },
            },
            required = { "name" },
        },
    },
}

function hello_tool.execute(arguments)
    local name = arguments.name or "there"
    return {
        tool_name = hello_tool.spec.name,
        content = "hello " .. name,
        success = true,
        metadata = { name = name },
    }
end

tool.register(hello_tool)
```

The `spec` is what the model sees. Keep descriptions short and action-oriented, and use a JSON schema object for `parameters`.

The `execute` result should contain:

- `tool_name`: usually `tool.spec.name`.
- `content`: the observation text passed back to the model or caller.
- `success`: `true` when the action completed, `false` when the tool handled an input or runtime error.
- `metadata`: structured data for traces and programmatic callers.

The registry automatically adds `metadata.arguments` to execution results so callers can inspect the exact input used for the call.

## Use A Custom Tool With An Agent

```lua
local agent = require("agent")

local runner = agent.create("react", {
    model = "ministral-3:14b",
    max_turns = 6,
    tools = { "hello" },
})

runner:run("Use the hello tool to greet Sven.")
```

For a complete custom tool example, see [Build A Weather Agent](../examples/weather-agent.md).
