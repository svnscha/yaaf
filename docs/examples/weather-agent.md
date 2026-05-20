# Build A Weather Agent

This guide builds the included weather example from [examples/weather_agent.lua](https://github.com/svnscha/yaaf/blob/main/examples/weather_agent.lua). The script registers a local `weather` tool, creates the included ReAct agent, and runs that agent with the script-local tool enabled.

Run the complete example:

```powershell
yaaf ./examples/weather_agent.lua "Use the weather tool to tell me the weather in Berlin."
```

## Step 1: Load Runtime Modules

Use `agent` to create an agent and `tool` to register tools for this Lua runtime instance.

```lua
local agent = require("agent")
local tool = require("tool")
```

## Step 2: Describe The Tool

A tool is a Lua table with a `spec` and an `execute(arguments)` function. The `spec` uses the same general shape expected by model tool APIs: a name, a description, and JSON schema parameters.

```lua
local weather_tool = {
    spec = {
        name = "weather",
        description = "Returns a small canned weather report for a requested location.",
        parameters = {
            type = "object",
            properties = {
                location = {
                    type = "string",
                    description = "City or location name.",
                },
            },
            required = { "location" },
        },
    },
}
```

## Step 3: Implement Execution

`content` is the observation the agent receives. `success` tells the registry whether the tool call worked. `metadata` preserves structured data for traces and callers.

```lua
function weather_tool.execute(arguments)
    if type(arguments) ~= "table" then
        return {
            tool_name = weather_tool.spec.name,
            content = "weather expects a JSON object with a location field",
            success = false,
            metadata = {},
        }
    end

    local location = arguments.location or ""
    if location == "" then
        return {
            tool_name = weather_tool.spec.name,
            content = "weather requires a non-empty location",
            success = false,
            metadata = {},
        }
    end

    local lowered = string.lower(location)
    local report
    if lowered == "berlin" then
        report = "Berlin: 18 C, light rain, wind 12 km/h."
    elseif lowered == "hamburg" then
        report = "Hamburg: 16 C, overcast, wind 20 km/h."
    elseif lowered == "london" then
        report = "London: 17 C, cloudy, wind 15 km/h."
    else
        report = location .. ": 21 C, clear skies, wind 8 km/h."
    end

    return {
        tool_name = weather_tool.spec.name,
        content = report,
        success = true,
        metadata = { location = location },
    }
end
```

## Step 4: Register The Tool

`tool.register(tool)` makes a custom tool available in the current Lua runtime. It does not modify the included `lua/tools/` implementations, so examples and user scripts can carry local tools without changing the product defaults.

```lua
tool.register(weather_tool)
```

## Step 5: Create And Run The Agent

The included ReAct agent is created through `agent.create("react", options)`. Passing `tools = { "weather" }` limits the agent to the custom weather tool.

```lua
local prompt = table.concat(arg, " ")
if prompt == "" then
    prompt = "Use the weather tool to tell me the weather in Berlin."
end

local runner = agent.create("react", {
    model = "ministral-3:14b",
    max_turns = 6,
    tools = { "weather" },
})

runner:run(prompt)
```

## Complete Code

This is the whole example script.

```lua
local agent = require("agent")
local tool = require("tool")

local weather_tool = {
    spec = {
        name = "weather",
        description = "Returns a small canned weather report for a requested location.",
        parameters = {
            type = "object",
            properties = {
                location = {
                    type = "string",
                    description = "City or location name.",
                },
            },
            required = { "location" },
        },
    },
}

function weather_tool.execute(arguments)
    if type(arguments) ~= "table" then
        return {
            tool_name = weather_tool.spec.name,
            content = "weather expects a JSON object with a location field",
            success = false,
            metadata = {},
        }
    end

    local location = arguments.location or ""
    if location == "" then
        return {
            tool_name = weather_tool.spec.name,
            content = "weather requires a non-empty location",
            success = false,
            metadata = {},
        }
    end

    local lowered = string.lower(location)
    local report
    if lowered == "berlin" then
        report = "Berlin: 18 C, light rain, wind 12 km/h."
    elseif lowered == "hamburg" then
        report = "Hamburg: 16 C, overcast, wind 20 km/h."
    elseif lowered == "london" then
        report = "London: 17 C, cloudy, wind 15 km/h."
    else
        report = location .. ": 21 C, clear skies, wind 8 km/h."
    end

    return {
        tool_name = weather_tool.spec.name,
        content = report,
        success = true,
        metadata = { location = location },
    }
end

tool.register(weather_tool)

local prompt = table.concat(arg, " ")
if prompt == "" then
    prompt = "Use the weather tool to tell me the weather in Berlin."
end

local runner = agent.create("react", {
    model = "ministral-3:14b",
    max_turns = 6,
    tools = { "weather" },
})

runner:run(prompt)
```
