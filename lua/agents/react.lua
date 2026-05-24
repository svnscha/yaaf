local native = require("agent")
local llm = require("llm")
local json = require("json")
local tool_registry = require("tool")

local ReactAgent = {}
ReactAgent.__index = ReactAgent

local function trim(value)
    if type(value) ~= "string" then
        return ""
    end
    return (value:match("^%s*(.-)%s*$"))
end

local function read_optional_string(payload, key)
    if type(payload) ~= "table" or type(payload[key]) ~= "string" then
        return ""
    end
    return trim(payload[key])
end

local function parse_response(text)
    local ok, payload = pcall(json.decode, text or "")
    if not ok or type(payload) ~= "table" then
        return { thought = "", action = "", action_input = {}, final_answer = "" }
    end

    local result = {
        thought = read_optional_string(payload, "thought"),
        action = "",
        action_input = {},
        final_answer = "",
    }

    local response_type = read_optional_string(payload, "type")
    if response_type == "action" and type(payload.action) == "table" then
        result.action = read_optional_string(payload.action, "name")
        if result.action == "" then
            return { thought = "", action = "", action_input = {}, final_answer = "" }
        end

        if payload.action.arguments == nil then
            result.action_input = {}
        elseif type(payload.action.arguments) == "table" then
            result.action_input = payload.action.arguments
        else
            return { thought = "", action = "", action_input = {}, final_answer = "" }
        end

        return result
    end

    if response_type == "final_answer" then
        result.final_answer = read_optional_string(payload, "final_answer")
        if result.final_answer == "" then
            return { thought = "", action = "", action_input = {}, final_answer = "" }
        end
    end

    return result
end

local function response_schema()
    local action_schema = {
        type = "object",
        properties = {
            name = { type = "string" },
            arguments = { type = "object" },
        },
        required = { "name", "arguments" },
        additionalProperties = false,
    }

    return {
        oneOf = {
            {
                type = "object",
                properties = {
                    type = { type = "string", enum = { "action" } },
                    thought = { type = "string" },
                    action = action_schema,
                },
                required = { "type", "thought", "action" },
                additionalProperties = false,
            },
            {
                type = "object",
                properties = {
                    type = { type = "string", enum = { "final_answer" } },
                    thought = { type = "string" },
                    final_answer = { type = "string" },
                },
                required = { "type", "thought", "final_answer" },
                additionalProperties = false,
            },
        },
    }
end

local function build_system_prompt(tool_descriptions)
    return [[# ReAct Agent

You are a ReAct agent. Follow the JSON protocol exactly.

## Response Contract

Return only one JSON object. Do not wrap it in Markdown fences.

For a tool action:
{"type":"action","thought":"<short first-person next step>","action":{"name":"<tool_name>","arguments":{}}}

For a final answer:
{"type":"final_answer","thought":"<short first-person next step>","final_answer":"<answer>"}

## Thought Style

- Keep `thought` to one short first-person sentence.
- Use an `I ...` tone that states the next visible step or what the latest result enables.
- Prefer `I need to ...` for tool steps and `Now I can ...` after enough observations are available.
- Examples: `I need to use <tool> for <item>.`, `I need the next required item.`, `Now I can compare the gathered results.`, or `Now I can answer from the tool results.`
- Do not write long explanations of why you chose a step.
- Do not expose private chain-of-thought; summarize the next visible step.

## Tool Use

- When a relevant tool is available, call it before giving a final answer.
- Do not answer tool-backed questions from memory.
- If the user asks for multiple items, comparisons, rankings, or recommendations, gather the required observations first.
- After every tool result, compare the observation history with the original user request.
- If any requested item or fact is still missing, continue with another action instead of finalizing.
- If no tools are available, do not invent tool calls; answer directly.

## Available Tools

]] .. tool_descriptions
end

local function requires_react_retry(parsed, raw_content, has_tools, has_tool_results)
    if not has_tools then
        return false
    end

    if parsed.action ~= "" then
        return false
    end

    if parsed.final_answer ~= "" then
        return not has_tool_results
    end

    return trim(raw_content) ~= ""
end

function ReactAgent.new(options)
    options = options or {}
    local max_turns = tonumber(options.max_turns or 10) or 10
    if max_turns <= 0 then
        error("--max-turns must be greater than zero for agent")
    end

    local tools = options.tools or {}
    local provider = options.provider or "ollama"
    tool_registry.create_many(tools)

    return setmetatable({
        provider = provider,
        client = llm.create(provider),
        endpoint = options.endpoint,
        model = options.model,
        think = options.think,
        max_turns = max_turns,
        tools = tools,
    }, ReactAgent)
end

function ReactAgent:build_messages(input, system_prompt)
    local messages = {}
    if system_prompt and system_prompt ~= "" then
        table.insert(messages, { role = "system", content = system_prompt })
    end
    table.insert(messages, { role = "user", content = input })
    return messages
end

function ReactAgent:complete_chat(messages)
    return self.client.chat({
        endpoint = self.endpoint,
        model = self.model,
        think = self.think,
        messages = messages,
        format = response_schema(),
    })
end

function ReactAgent:run(input)
    local tool_descriptions = tool_registry.descriptions(self.tools)
    local has_tools = tool_descriptions ~= "No tools available."
    local messages = self:build_messages(input, build_system_prompt(tool_descriptions))
    local tool_results = {}

    for turn = 1, self.max_turns do
        local response = self:complete_chat(messages)
        local content = native.strip_think_tags(response.message and response.message.content or "")
        local parsed = parse_response(content)

        if requires_react_retry(parsed, content, has_tools, #tool_results > 0) then
            table.insert(messages, { role = "assistant", content = content })
            table.insert(messages, {
                role = "user",
                content = "You did not follow the required ReAct JSON format. Respond with one JSON object of type action or final_answer. If a relevant tool is available, do not answer from memory before using it. If the user asked for multiple locations, comparisons, or rankings, use the tool separately for each required location before Final Answer.",
            })
        elseif parsed.action == "" then
            if parsed.final_answer ~= "" then
                native.emit({ kind = "thought", content = parsed.thought })
                native.emit({ kind = "final_answer", content = parsed.final_answer })
                return {
                    content = parsed.final_answer,
                    tool_results = tool_results,
                    turns = turn,
                    metadata = { model = response.model, provider = self.provider },
                }
            end

            native.emit({ kind = "yaaf", content = content })
            return {
                content = content,
                tool_results = tool_results,
                turns = turn,
                metadata = { model = response.model, provider = self.provider },
            }
        else
            native.emit({ kind = "thought", content = parsed.thought })
            native.emit({ kind = "tool_call", tool_name = parsed.action, arguments = parsed.action_input })

            table.insert(messages, {
                role = "assistant",
                content = parsed.thought,
                tool_calls = {
                    {
                        ["function"] = {
                            name = parsed.action,
                            arguments = parsed.action_input,
                        },
                    },
                },
            })

            local tool_result = tool_registry.execute(self.tools, parsed.action, parsed.action_input)
            native.emit({
                kind = "tool_observation",
                content = tool_result.content,
                tool_name = tool_result.tool_name,
                success = tool_result.success,
                metadata = tool_result.metadata,
            })
            table.insert(tool_results, tool_result)
            table.insert(messages, { role = "tool", content = tool_result.content })
        end
    end

    local result = {
        content = "Maximum turns reached without a final answer.",
        tool_results = tool_results,
        turns = self.max_turns,
        metadata = { max_turns_exceeded = true, provider = self.provider },
    }
    native.emit({ kind = "final_answer", content = result.content })
    return result
end

return {
    new = ReactAgent.new,
    parse_response = parse_response,
    response_schema = response_schema,
}
