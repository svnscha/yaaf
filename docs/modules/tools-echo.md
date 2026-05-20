# tools.echo

`tools.echo` is the included deterministic echo tool.

It is loaded through the [tool](tool.md) registry as `echo`:

The broader built-in tool list lives in [Tool Reference](../tools/index.md).

```lua
local tool = require("tool")

local result = tool.execute({ "echo" }, "echo", { text = "hello" })
print(result.content)
```

## Input Schema

```json
{
  "type": "object",
  "properties": {
    "text": { "type": "string" }
  },
  "required": ["text"]
}
```

The tool returns the provided `text` unchanged. [examples/weather_agent.lua](https://github.com/svnscha/yaaf/blob/main/examples/weather_agent.lua) demonstrates a richer custom tool registered from a standalone script.
