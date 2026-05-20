#pragma once

#include "../../mcp/mcp_client.h"

struct lua_State;

namespace yaaf::script
{
struct ScriptMcpContext
{
    yaaf::mcp::ClientOptions options;
    std::shared_ptr<yaaf::mcp::Client> client;
};

namespace modules
{
/**
 * Registers the MCP bridge module as `require("mcp")`.
 *
 * Lua receives normalized server, tool, and call result tables while native code owns MCP protocol behavior.
 */
void register_mcp_module(lua_State *state, ScriptMcpContext &context);
} // namespace modules
} // namespace yaaf::script
