#include "script_mcp.h"
#include "lua_module_utils.h"

#include <set>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

namespace yaaf::script::modules
{
namespace
{
using lua_module_utils::absolute_index;
using lua_module_utils::push_json;
using lua_module_utils::require_module;
using lua_module_utils::throw_lua_error;
using lua_module_utils::lua_error_message;

[[nodiscard]] ScriptMcpContext &context(lua_State *state)

{
    return *static_cast<ScriptMcpContext *>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] nlohmann::json lua_to_json(lua_State *state, int index)
{
    return lua_module_utils::lua_to_json(
        state, index,
        {.object_key_mode = lua_module_utils::JsonObjectKeyMode::StringOnly,
         .unsupported_value_error = "unsupported Lua value type for MCP JSON conversion",
         .invalid_key_error = "Lua MCP table keys must be strings"});
}

[[nodiscard]] yaaf::mcp::Client &client(ScriptMcpContext &runtime)
{
    if (runtime.client == nullptr)
    {
        runtime.client = std::make_shared<yaaf::mcp::Client>(runtime.options);
    }
    return *runtime.client;
}

[[nodiscard]] nlohmann::json tool_to_json(const yaaf::mcp::ToolDescriptor &tool)
{
    auto payload = nlohmann::json{{"server_id", tool.server_id},     {"name", tool.name},
                                  {"local_name", tool.local_name},   {"title", tool.title},
                                  {"description", tool.description}, {"inputSchema", tool.input_schema},
                                  {"parameters", tool.input_schema}, {"outputSchema", tool.output_schema},
                                  {"annotations", tool.annotations}};
    return payload;
}

int lua_config(lua_State *state)
{
    try
    {
        push_json(state, yaaf::mcp::config_to_json(client(context(state)).config()));
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_servers(lua_State *state)
{
    try
    {
        auto payload = nlohmann::json::array();
        for (const auto &server : client(context(state)).config().servers)
        {
            payload.push_back({{"id", server.id},
                               {"type", server.type},
                               {"supported", server.supported},
                               {"diagnostics", server.diagnostics}});
        }
        push_json(state, payload);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_diagnostics(lua_State *state)
{
    try
    {
        push_json(state, client(context(state)).diagnose_servers());
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_list_tools(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        const char *server_id = luaL_checkstring(state, 1);
        auto payload = nlohmann::json::array();
        for (const auto &tool : client(runtime).list_tools(server_id))
        {
            payload.push_back(tool_to_json(tool));
        }
        push_json(state, payload);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_call_tool(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        const char *server_id = luaL_checkstring(state, 1);
        const char *tool_name = luaL_checkstring(state, 2);
        nlohmann::json arguments = nlohmann::json::object();
        if (!lua_isnoneornil(state, 3))
        {
            arguments = lua_to_json(state, 3);
        }

        const auto result = client(runtime).call_tool(server_id, tool_name, arguments);
        push_json(state, nlohmann::json{{"tool_name", result.tool_name},
                                        {"content", result.content},
                                        {"success", result.success},
                                        {"metadata", result.metadata}});
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

/// Helper to extract prompt argument specs from Lua table.
[[nodiscard]] std::vector<yaaf::mcp::PromptArgument> extract_prompt_arguments(lua_State *state, int table_index)
{
    std::vector<yaaf::mcp::PromptArgument> result;
    if (lua_isnoneornil(state, table_index))
    {
        return result;
    }

    table_index = absolute_index(state, table_index);
    if (!lua_istable(state, table_index))
    {
        throw std::invalid_argument("prompt arguments must be a table or nil");
    }

    const auto count = static_cast<std::size_t>(lua_rawlen(state, table_index));
    result.reserve(count);
    for (std::size_t array_index = 1; array_index <= count; ++array_index)
    {
        lua_rawgeti(state, table_index, static_cast<int>(array_index));
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 1);
            throw std::invalid_argument("each prompt argument must be a table");
        }

        const int arg_index = absolute_index(state, -1);
        
        // Extract name
        lua_getfield(state, arg_index, "name");
        if (!lua_isstring(state, -1))
        {
            lua_pop(state, 2);
            throw std::invalid_argument("prompt argument 'name' must be a string");
        }
        std::string name = lua_tostring(state, -1);
        lua_pop(state, 1);

        // Extract description
        lua_getfield(state, arg_index, "description");
        std::string description = lua_isstring(state, -1) ? lua_tostring(state, -1) : "";
        lua_pop(state, 1);

        // Extract required flag
        lua_getfield(state, arg_index, "required");
        bool required = lua_toboolean(state, -1) != 0;
        lua_pop(state, 1);

        result.emplace_back(yaaf::mcp::PromptArgument{name, description, required});
        lua_pop(state, 1);
    }

    return result;
}

/// Handler for mcp.register_prompt(descriptor)
int lua_register_prompt(lua_State *state)
{
    try
    {
        auto &runtime = context(state);

        // Validate descriptor table
        if (!lua_istable(state, 1))
        {
            throw std::invalid_argument("register_prompt requires a table descriptor");
        }

        // Extract name
        lua_getfield(state, 1, "name");
        if (!lua_isstring(state, -1))
        {
            lua_pop(state, 1);
            throw std::invalid_argument("prompt descriptor 'name' must be a string");
        }
        std::string name = lua_tostring(state, -1);
        lua_pop(state, 1);

        if (name.empty())
        {
            throw std::invalid_argument("prompt name cannot be empty");
        }

        if (runtime.hosted_prompts.find(name) != runtime.hosted_prompts.end())
        {
            throw std::invalid_argument(fmt::format("prompt '{}' already registered", name));
        }

        // Extract description
        lua_getfield(state, 1, "description");
        std::string description = lua_isstring(state, -1) ? lua_tostring(state, -1) : "";
        lua_pop(state, 1);

        // Extract arguments
        lua_getfield(state, 1, "arguments");
        auto arguments = extract_prompt_arguments(state, -1);
        lua_pop(state, 1);

        // Extract and validate handler function
        lua_getfield(state, 1, "handler");
        if (!lua_isfunction(state, -1))
        {
            lua_pop(state, 1);
            throw std::invalid_argument("prompt descriptor 'handler' must be a function");
        }

        // Store handler function reference in Lua registry
        int handler_ref = luaL_ref(state, LUA_REGISTRYINDEX);

        // Store prompt info
        PromptInfo prompt_info;
        prompt_info.description = description;
        prompt_info.arguments = arguments;
        prompt_info.handler_ref = handler_ref;

        runtime.hosted_prompts[name] = std::move(prompt_info);

        lua_pushboolean(state, 1);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

/// Helper to execute a prompt handler and return messages.
[[nodiscard]] std::vector<yaaf::mcp::PromptMessage> execute_prompt_handler(
    lua_State *state, int handler_ref, const nlohmann::json &arguments)
{
    std::vector<yaaf::mcp::PromptMessage> result;

    // Get handler from registry
    lua_rawgeti(state, LUA_REGISTRYINDEX, handler_ref);
    if (!lua_isfunction(state, -1))
    {
        lua_pop(state, 1);
        throw std::runtime_error("prompt handler is no longer available in registry");
    }

    // Push arguments as Lua table
    push_json(state, arguments);

    // Call handler
    if (lua_pcall(state, 1, 1, 0) != 0)
    {
        std::string error = lua_error_message(state);
        lua_pop(state, 1);
        throw std::runtime_error(fmt::format("prompt handler failed: {}", error));
    }

    // Extract result messages array
    if (!lua_istable(state, -1))
    {
        lua_pop(state, 1);
        throw std::runtime_error("prompt handler must return a table");
    }

    const int result_index = absolute_index(state, -1);
    lua_getfield(state, result_index, "messages");
    if (!lua_istable(state, -1))
    {
        lua_pop(state, 2);
        throw std::runtime_error("prompt handler result must contain 'messages' array");
    }

    const int messages_index = absolute_index(state, -1);
    const auto msg_count = static_cast<std::size_t>(lua_rawlen(state, messages_index));
    result.reserve(msg_count);

    for (std::size_t msg_index = 1; msg_index <= msg_count; ++msg_index)
    {
        lua_rawgeti(state, messages_index, static_cast<int>(msg_index));
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 3);
            throw std::runtime_error("each message in prompt result must be a table");
        }

        const int msg_table_index = absolute_index(state, -1);

        // Extract role
        lua_getfield(state, msg_table_index, "role");
        if (!lua_isstring(state, -1))
        {
            lua_pop(state, 4);
            throw std::runtime_error("message 'role' must be a string");
        }
        std::string role = lua_tostring(state, -1);
        lua_pop(state, 1);

        // Extract content
        lua_getfield(state, msg_table_index, "content");
        if (!lua_isstring(state, -1))
        {
            lua_pop(state, 4);
            throw std::runtime_error("message 'content' must be a string");
        }
        std::string content = lua_tostring(state, -1);
        lua_pop(state, 1);

        result.emplace_back(yaaf::mcp::PromptMessage{role, content});
        lua_pop(state, 1);
    }

    lua_pop(state, 2);
    return result;
}

/// Executor callback for tools hosted via mcp.host_stdio().
/// Calls tool.execute() from the tool registry.
[[nodiscard]] yaaf::mcp::ToolExecutorResult tool_executor_callback(
    lua_State *state, const std::string &tool_name, const nlohmann::json &arguments)
{
    yaaf::mcp::ToolExecutorResult result;
    result.is_error = false;

    try
    {
        // Require tool module
        require_module(state, "tool");
        const int tool_module_index = absolute_index(state, -1);

        // Call tool.execute({}, tool_name, arguments)
        lua_getfield(state, tool_module_index, "execute");
        if (!lua_isfunction(state, -1))
        {
            lua_pop(state, 2);
            result.content = "tool.execute is not available";
            result.is_error = true;
            return result;
        }

        // Push empty tool selection array (use all tools)
        lua_newtable(state);

        // Push tool name
        lua_pushlstring(state, tool_name.c_str(), tool_name.size());

        // Push arguments
        push_json(state, arguments);

        // Call tool.execute({}, tool_name, arguments)
        if (lua_pcall(state, 3, 1, 0) != 0)
        {
            result.content = lua_error_message(state);
            lua_pop(state, 2);
            result.is_error = true;
            return result;
        }

        // Extract result table
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 2);
            result.content = "tool execution returned non-table result";
            result.is_error = true;
            return result;
        }

        const int exec_result_index = absolute_index(state, -1);

        // Extract success flag
        lua_getfield(state, exec_result_index, "success");
        bool success = lua_toboolean(state, -1) != 0;
        lua_pop(state, 1);

        // Extract content
        lua_getfield(state, exec_result_index, "content");
        result.content = lua_isstring(state, -1) ? lua_tostring(state, -1) : "";
        lua_pop(state, 1);

        result.is_error = !success;

        lua_pop(state, 2);
        return result;
    }
    catch (const std::exception &error)
    {
        result.content = fmt::format("tool executor error: {}", error.what());
        result.is_error = true;
        return result;
    }
}

/// Executor callback for prompts hosted via mcp.host_stdio().
[[nodiscard]] std::vector<yaaf::mcp::PromptMessage> prompt_executor_callback(
    lua_State *state, ScriptMcpContext &context, const std::string &prompt_name,
    const nlohmann::json &arguments)
{
    std::vector<yaaf::mcp::PromptMessage> result;

    try
    {
        auto it = context.hosted_prompts.find(prompt_name);
        if (it == context.hosted_prompts.end())
        {
            throw std::runtime_error(fmt::format("unknown prompt: {}", prompt_name));
        }

        result = execute_prompt_handler(state, it->second.handler_ref, arguments);
        return result;
    }
    catch (const std::exception &error)
    {
        // Return error in message format
        result.emplace_back(yaaf::mcp::PromptMessage{"assistant", error.what()});
        return result;
    }
}

/// Handler for mcp.host_stdio({tools, prompts})
int lua_host_stdio(lua_State *state)
{
    try
    {
        auto &runtime = context(state);

        // Get schema backend from options registry
        const auto schema_registry = runtime.options.schema_registry;
        if (!schema_registry)
        {
            throw std::runtime_error(
                "schema_registry not available in MCP options; cannot host server without schema backend");
        }

        // Get the backend for the latest protocol version
        const auto schema_backend = schema_registry->backend(schema_registry->latest_protocol_version());
        if (!schema_backend)
        {
            throw std::runtime_error(
                "failed to get schema backend for latest protocol version");
        }

        // Extract tool and prompt filter lists
        std::vector<std::string> tool_filter;
        std::vector<std::string> prompt_filter;

        if (!lua_isnoneornil(state, 1))
        {
            if (!lua_istable(state, 1))
            {
                throw std::invalid_argument("host_stdio requires a table argument or nil");
            }

            // Extract tools array
            lua_getfield(state, 1, "tools");
            if (!lua_isnil(state, -1))
            {
                if (!lua_istable(state, -1))
                {
                    lua_pop(state, 1);
                    throw std::invalid_argument("host_stdio 'tools' must be an array or nil");
                }
                const int tools_index = absolute_index(state, -1);
                const auto count = static_cast<std::size_t>(lua_rawlen(state, tools_index));
                tool_filter.reserve(count);
                for (std::size_t idx = 1; idx <= count; ++idx)
                {
                    lua_rawgeti(state, tools_index, static_cast<int>(idx));
                    if (!lua_isstring(state, -1))
                    {
                        lua_pop(state, 2);
                        throw std::invalid_argument("tool names must be strings");
                    }
                    tool_filter.emplace_back(lua_tostring(state, -1));
                    lua_pop(state, 1);
                }
            }
            lua_pop(state, 1);

            // Extract prompts array
            lua_getfield(state, 1, "prompts");
            if (!lua_isnil(state, -1))
            {
                if (!lua_istable(state, -1))
                {
                    lua_pop(state, 1);
                    throw std::invalid_argument("host_stdio 'prompts' must be an array or nil");
                }
                const int prompts_index = absolute_index(state, -1);
                const auto count = static_cast<std::size_t>(lua_rawlen(state, prompts_index));
                prompt_filter.reserve(count);
                for (std::size_t idx = 1; idx <= count; ++idx)
                {
                    lua_rawgeti(state, prompts_index, static_cast<int>(idx));
                    if (!lua_isstring(state, -1))
                    {
                        lua_pop(state, 2);
                        throw std::invalid_argument("prompt names must be strings");
                    }
                    prompt_filter.emplace_back(lua_tostring(state, -1));
                    lua_pop(state, 1);
                }
            }
            lua_pop(state, 1);
        }

        // Create tool executor callback (captures state and runtime)
        yaaf::mcp::ToolExecutor tool_executor = [state](const std::string &tool_name, const nlohmann::json &arguments) {
            return tool_executor_callback(state, tool_name, arguments);
        };

        // Create prompt executor callback (captures state and runtime context)
        yaaf::mcp::PromptExecutor prompt_executor = [state, &runtime](const std::string &prompt_name, const nlohmann::json &arguments) {
            return prompt_executor_callback(state, runtime, prompt_name, arguments);
        };

        // Create tool lister callback that retrieves available tools from Lua
        yaaf::mcp::ToolLister tool_lister = [state, tool_filter]() -> std::vector<yaaf::mcp::ToolInfo> {
            std::vector<yaaf::mcp::ToolInfo> result;
            const int stack_top = lua_gettop(state);

            try
            {
                // Require tool module
                require_module(state, "tool");
                const int tool_module_index = absolute_index(state, -1);

                // Call tool.names() to get all available tool names
                lua_getfield(state, tool_module_index, "names");
                if (!lua_isfunction(state, -1))
                {
                    lua_settop(state, stack_top);
                    return result;
                }

                if (lua_pcall(state, 0, 1, 0) != 0)
                {
                    lua_settop(state, stack_top);
                    return result;
                }

                if (!lua_istable(state, -1))
                {
                    lua_settop(state, stack_top);
                    return result;
                }

                // Extract tool names from the returned array
                const int names_index = absolute_index(state, -1);
                const auto names_count = static_cast<std::size_t>(lua_rawlen(state, names_index));
                std::vector<std::string> all_tool_names;
                all_tool_names.reserve(names_count);

                for (std::size_t idx = 1; idx <= names_count; ++idx)
                {
                    lua_rawgeti(state, names_index, static_cast<int>(idx));
                    if (lua_isstring(state, -1))
                    {
                        all_tool_names.emplace_back(lua_tostring(state, -1));
                    }
                    lua_pop(state, 1);
                }
                lua_pop(state, 2);

                // Filter tool names if filter list is provided
                std::vector<std::string> filtered_names;
                if (!tool_filter.empty())
                {
                    std::set<std::string> filter_set(tool_filter.begin(), tool_filter.end());
                    for (const auto &name : all_tool_names)
                    {
                        if (filter_set.count(name) > 0)
                        {
                            filtered_names.push_back(name);
                        }
                    }
                }
                else
                {
                    filtered_names = all_tool_names;
                }

                // For each filtered tool, get its spec
                if (!filtered_names.empty())
                {
                    require_module(state, "tool");
                    const int tool_module_idx = absolute_index(state, -1);

                    lua_getfield(state, tool_module_idx, "specs");
                    if (lua_isfunction(state, -1))
                    {
                        // Build array of tool names to pass to specs()
                        lua_newtable(state);
                        for (std::size_t idx = 0; idx < filtered_names.size(); ++idx)
                        {
                            lua_pushlstring(state, filtered_names[idx].c_str(), filtered_names[idx].size());
                            lua_rawseti(state, -2, static_cast<int>(idx + 1));
                        }

                        if (lua_pcall(state, 1, 1, 0) == 0 && lua_istable(state, -1))
                        {
                            const int specs_index = absolute_index(state, -1);
                            const auto specs_count = static_cast<std::size_t>(lua_rawlen(state, specs_index));

                            for (std::size_t idx = 1; idx <= specs_count; ++idx)
                            {
                                lua_rawgeti(state, specs_index, static_cast<int>(idx));
                                if (lua_istable(state, -1))
                                {
                                    const int spec_idx = absolute_index(state, -1);

                                    // Extract tool info
                                    yaaf::mcp::ToolInfo tool_info;

                                    // Get function table
                                    lua_getfield(state, spec_idx, "function");
                                    if (lua_istable(state, -1))
                                    {
                                        const int func_idx = absolute_index(state, -1);

                                        // Get name
                                        lua_getfield(state, func_idx, "name");
                                        if (lua_isstring(state, -1))
                                        {
                                            tool_info.name = lua_tostring(state, -1);
                                        }
                                        lua_pop(state, 1);

                                        // Get description
                                        lua_getfield(state, func_idx, "description");
                                        if (lua_isstring(state, -1))
                                        {
                                            tool_info.description = lua_tostring(state, -1);
                                        }
                                        lua_pop(state, 1);

                                        // Get parameters as inputSchema
                                        lua_getfield(state, func_idx, "parameters");
                                        if (!lua_isnil(state, -1))
                                        {
                                            tool_info.input_schema = lua_to_json(state, -1);
                                        }
                                        else
                                        {
                                            tool_info.input_schema = nlohmann::json::object();
                                        }
                                        lua_pop(state, 1);
                                    }
                                    lua_pop(state, 1);

                                    if (!tool_info.name.empty())
                                    {
                                        result.push_back(tool_info);
                                    }
                                }
                                lua_pop(state, 1);
                            }
                        }
                        lua_pop(state, 1);
                    }
                    else
                    {
                        lua_pop(state, 1);
                    }
                    lua_pop(state, 1);
                }

                lua_settop(state, stack_top);
                return result;
            }
            catch (const std::exception &)
            {
                lua_settop(state, stack_top);
                return result;
            }
        };

        // Create prompt lister callback that retrieves hosted prompts
        yaaf::mcp::PromptLister prompt_lister = [&runtime, prompt_filter]() -> std::vector<yaaf::mcp::PromptDescriptor> {
            std::vector<yaaf::mcp::PromptDescriptor> result;

            // Filter prompts if filter list is provided
            if (!prompt_filter.empty())
            {
                std::set<std::string> filter_set(prompt_filter.begin(), prompt_filter.end());
                for (const auto &pair : runtime.hosted_prompts)
                {
                    if (filter_set.count(pair.first) > 0)
                    {
                        yaaf::mcp::PromptDescriptor descriptor;
                        descriptor.name = pair.first;
                        descriptor.description = pair.second.description;
                        descriptor.arguments = pair.second.arguments;
                        result.push_back(descriptor);
                    }
                }
            }
            else
            {
                for (const auto &pair : runtime.hosted_prompts)
                {
                    yaaf::mcp::PromptDescriptor descriptor;
                    descriptor.name = pair.first;
                    descriptor.description = pair.second.description;
                    descriptor.arguments = pair.second.arguments;
                    result.push_back(descriptor);
                }
            }

            return result;
        };

        // Create Host instance with lister callbacks
        auto host_ptr = std::make_unique<yaaf::mcp::Host>(
            schema_backend,
            std::move(tool_executor),
            std::move(prompt_executor),
            std::move(tool_lister),
            std::move(prompt_lister)
        );
        auto host = std::shared_ptr<yaaf::mcp::Host>(std::move(host_ptr));

        // Create StdioHost wrapper
        auto stdio_host = std::make_shared<yaaf::mcp::StdioHost>(*host, std::cin, std::cout);

        // Store in runtime context for cleanup
        runtime.host = host;
        runtime.stdio_host = stdio_host;

        // Run the server (blocks until client disconnects or error)
        stdio_host->run();

        lua_pushboolean(state, 1);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void push_mcp_function(lua_State *state, ScriptMcpContext &runtime, lua_CFunction function)
{
    lua_pushlightuserdata(state, &runtime);
    lua_pushcclosure(state, function, 1);
}

int open_mcp_module(lua_State *state)
{
    auto &runtime = context(state);
    lua_newtable(state);

    push_mcp_function(state, runtime, lua_config);
    lua_setfield(state, -2, "config");
    push_mcp_function(state, runtime, lua_servers);
    lua_setfield(state, -2, "servers");
    push_mcp_function(state, runtime, lua_diagnostics);
    lua_setfield(state, -2, "diagnostics");
    push_mcp_function(state, runtime, lua_list_tools);
    lua_setfield(state, -2, "list_tools");
    push_mcp_function(state, runtime, lua_call_tool);
    lua_setfield(state, -2, "call_tool");
    push_mcp_function(state, runtime, lua_register_prompt);
    lua_setfield(state, -2, "register_prompt");
    push_mcp_function(state, runtime, lua_host_stdio);
    lua_setfield(state, -2, "host_stdio");

    return 1;
}
} // namespace

void register_mcp_module(lua_State *state, ScriptMcpContext &context)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");
    push_mcp_function(state, context, open_mcp_module);
    lua_setfield(state, -2, "mcp");
    lua_pop(state, 2);
}
} // namespace yaaf::script::modules
