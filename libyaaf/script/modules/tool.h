#pragma once

extern "C"
{
struct lua_State;
}

namespace yaaf::script::modules
{
/**
 * Registers the native tool registry as `require("tool")`.
 *
 * The registry is scoped to the current Lua state. Built-in Lua tool implementations stay under `lua/tools/`,
 * custom tools are retained as Lua function references, and MCP tools are projected through the native `mcp` module.
 */
void register_tool_module(lua_State *state);
} // namespace yaaf::script::modules