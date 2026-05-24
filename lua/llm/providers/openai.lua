local http = require("http")
local json = require("json")
local yaaf = require("yaaf")

local M = {}

local kDefaultEndpoint = "https://api.openai.com/v1"

local function trim_trailing_slashes(value)
    return (value or ""):gsub("/*$", "")
end

local function default_openai_endpoint(requested_endpoint)
    local configured = os.getenv("YAAF_OPENAI_ENDPOINT") or ""
    if requested_endpoint == nil or requested_endpoint == "" then
        if configured ~= "" then
            return configured
        end
        return kDefaultEndpoint
    end
    return requested_endpoint
end
local function default_openai_api_key(request)
    if request ~= nil and type(request.api_key) == "string" and request.api_key ~= "" then
        return request.api_key
    end

    return os.getenv("YAAF_OPENAI_API_KEY") or ""
end

local function first_configured_env(names)
    for _, name in ipairs(names or {}) do
        local value = os.getenv(name) or ""
        if value ~= "" then
            return value
        end
    end

    return ""
end

local function with_defaults(request, fallback_model_env_names, missing_model_message)
    local payload = {}
    for key, value in pairs(request or {}) do
        if key ~= "provider" and key ~= "on_stream" then
            payload[key] = value
        end
    end

    if payload.model == nil or payload.model == "" then
        payload.model = first_configured_env(fallback_model_env_names)
    end

    if payload.model == nil or payload.model == "" then
        error(missing_model_message)
    end

    payload.endpoint = trim_trailing_slashes(default_openai_endpoint(payload.endpoint))
    payload.api_key = default_openai_api_key(payload)

    if request ~= nil and request.on_stream ~= nil then
        payload.stream = true
    elseif payload.stream == nil then
        payload.stream = false
    end

    return payload
end

local function first_choice(payload)
    if type(payload) ~= "table" or type(payload.choices) ~= "table" then
        return nil
    end
    return payload.choices[1]
end

local function decode_tool_arguments(arguments)
    if type(arguments) == "table" then
        return arguments
    end

    if arguments == nil or arguments == "" then
        return {}
    end

    if type(arguments) ~= "string" then
        error("openai tool call arguments must be a JSON object or JSON-encoded object string")
    end

    local ok, decoded = pcall(json.decode, arguments)
    if not ok or type(decoded) ~= "table" then
        error("openai tool call arguments must decode to a JSON object")
    end

    return decoded
end
local function flatten_message_content(content)
    if type(content) == "string" then
        return content
    end

    if type(content) ~= "table" then
        return ""
    end

    local parts = {}
    for _, part in ipairs(content) do
        if type(part) == "string" then
            table.insert(parts, part)
        elseif type(part) == "table" then
            if part.type == "text" and type(part.text) == "string" then
                table.insert(parts, part.text)
            elseif part.type == "output_text" and type(part.text) == "string" then
                table.insert(parts, part.text)
            end
        end
    end

    return table.concat(parts, "")
end

local function encode_message_content(message)
    if type(message) ~= "table" or type(message.images) ~= "table" or #message.images == 0 then
        return message and message.content or ""
    end

    local content = {}
    if type(message.content) == "string" and message.content ~= "" then
        table.insert(content, { type = "text", text = message.content })
    end

    for _, image in ipairs(message.images) do
        table.insert(content, {
            type = "image_url",
            image_url = { url = image },
        })
    end

    return content
end

local function synthetic_tool_call_id(message_index, tool_index)
    return string.format("yaaf_tool_%d_%d", message_index, tool_index)
end

local function encode_tool_call(tool_call, id)
    local function_payload = tool_call and tool_call["function"] or {}
    return {
        id = id,
        type = tool_call and tool_call.type or "function",
        ["function"] = {
            name = function_payload.name or "",
            arguments = json.encode(function_payload.arguments or {}),
        },
    }
end

local function encode_messages(messages)
    local payload = {}
    local pending_tool_call_ids = {}

    for message_index, message in ipairs(messages or {}) do
        local encoded = {
            role = message.role or "user",
        }

        local content = encode_message_content(message)
        if content ~= nil and not (type(content) == "string" and content == "" and type(message.tool_calls) == "table" and #message.tool_calls > 0) then
            encoded.content = content
        end

        if encoded.role == "tool" then
            encoded.tool_call_id = table.remove(pending_tool_call_ids, 1) or synthetic_tool_call_id(message_index, 1)
            encoded.content = message.content or ""
        elseif type(message.tool_calls) == "table" and #message.tool_calls > 0 then
            encoded.tool_calls = {}
            for tool_index, tool_call in ipairs(message.tool_calls) do
                local id = synthetic_tool_call_id(message_index, tool_index)
                table.insert(pending_tool_call_ids, id)
                table.insert(encoded.tool_calls, encode_tool_call(tool_call, id))
            end
        end

        table.insert(payload, encoded)
    end

    return payload
end

local function encode_tools(tools)
    local payload = {}
    for _, tool in ipairs(tools or {}) do
        local function_payload = tool["function"] or {}
        table.insert(payload, {
            type = tool.type or "function",
            ["function"] = {
                name = function_payload.name or "",
                description = function_payload.description,
                parameters = function_payload.parameters or function_payload.arguments or { type = "object" },
            },
        })
    end
    return payload
end

local function apply_options(body, request)
    local options = request.options
    if type(options) == "table" then
        if options.seed ~= nil then
            body.seed = options.seed
        end
        if options.temperature ~= nil then
            body.temperature = options.temperature
        end
        if options.top_p ~= nil then
            body.top_p = options.top_p
        end
        if options.stop ~= nil then
            body.stop = options.stop
        end
        if options.num_predict ~= nil then
            body.max_tokens = options.num_predict
        end
        if type(options.extra) == "table" then
            for key, value in pairs(options.extra) do
                body[key] = value
            end
        end
    end

    if request.logprobs ~= nil then
        body.logprobs = request.logprobs
    end
    if request.top_logprobs ~= nil then
        body.top_logprobs = request.top_logprobs
    end

    if body.reasoning_effort == nil then
        if request.think == true then
            body.reasoning_effort = "medium"
        elseif type(request.think) == "string" and request.think ~= "" and request.think ~= "none" then
            body.reasoning_effort = request.think
        end
    end
end

local function apply_response_format(body, request)
    if request.format == nil then
        return
    end

    if request.format == "json" then
        body.response_format = { type = "json_object" }
        return
    end

    if type(request.format) == "table" then
        body.response_format = {
            type = "json_schema",
            json_schema = {
                name = "yaaf_response",
                schema = request.format,
            },
        }
    end
end

local function build_headers(payload)
    local headers = {
        Accept = "application/json",
    }

    if payload.api_key ~= nil and payload.api_key ~= "" then
        headers.Authorization = "Bearer " .. payload.api_key
    end

    if payload.organization ~= nil and payload.organization ~= "" then
        headers["OpenAI-Organization"] = payload.organization
    end

    if payload.project ~= nil and payload.project ~= "" then
        headers["OpenAI-Project"] = payload.project
    end

    if type(payload.headers) == "table" then
        for key, value in pairs(payload.headers) do
            headers[key] = value
        end
    end

    return headers
end

local function decode_response(response)
    local ok, payload = pcall(json.decode, response.body or "")
    if response.status_code < 200 or response.status_code >= 300 then
        if ok and type(payload) == "table" and type(payload.error) == "table" and type(payload.error.message) == "string" then
            error("openai request failed with status " .. tostring(response.status_code) .. ": " .. payload.error.message)
        end
        error("openai request failed with status " .. tostring(response.status_code) .. ": " .. (response.body or ""))
    end

    if not ok then
        error("openai returned invalid JSON: " .. tostring(payload))
    end

    return payload
end

local function normalize_tool_calls(tool_calls)
    local payload = {}
    for _, tool_call in ipairs(tool_calls or {}) do
        local function_payload = tool_call["function"] or {}
        table.insert(payload, {
            type = tool_call.type or "function",
            ["function"] = {
                name = function_payload.name or "",
                arguments = decode_tool_arguments(function_payload.arguments),
            },
        })
    end
    return payload
end

local function normalize_chat_response(payload)
    local choice = first_choice(payload) or {}
    local message = choice.message or {}
    local response = {
        model = payload.model or "",
        created_at = tostring(payload.created or ""),
        message = {
            role = message.role or "assistant",
            content = flatten_message_content(message.content),
            tool_calls = normalize_tool_calls(message.tool_calls),
        },
        done = true,
        done_reason = choice.finish_reason or "stop",
    }

    if type(payload.usage) == "table" and payload.usage.prompt_tokens ~= nil then
        response.prompt_eval_count = payload.usage.prompt_tokens
    end

    return response
end

local function new_stream_aggregate(model)
    return {
        model = model or "",
        created_at = "",
        done = false,
        done_reason = "",
        prompt_eval_count = nil,
        message = {
            role = "assistant",
            content = "",
            tool_calls = {},
        },
    }
end

local function ensure_stream_tool_call(tool_calls, index)
    local lua_index = (tonumber(index) or 0) + 1
    while #tool_calls < lua_index do
        table.insert(tool_calls, {
            type = "function",
            ["function"] = {
                name = "",
                arguments = "",
            },
        })
    end
    return tool_calls[lua_index]
end

local function process_stream_payload(decoded, aggregate, on_stream)
    local choice = first_choice(decoded)
    if decoded.model ~= nil and decoded.model ~= "" then
        aggregate.model = decoded.model
    end
    if decoded.created ~= nil then
        aggregate.created_at = tostring(decoded.created)
    end
    if type(decoded.usage) == "table" and decoded.usage.prompt_tokens ~= nil then
        aggregate.prompt_eval_count = decoded.usage.prompt_tokens
    end
    if choice == nil then
        return
    end

    local delta = choice.delta or {}
    if delta.role ~= nil and delta.role ~= "" then
        aggregate.message.role = delta.role
    end
    local content = flatten_message_content(delta.content)
    if content ~= "" then
        aggregate.message.content = aggregate.message.content .. content
    end

    if type(delta.tool_calls) == "table" then
        for _, tool_delta in ipairs(delta.tool_calls) do
            local aggregate_tool = ensure_stream_tool_call(aggregate.message.tool_calls, tool_delta.index)
            aggregate_tool.type = tool_delta.type or aggregate_tool.type or "function"

            local function_delta = tool_delta["function"] or {}
            if type(function_delta.name) == "string" and function_delta.name ~= "" then
                aggregate_tool["function"].name = aggregate_tool["function"].name .. function_delta.name
            end
            if type(function_delta.arguments) == "string" and function_delta.arguments ~= "" then
                aggregate_tool["function"].arguments = aggregate_tool["function"].arguments .. function_delta.arguments
            end
        end
    end

    if choice.finish_reason ~= nil and choice.finish_reason ~= "" then
        aggregate.done = true
        aggregate.done_reason = choice.finish_reason
    end

    if on_stream ~= nil and (content ~= "" or type(delta.tool_calls) == "table" or aggregate.done) then
        on_stream({
            model = aggregate.model,
            created_at = aggregate.created_at,
            message = {
                role = delta.role or aggregate.message.role,
                content = content,
                tool_calls = type(delta.tool_calls) == "table" and normalize_tool_calls(aggregate.message.tool_calls) or {},
            },
            done = choice.finish_reason ~= nil and choice.finish_reason ~= "",
            done_reason = choice.finish_reason or "",
        })
    end
end

local function consume_sse_buffer(buffer, on_data)
    while true do
        local newline = buffer:find("\n", 1, true)
        if newline == nil then
            break
        end

        local line = buffer:sub(1, newline - 1)
        buffer = buffer:sub(newline + 1)
        if line:sub(-1) == "\r" then
            line = line:sub(1, -2)
        end

        if line:sub(1, 5) == "data:" then
            local data = line:sub(6)
            if data:sub(1, 1) == " " then
                data = data:sub(2)
            end
            if data ~= "" and data ~= "[DONE]" then
                on_data(json.decode(data))
            end
        end
    end

    return buffer
end

local function chat_request_body(payload)
    local body = {
        model = payload.model,
        messages = encode_messages(payload.messages),
        stream = payload.stream,
    }

    if type(payload.tools) == "table" and #payload.tools > 0 then
        body.tools = encode_tools(payload.tools)
    end

    apply_response_format(body, payload)
    apply_options(body, payload)
    return body
end

local function run_chat_completion(request)
    local payload = with_defaults(
        request,
        { "YAAF_OPENAI_MODEL" },
        "openai model is required; pass --model or set YAAF_OPENAI_MODEL"
    )
    local body = chat_request_body(payload)
    local headers = build_headers(payload)

    if request.on_stream ~= nil then
        local aggregate = new_stream_aggregate(payload.model)
        local buffer = ""
        local saw_stream_data = false
        local response = http.post({
            url = payload.endpoint .. "/chat/completions",
            body = json.encode(body),
            content_type = "application/json",
            headers = headers,
            on_response_chunk = function(chunk)
                saw_stream_data = true
                buffer = buffer .. (chunk or "")
                buffer = consume_sse_buffer(buffer, function(decoded)
                    process_stream_payload(decoded, aggregate, request.on_stream)
                end)
            end,
        })

        if not saw_stream_data then
            if response.body ~= nil and response.body ~= "" and response.body:find("data:", 1, true) == 1 then
                buffer = buffer .. response.body
                buffer = consume_sse_buffer(buffer, function(decoded)
                    process_stream_payload(decoded, aggregate, request.on_stream)
                end)
            else
                return normalize_chat_response(decode_response(response))
            end
        end
        if buffer ~= "" then
            local decoded = json.decode(buffer)
            process_stream_payload(decoded, aggregate, request.on_stream)
        end

        return {
            model = aggregate.model,
            created_at = aggregate.created_at,
            message = {
                role = aggregate.message.role,
                content = aggregate.message.content,
                tool_calls = normalize_tool_calls(aggregate.message.tool_calls),
            },
            done = true,
            done_reason = aggregate.done_reason ~= "" and aggregate.done_reason or "stop",
            prompt_eval_count = aggregate.prompt_eval_count,
        }
    end

    return normalize_chat_response(decode_response(http.post({
        url = payload.endpoint .. "/chat/completions",
        body = json.encode(body),
        content_type = "application/json",
        headers = headers,
    })))
end

function M.generate(request)
    local payload = {}
    for key, value in pairs(request or {}) do
        payload[key] = value
    end

    payload.messages = {}
    if type(request.system) == "string" and request.system ~= "" then
        table.insert(payload.messages, { role = "system", content = request.system })
    end
    table.insert(payload.messages, { role = "user", content = request.prompt or "" })

    if request.on_stream ~= nil then
        payload.on_stream = function(event)
            request.on_stream({
                model = event.model,
                created_at = event.created_at,
                response = event.message and event.message.content or "",
                done = event.done,
                done_reason = event.done_reason,
            })
        end
    end

    local response = run_chat_completion(payload)
    return {
        model = response.model,
        created_at = response.created_at,
        response = response.message and response.message.content or "",
        done = response.done,
        done_reason = response.done_reason,
        prompt_eval_count = response.prompt_eval_count,
    }
end

function M.chat(request)
    return run_chat_completion(request or {})
end

function M.embed(request)
    local payload = with_defaults(
        request,
        { "YAAF_OPENAI_EMBED_MODEL", "YAAF_OPENAI_MODEL" },
        "openai embedding model is required; pass --model or set YAAF_OPENAI_EMBED_MODEL"
    )
    local body = {
        model = payload.model,
        input = payload.input,
    }
    if payload.dimensions ~= nil then
        body.dimensions = payload.dimensions
    end
    apply_options(body, payload)

    local response = decode_response(http.post({
        url = payload.endpoint .. "/embeddings",
        body = json.encode(body),
        content_type = "application/json",
        headers = build_headers(payload),
    }))

    local embeddings = {}
    for _, item in ipairs(response.data or {}) do
        table.insert(embeddings, item.embedding or {})
    end

    local result = {
        model = response.model or payload.model,
        embeddings = embeddings,
    }
    if type(response.usage) == "table" and response.usage.prompt_tokens ~= nil then
        result.prompt_eval_count = response.usage.prompt_tokens
    end
    return result
end

return M
