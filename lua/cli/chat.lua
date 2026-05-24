local yaaf = require("yaaf")
local json = require("json")
local llm = require("llm")
local tool_registry = require("tool")

local function selected_client(options)
    return llm.create(options.provider or "ollama")
end

local function think_value(value)
    if value == nil or value == "" or value == "none" then
        return false
    end

    return value
end

local function tool_call_parts(tool_call)
    local function_payload = tool_call and tool_call["function"] or {}
    return function_payload.name or "", function_payload.arguments or {}
end

local function has_tool_calls(message)
    return type(message) == "table" and type(message.tool_calls) == "table" and #message.tool_calls > 0
end

local function write_tool_result(tool_name, arguments, result)
    yaaf.write("tool: " .. tool_name .. " " .. json.encode(arguments or {}) .. "\n")
    yaaf.write("observation: " .. (result.content or "") .. "\n")
end

local function complete_with_tools(client, options, messages, tool_names)
    local tool_specs = tool_registry.specs(tool_names)

    for _ = 1, 10 do
        local response = client.chat({
            endpoint = options.endpoint,
            model = options.model,
            messages = messages,
            tools = tool_specs,
            stream = false,
            think = think_value(options.think),
        })

        local message = response.message or {}
        if not has_tool_calls(message) then
            return response
        end

        table.insert(messages, message)
        for _, tool_call in ipairs(message.tool_calls) do
            local tool_name, arguments = tool_call_parts(tool_call)
            local result = tool_registry.execute(tool_names, tool_name, arguments)
            write_tool_result(tool_name, arguments, result)
            table.insert(messages, { role = "tool", content = result.content or "" })
        end
    end

    error("maximum tool turns reached for chat")
end

local function run(command)
    local options = command.options
    local client = selected_client(options)
    local tool_names = options.tool or {}
    if options.stream and #tool_names > 0 then
        error("--stream is not supported with --tool for chat")
    end

    tool_registry.create_many(tool_names)

    local messages = {}
    local next_prompt = command.positionals.prompt

    while true do
        if next_prompt == nil or next_prompt == "" then
            yaaf.write("user: ")
            next_prompt = yaaf.read_line()
            if next_prompt == nil then
                break
            end
        end

        if next_prompt ~= "" then
            table.insert(messages, { role = "user", content = next_prompt })

            local thinking_started = false
            local response_started = false

            local function on_stream(event)
                local message = event.message or {}
                if message.thinking ~= nil and message.thinking ~= "" then
                    if not thinking_started then
                        yaaf.write("yaaf thinking: ")
                        thinking_started = true
                    end

                    yaaf.write(message.thinking)
                    yaaf.flush()
                end

                if message.content ~= nil and message.content ~= "" then
                    if thinking_started and not response_started then
                        yaaf.write("\n")
                    end

                    if not response_started then
                        yaaf.write("assistant: ")
                        response_started = true
                    end

                    yaaf.write(message.content)
                    yaaf.flush()
                end
            end

            local response
            if #tool_names > 0 then
                response = complete_with_tools(client, options, messages, tool_names)
            else
                response = client.chat({
                    endpoint = options.endpoint,
                    model = options.model,
                    messages = messages,
                    stream = options.stream,
                    think = think_value(options.think),
                    on_stream = options.stream and on_stream or nil,
                })
            end

            if options.stream then
                local printed_output = thinking_started or response_started
                local message = response.message or {}

                if not thinking_started and message.thinking ~= nil and message.thinking ~= "" then
                    yaaf.write("yaaf thinking: " .. message.thinking .. "\n")
                    printed_output = true
                end

                if not response_started and message.content ~= nil and message.content ~= "" then
                    yaaf.write("assistant: " .. message.content)
                    printed_output = true
                end

                if not printed_output then
                    yaaf.write("assistant:")
                end
            else
                local message = response.message or {}
                if message.thinking ~= nil and message.thinking ~= "" then
                    yaaf.write("yaaf thinking: " .. message.thinking .. "\n")
                end

                yaaf.write("assistant: " .. (message.content or ""))
            end

            yaaf.write("\n")
            table.insert(messages, response.message)
        end

        next_prompt = ""
    end
end

return yaaf.command({
    description = "Start an interactive chat session against the selected model endpoint",
    options = {
        {
            name = "provider",
            flags = { "--provider" },
            type = "string",
            default = "ollama",
            description = "Provider used by this command",
        },
        {
            name = "endpoint",
            flags = { "--endpoint" },
            type = "string",
            default = yaaf.defaults.endpoint,
            description = "Endpoint used by this command",
        },
        {
            name = "model",
            flags = { "--model" },
            type = "string",
            default = yaaf.defaults.model,
            description = "Model used by this command",
        },
        {
            name = "stream",
            flags = { "--stream" },
            type = "flag",
            description = "Stream chat output incrementally for chat",
        },
        {
            name = "think",
            flags = { "--think" },
            type = "string",
            default = "none",
            description = "Thinking level for this command: none, high, medium, or low",
        },
        {
            name = "tool",
            flags = { "--tool" },
            type = "strings",
            description = "Registered tool made available to chat; may be passed multiple times",
        },
        {
            name = "mcp",
            flags = { "--mcp" },
            type = "string",
            description = "MCP server configuration file used for this chat session",
        },
    },
    positionals = {
        {
            name = "prompt",
            type = "string",
            required = false,
            description = "Optional first user message to send before entering interactive chat",
        },
    },
    run = run,
})
