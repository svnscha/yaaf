local yaaf = require("yaaf")
local json = require("json")
local llm = require("llm")
local tool_registry = require("tool")

local function selected_client(options)
    return llm.create(options.provider or "ollama")
end

local function starts_with_json(text)
    local trimmed = text:match("^%s*(.-)%s*$")
    local first = trimmed:sub(1, 1)
    return first == "{" or first == "["
end

local function parse_format(value)
    if value == nil or value == "" then
        return nil
    end

    if starts_with_json(value) then
        return json.decode(value)
    end

    return value
end

local function thinking_enabled(value)
    return value ~= nil and value ~= "" and value ~= "none"
end

local function build_json_output(response, wrap_response)
    local ok, parsed = pcall(json.decode, response.response)
    local payload

    if wrap_response and not (ok and type(parsed) == "table" and #parsed == 0) then
        payload = { response = response.response }
    elseif ok and type(parsed) == "table" and #parsed == 0 then
        payload = parsed
    elseif ok then
        payload = { answer = parsed }
    else
        payload = { answer = response.response }
    end

    if response.thinking ~= nil and response.thinking ~= "" then
        payload.thinking = response.thinking
    end

    return payload
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

local function run_chat_with_tools(client, options, prompt, format, emit_tool_results)
    local tool_names = options.tool or {}
    local tool_specs = tool_registry.specs(tool_names)
    local messages = { { role = "user", content = prompt } }

    for _ = 1, 10 do
        local response = client.chat({
            endpoint = options.endpoint,
            model = options.model,
            messages = messages,
            tools = tool_specs,
            stream = false,
            think = options.think,
            format = format,
        })

        local message = response.message or {}
        if not has_tool_calls(message) then
            return response
        end

        table.insert(messages, message)
        for _, tool_call in ipairs(message.tool_calls) do
            local tool_name, arguments = tool_call_parts(tool_call)
            local result = tool_registry.execute(tool_names, tool_name, arguments)
            if emit_tool_results then
                write_tool_result(tool_name, arguments, result)
            end
            table.insert(messages, { role = "tool", content = result.content or "" })
        end
    end

    error("maximum tool turns reached for ask")
end

local function run(command)
    local options = command.options
    local client = selected_client(options)
    local format = parse_format(options.format)
    if options.pretty and format ~= "json" then
        error("--pretty is only supported with --format json for ask")
    end

    if options.stream and format == "json" then
        error("--format json is not supported with --stream for ask")
    end

    local tool_names = options.tool or {}
    if options.stream and #tool_names > 0 then
        error("--stream is not supported with --tool for ask")
    end

    local prompt = table.concat(command.positionals.prompt, " ")
    if prompt == "" then
        error("ask requires a prompt")
    end

    local thinking_started = false
    local response_started = false
    local should_wrap_thinking_json = format == "json" and thinking_enabled(options.think)
    local request_format = format
    if should_wrap_thinking_json then
        request_format = nil
    end

    if #tool_names > 0 then
        local response = run_chat_with_tools(client, options, prompt, request_format, format ~= "json")
        local message = response.message or {}
        if format == "json" then
            print(json.encode(build_json_output({ response = message.content or "", thinking = message.thinking }, should_wrap_thinking_json), options.pretty))
        else
            if message.thinking ~= nil and message.thinking ~= "" then
                print("thinking: " .. message.thinking)
            end

            print("assistant: " .. (message.content or ""))
        end
        return
    end

    local function on_stream(event)
        if event.thinking ~= nil and event.thinking ~= "" then
            if not thinking_started then
                yaaf.write("thinking: ")
                thinking_started = true
            end

            yaaf.write(event.thinking)
            yaaf.flush()
        end

        if event.response ~= nil and event.response ~= "" then
            if thinking_started and not response_started then
                yaaf.write("\n")
            end

            if not response_started then
                yaaf.write("assistant: ")
                response_started = true
            end

            yaaf.write(event.response)
            yaaf.flush()
        end
    end

    local response = client.generate({
        endpoint = options.endpoint,
        model = options.model,
        prompt = prompt,
        stream = options.stream,
        think = options.think,
        format = request_format,
        on_stream = options.stream and on_stream or nil,
    })

    if format == "json" then
        print(json.encode(build_json_output(response, should_wrap_thinking_json), options.pretty))
    elseif options.stream then
        local printed_output = thinking_started or response_started

        if not thinking_started and response.thinking ~= nil and response.thinking ~= "" then
            yaaf.write("thinking: " .. response.thinking .. "\n")
            printed_output = true
        end

        if not response_started and response.response ~= nil and response.response ~= "" then
            yaaf.write("assistant: " .. response.response)
            printed_output = true
        end

        if not printed_output then
            yaaf.write("assistant:")
        end

        yaaf.write("\n")
    else
        if response.thinking ~= nil and response.thinking ~= "" then
            print("thinking: " .. response.thinking)
        end

        print("assistant: " .. response.response)
    end
end

return yaaf.command({
    description = "Ask the configured model a question",
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
            description = "Stream model output incrementally",
        },
        {
            name = "think",
            flags = { "--think" },
            type = "string",
            default = "none",
            description = "Thinking level to request from the selected model",
        },
        {
            name = "format",
            flags = { "--format" },
            type = "string",
            description = "Output format, such as json or a JSON schema object",
        },
        {
            name = "pretty",
            flags = { "--pretty" },
            type = "flag",
            description = "Pretty-print JSON output",
        },
        {
            name = "tool",
            flags = { "--tool" },
            type = "strings",
            description = "Registered tool made available to ask; may be passed multiple times",
        },
        {
            name = "mcp",
            flags = { "--mcp" },
            type = "string",
            description = "MCP server configuration file used for this ask run",
        },
    },
    positionals = {
        {
            name = "prompt",
            type = "string",
            multiple = true,
            required = true,
            description = "Prompt to send to the selected model",
        },
    },
    run = run,
})
