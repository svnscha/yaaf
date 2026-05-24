#include "script_mcp.h"
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

[[nodiscard]] ScriptMcpContext &context(lua_State *state)

{
    return *static_cast<ScriptMcpContext *>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] nlohmann::json lua_to_json(lua_State *state, int index)
{
    return lua_module_utils::lua_to_json(
        state, index,
        {.object_key_mode = lua_module_utils::JsonObjectKeyMode::StringOnly,
         .unsupported_value_error = "unsupported Lua value type for MCP JSON conversion",
         .invalid_key_error = "Lua MCP table keys must be strings"});
}

[[nodiscard]] yaaf::mcp::Client &client(ScriptMcpContext &runtime)
{
    if (runtime.client == nullptr)
    {
        runtime.client = std::make_shared<yaaf::mcp::Client>(runtime.options);
    }
    return *runtime.client;
}

[[nodiscard]] nlohmann::json tool_to_json(const yaaf::mcp::ToolDescriptor &tool)
{
    auto payload = nlohmann::json{{"server_id", tool.server_id},     {"name", tool.name},
                                  {"local_name", tool.local_name},   {"title", tool.title},
                                  {"description", tool.description}, {"inputSchema", tool.input_schema},
                                  {"parameters", tool.input_schema}, {"outputSchema", tool.output_schema},
                                  {"annotations", tool.annotations}};
    return payload;
}

int lua_config(lua_State *state)
{
    try
    {
        push_json(state, yaaf::mcp::config_to_json(client(context(state)).config()));
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_servers(lua_State *state)
{
    try
    {
        auto payload = nlohmann::json::array();
        for (const auto &server : client(context(state)).config().servers)
        {
            payload.push_back({{"id", server.id},
                               {"type", server.type},
                               {"supported", server.supported},
                               {"diagnostics", server.diagnostics}});
        }
        push_json(state, payload);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_diagnostics(lua_State *state)
{
    try
    {
        push_json(state, client(context(state)).diagnose_servers());
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_list_tools(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        const char *server_id = luaL_checkstring(state, 1);
        auto payload = nlohmann::json::array();
        for (const auto &tool : client(runtime).list_tools(server_id))
        {
            payload.push_back(tool_to_json(tool));
        }
        push_json(state, payload);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_call_tool(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        const char *server_id = luaL_checkstring(state, 1);
        const char *tool_name = luaL_checkstring(state, 2);
        nlohmann::json arguments = nlohmann::json::object();
        if (!lua_isnoneornil(state, 3))
        {
            arguments = lua_to_json(state, 3);
        }

        const auto result = client(runtime).call_tool(server_id, tool_name, arguments);
        push_json(state, nlohmann::json{{"tool_name", result.tool_name},
                                        {"content", result.content},
                                        {"success", result.success},
                                        {"metadata", result.metadata}});
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void push_mcp_function(lua_State *state, ScriptMcpContext &runtime, lua_CFunction function)
{
    lua_pushlightuserdata(state, &runtime);
    lua_pushcclosure(state, function, 1);
}

int open_mcp_module(lua_State *state)
{
    auto &runtime = context(state);
    lua_newtable(state);

    push_mcp_function(state, runtime, lua_config);
    lua_setfield(state, -2, "config");
    push_mcp_function(state, runtime, lua_servers);
    lua_setfield(state, -2, "servers");
    push_mcp_function(state, runtime, lua_diagnostics);
    lua_setfield(state, -2, "diagnostics");
    push_mcp_function(state, runtime, lua_list_tools);
    lua_setfield(state, -2, "list_tools");
    push_mcp_function(state, runtime, lua_call_tool);
    lua_setfield(state, -2, "call_tool");

    return 1;
}
} // namespace

void register_mcp_module(lua_State *state, ScriptMcpContext &context)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");
    push_mcp_function(state, context, open_mcp_module);
    lua_setfield(state, -2, "mcp");
    lua_pop(state, 2);
}
} // namespace yaaf::script::modules
