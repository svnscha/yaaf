#include "agent.h"
#include "lua_module_utils.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace yaaf::script::modules
{
namespace
{
using lua_module_utils::absolute_index;
using lua_module_utils::push_json;
using lua_module_utils::throw_lua_error;

[[nodiscard]] AgentContext &context(lua_State *state)
{
    return *static_cast<AgentContext *>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] nlohmann::json lua_to_json(lua_State *state, int index)
{
    return lua_module_utils::lua_to_json(
        state, index,
        {.object_key_mode = lua_module_utils::JsonObjectKeyMode::StringOnly,
         .unsupported_value_error = "unsupported Lua value type for JSON conversion",
         .invalid_key_error = "Lua table keys must be strings for agent JSON conversion"});
}

[[nodiscard]] std::string get_string_or_default(lua_State *state, int table_index, const char *field,
                                                std::string fallback)
{
    lua_getfield(state, table_index, field);
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        return fallback;
    }

    if (!lua_isstring(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument(fmt::format("field '{}' must be a string", field));
    }

    std::string value = lua_tostring(state, -1);
    lua_pop(state, 1);
    return value;
}

[[nodiscard]] std::optional<yaaf::llm::Think> get_optional_think(lua_State *state, int table_index)
{
    lua_getfield(state, table_index, "think");
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        return std::nullopt;
    }

    if (lua_isboolean(state, -1))
    {
        const bool value = lua_toboolean(state, -1) != 0;
        lua_pop(state, 1);
        return yaaf::llm::Think{value};
    }

    if (!lua_isstring(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument("field 'think' must be a boolean or string");
    }

    std::string value = lua_tostring(state, -1);
    lua_pop(state, 1);
    if (value.empty() || value == "none")
    {
        return yaaf::llm::Think{false};
    }

    return yaaf::llm::Think{std::move(value)};
}

[[nodiscard]] std::optional<yaaf::llm::Format> get_optional_format(lua_State *state, int table_index)
{
    lua_getfield(state, table_index, "format");
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        return std::nullopt;
    }

    if (lua_isstring(state, -1))
    {
        std::string value = lua_tostring(state, -1);
        lua_pop(state, 1);
        return yaaf::llm::Format{std::move(value)};
    }

    if (lua_istable(state, -1))
    {
        auto value = lua_to_json(state, -1);
        lua_pop(state, 1);
        return yaaf::llm::Format{std::move(value)};
    }

    lua_pop(state, 1);
    throw std::invalid_argument("field 'format' must be a string or table");
}

[[nodiscard]] yaaf::llm::Tool read_tool_call(lua_State *state, int table_index)
{
    yaaf::llm::Tool tool_call;

    lua_getfield(state, table_index, "function");
    const int function_index = lua_istable(state, -1) ? absolute_index(state, -1) : table_index;
    tool_call.function.name = get_string_or_default(state, function_index, "name", "");

    lua_getfield(state, function_index, "arguments");
    if (!lua_isnil(state, -1))
    {
        tool_call.function.arguments = lua_to_json(state, -1);
    }
    lua_pop(state, 1);

    if (function_index != table_index)
    {
        lua_pop(state, 1);
    }

    return tool_call;
}

[[nodiscard]] std::vector<yaaf::llm::ChatMessage> read_messages(lua_State *state, int table_index)
{
    std::vector<yaaf::llm::ChatMessage> messages;

    lua_getfield(state, table_index, "messages");
    if (!lua_istable(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument("field 'messages' must be an array of message tables");
    }

    const auto message_count = static_cast<std::size_t>(lua_rawlen(state, -1));
    messages.reserve(message_count);
    for (std::size_t index = 1; index <= message_count; ++index)
    {
        lua_rawgeti(state, -1, static_cast<int>(index));
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 2);
            throw std::invalid_argument("message entries must be tables");
        }

        const int message_index = absolute_index(state, -1);
        yaaf::llm::ChatMessage message;
        message.role = get_string_or_default(state, message_index, "role", "user");
        message.content = get_string_or_default(state, message_index, "content", "");

        lua_getfield(state, message_index, "tool_calls");
        if (lua_istable(state, -1))
        {
            const auto tool_call_count = static_cast<std::size_t>(lua_rawlen(state, -1));
            message.tool_calls.reserve(tool_call_count);
            for (std::size_t tool_index = 1; tool_index <= tool_call_count; ++tool_index)
            {
                lua_rawgeti(state, -1, static_cast<int>(tool_index));
                if (!lua_istable(state, -1))
                {
                    lua_pop(state, 3);
                    throw std::invalid_argument("tool call entries must be tables");
                }

                message.tool_calls.push_back(read_tool_call(state, absolute_index(state, -1)));
                lua_pop(state, 1);
            }
        }
        lua_pop(state, 1);

        messages.push_back(std::move(message));
        lua_pop(state, 1);
    }

    lua_pop(state, 1);
    return messages;
}

[[nodiscard]] yaaf::llm::ChatResponse run_chat(AgentContext &runtime, std::string endpoint,
                                               const yaaf::llm::ChatRequest &request)
{
    if (runtime.services != nullptr && runtime.services->chat)
    {
        return runtime.services->chat(request, nullptr);
    }

    throw std::runtime_error(fmt::format(
        "native agent.complete_chat has no backing llm service for endpoint '{}'; use the Lua llm provider instead",
        endpoint));
}

void push_chat_response(lua_State *state, const yaaf::llm::ChatResponse &response)
{
    const nlohmann::json payload = {
        {"model", response.model},
        {"message", nlohmann::json{{"role", response.message.role}, {"content", response.message.content}}},
        {"done", response.done},
        {"done_reason", response.done_reason}};
    push_json(state, payload);
}

void write_text_event(std::ostream &output, std::string_view label, const std::string &content)
{
    if (!content.empty())
    {
        output << label << ": " << content << '\n';
    }
}

[[nodiscard]] std::optional<std::string_view> agent_module_name(std::string_view name)
{
    if (name == "react")
    {
        return "agents.react";
    }
    return std::nullopt;
}

[[nodiscard]] std::string available_agents_suffix()
{
    return "available agents: react";
}

void require_module(lua_State *state, std::string_view module_name)
{
    lua_getglobal(state, "require");
    lua_pushlstring(state, module_name.data(), module_name.size());
    if (lua_pcall(state, 1, 1, 0) != LUA_OK)
    {
        const char *message = lua_tostring(state, -1);
        const auto text = message != nullptr ? std::string(message) : std::string("unknown Lua error");
        lua_pop(state, 1);
        throw std::runtime_error(text);
    }
}

void push_agent_function(lua_State *state, AgentContext &runtime, lua_CFunction function)
{
    lua_pushlightuserdata(state, &runtime);
    lua_pushcclosure(state, function, 1);
}

int lua_complete_chat(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        luaL_checktype(state, 1, LUA_TTABLE);
        const int request_index = absolute_index(state, 1);

        yaaf::llm::ChatRequest request;
        request.model = get_string_or_default(state, request_index, "model", runtime.default_model);
        request.messages = read_messages(state, request_index);
        request.stream = false;
        request.think = get_optional_think(state, request_index);
        request.format = get_optional_format(state, request_index);

        const auto endpoint = get_string_or_default(state, request_index, "endpoint", runtime.default_endpoint);
        push_chat_response(state, run_chat(runtime, endpoint, request));
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_emit(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        if (runtime.output == nullptr)
        {
            return 0;
        }

        luaL_checktype(state, 1, LUA_TTABLE);
        const int event_index = absolute_index(state, 1);
        const auto kind = get_string_or_default(state, event_index, "kind", "");
        const auto content = get_string_or_default(state, event_index, "content", "");
        const auto tool_name = get_string_or_default(state, event_index, "tool_name", "");

        if (kind == "user")
        {
            write_text_event(*runtime.output, "user", content);
        }
        else if (kind == "yaaf" || kind == "final_answer")
        {
            write_text_event(*runtime.output, "assistant", content);
        }
        else if (kind == "thought")
        {
            write_text_event(*runtime.output, "thought", content);
        }
        else if (kind == "tool_call")
        {
            lua_getfield(state, event_index, "arguments");
            const auto arguments = lua_isnil(state, -1) ? nlohmann::json::object() : lua_to_json(state, -1);
            lua_pop(state, 1);
            *runtime.output << "tool: " << tool_name << ' ' << arguments.dump() << '\n';
        }
        else if (kind == "tool_observation")
        {
            *runtime.output << "observation: " << content << '\n';
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_strip_think_tags(lua_State *state)
{
    try
    {
        std::string result = luaL_checkstring(state, 1);
        constexpr std::string_view kOpenTag = "<think>";
        constexpr std::string_view kCloseTag = "</think>";

        std::size_t search_from = 0;
        while (true)
        {
            const auto open_index = result.find(kOpenTag, search_from);
            if (open_index == std::string::npos)
            {
                break;
            }

            const auto close_index = result.find(kCloseTag, open_index + kOpenTag.size());
            const auto erase_end = close_index == std::string::npos ? result.size() : close_index + kCloseTag.size();
            result.erase(open_index, erase_end - open_index);
            search_from = open_index;
        }

        lua_pushlstring(state, result.c_str(), result.size());
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_names(lua_State *state)
{
    try
    {
        lua_newtable(state);
        lua_pushstring(state, "react");
        lua_rawseti(state, -2, 1);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_load(lua_State *state)
{
    try
    {
        const auto name = std::string(luaL_checkstring(state, 1));
        const auto module_name = agent_module_name(name);
        if (!module_name)
        {
            throw std::runtime_error(fmt::format("unknown agent: {} ({})", name, available_agents_suffix()));
        }

        require_module(state, *module_name);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_create(lua_State *state)
{
    try
    {
        const auto name = std::string(luaL_checkstring(state, 1));
        const auto module_name = agent_module_name(name);
        if (!module_name)
        {
            throw std::runtime_error(fmt::format("unknown agent: {} ({})", name, available_agents_suffix()));
        }

        require_module(state, *module_name);
        if (!lua_istable(state, -1))
        {
            throw std::runtime_error(fmt::format("agent module '{}' did not return a table", name));
        }

        lua_getfield(state, -1, "new");
        if (!lua_isfunction(state, -1))
        {
            throw std::runtime_error(fmt::format("agent module '{}' must export a new function", name));
        }

        if (lua_isnoneornil(state, 2))
        {
            lua_newtable(state);
        }
        else
        {
            lua_pushvalue(state, 2);
        }

        if (lua_pcall(state, 1, 1, 0) != LUA_OK)
        {
            const char *message = lua_tostring(state, -1);
            const auto text = message != nullptr ? std::string(message) : std::string("unknown Lua error");
            lua_pop(state, 1);
            throw std::runtime_error(text);
        }

        lua_remove(state, -2);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int open_agent_module(lua_State *state)
{
    auto &runtime = context(state);
    lua_newtable(state);

    push_agent_function(state, runtime, lua_complete_chat);
    lua_setfield(state, -2, "complete_chat");
    push_agent_function(state, runtime, lua_emit);
    lua_setfield(state, -2, "emit");
    lua_pushcfunction(state, lua_strip_think_tags);
    lua_setfield(state, -2, "strip_think_tags");
    lua_pushcfunction(state, lua_names);
    lua_setfield(state, -2, "names");
    lua_pushcfunction(state, lua_load);
    lua_setfield(state, -2, "load");
    lua_pushcfunction(state, lua_create);
    lua_setfield(state, -2, "create");

    return 1;
}
} // namespace

void register_agent_module(lua_State *state, AgentContext &context)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");
    push_agent_function(state, context, open_agent_module);
    lua_setfield(state, -2, "agent");
    lua_pop(state, 2);
}
} // namespace yaaf::script::modules
