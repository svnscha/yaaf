#include "script_process.h"
#include "../../process/process.h"
#include "lua_module_utils.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
}

#include <chrono>
#include <memory>

namespace yaaf::script::modules
{

namespace
{

using lua_module_utils::absolute_index;
using lua_module_utils::throw_lua_error;

// Metatable name for process handles
constexpr const char *PROCESS_HANDLE_METATABLE = "yaaf_process_handle";

/**
 * Extract PlatformProcess* from Lua userdata.
 */
[[nodiscard]] yaaf::process::PlatformProcess *get_process_handle(lua_State *L, int index)
{
    index = absolute_index(L, index);
    auto *handle_ptr =
        static_cast<yaaf::process::PlatformProcess **>(luaL_checkudata(L, index, PROCESS_HANDLE_METATABLE));
    if (handle_ptr == nullptr || *handle_ptr == nullptr)
    {
        throw_lua_error(L, "process handle is nil or invalid");
    }
    return *handle_ptr;
}

/**
 * Extract string table from Lua, default to std::vector<std::string>
 */
[[nodiscard]] std::vector<std::string> extract_string_array(lua_State *L, int index)
{
    index = absolute_index(L, index);
    std::vector<std::string> result;

    if (!lua_istable(L, index))
    {
        return result; // Return empty array if not a table
    }

    // Iterate over the table
    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (lua_isstring(L, -1))
        {
            size_t len = 0;
            const char *str = lua_tolstring(L, -1, &len);
            result.emplace_back(str, len);
        }
        lua_pop(L, 1); // Pop value, keep key for next iteration
    }

    return result;
}

/**
 * Extract string table from Lua to map<string, string>
 */
[[nodiscard]] std::map<std::string, std::string> extract_string_map(lua_State *L, int index)
{
    index = absolute_index(L, index);
    std::map<std::string, std::string> result;

    if (!lua_istable(L, index))
    {
        return result;
    }

    lua_pushnil(L);
    while (lua_next(L, index) != 0)
    {
        if (lua_isstring(L, -2) && lua_isstring(L, -1))
        {
            size_t key_len = 0;
            size_t val_len = 0;
            const char *key = lua_tolstring(L, -2, &key_len);
            const char *val = lua_tolstring(L, -1, &val_len);
            result[std::string(key, key_len)] = std::string(val, val_len);
        }
        lua_pop(L, 1);
    }

    return result;
}

// ============ Lua C Functions for process module ============

/**
 * process.start(options) -> process_handle
 * Options table:
 *   - command (string, required)
 *   - args (array of strings, optional)
 *   - cwd (string, optional)
 *   - env (table of key=value, optional)
 *   - inherit_env (boolean, optional, default=true)
 */
int lua_process_start(lua_State *L)
{
    try
    {
        if (!lua_istable(L, 1))
        {
            throw_lua_error(L, "process.start() requires a table argument");
        }

        // Extract command (required)
        lua_getfield(L, 1, "command");
        if (!lua_isstring(L, -1))
        {
            throw_lua_error(L, "process.start() requires 'command' field (string)");
        }
        std::string command = lua_tostring(L, -1);
        lua_pop(L, 1);

        if (command.empty())
        {
            throw_lua_error(L, "process.start() 'command' cannot be empty");
        }

        // Extract args (optional)
        lua_getfield(L, 1, "args");
        std::vector<std::string> args;
        if (lua_istable(L, -1))
        {
            args = extract_string_array(L, -1);
        }
        lua_pop(L, 1);

        // Extract cwd (optional)
        lua_getfield(L, 1, "cwd");
        std::filesystem::path working_directory;
        if (lua_isstring(L, -1))
        {
            working_directory = lua_tostring(L, -1);
        }
        lua_pop(L, 1);

        // Extract env (optional)
        lua_getfield(L, 1, "env");
        std::map<std::string, std::string> env_overrides;
        if (lua_istable(L, -1))
        {
            env_overrides = extract_string_map(L, -1);
        }
        lua_pop(L, 1);

        // Extract inherit_env (optional, default true)
        lua_getfield(L, 1, "inherit_env");
        bool inherit_env = true;
        if (lua_isboolean(L, -1))
        {
            inherit_env = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);

        // Build options and start process
        yaaf::process::ProcessOptions options{
            .command = command,
            .args = args,
            .working_directory = working_directory,
            .env_overrides = env_overrides,
            .inherit_parent_env = inherit_env,
        };

        auto process_ptr = yaaf::process::start_process(options);

        // Create Lua userdata to hold the process pointer
        auto *handle_ptr = static_cast<yaaf::process::PlatformProcess **>(
            lua_newuserdata(L, sizeof(yaaf::process::PlatformProcess *)));
        *handle_ptr = process_ptr.release(); // Release ownership to Lua

        // Set metatable
        luaL_setmetatable(L, PROCESS_HANDLE_METATABLE);

        return 1; // Return the userdata
    }
    catch (const std::exception &e)
    {
        throw_lua_error(L, e.what());
    }
}

/**
 * handle:write(data) - Write data to child's stdin
 */
int lua_process_write(lua_State *L)
{
    try
    {
        auto *handle = get_process_handle(L, 1);

        if (lua_isstring(L, 2))
        {
            size_t len = 0;
            const char *data = lua_tolstring(L, 2, &len);
            handle->write(std::string_view(data, len));
        }
        else
        {
            throw_lua_error(L, "handle:write() requires a string argument");
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        throw_lua_error(L, e.what());
    }
}

/**
 * handle:read(timeout_ms) -> (line, error_string)
 * Returns:
 *   - (line, nil) on success
 *   - (nil, "timeout") on timeout
 *   - (nil, "exited") if process exited
 *   - (nil, error_msg) on I/O error
 */
int lua_process_read(lua_State *L)
{
    try
    {
        auto *handle = get_process_handle(L, 1);

        int timeout_ms = 5000; // Default 5 seconds
        if (lua_isinteger(L, 2))
        {
            timeout_ms = static_cast<int>(lua_tointeger(L, 2));
        }

        auto result = handle->read_line(std::chrono::milliseconds(timeout_ms));

        if (result.timed_out)
        {
            lua_pushnil(L);
            lua_pushstring(L, "timeout");
            return 2;
        }

        if (result.process_exited)
        {
            lua_pushnil(L);
            lua_pushstring(L, "exited");
            return 2;
        }

        lua_pushlstring(L, result.data.c_str(), result.data.size());
        lua_pushnil(L);
        return 2;
    }
    catch (const std::exception &e)
    {
        lua_pushnil(L);
        lua_pushstring(L, e.what());
        return 2;
    }
}

/**
 * handle:is_alive() -> bool
 */
int lua_process_is_alive(lua_State *L)
{
    try
    {
        auto *handle = get_process_handle(L, 1);
        bool alive = !handle->has_exited();
        lua_pushboolean(L, alive ? 1 : 0);
        return 1;
    }
    catch (const std::exception &e)
    {
        throw_lua_error(L, e.what());
    }
}

/**
 * handle:shutdown(timeout_ms) - Gracefully shutdown process
 */
int lua_process_shutdown(lua_State *L)
{
    try
    {
        auto *handle = get_process_handle(L, 1);

        int timeout_ms = 1000; // Default 1 second
        if (lua_isinteger(L, 2))
        {
            timeout_ms = static_cast<int>(lua_tointeger(L, 2));
        }

        handle->shutdown(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    catch (const std::exception &e)
    {
        throw_lua_error(L, e.what());
    }
}

/**
 * handle:close() - Explicitly close and cleanup process handle
 * Also called by __gc finalizer
 */
int lua_process_close(lua_State *L)
{
    try
    {
        auto *handle_ptr =
            static_cast<yaaf::process::PlatformProcess **>(luaL_checkudata(L, 1, PROCESS_HANDLE_METATABLE));
        if (handle_ptr != nullptr && *handle_ptr != nullptr)
        {
            delete *handle_ptr;
            *handle_ptr = nullptr;
        }
        return 0;
    }
    catch (const std::exception &e)
    {
        throw_lua_error(L, e.what());
    }
}

/**
 * Metatable __gc finalizer
 */
int lua_process_gc(lua_State *L)
{
    try
    {
        auto *handle_ptr =
            static_cast<yaaf::process::PlatformProcess **>(luaL_checkudata(L, 1, PROCESS_HANDLE_METATABLE));
        if (handle_ptr != nullptr && *handle_ptr != nullptr)
        {
            delete *handle_ptr;
            *handle_ptr = nullptr;
        }
        return 0;
    }
    catch (...)
    {
        // Ignore exceptions in finalizer
    }
    return 0;
}

/**
 * Metatable __tostring for debugging
 */
int lua_process_tostring(lua_State *L)
{
    try
    {
        auto *handle = get_process_handle(L, 1);
        bool alive = !handle->has_exited();
        const char *status = alive ? "alive" : "exited";
        lua_pushfstring(L, "process_handle<%s>", status);
        return 1;
    }
    catch (const std::exception &)
    {
        lua_pushfstring(L, "process_handle<invalid>");
        return 1;
    }
}

// ============ Module registration ============

int open_process_module(lua_State *L)
{
    // Create metatable for process handles
    luaL_newmetatable(L, PROCESS_HANDLE_METATABLE);

    // Set metamethods
    lua_pushcfunction(L, lua_process_write);
    lua_setfield(L, -2, "write");

    lua_pushcfunction(L, lua_process_read);
    lua_setfield(L, -2, "read");

    lua_pushcfunction(L, lua_process_is_alive);
    lua_setfield(L, -2, "is_alive");

    lua_pushcfunction(L, lua_process_shutdown);
    lua_setfield(L, -2, "shutdown");

    lua_pushcfunction(L, lua_process_close);
    lua_setfield(L, -2, "close");

    lua_pushcfunction(L, lua_process_gc);
    lua_setfield(L, -2, "__gc");

    lua_pushcfunction(L, lua_process_tostring);
    lua_setfield(L, -2, "__tostring");

    // Set __index to self for method lookup
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    // Pop metatable
    lua_pop(L, 1);

    // Create module table
    lua_newtable(L);
    lua_pushcfunction(L, lua_process_start);
    lua_setfield(L, -2, "start");

    return 1; // Return module table
}

} // namespace

void register_process_module(lua_State *L)
{
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, open_process_module);
    lua_setfield(L, -2, "process");
    lua_pop(L, 2);
}

} // namespace yaaf::script::modules
