local spec = {
    name = "echo",
    description = "Returns the provided text unchanged.",
    parameters = {
        type = "object",
        properties = {
            text = {
                type = "string",
                description = "Text to echo.",
            },
        },
        required = { "text" },
    },
}

local function execute(arguments)
    if type(arguments) ~= "table" then
        return {
            tool_name = spec.name,
            content = "echo expects a JSON object with a text field",
            success = false,
            metadata = {},
        }
    end

    local text = arguments.text
    if type(text) ~= "string" then
        return {
            tool_name = spec.name,
            content = "echo requires a text string",
            success = false,
            metadata = {},
        }
    end

    return {
        tool_name = spec.name,
        content = text,
        success = true,
        metadata = { text = text },
    }
end

return {
    spec = spec,
    execute = execute,
}
