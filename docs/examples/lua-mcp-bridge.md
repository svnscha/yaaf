# Lua MCP Bridge

Run a script with an explicit MCP config:

```powershell
yaaf --mcp ./configs/hello.mcp.json ./examples/example.lua
```

Use the bridge from Lua:

```lua
local mcp = require("mcp")

for _, server in ipairs(mcp.servers()) do
    print(server.id .. ": " .. tostring(server.supported))
end

local result = mcp.call_tool("hello", "hello", { name = "Sven" })
print(result.content)
```
