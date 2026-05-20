#include "script_json.h"
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
using lua_module_utils::push_json;
using lua_module_utils::throw_lua_error;

[[nodiscard]] nlohmann::json lua_to_json(lua_State *state, int index)
{
    return lua_module_utils::lua_to_json(
        state, index,
        {.object_key_mode = lua_module_utils::JsonObjectKeyMode::StringOrNumber,
         .unsupported_value_error = "unsupported Lua value type for JSON conversion",
         .invalid_key_error = "Lua table keys must be strings or numbers for JSON conversion"});
}

int lua_encode(lua_State *state)
{
    try
    {
        const auto payload = lua_to_json(state, 1);
        const bool pretty = lua_gettop(state) >= 2 && lua_isboolean(state, 2) && lua_toboolean(state, 2) != 0;
        const auto text = pretty ? payload.dump(2) : payload.dump();
        lua_pushlstring(state, text.c_str(), text.size());
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_decode(lua_State *state)
{
    try
    {
        const char *text = luaL_checkstring(state, 1);
        push_json(state, nlohmann::json::parse(text));
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int open_json_module(lua_State *state)
{
    lua_newtable(state);
    lua_pushcfunction(state, lua_encode);
    lua_setfield(state, -2, "encode");
    lua_pushcfunction(state, lua_decode);
    lua_setfield(state, -2, "decode");
    return 1;
}
} // namespace

void register_json_module(lua_State *state)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");
    lua_pushcfunction(state, open_json_module);
    lua_setfield(state, -2, "json");
    lua_pop(state, 2);
}
} // namespace yaaf::script::modules
