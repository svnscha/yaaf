#pragma once

#include "script_llm.h"

struct lua_State;

namespace yaaf::script
{
struct AgentContext
{
    std::string default_endpoint;
    std::string default_model;
    HttpClient::Options http;
    const Services *services = nullptr;
    std::ostream *output = nullptr;
};

namespace modules
{
/**
 * Registers native agent primitives and the agent registry as `require("agent")`.
 *
 * The module exposes chat completion, output rendering, and small helper functions used by Lua agent implementations.
 */
void register_agent_module(lua_State *state, AgentContext &context);
} // namespace modules
} // namespace yaaf::script
