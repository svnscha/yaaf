#include "lua_module_utils.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace yaaf::script::modules::lua_module_utils
{
namespace
{
[[nodiscard]] nlohmann::json lua_object_to_json(lua_State *state, int index, const JsonConversionOptions &options)
{
    bool is_array = true;
    std::size_t count = 0;
    std::size_t max_index = 0;

    lua_pushnil(state);
    while (lua_next(state, index) != 0)
    {
        ++count;
        if (lua_type(state, -2) == LUA_TNUMBER)
        {
            const auto numeric_key = lua_tonumber(state, -2);
            const auto integer_key = static_cast<std::size_t>(numeric_key);
            if (numeric_key < 1 || static_cast<double>(integer_key) != numeric_key)
            {
                is_array = false;
            }
            else
            {
                max_index = std::max(max_index, integer_key);
            }
        }
        else
        {
            is_array = false;
        }

        lua_pop(state, 1);
    }

    is_array = is_array && count == max_index;
    if (is_array)
    {
        nlohmann::json payload = nlohmann::json::array();
        for (std::size_t array_index = 1; array_index <= max_index; ++array_index)
        {
            lua_rawgeti(state, index, static_cast<int>(array_index));
            payload.push_back(lua_to_json(state, -1, options));
            lua_pop(state, 1);
        }

        return payload;
    }

    nlohmann::json payload = nlohmann::json::object();
    lua_pushnil(state);
    while (lua_next(state, index) != 0)
    {
        std::string key;
        if (lua_type(state, -2) == LUA_TSTRING)
        {
            key = lua_tostring(state, -2);
        }
        else if (options.object_key_mode == JsonObjectKeyMode::StringOrNumber && lua_type(state, -2) == LUA_TNUMBER)
        {
            key = fmt::format("{}", lua_tonumber(state, -2));
        }
        else
        {
            lua_pop(state, 1);
            throw std::invalid_argument(options.invalid_key_error);
        }

        payload[key] = lua_to_json(state, -1, options);
        lua_pop(state, 1);
    }

    return payload;
}
} // namespace

int absolute_index(lua_State *state, int index)
{
    return index > 0 ? index : lua_gettop(state) + index + 1;
}

[[noreturn]] void throw_lua_error(lua_State *state, const std::string &message)
{
    lua_pushlstring(state, message.c_str(), message.size());
    lua_error(state);
    std::abort();
}

std::string lua_error_message(lua_State *state)
{
    const char *message = lua_tostring(state, -1);
    return message != nullptr ? std::string(message) : std::string("unknown Lua error");
}

void require_module(lua_State *state, std::string_view module_name)
{
    lua_getglobal(state, "require");
    lua_pushlstring(state, module_name.data(), module_name.size());
    if (lua_pcall(state, 1, 1, 0) != 0)
    {
        auto message = lua_error_message(state);
        lua_pop(state, 1);
        throw std::runtime_error(message);
    }
}

nlohmann::json lua_to_json(lua_State *state, int index, const JsonConversionOptions &options)
{
    index = absolute_index(state, index);

    switch (lua_type(state, index))
    {
    case LUA_TNIL:
        return nullptr;
    case LUA_TBOOLEAN:
        return lua_toboolean(state, index) != 0;
    case LUA_TNUMBER:
        if (lua_isinteger(state, index) != 0)
        {
            return static_cast<std::int64_t>(lua_tointeger(state, index));
        }
        return lua_tonumber(state, index);
    case LUA_TSTRING:
        return std::string(lua_tostring(state, index));
    case LUA_TTABLE:
        return lua_object_to_json(state, index, options);
    default:
        throw std::invalid_argument(options.unsupported_value_error);
    }
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
} // namespace yaaf::script::modules::lua_module_utils
