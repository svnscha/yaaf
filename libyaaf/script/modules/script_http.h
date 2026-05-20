#pragma once

#include "script_llm.h"

struct lua_State;

namespace yaaf::script
{
struct ScriptHttpContext
{
    HttpClient::Options http;
    const Services *services = nullptr;
};

namespace modules
{
/**
 * Registers the low-level HTTP bridge as `require("http")`.
 *
 * The module exposes direct GET and POST helpers for Lua providers that need transport access
 * without depending on provider-specific native bindings.
 */
void register_http_module(lua_State *state, ScriptHttpContext &context);
} // namespace modules
} // namespace yaaf::script
