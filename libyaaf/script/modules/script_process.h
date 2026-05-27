#pragma once

struct lua_State;

namespace yaaf::script::modules
{

/**
 * Registers the process module as `require("process")`.
 *
 * The module provides Lua scripts with process spawning capabilities:
 * - process.start(options) -> handle
 * - handle:write(data)
 * - handle:read(timeout_ms) -> (line, error_string)
 * - handle:is_alive() -> bool
 * - handle:shutdown(timeout_ms)
 * - handle:close()
 */
void register_process_module(lua_State *state);

} // namespace yaaf::script::modules
