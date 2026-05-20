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