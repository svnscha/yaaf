#pragma once

#include <nlohmann/json_fwd.hpp>

struct lua_State;

namespace yaaf::script::modules::lua_module_utils
{
enum class JsonObjectKeyMode
{
    StringOnly,
    StringOrNumber,
};

struct JsonConversionOptions
{
    JsonObjectKeyMode object_key_mode = JsonObjectKeyMode::StringOnly;
    const char *unsupported_value_error = "unsupported Lua value type for JSON conversion";
    const char *invalid_key_error = "Lua table keys must be strings for JSON conversion";
};

[[nodiscard]] int absolute_index(lua_State *state, int index);

[[noreturn]] void throw_lua_error(lua_State *state, const std::string &message);

[[nodiscard]] std::string lua_error_message(lua_State *state);

void require_module(lua_State *state, std::string_view module_name);

[[nodiscard]] nlohmann::json lua_to_json(lua_State *state, int index, const JsonConversionOptions &options = {});

void push_json(lua_State *state, const nlohmann::json &payload);
} // namespace yaaf::script::modules::lua_module_utils
