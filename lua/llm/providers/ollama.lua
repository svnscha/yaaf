local http = require("http")
local json = require("json")
local yaaf = require("yaaf")

local M = {}

local function with_defaults(request)
    local payload = {}
    for key, value in pairs(request or {}) do
        if key ~= "provider" and key ~= "on_stream" then
            payload[key] = value
        end
    end

    if payload.endpoint == nil or payload.endpoint == "" then
        payload.endpoint = yaaf.defaults.endpoint
    end
    if payload.model == nil or payload.model == "" then
        payload.model = yaaf.defaults.model
    end
    if payload.think == nil or payload.think == "" or payload.think == "none" then
        payload.think = false
    end

    if request ~= nil and request.on_stream ~= nil then
        payload.stream = true
    elseif payload.stream == nil then
        payload.stream = false
    end

    return payload
end

local function decode_response(response)
    if response.status_code < 200 or response.status_code >= 300 then
        error("ollama request failed with status " .. tostring(response.status_code) .. ": " .. (response.body or ""))
    end

    local ok, payload = pcall(json.decode, response.body or "")
    if not ok then
        error("ollama returned invalid JSON: " .. tostring(payload))
    end

    return payload
end

local function finish_stream(buffer, last_payload, on_stream)
    if buffer ~= "" then
        local payload = json.decode(buffer)
        last_payload = payload
        if on_stream then
            on_stream(payload)
        end
    end

    return last_payload
end

local function new_aggregate(path)
    if path == "/api/generate" then
        return { response = "", thinking = "" }
    end

    return { message = { content = "", thinking = "" } }
end

local function aggregate_stream_payload(path, aggregate, payload)
    if path == "/api/generate" then
        aggregate.response = aggregate.response .. (payload.response or "")
        aggregate.thinking = aggregate.thinking .. (payload.thinking or "")
        return
    end

    local message = payload.message or {}
    aggregate.message.content = aggregate.message.content .. (message.content or "")
    aggregate.message.thinking = aggregate.message.thinking .. (message.thinking or "")
end

local function apply_aggregate(path, last_payload, aggregate)
    if last_payload == nil then
        return nil
    end

    if path == "/api/generate" then
        if aggregate.response ~= "" then
            last_payload.response = aggregate.response
        end
        if aggregate.thinking ~= "" then
            last_payload.thinking = aggregate.thinking
        end
        return last_payload
    end

    last_payload.message = last_payload.message or {}
    if aggregate.message.content ~= "" then
        last_payload.message.content = aggregate.message.content
    end
    if aggregate.message.thinking ~= "" then
        last_payload.message.thinking = aggregate.message.thinking
    end
    return last_payload
end

local function post_json(request, path)
    local payload = with_defaults(request)
    local last_payload = nil

    if request.on_stream then
        local buffer = ""
        local aggregate = new_aggregate(path)
        local response = http.post({
            url = payload.endpoint .. path,
            body = json.encode(payload),
            content_type = "application/json",
            on_response_chunk = function(chunk)
                buffer = buffer .. (chunk or "")
                while true do
                    local newline = buffer:find("\n", 1, true)
                    if newline == nil then
                        break
                    end

                    local line = buffer:sub(1, newline - 1)
                    buffer = buffer:sub(newline + 1)
                    if line ~= "" then
                        local decoded = json.decode(line)
                        last_payload = decoded
                        aggregate_stream_payload(path, aggregate, decoded)
                        request.on_stream(decoded)
                    end
                end
            end,
        })

        if last_payload == nil and response.body ~= nil and response.body ~= "" then
            return decode_response(response)
        end

        local final_payload = finish_stream(buffer, last_payload, function(decoded)
            aggregate_stream_payload(path, aggregate, decoded)
            request.on_stream(decoded)
        end)
        return apply_aggregate(path, final_payload, aggregate)
    end

    return decode_response(http.post({
        url = payload.endpoint .. path,
        body = json.encode(payload),
        content_type = "application/json",
    }))
end

function M.generate(request)
    return post_json(request, "/api/generate")
end

function M.chat(request)
    return post_json(request, "/api/chat")
end

function M.embed(request)
    return post_json(request, "/api/embed")
end

return M