#include "lua_runtime.h"

#include "../platform/executable_path.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace yaaf::script
{
namespace
{
#ifndef LUA_OK
constexpr int kLuaOk = 0;
#else
constexpr int kLuaOk = LUA_OK;
#endif

struct RuntimeOutputContext
{
    std::ostream *output = nullptr;
};

[[nodiscard]] RuntimeOutputContext &output_context(lua_State *state)
{
    return *static_cast<RuntimeOutputContext *>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] int absolute_index(lua_State *state, int index)
{
    return index > 0 ? index : lua_gettop(state) + index + 1;
}

[[noreturn]] void throw_lua_error(lua_State *state, const std::string &message)
{
    lua_pushlstring(state, message.c_str(), message.size());
    lua_error(state);
    std::abort();
}

int lua_print(lua_State *state)
{
    try
    {
        auto &runtime = output_context(state);
        if (runtime.output == nullptr)
        {
            return 0;
        }

        const int argument_count = lua_gettop(state);
        lua_getglobal(state, "tostring");
        for (int index = 1; index <= argument_count; ++index)
        {
            lua_pushvalue(state, -1);
            lua_pushvalue(state, index);
            lua_call(state, 1, 1);

            if (index > 1)
            {
                *runtime.output << '\t';
            }

            *runtime.output << lua_tostring(state, -1);
            lua_pop(state, 1);
        }

        *runtime.output << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void register_print(lua_State *state, RuntimeOutputContext &context)
{
    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, lua_print, 1);
    lua_setglobal(state, "print");
}

void prepend_package_path(lua_State *state, const std::filesystem::path &directory)
{
    if (directory.empty())
    {
        return;
    }

    const auto normalized = directory.generic_string();
    const auto entries = fmt::format("{}/?.lua;{}/?/init.lua;", normalized, normalized);

    lua_getglobal(state, "package");
    lua_getfield(state, -1, "path");
    const char *current_path = lua_tostring(state, -1);
    const auto next_path = entries + (current_path != nullptr ? current_path : "");
    lua_pop(state, 1);

    lua_pushlstring(state, next_path.c_str(), next_path.size());
    lua_setfield(state, -2, "path");
    lua_pop(state, 1);
}

[[nodiscard]] bool is_yaaf_command(lua_State *state, int index)
{
    index = absolute_index(state, index);
    if (!lua_istable(state, index))
    {
        return false;
    }

    lua_getfield(state, index, "__yaaf_command");
    const bool result = lua_toboolean(state, -1) != 0;
    lua_pop(state, 1);
    return result;
}

void run_returned_command(lua_State *state, int index)
{
    index = absolute_index(state, index);
    if (!is_yaaf_command(state, index))
    {
        return;
    }

    lua_getfield(state, index, "run");
    if (!lua_isfunction(state, -1))
    {
        lua_pop(state, 1);
        throw std::runtime_error("Lua command returned by yaaf.command must declare a run function");
    }

    lua_pushvalue(state, index);
    if (lua_pcall(state, 1, 0, 0) != kLuaOk)
    {
        const char *message = lua_tostring(state, -1);
        throw std::runtime_error(fmt::format("Lua command failed: {}", message != nullptr ? message : "unknown error"));
    }
}

int run_file_impl(const LuaRuntimeOptions &options, const Services *services, nlohmann::json *command_metadata)
{
    if (options.file_path.empty())
    {
        throw std::invalid_argument("script --file is required");
    }

    if (options.endpoint.empty())
    {
        throw std::invalid_argument("script endpoint is required");
    }

    if (options.model.empty())
    {
        throw std::invalid_argument("script model is required");
    }

    const std::filesystem::path script_path{options.file_path};
    if (!std::filesystem::exists(script_path))
    {
        throw std::runtime_error(fmt::format("Lua script not found: {}", options.file_path));
    }

    const auto absolute_path = std::filesystem::absolute(script_path);
    const auto previous_path = std::filesystem::current_path();
    struct CurrentPathGuard
    {
        std::filesystem::path previous;
        ~CurrentPathGuard()
        {
            std::error_code ignored;
            std::filesystem::current_path(previous, ignored);
        }
    } guard{previous_path};

    std::filesystem::current_path(absolute_path.parent_path());

    lua_State *state = luaL_newstate();
    if (state == nullptr)
    {
        throw std::runtime_error("failed to create Lua state");
    }

    struct LuaStateGuard
    {
        lua_State *state;
        ~LuaStateGuard()
        {
            lua_close(state);
        }
    } state_guard{state};

    luaL_openselectedlibs(state, -1, 0);

    {
        RuntimeOutputContext print_context;
        print_context.output = options.output;
        register_print(state, print_context);

        // Package path precedence (highest priority listed last because
        // prepend_package_path puts each entry at the front of package.path):
        //   1. The directory containing the invoked script (highest).
        //   2. The script's grandparent directory, so module-style layouts work.
        //   3. The bundled runtime `lua/` directory next to the yaaf executable
        //      (or an explicit `options.runtime_root` override) — this is what
        //      makes `require("yaaf")` etc. work regardless of the caller's
        //      current working directory.
        const auto runtime_root =
            options.runtime_root.empty() ? yaaf::platform::executable_directory() : options.runtime_root;
        if (!runtime_root.empty())
        {
            prepend_package_path(state, runtime_root / "lua");
        }
        prepend_package_path(state, absolute_path.parent_path().parent_path());
        prepend_package_path(state, absolute_path.parent_path());

        modules::register_json_module(state);

        ScriptHttpContext http_context;
        http_context.http = options.http;
        http_context.services = services;
        modules::register_http_module(state, http_context);

        ScriptLlmContext llm_context;
        llm_context.default_endpoint = options.endpoint;
        llm_context.default_model = options.model;
        llm_context.http = options.http;
        llm_context.services = services;
        modules::register_llm_module(state, llm_context);

        AgentContext agent_context;
        agent_context.default_endpoint = options.endpoint;
        agent_context.default_model = options.model;
        agent_context.http = options.http;
        agent_context.services = services;
        agent_context.output = options.output;
        modules::register_agent_module(state, agent_context);

        ScriptMcpContext mcp_context;
        mcp_context.options.workspace_root = options.workspace_root.empty() ? previous_path : options.workspace_root;
        mcp_context.options.config_path = options.mcp_config_path;
        mcp_context.options.http = options.http;
        if (services != nullptr && services->mcp_http_post)
        {
            mcp_context.options.http_post = services->mcp_http_post;
        }
        if (services != nullptr && services->mcp_schema_registry != nullptr)
        {
            mcp_context.options.schema_registry = services->mcp_schema_registry;
        }
        modules::register_mcp_module(state, mcp_context);

        modules::register_tool_module(state);

        ScriptYaafContext yaaf_context;
        yaaf_context.arguments = options.arguments;
        yaaf_context.default_endpoint = options.endpoint;
        yaaf_context.default_model = options.model;
        yaaf_context.options = options.options;
        yaaf_context.positionals = options.positionals;
        yaaf_context.command_metadata = command_metadata;
        yaaf_context.input = options.input;
        yaaf_context.output = options.output;
        modules::register_yaaf_module(state, yaaf_context);

        const auto file_name = absolute_path.string();
        const int stack_top = lua_gettop(state);
        if (luaL_dofile(state, file_name.c_str()) != kLuaOk)
        {
            const char *message = lua_tostring(state, -1);
            throw std::runtime_error(
                fmt::format("Lua script failed: {}", message != nullptr ? message : "unknown error"));
        }

        if (command_metadata == nullptr && lua_gettop(state) > stack_top)
        {
            run_returned_command(state, stack_top + 1);
        }
    }

    return EXIT_SUCCESS;
}
} // namespace

nlohmann::json read_command_metadata(const LuaRuntimeOptions &options)
{
    nlohmann::json metadata;
    run_file_impl(options, nullptr, &metadata);

    if (metadata.is_null())
    {
        throw std::runtime_error(fmt::format("Lua script did not declare command metadata: {}", options.file_path));
    }

    return metadata;
}

int run_file(const LuaRuntimeOptions &options, const Services *services)
{
    return run_file_impl(options, services, nullptr);
}
} // namespace yaaf::script
