local yaaf = require("yaaf")

local M = {}

local function with_defaults(request)
    local payload = {}
    for key, value in pairs(request or {}) do
        if key ~= "provider" then
            payload[key] = value
        end
    end

    if payload.model == nil or payload.model == "" then
        payload.model = yaaf.defaults.model
    end

    return payload
end

local function normalize_inputs(input)
    if type(input) == "table" then
        return input
    end
    return { input or "" }
end

function M.generate(request)
    local payload = with_defaults(request)
    return {
        model = payload.model,
        response = payload.prompt or "",
        done = true,
        done_reason = "stop",
    }
end

function M.chat(request)
    local payload = with_defaults(request)
    local messages = payload.messages or {}
    local content = ""
    for index = #messages, 1, -1 do
        local message = messages[index]
        if type(message) == "table" and message.role == "user" then
            content = message.content or ""
            break
        end
    end

    return {
        model = payload.model,
        message = {
            role = "yaaf",
            content = content,
        },
        done = true,
        done_reason = "stop",
    }
end

function M.embed(request)
    local payload = with_defaults(request)
    local inputs = normalize_inputs(payload.input)
    local embeddings = {}
    for _, value in ipairs(inputs) do
        table.insert(embeddings, { #(value or "") })
    end

    return {
        model = payload.model,
        embeddings = embeddings,
        total_duration = 0,
        load_duration = 0,
        prompt_eval_count = #inputs,
    }
end

return M