#pragma once

struct lua_State;

namespace yaaf::script::modules
{
/**
 * Registers JSON encode/decode helpers as `require("json")`.
 *
 * Lua tables are converted to JSON arrays when they use contiguous integer keys starting at 1;
 * otherwise string and numeric keys are converted to JSON object keys.
 */
void register_json_module(lua_State *state);
} // namespace yaaf::script::modules
