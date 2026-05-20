#include "tool.h"
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
using lua_module_utils::lua_error_message;
using lua_module_utils::push_json;
using lua_module_utils::require_module;
using lua_module_utils::throw_lua_error;

struct McpTool
{
    std::string name;
    std::string server_id;
    std::string tool_name;
    std::string description;
    nlohmann::json parameters = nlohmann::json::object();
};

[[nodiscard]] nlohmann::json lua_to_json(lua_State *state, int index)
{
    return lua_module_utils::lua_to_json(
        state, index,
        {.object_key_mode = lua_module_utils::JsonObjectKeyMode::StringOnly,
         .unsupported_value_error = "unsupported Lua value type for tool JSON conversion",
         .invalid_key_error = "Lua tool table keys must be strings for JSON conversion"});
}

[[nodiscard]] std::string get_field_string(lua_State *state, int table_index, const char *field,
                                           std::string fallback = {})
{
    table_index = absolute_index(state, table_index);
    lua_getfield(state, table_index, field);
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        return fallback;
    }
    if (!lua_isstring(state, -1))
    {
        lua_pop(state, 1);
        return fallback;
    }
    std::string value = lua_tostring(state, -1);
    lua_pop(state, 1);
    return value;
}

[[nodiscard]] nlohmann::json get_field_json(lua_State *state, int table_index, const char *field)
{
    table_index = absolute_index(state, table_index);
    lua_getfield(state, table_index, field);
    auto value = lua_isnil(state, -1) ? nlohmann::json::object() : lua_to_json(state, -1);
    lua_pop(state, 1);
    return value;
}

[[nodiscard]] std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

[[nodiscard]] std::string join_strings(const std::vector<std::string> &values, std::string_view separator)
{
    std::string result;
    for (const auto &value : values)
    {
        if (!result.empty())
        {
            result.append(separator);
        }
        result.append(value);
    }
    return result;
}

[[nodiscard]] std::vector<std::string> read_tool_names(lua_State *state, int index)
{
    std::vector<std::string> result;
    if (lua_isnoneornil(state, index))
    {
        return result;
    }

    luaL_checktype(state, index, LUA_TTABLE);
    index = absolute_index(state, index);
    const auto count = static_cast<std::size_t>(lua_rawlen(state, index));
    result.reserve(count);
    for (std::size_t array_index = 1; array_index <= count; ++array_index)
    {
        lua_rawgeti(state, index, static_cast<int>(array_index));
        if (!lua_isstring(state, -1))
        {
            lua_pop(state, 1);
            throw std::invalid_argument("tool names must be strings");
        }
        result.emplace_back(lua_tostring(state, -1));
        lua_pop(state, 1);
    }
    return result;
}

[[nodiscard]] std::vector<McpTool> list_mcp_tools(lua_State *state)
{
    const int stack_top = lua_gettop(state);
    std::vector<McpTool> result;

    try
    {
        require_module(state, "mcp");
        const int mcp_index = absolute_index(state, -1);

        lua_getfield(state, mcp_index, "servers");
        if (lua_pcall(state, 0, 1, 0) != 0)
        {
            lua_settop(state, stack_top);
            return result;
        }

        if (!lua_istable(state, -1))
        {
            lua_settop(state, stack_top);
            return result;
        }

        const int servers_index = absolute_index(state, -1);
        const auto server_count = static_cast<std::size_t>(lua_rawlen(state, servers_index));
        for (std::size_t server_index = 1; server_index <= server_count; ++server_index)
        {
            lua_rawgeti(state, servers_index, static_cast<int>(server_index));
            if (!lua_istable(state, -1))
            {
                lua_pop(state, 1);
                continue;
            }

            const int server_table_index = absolute_index(state, -1);
            lua_getfield(state, server_table_index, "supported");
            const bool supported = lua_toboolean(state, -1) != 0;
            lua_pop(state, 1);
            const auto server_id = get_field_string(state, server_table_index, "id");
            lua_pop(state, 1);

            if (!supported || server_id.empty())
            {
                continue;
            }

            lua_getfield(state, mcp_index, "list_tools");
            lua_pushlstring(state, server_id.c_str(), server_id.size());
            if (lua_pcall(state, 1, 1, 0) != 0)
            {
                lua_pop(state, 1);
                continue;
            }

            if (!lua_istable(state, -1))
            {
                lua_pop(state, 1);
                continue;
            }

            const int tools_index = absolute_index(state, -1);
            const auto tool_count = static_cast<std::size_t>(lua_rawlen(state, tools_index));
            for (std::size_t tool_index = 1; tool_index <= tool_count; ++tool_index)
            {
                lua_rawgeti(state, tools_index, static_cast<int>(tool_index));
                if (!lua_istable(state, -1))
                {
                    lua_pop(state, 1);
                    continue;
                }

                const int descriptor_index = absolute_index(state, -1);
                McpTool tool;
                tool.server_id = get_field_string(state, descriptor_index, "server_id", server_id);
                tool.tool_name = get_field_string(state, descriptor_index, "name");
                tool.name = get_field_string(state, descriptor_index, "local_name");
                if (tool.name.empty())
                {
                    tool.name = fmt::format("{}.{}", tool.server_id, tool.tool_name);
                }
                tool.description = get_field_string(state, descriptor_index, "description");
                if (tool.description.empty())
                {
                    tool.description = get_field_string(state, descriptor_index, "title");
                }
                tool.parameters = get_field_json(state, descriptor_index, "parameters");
                if (tool.parameters.empty())
                {
                    tool.parameters = get_field_json(state, descriptor_index, "inputSchema");
                }
                result.push_back(std::move(tool));
                lua_pop(state, 1);
            }

            lua_pop(state, 1);
        }

        lua_settop(state, stack_top);
        return result;
    }
    catch (...)
    {
        lua_settop(state, stack_top);
        return result;
    }
}

[[nodiscard]] std::optional<McpTool> find_mcp_tool(lua_State *state, const std::string &name)
{
    for (auto &tool : list_mcp_tools(state))
    {
        if (tool.name == name)
        {
            return tool;
        }
    }
    return std::nullopt;
}

int lua_mcp_execute(lua_State *state)
{
    try
    {
        const char *server_id = luaL_checkstring(state, lua_upvalueindex(1));
        const char *tool_name = luaL_checkstring(state, lua_upvalueindex(2));
        const auto arguments = lua_isnoneornil(state, 1) ? nlohmann::json::object() : lua_to_json(state, 1);

        require_module(state, "mcp");
        lua_getfield(state, -1, "call_tool");
        lua_pushstring(state, server_id);
        lua_pushstring(state, tool_name);
        push_json(state, arguments);
        if (lua_pcall(state, 3, 1, 0) != 0)
        {
            auto message = lua_error_message(state);
            lua_pop(state, 1);
            throw std::runtime_error(message);
        }
        lua_remove(state, -2);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void push_mcp_tool(lua_State *state, const McpTool &tool)
{
    lua_newtable(state);

    lua_newtable(state);
    lua_pushlstring(state, tool.name.c_str(), tool.name.size());
    lua_setfield(state, -2, "name");
    lua_pushlstring(state, tool.description.c_str(), tool.description.size());
    lua_setfield(state, -2, "description");
    push_json(state, tool.parameters);
    lua_setfield(state, -2, "parameters");
    lua_setfield(state, -2, "spec");

    lua_newtable(state);
    lua_pushstring(state, "mcp");
    lua_setfield(state, -2, "type");
    lua_pushlstring(state, tool.server_id.c_str(), tool.server_id.size());
    lua_setfield(state, -2, "server");
    lua_pushlstring(state, tool.tool_name.c_str(), tool.tool_name.size());
    lua_setfield(state, -2, "tool");
    lua_setfield(state, -2, "provider");

    lua_pushlstring(state, tool.server_id.c_str(), tool.server_id.size());
    lua_pushlstring(state, tool.tool_name.c_str(), tool.tool_name.size());
    lua_pushcclosure(state, lua_mcp_execute, 2);
    lua_setfield(state, -2, "execute");
}

[[nodiscard]] bool push_custom_tool(lua_State *state, int custom_index, const std::string &name)
{
    lua_getfield(state, custom_index, name.c_str());
    if (lua_istable(state, -1))
    {
        return true;
    }
    lua_pop(state, 1);
    return false;
}

[[nodiscard]] bool push_tool(lua_State *state, int custom_index, const std::string &name)
{
    if (name == "echo")
    {
        require_module(state, "tools.echo");
        return true;
    }
    if (push_custom_tool(state, custom_index, name))
    {
        return true;
    }
    if (auto mcp_tool = find_mcp_tool(state, name))
    {
        push_mcp_tool(state, *mcp_tool);
        return true;
    }
    return false;
}

[[nodiscard]] std::vector<std::string> all_names(lua_State *state, int custom_index)
{
    std::vector<std::string> names;
    names.emplace_back("echo");

    lua_pushnil(state);
    while (lua_next(state, custom_index) != 0)
    {
        if (lua_type(state, -2) == LUA_TSTRING)
        {
            names.emplace_back(lua_tostring(state, -2));
        }
        lua_pop(state, 1);
    }

    for (const auto &tool : list_mcp_tools(state))
    {
        names.push_back(tool.name);
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

[[nodiscard]] std::string available_suffix(lua_State *state, int custom_index)
{
    const auto names = all_names(state, custom_index);
    if (names.empty())
    {
        return "no registered tools available";
    }
    return fmt::format("available tools: {}", join_strings(names, ", "));
}

void validate_tool(lua_State *state, int tool_index)
{
    tool_index = absolute_index(state, tool_index);
    if (!lua_istable(state, tool_index))
    {
        throw std::invalid_argument("tool must be a table");
    }
    lua_getfield(state, tool_index, "spec");
    if (!lua_istable(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument("tool.spec must be a table");
    }

    const int spec_index = absolute_index(state, -1);
    lua_getfield(state, spec_index, "name");
    const bool has_name = lua_isstring(state, -1) && std::string(lua_tostring(state, -1)).empty() == false;
    lua_pop(state, 1);
    lua_pop(state, 1);
    if (!has_name)
    {
        throw std::invalid_argument("tool.spec.name must be a non-empty string");
    }

    lua_getfield(state, tool_index, "execute");
    const bool has_execute = lua_isfunction(state, -1) != 0;
    lua_pop(state, 1);
    if (!has_execute)
    {
        throw std::invalid_argument("tool.execute must be a function");
    }
}

std::vector<std::string> selected_names(lua_State *state)
{
    return read_tool_names(state, 1);
}

int lua_register_tool(lua_State *state)
{
    try
    {
        validate_tool(state, 1);
        lua_pushvalue(state, lua_upvalueindex(1));
        const int custom_index = absolute_index(state, -1);

        lua_getfield(state, 1, "spec");
        const auto name = get_field_string(state, -1, "name");
        lua_pop(state, 1);

        lua_pushvalue(state, 1);
        lua_setfield(state, custom_index, name.c_str());
        lua_pop(state, 1);

        lua_pushvalue(state, 1);
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
        lua_pushvalue(state, lua_upvalueindex(1));
        const int custom_index = absolute_index(state, -1);
        const auto names = all_names(state, custom_index);
        lua_pop(state, 1);

        lua_newtable(state);
        int lua_index = 1;
        for (const auto &name : names)
        {
            lua_pushlstring(state, name.c_str(), name.size());
            lua_rawseti(state, -2, lua_index++);
        }
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_create_many(lua_State *state)
{
    try
    {
        const auto names = selected_names(state);
        lua_pushvalue(state, lua_upvalueindex(1));
        const int custom_index = absolute_index(state, -1);

        lua_newtable(state);
        int lua_index = 1;
        for (const auto &name : names)
        {
            if (!push_tool(state, custom_index, name))
            {
                throw std::runtime_error(
                    fmt::format("unknown tool: {} ({})", name, available_suffix(state, custom_index)));
            }
            lua_rawseti(state, -2, lua_index++);
        }

        lua_remove(state, custom_index);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void push_tool_spec(lua_State *state, int tool_index)
{
    tool_index = absolute_index(state, tool_index);
    lua_getfield(state, tool_index, "spec");
    const int spec_index = absolute_index(state, -1);

    lua_newtable(state);
    lua_pushstring(state, "function");
    lua_setfield(state, -2, "type");

    lua_newtable(state);
    lua_getfield(state, spec_index, "name");
    lua_setfield(state, -2, "name");
    lua_getfield(state, spec_index, "description");
    lua_setfield(state, -2, "description");
    lua_getfield(state, spec_index, "parameters");
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        lua_newtable(state);
    }
    lua_setfield(state, -2, "parameters");
    lua_setfield(state, -2, "function");

    lua_remove(state, spec_index);
}

int lua_specs(lua_State *state)
{
    try
    {
        const auto names = selected_names(state);
        lua_pushvalue(state, lua_upvalueindex(1));
        const int custom_index = absolute_index(state, -1);

        lua_newtable(state);
        int lua_index = 1;
        for (const auto &name : names)
        {
            if (!push_tool(state, custom_index, name))
            {
                throw std::runtime_error(
                    fmt::format("unknown tool: {} ({})", name, available_suffix(state, custom_index)));
            }
            push_tool_spec(state, -1);
            lua_rawseti(state, -3, lua_index++);
            lua_pop(state, 1);
        }

        lua_remove(state, custom_index);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_descriptions(lua_State *state)
{
    try
    {
        const auto names = selected_names(state);
        if (names.empty())
        {
            lua_pushstring(state, "No tools available.");
            return 1;
        }

        lua_pushvalue(state, lua_upvalueindex(1));
        const int custom_index = absolute_index(state, -1);
        std::vector<std::string> lines;
        lines.reserve(names.size());
        for (const auto &name : names)
        {
            if (!push_tool(state, custom_index, name))
            {
                throw std::runtime_error(
                    fmt::format("unknown tool: {} ({})", name, available_suffix(state, custom_index)));
            }

            lua_getfield(state, -1, "spec");
            const int spec_index = absolute_index(state, -1);
            const auto spec_name = get_field_string(state, spec_index, "name");
            auto description = get_field_string(state, spec_index, "description");
            if (description.empty())
            {
                description = "No description provided.";
            }
            auto parameters = get_field_json(state, spec_index, "parameters");
            lines.push_back(fmt::format("- {}: {}\n  Parameters: {}", spec_name, description, parameters.dump()));
            lua_pop(state, 2);
        }

        lua_pop(state, 1);
        const auto result = fmt::format("Available tools:\n{}", join_strings(lines, "\n"));
        lua_pushlstring(state, result.c_str(), result.size());
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void push_failure_result(lua_State *state, const std::string &tool_name, const std::string &content,
                         const nlohmann::json &arguments)
{
    lua_newtable(state);
    lua_pushlstring(state, tool_name.c_str(), tool_name.size());
    lua_setfield(state, -2, "tool_name");
    lua_pushlstring(state, content.c_str(), content.size());
    lua_setfield(state, -2, "content");
    lua_pushboolean(state, 0);
    lua_setfield(state, -2, "success");
    lua_newtable(state);
    push_json(state, arguments);
    lua_setfield(state, -2, "arguments");
    lua_setfield(state, -2, "metadata");
}

void attach_arguments_metadata(lua_State *state, int result_index, const nlohmann::json &arguments)
{
    result_index = absolute_index(state, result_index);
    lua_getfield(state, result_index, "metadata");
    if (!lua_istable(state, -1))
    {
        lua_pop(state, 1);
        lua_newtable(state);
    }
    push_json(state, arguments);
    lua_setfield(state, -2, "arguments");
    lua_setfield(state, result_index, "metadata");
}

int lua_execute(lua_State *state)
{
    try
    {
        const auto names = read_tool_names(state, 1);
        const auto requested_name = std::string(luaL_checkstring(state, 2));
        const auto requested_lower = lowercase(requested_name);
        const auto arguments = lua_isnoneornil(state, 3) ? nlohmann::json::object() : lua_to_json(state, 3);

        lua_pushvalue(state, lua_upvalueindex(1));
        const int custom_index = absolute_index(state, -1);
        for (const auto &name : names)
        {
            if (!push_tool(state, custom_index, name))
            {
                throw std::runtime_error(
                    fmt::format("unknown tool: {} ({})", name, available_suffix(state, custom_index)));
            }

            lua_getfield(state, -1, "spec");
            const auto spec_name = get_field_string(state, -1, "name");
            lua_pop(state, 1);
            if (lowercase(spec_name) != requested_lower)
            {
                lua_pop(state, 1);
                continue;
            }

            lua_getfield(state, -1, "execute");
            push_json(state, arguments);
            if (lua_pcall(state, 1, 1, 0) != 0)
            {
                auto message = lua_error_message(state);
                lua_pop(state, 2);
                push_failure_result(state, spec_name, message, arguments);
                return 1;
            }

            if (!lua_istable(state, -1))
            {
                lua_pop(state, 2);
                push_failure_result(state, spec_name, "tool.execute must return a table", arguments);
                return 1;
            }

            attach_arguments_metadata(state, -1, arguments);
            lua_remove(state, -2);
            lua_remove(state, custom_index);
            return 1;
        }

        lua_pop(state, 1);
        push_failure_result(state, requested_name, fmt::format("unknown tool: {}", requested_name),
                            nlohmann::json::object());
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_providers(lua_State *state)
{
    try
    {
        lua_pushvalue(state, lua_upvalueindex(1));
        const int custom_index = absolute_index(state, -1);
        const auto names = all_names(state, custom_index);

        lua_newtable(state);
        for (const auto &name : names)
        {
            if (!push_tool(state, custom_index, name))
            {
                continue;
            }

            lua_getfield(state, -1, "provider");
            if (lua_istable(state, -1))
            {
                lua_setfield(state, -3, name.c_str());
            }
            else
            {
                lua_pop(state, 1);
                lua_newtable(state);
                lua_pushstring(state, "lua");
                lua_setfield(state, -2, "type");
                lua_setfield(state, -3, name.c_str());
            }
            lua_pop(state, 1);
        }

        lua_remove(state, custom_index);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void set_tool_function(lua_State *state, int module_index, const char *name, lua_CFunction function)
{
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, module_index, name);
}

int open_tool_module(lua_State *state)
{
    lua_newtable(state);
    const int module_index = absolute_index(state, -1);

    lua_newtable(state);
    const int custom_index = absolute_index(state, -1);

    lua_pushvalue(state, custom_index);
    lua_pushcclosure(state, lua_register_tool, 1);
    lua_setfield(state, module_index, "register");

    lua_pushvalue(state, custom_index);
    lua_pushcclosure(state, lua_names, 1);
    lua_setfield(state, module_index, "names");

    lua_pushvalue(state, custom_index);
    lua_pushcclosure(state, lua_create_many, 1);
    lua_setfield(state, module_index, "create_many");

    lua_pushvalue(state, custom_index);
    lua_pushcclosure(state, lua_specs, 1);
    lua_setfield(state, module_index, "specs");

    lua_pushvalue(state, custom_index);
    lua_pushcclosure(state, lua_descriptions, 1);
    lua_setfield(state, module_index, "descriptions");

    lua_pushvalue(state, custom_index);
    lua_pushcclosure(state, lua_execute, 1);
    lua_setfield(state, module_index, "execute");

    lua_pushvalue(state, custom_index);
    lua_pushcclosure(state, lua_providers, 1);
    lua_setfield(state, module_index, "providers");

    lua_pop(state, 1);
    return 1;
}
} // namespace

void register_tool_module(lua_State *state)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");
    lua_pushcfunction(state, open_tool_module);
    lua_setfield(state, -2, "tool");
    lua_pop(state, 2);
}
} // namespace yaaf::script::modules
