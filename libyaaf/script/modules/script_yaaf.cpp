#include "script_yaaf.h"
#include "../../platform/platform_name.h"
#include "lua_module_utils.h"

extern "C"
{
#include <lua.h>
}

namespace yaaf::script::modules
{
namespace
{
using lua_module_utils::absolute_index;
using lua_module_utils::throw_lua_error;

[[nodiscard]] ScriptYaafContext &yaaf_context(lua_State *state)
{
    return *static_cast<ScriptYaafContext *>(lua_touserdata(state, lua_upvalueindex(1)));
}

void push_string_array(lua_State *state, const std::vector<std::string> &values)
{
    lua_newtable(state);
    int lua_index = 1;
    for (const auto &value : values)
    {
        lua_pushlstring(state, value.c_str(), value.size());
        lua_rawseti(state, -2, lua_index++);
    }
}

[[nodiscard]] nlohmann::json lua_to_json(lua_State *state, int index)
{
    return lua_module_utils::lua_to_json(state, index,
                                         {.object_key_mode = lua_module_utils::JsonObjectKeyMode::StringOnly,
                                          .unsupported_value_error = "unsupported Lua value type for JSON conversion",
                                          .invalid_key_error = "Lua metadata table keys must be strings"});
}

[[nodiscard]] nlohmann::json lua_command_metadata_to_json(lua_State *state, int index)
{
    index = absolute_index(state, index);
    if (!lua_istable(state, index))
    {
        throw std::invalid_argument("yaaf.command metadata must be a table");
    }

    nlohmann::json payload = nlohmann::json::object();
    lua_pushnil(state);
    while (lua_next(state, index) != 0)
    {
        if (lua_type(state, -2) != LUA_TSTRING)
        {
            lua_pop(state, 1);
            throw std::invalid_argument("yaaf.command metadata keys must be strings");
        }

        const std::string key = lua_tostring(state, -2);
        if (key != "run")
        {
            payload[key] = lua_to_json(state, -1);
        }
        lua_pop(state, 1);
    }

    return payload;
}

void push_json(lua_State *state, const nlohmann::json &payload)
{
    if (payload.is_null())
    {
        lua_pushnil(state);
        return;
    }

    if (payload.is_boolean())
    {
        lua_pushboolean(state, payload.get<bool>() ? 1 : 0);
        return;
    }

    if (payload.is_number_integer())
    {
        lua_pushinteger(state, static_cast<lua_Integer>(payload.get<std::int64_t>()));
        return;
    }

    if (payload.is_number_unsigned())
    {
        lua_pushinteger(state, static_cast<lua_Integer>(payload.get<std::uint64_t>()));
        return;
    }

    if (payload.is_number())
    {
        lua_pushnumber(state, payload.get<double>());
        return;
    }

    if (payload.is_string())
    {
        const auto value = payload.get<std::string>();
        lua_pushlstring(state, value.c_str(), value.size());
        return;
    }

    lua_newtable(state);
    if (payload.is_array())
    {
        int lua_index = 1;
        for (const auto &entry : payload)
        {
            push_json(state, entry);
            lua_rawseti(state, -2, lua_index++);
        }

        return;
    }

    for (auto it = payload.begin(); it != payload.end(); ++it)
    {
        push_json(state, it.value());
        lua_setfield(state, -2, it.key().c_str());
    }
}

void push_yaaf_function(lua_State *state, ScriptYaafContext &context, lua_CFunction function);
int lua_command(lua_State *state);
int lua_read_line(lua_State *state);
int lua_write(lua_State *state);
int lua_flush(lua_State *state);

int open_yaaf_module(lua_State *state)
{
    auto &runtime = yaaf_context(state);

    lua_newtable(state);

    push_string_array(state, runtime.arguments);
    lua_setfield(state, -2, "args");

    push_json(state, runtime.options);
    lua_setfield(state, -2, "options");
    push_json(state, runtime.positionals);
    lua_setfield(state, -2, "positionals");

    lua_pushlstring(state, runtime.platform.c_str(), runtime.platform.size());
    lua_setfield(state, -2, "platform");

    lua_newtable(state);
    lua_pushlstring(state, runtime.default_endpoint.c_str(), runtime.default_endpoint.size());
    lua_setfield(state, -2, "endpoint");
    lua_pushlstring(state, runtime.default_model.c_str(), runtime.default_model.size());
    lua_setfield(state, -2, "model");
    lua_setfield(state, -2, "defaults");

    push_yaaf_function(state, runtime, lua_command);
    lua_setfield(state, -2, "command");

    push_yaaf_function(state, runtime, lua_read_line);
    lua_setfield(state, -2, "read_line");

    push_yaaf_function(state, runtime, lua_write);
    lua_setfield(state, -2, "write");
    push_yaaf_function(state, runtime, lua_flush);
    lua_setfield(state, -2, "flush");

    return 1;
}

void push_yaaf_function(lua_State *state, ScriptYaafContext &context, lua_CFunction function)
{
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, function, 1);
}

int lua_command(lua_State *state)
{
    try
    {
        auto &runtime = yaaf_context(state);
        if (runtime.command_metadata != nullptr)
        {
            *runtime.command_metadata = lua_command_metadata_to_json(state, 1);
        }

        lua_newtable(state);
        lua_pushboolean(state, 1);
        lua_setfield(state, -2, "__yaaf_command");
        lua_pushboolean(state, runtime.command_metadata != nullptr ? 1 : 0);
        lua_setfield(state, -2, "metadata_only");
        push_json(state, runtime.options);
        lua_setfield(state, -2, "options");
        push_json(state, runtime.positionals);
        lua_setfield(state, -2, "positionals");

        lua_getfield(state, 1, "run");
        if (lua_isfunction(state, -1))
        {
            lua_setfield(state, -2, "run");
        }
        else
        {
            lua_pop(state, 1);
        }

        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_read_line(lua_State *state)
{
    try
    {
        auto &runtime = yaaf_context(state);
        if (runtime.input == nullptr)
        {
            lua_pushnil(state);
            return 1;
        }

        std::string line;
        if (!std::getline(*runtime.input, line))
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushlstring(state, line.c_str(), line.size());
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_write(lua_State *state)
{
    try
    {
        auto &runtime = yaaf_context(state);
        if (runtime.output == nullptr)
        {
            return 0;
        }

        const int argument_count = lua_gettop(state);
        lua_getglobal(state, "tostring");
        for (int index = 1; index <= argument_count; ++index)
        {
            lua_pushvalue(state, -1);
            lua_pushvalue(state, index);
            lua_call(state, 1, 1);
            *runtime.output << lua_tostring(state, -1);
            lua_pop(state, 1);
        }

        return 0;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_flush(lua_State *state)
{
    auto &runtime = yaaf_context(state);
    if (runtime.output != nullptr)
    {
        runtime.output->flush();
    }

    return 0;
}
} // namespace

void register_yaaf_module(lua_State *state, ScriptYaafContext &context)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, open_yaaf_module, 1);
    lua_setfield(state, -2, "yaaf");
    lua_pop(state, 2);

    lua_newtable(state);
    lua_pushlstring(state, "", 0);
    lua_rawseti(state, -2, 0);
    int lua_index = 1;
    for (const auto &argument : context.arguments)
    {
        lua_pushlstring(state, argument.c_str(), argument.size());
        lua_rawseti(state, -2, lua_index++);
    }
    lua_setglobal(state, "arg");
}
} // namespace yaaf::script::modules
