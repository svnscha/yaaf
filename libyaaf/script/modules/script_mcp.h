#pragma once

#include "../../mcp/mcp_client.h"
#include "../../mcp/mcp_host.h"
#include "../../mcp/mcp_host_stdio.h"
#include <map>

struct lua_State;

namespace yaaf::script
{
/// Descriptor for a registered Lua-based prompt handler.
struct PromptInfo
{
    std::string description;
    std::vector<yaaf::mcp::PromptArgument> arguments;
    int handler_ref = LUA_NOREF;  ///< Lua registry reference to handler function
};

struct ScriptMcpContext
{
    yaaf::mcp::ClientOptions options;
    std::shared_ptr<yaaf::mcp::Client> client;
    
    /// Hosted prompts registered via mcp.register_prompt()
    std::map<std::string, PromptInfo> hosted_prompts;
    
    /// Host instance created by mcp.host_stdio()
    std::shared_ptr<yaaf::mcp::Host> host;
    
    /// StdioHost wrapper created by mcp.host_stdio()
    std::shared_ptr<yaaf::mcp::StdioHost> stdio_host;
};

namespace modules
{
/**
 * Registers the MCP bridge module as `require("mcp")`.
 *
 * Lua receives normalized server, tool, and call result tables while native code owns MCP protocol behavior.
 * Server-side hosting APIs (mcp.register_prompt, mcp.host_stdio) enable Lua scripts to act as MCP servers.
 */
void register_mcp_module(lua_State *state, ScriptMcpContext &context);
} // namespace modules
} // namespace yaaf::script
