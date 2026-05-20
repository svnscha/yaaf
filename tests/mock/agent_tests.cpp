#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/script/lua_runtime.h"
#include "../support/runtime_test_environment.h"

namespace
{
[[nodiscard]] std::string make_action_response(std::string thought, std::string action, nlohmann::json arguments)
{
    return nlohmann::json{{"type", "action"},
                          {"thought", std::move(thought)},
                          {"action", nlohmann::json{{"name", std::move(action)}, {"arguments", std::move(arguments)}}}}
        .dump();
}

[[nodiscard]] std::string make_final_response(std::string thought, std::string final_answer)
{
    return nlohmann::json{
        {"type", "final_answer"}, {"thought", std::move(thought)}, {"final_answer", std::move(final_answer)}}
        .dump();
}

[[nodiscard]] std::filesystem::path write_react_script(std::string_view body)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_react_test.lua";
    const auto lua_path = yaaf::tests::lua_root();
    const auto package_path =
        (lua_path / "?.lua").generic_string() + ";" + (lua_path / "?" / "init.lua").generic_string() + ";";

    std::ofstream script{script_path};
    script << "package.path = " << nlohmann::json(package_path).dump() << " .. package.path\n";
    script << body;
    return script_path;
}

void run_react_script(std::string_view body, const yaaf::script::Services *services = nullptr)
{
    yaaf::script::LuaRuntimeOptions options;
    options.file_path = write_react_script(body).string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";

    ASSERT_EQ(yaaf::script::run_file(options, services), EXIT_SUCCESS);
}

[[nodiscard]] std::string run_agent_script(std::string_view body, const yaaf::script::Services *services = nullptr)
{
    yaaf::script::LuaRuntimeOptions options;
    options.file_path = write_react_script(body).string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";
    std::ostringstream output;
    options.output = &output;

    EXPECT_EQ(yaaf::script::run_file(options, services), EXIT_SUCCESS);
    return output.str();
}

void expect_agent_script_failure(std::string_view body, std::string_view message_fragment)
{
    yaaf::script::LuaRuntimeOptions options;
    options.file_path = write_react_script(body).string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";

    try
    {
        (void)yaaf::script::run_file(options);
        FAIL() << "expected Lua runtime failure";
    }
    catch (const std::runtime_error &error)
    {
        EXPECT_NE(std::string(error.what()).find(message_fragment), std::string::npos);
    }
}

void expect_agent_script_failure(std::string_view body)
{
    yaaf::script::LuaRuntimeOptions options;
    options.file_path = write_react_script(body).string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";

    EXPECT_THROW((void)yaaf::script::run_file(options), std::runtime_error);
}
} // namespace

TEST(AgentTests, NativeCompleteChatParsesThinkFormatAndToolCalls)
{
    std::size_t call_count = 0;
    yaaf::script::Services services;
    services.chat = [&](const yaaf::llm::ChatRequest &request,
                        const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        ++call_count;

        if (call_count == 1)
        {
            EXPECT_TRUE(request.think.has_value());
            if (request.think.has_value())
            {
                const auto *think = std::get_if<bool>(&*request.think);
                EXPECT_NE(think, nullptr);
                if (think != nullptr)
                {
                    EXPECT_TRUE(*think);
                }
            }

            EXPECT_TRUE(request.format.has_value());
            if (request.format.has_value())
            {
                const auto *format = std::get_if<nlohmann::json>(&*request.format);
                EXPECT_NE(format, nullptr);
                if (format != nullptr)
                {
                    EXPECT_EQ(format->at("type"), "object");
                }
            }

            EXPECT_EQ(request.messages.size(), 1U);
            if (request.messages.size() == 1U)
            {
                EXPECT_EQ(request.messages.front().role, "user");
                EXPECT_EQ(request.messages.front().content, "hello");
            }
        }
        else if (call_count == 2)
        {
            EXPECT_TRUE(request.think.has_value());
            if (request.think.has_value())
            {
                const auto *think = std::get_if<std::string>(&*request.think);
                EXPECT_NE(think, nullptr);
                if (think != nullptr)
                {
                    EXPECT_EQ(*think, "high");
                }
            }

            EXPECT_TRUE(request.format.has_value());
            if (request.format.has_value())
            {
                const auto *format = std::get_if<std::string>(&*request.format);
                EXPECT_NE(format, nullptr);
                if (format != nullptr)
                {
                    EXPECT_EQ(*format, "json");
                }
            }

            EXPECT_EQ(request.messages.size(), 1U);
            if (request.messages.size() == 1U)
            {
                EXPECT_EQ(request.messages.front().tool_calls.size(), 1U);
                if (request.messages.front().tool_calls.size() == 1U)
                {
                    EXPECT_EQ(request.messages.front().tool_calls.front().function.name, "echo");
                    EXPECT_EQ(request.messages.front().tool_calls.front().function.arguments.at("text"), "hello");
                }
            }
        }
        else
        {
            EXPECT_TRUE(request.think.has_value());
            if (request.think.has_value())
            {
                const auto *think = std::get_if<bool>(&*request.think);
                EXPECT_NE(think, nullptr);
                if (think != nullptr)
                {
                    EXPECT_FALSE(*think);
                }
            }
            EXPECT_FALSE(request.format.has_value());
            EXPECT_EQ(request.messages.size(), 1U);
            if (request.messages.size() == 1U)
            {
                EXPECT_EQ(request.messages.front().content, "bye");
            }
        }

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        response.message.content = "ok";
        return response;
    };

    const auto output = run_agent_script(R"lua(
local agent = require("agent")
local first = agent.complete_chat({
    messages = {{ role = "user", content = "hello" }},
    think = true,
    format = { type = "object" },
})
print(first.message.content)

local second = agent.complete_chat({
    messages = {{ role = "assistant", content = "", tool_calls = {{ ["function"] = { name = "echo", arguments = { text = "hello" } } }} }},
    think = "high",
    format = "json",
})
print(second.message.content)

local third = agent.complete_chat({
    messages = {{ role = "user", content = "bye" }},
    think = "none",
})
print(third.message.content)
)lua",
                                         &services);

    EXPECT_EQ(call_count, 3U);
    EXPECT_EQ(output, "ok\nok\nok\n");
}

TEST(AgentTests, NativeCompleteChatRejectsInvalidThinkType)
{
    expect_agent_script_failure(R"lua(
local agent = require("agent")
agent.complete_chat({
    messages = {{ role = "user", content = "hello" }},
    think = 42,
})
)lua");
}

TEST(AgentTests, NativeCompleteChatRejectsInvalidFormatType)
{
    expect_agent_script_failure(R"lua(
local agent = require("agent")
agent.complete_chat({
    messages = {{ role = "user", content = "hello" }},
    format = true,
})
)lua",
                                "field 'format' must be a string or table");
}

TEST(AgentTests, NativeCompleteChatRejectsInvalidMessageAndToolCallShapes)
{
    expect_agent_script_failure(R"lua(
local agent = require("agent")
agent.complete_chat({
    messages = { "not a table" },
})
)lua",
                                "message entries must be tables");

    expect_agent_script_failure(R"lua(
local agent = require("agent")
agent.complete_chat({
    messages = {{ role = "assistant", content = "", tool_calls = { "not a table" } }},
})
)lua",
                                "tool call entries must be tables");
}

TEST(AgentTests, LuaReActReturnsFinalAnswerWithoutTools)
{
    std::vector<yaaf::llm::ChatRequest> requests;
    yaaf::script::Services services;
    services.chat = [&](const yaaf::llm::ChatRequest &request, const yaaf::llm::ChatStreamCallback *on_stream_event) {
        EXPECT_EQ(on_stream_event, nullptr);
        requests.push_back(request);

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        response.message.content = make_final_response("I can answer directly.", "Hello!");
        return response;
    };

    run_react_script(R"lua(
local react = require("agents.react")
local agent = react.new({ endpoint = "http://localhost:11434", model = "qwen3:0.6b", max_turns = 3, tools = {} })
local result = agent:run("Say hello.")
assert(result.content == "Hello!")
assert(result.turns == 1)
assert(#result.tool_results == 0)
)lua",
                     &services);

    ASSERT_EQ(requests.size(), 1U);
    ASSERT_GE(requests.front().messages.size(), 2U);
    ASSERT_TRUE(requests.front().format.has_value());
    EXPECT_EQ(requests.front().messages.front().role, "system");
    EXPECT_NE(requests.front().messages.front().content.find("# ReAct Agent"), std::string::npos);
    EXPECT_NE(requests.front().messages.front().content.find("## Thought Style"), std::string::npos);
    EXPECT_NE(requests.front().messages.front().content.find("<short first-person next step>"), std::string::npos);
    EXPECT_NE(requests.front().messages.front().content.find("Use an `I ...` tone"), std::string::npos);
    EXPECT_NE(requests.front().messages.front().content.find("I need to use <tool> for <item>."), std::string::npos);
    EXPECT_NE(requests.front().messages.front().content.find("Now I can answer from the tool results."),
              std::string::npos);
    EXPECT_NE(requests.front().messages.front().content.find("No tools available."), std::string::npos);
    EXPECT_EQ(requests.front().messages.back().content, "Say hello.");
}

TEST(AgentTests, LuaReActExecutesToolAndAppendsObservation)
{
    std::vector<yaaf::llm::ChatRequest> requests;
    std::size_t call_count = 0;
    yaaf::script::Services services;
    services.chat = [&](const yaaf::llm::ChatRequest &request, const yaaf::llm::ChatStreamCallback *on_stream_event) {
        EXPECT_EQ(on_stream_event, nullptr);
        requests.push_back(request);
        ++call_count;

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        response.message.content =
            call_count == 1 ? make_action_response("I should echo the text.", "echo", nlohmann::json{{"text", "hello"}})
                            : make_final_response("I know it now.", "hello");
        return response;
    };

    run_react_script(R"lua(
local react = require("agents.react")
local agent = react.new({ endpoint = "http://localhost:11434", model = "qwen3:0.6b", max_turns = 2, tools = { "echo" } })
local result = agent:run("Echo hello.")
assert(result.content == "hello")
assert(result.turns == 2)
assert(#result.tool_results == 1)
assert(result.tool_results[1].tool_name == "echo")
assert(result.tool_results[1].success == true)
assert(result.tool_results[1].metadata.arguments.text == "hello")
)lua",
                     &services);

    ASSERT_EQ(requests.size(), 2U);
    EXPECT_EQ(requests.back().messages.back().role, "tool");
    EXPECT_EQ(requests.back().messages.back().content, "hello");
    ASSERT_GE(requests.back().messages.size(), 2U);
    const auto &assistant_tool_call_message = requests.back().messages[requests.back().messages.size() - 2];
    EXPECT_EQ(assistant_tool_call_message.role, "assistant");
    ASSERT_EQ(assistant_tool_call_message.tool_calls.size(), 1U);
    EXPECT_EQ(assistant_tool_call_message.tool_calls.front().function.name, "echo");
    EXPECT_EQ(assistant_tool_call_message.tool_calls.front().function.arguments.at("text"), "hello");
}

TEST(AgentTests, LuaReActRetriesMalformedJsonBeforeToolUse)
{
    std::size_t call_count = 0;
    std::vector<yaaf::llm::ChatRequest> requests;
    yaaf::script::Services services;
    services.chat = [&](const yaaf::llm::ChatRequest &request, const yaaf::llm::ChatStreamCallback *on_stream_event) {
        EXPECT_EQ(on_stream_event, nullptr);
        ++call_count;
        requests.push_back(request);

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        if (call_count == 1)
        {
            response.message.content = "not json";
        }
        else if (call_count == 2)
        {
            response.message.content =
                make_action_response("I should echo hello first.", "echo", nlohmann::json{{"text", "hello"}});
        }
        else
        {
            response.message.content = make_final_response("I know it now.", "hello");
        }
        return response;
    };

    run_react_script(R"lua(
local react = require("agents.react")
local agent = react.new({ endpoint = "http://localhost:11434", model = "qwen3:0.6b", max_turns = 3, tools = { "echo" } })
local result = agent:run("Echo hello.")
assert(result.content == "hello")
assert(result.turns == 3)
assert(#result.tool_results == 1)
)lua",
                     &services);

    ASSERT_EQ(requests.size(), 3U);
    EXPECT_NE(requests[1].messages.back().content.find("required ReAct JSON format"), std::string::npos);
}

TEST(AgentTests, LuaReActRejectsIncompleteStructuredResponses)
{
    run_react_script(R"lua(
local json = require("json")
local react = require("agents.react")
local missing_action = react.parse_response(json.encode({ type = "action", thought = "I should use a tool." }))
local empty_final_answer = react.parse_response(json.encode({ type = "final_answer", thought = "I know it.", final_answer = "" }))
local non_object_arguments = react.parse_response(json.encode({ type = "action", thought = "I should use a tool.", action = { name = "echo", arguments = "not an object" } }))
assert(missing_action.action == "")
assert(empty_final_answer.final_answer == "")
assert(non_object_arguments.action == "")
)lua");
}

TEST(AgentTests, LuaReActReturnsMaxTurnsExceededResult)
{
    yaaf::script::Services services;
    services.chat = [&](const yaaf::llm::ChatRequest &request, const yaaf::llm::ChatStreamCallback *on_stream_event) {
        EXPECT_EQ(on_stream_event, nullptr);

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        response.message.content = make_action_response("Keep trying.", "echo", nlohmann::json{{"text", "hello"}});
        return response;
    };

    run_react_script(R"lua(
local react = require("agents.react")
local agent = react.new({ endpoint = "http://localhost:11434", model = "qwen3:0.6b", max_turns = 2, tools = { "echo" } })
local result = agent:run("Loop forever.")
assert(result.turns == 2)
assert(result.content == "Maximum turns reached without a final answer.")
assert(result.metadata.max_turns_exceeded == true)
)lua",
                     &services);
}

TEST(AgentTests, LuaToolRegistryLoadsEchoTool)
{
    run_react_script(R"lua(
local tool = require("tool")
local result = tool.execute({ "echo" }, "echo", { text = "hello" })
assert(result.tool_name == "echo")
assert(result.success == true)
assert(result.content == "hello")
assert(result.metadata.text == "hello")
assert(result.metadata.arguments.text == "hello")
)lua");
}

TEST(AgentTests, LuaToolRegistryRegistersCustomTool)
{
    run_react_script(R"lua(
local tool = require("tool")
tool.register({
    spec = {
        name = "custom_weather",
        description = "Custom weather example",
        parameters = { type = "object" },
    },
    execute = function(arguments)
        return {
            tool_name = "custom_weather",
            content = (arguments.location or "unknown") .. ": sunny",
            success = true,
            metadata = { location = arguments.location },
        }
    end,
})

local result = tool.execute({ "custom_weather" }, "custom_weather", { location = "Berlin" })
assert(result.tool_name == "custom_weather")
assert(result.success == true)
assert(result.content == "Berlin: sunny")
assert(result.metadata.location == "Berlin")
assert(result.metadata.arguments.location == "Berlin")
)lua");
}

TEST(AgentTests, LuaToolRegistryValidatesRegistrationsAndSelectionShapes)
{
    run_react_script(R"lua(
local tool = require("tool")

local ok, err = pcall(function()
    tool.register("bad")
end)
assert(ok == false)
assert(string.find(err, "tool must be a table", 1, true) ~= nil)

ok, err = pcall(function()
    tool.register({ spec = true, execute = function() return {} end })
end)
assert(ok == false)
assert(string.find(err, "tool.spec must be a table", 1, true) ~= nil)

ok, err = pcall(function()
    tool.register({ spec = { name = "" }, execute = function() return {} end })
end)
assert(ok == false)
assert(string.find(err, "tool.spec.name must be a non-empty string", 1, true) ~= nil)

ok, err = pcall(function()
    tool.register({ spec = { name = "bad_execute" }, execute = true })
end)
assert(ok == false)
assert(string.find(err, "tool.execute must be a function", 1, true) ~= nil)

ok, err = pcall(function()
    tool.specs({ true })
end)
assert(ok == false)
assert(string.find(err, "tool names must be strings", 1, true) ~= nil)

assert(tool.descriptions({}) == "No tools available.")
)lua");
}

TEST(AgentTests, LuaToolRegistryReportsDescriptionsProvidersAndFailureResults)
{
    run_react_script(R"lua(
local tool = require("tool")

tool.register({
    spec = {
        name = "custom_blank",
        description = true,
    },
    provider = {
        type = "custom",
    },
    execute = function(arguments)
        return {
            tool_name = "custom_blank",
            content = arguments.value or "",
            success = true,
        }
    end,
})

tool.register({
    spec = {
        name = "broken_return",
    },
    execute = function()
        return "bad"
    end,
})

tool.register({
    spec = {
        name = "broken_throw",
        parameters = { type = "object" },
    },
    execute = function()
        error("boom")
    end,
})

local names = tool.names()
assert(table.concat(names, ","):find("custom_blank", 1, true) ~= nil)
assert(table.concat(names, ","):find("echo", 1, true) ~= nil)

local all_specs = tool.specs()
assert(#all_specs == 0)

local specs = tool.specs({ "custom_blank" })
assert(specs[1]["function"].name == "custom_blank")
assert(type(specs[1]["function"].parameters) == "table")
assert(next(specs[1]["function"].parameters) == nil)

local descriptions = tool.descriptions({ "echo", "custom_blank" })
assert(string.find(descriptions, "Available tools:", 1, true) ~= nil)
assert(string.find(descriptions, "- custom_blank: No description provided.", 1, true) ~= nil)
assert(string.find(descriptions, "Parameters: {}", 1, true) ~= nil)

local providers = tool.providers()
assert(providers.custom_blank.type == "custom")
assert(providers.echo.type == "lua")

local created = tool.create_many({ "echo", "custom_blank" })
assert(#created == 2)

local missing_ok, missing_err = pcall(function()
    tool.create_many({ "missing" })
end)
assert(missing_ok == false)
assert(string.find(missing_err, "unknown tool: missing", 1, true) ~= nil)
assert(string.find(missing_err, "available tools:", 1, true) ~= nil)

local missing_specs_ok, missing_specs_err = pcall(function()
    tool.specs({ "missing" })
end)
assert(missing_specs_ok == false)
assert(string.find(missing_specs_err, "unknown tool: missing", 1, true) ~= nil)

local missing_descriptions_ok, missing_descriptions_err = pcall(function()
    tool.descriptions({ "missing" })
end)
assert(missing_descriptions_ok == false)
assert(string.find(missing_descriptions_err, "unknown tool: missing", 1, true) ~= nil)

local success = tool.execute({ "custom_blank" }, "custom_blank", { value = "kept" })
assert(success.success == true)
assert(success.metadata.arguments.value == "kept")

local bad_return = tool.execute({ "broken_return" }, "broken_return", { x = 1 })
assert(bad_return.success == false)
assert(bad_return.tool_name == "broken_return")
assert(bad_return.content == "tool.execute must return a table")
assert(bad_return.metadata.arguments.x == 1)

local bad_throw = tool.execute({ "broken_throw" }, "broken_throw", { y = 2 })
assert(bad_throw.success == false)
assert(bad_throw.tool_name == "broken_throw")
assert(string.find(bad_throw.content, "boom", 1, true) ~= nil)
assert(bad_throw.metadata.arguments.y == 2)

local missing_result = tool.execute({ "echo" }, "missing", {})
assert(missing_result.success == false)
assert(missing_result.tool_name == "missing")
assert(missing_result.content == "unknown tool: missing")
)lua");
}

TEST(AgentTests, LuaAgentRegistryLoadsReactAgent)
{
    run_react_script(R"lua(
local agent_registry = require("agent")
local names = agent_registry.names()
assert(#names == 1)
assert(names[1] == "react")
local agent = agent_registry.create("react", { endpoint = "http://localhost:11434", model = "qwen3:0.6b", tools = {} })
assert(type(agent.run) == "function")
)lua");
}
