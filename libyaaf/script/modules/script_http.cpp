#include "script_http.h"
#include "lua_module_utils.h"

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
using lua_module_utils::throw_lua_error;

[[nodiscard]] ScriptHttpContext &context(lua_State *state)
{
    return *static_cast<ScriptHttpContext *>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] std::string get_required_string(lua_State *state, int table_index, const char *field)
{
    lua_getfield(state, table_index, field);
    if (!lua_isstring(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument(fmt::format("field '{}' must be a string", field));
    }

    std::string value = lua_tostring(state, -1);
    lua_pop(state, 1);
    return value;
}

[[nodiscard]] std::string get_string_or_default(lua_State *state, int table_index, const char *field,
                                                std::string fallback)
{
    lua_getfield(state, table_index, field);
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        return fallback;
    }

    if (!lua_isstring(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument(fmt::format("field '{}' must be a string", field));
    }

    std::string value = lua_tostring(state, -1);
    lua_pop(state, 1);
    return value;
}

[[nodiscard]] HttpClient::Headers read_headers(lua_State *state, int table_index)
{
    HttpClient::Headers headers;

    lua_getfield(state, table_index, "headers");
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        return headers;
    }

    if (!lua_istable(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument("field 'headers' must be a table of header name/value pairs");
    }

    const int headers_index = absolute_index(state, -1);
    lua_pushnil(state);
    while (lua_next(state, headers_index) != 0)
    {
        if (!lua_isstring(state, -2) || !lua_isstring(state, -1))
        {
            lua_pop(state, 2);
            throw std::invalid_argument("header names and values must be strings");
        }

        headers.emplace_back(lua_tostring(state, -2), lua_tostring(state, -1));
        lua_pop(state, 1);
    }

    lua_pop(state, 1);
    return headers;
}

void push_headers(lua_State *state, const HttpClient::Headers &headers)
{
    lua_newtable(state);
    for (const auto &[name, value] : headers)
    {
        lua_pushlstring(state, value.c_str(), value.size());
        lua_setfield(state, -2, name.c_str());
    }
}

void push_response(lua_State *state, const HttpClient::Response &response)
{
    lua_newtable(state);

    lua_pushinteger(state, static_cast<lua_Integer>(response.status_code));
    lua_setfield(state, -2, "status_code");

    lua_pushlstring(state, response.content_type.c_str(), response.content_type.size());
    lua_setfield(state, -2, "content_type");

    lua_pushlstring(state, response.body.c_str(), response.body.size());
    lua_setfield(state, -2, "body");

    push_headers(state, response.headers);
    lua_setfield(state, -2, "headers");
}

[[nodiscard]] HttpClient::Response run_get(ScriptHttpContext &runtime, std::string_view url,
                                           const HttpClient::Headers &headers)
{
    if (runtime.services != nullptr && runtime.services->http_get)
    {
        return runtime.services->http_get(url, headers);
    }

    HttpClient client{runtime.http};
    return headers.empty() ? client.get(url) : client.get(url, headers);
}

[[nodiscard]] HttpClient::Response run_post(ScriptHttpContext &runtime, std::string_view url, std::string_view body,
                                            std::string_view content_type, const HttpClient::Headers &headers,
                                            const HttpClient::ResponseChunkHandler *on_response_chunk)
{
    if (runtime.services != nullptr && runtime.services->http_post)
    {
        return runtime.services->http_post(url, body, content_type, headers, on_response_chunk);
    }

    HttpClient client{runtime.http};
    if (on_response_chunk != nullptr)
    {
        return client.post(url, body, content_type, headers, *on_response_chunk);
    }

    return headers.empty() ? client.post(url, body, content_type) : client.post(url, body, content_type, headers);
}

int lua_get(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        luaL_checktype(state, 1, LUA_TTABLE);
        const int request_index = absolute_index(state, 1);

        const auto url = get_required_string(state, request_index, "url");
        const auto headers = read_headers(state, request_index);
        const auto response = run_get(runtime, url, headers);
        push_response(state, response);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_post(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        luaL_checktype(state, 1, LUA_TTABLE);
        const int request_index = absolute_index(state, 1);

        const auto url = get_required_string(state, request_index, "url");
        const auto body = get_string_or_default(state, request_index, "body", "");
        const auto content_type = get_string_or_default(state, request_index, "content_type", "application/json");
        const auto headers = read_headers(state, request_index);

        std::optional<int> callback_ref;
        lua_getfield(state, request_index, "on_response_chunk");
        if (!lua_isnil(state, -1))
        {
            if (!lua_isfunction(state, -1))
            {
                lua_pop(state, 1);
                throw std::invalid_argument("field 'on_response_chunk' must be a function");
            }

            callback_ref = luaL_ref(state, LUA_REGISTRYINDEX);
        }
        else
        {
            lua_pop(state, 1);
        }

        HttpClient::ResponseChunkHandler on_response_chunk;
        if (callback_ref.has_value())
        {
            on_response_chunk = [state, callback_ref](std::string_view chunk) {
                lua_rawgeti(state, LUA_REGISTRYINDEX, *callback_ref);
                lua_pushlstring(state, chunk.data(), chunk.size());
                if (lua_pcall(state, 1, 0, 0) != LUA_OK)
                {
                    const char *message = lua_tostring(state, -1);
                    throw std::runtime_error(message != nullptr ? message : "Lua HTTP chunk callback failed");
                }
            };
        }

        const auto response = run_post(runtime, url, body, content_type, headers,
                                       callback_ref.has_value() ? &on_response_chunk : nullptr);
        if (callback_ref.has_value())
        {
            luaL_unref(state, LUA_REGISTRYINDEX, *callback_ref);
        }

        push_response(state, response);
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void push_http_function(lua_State *state, ScriptHttpContext &runtime, lua_CFunction function)
{
    lua_pushlightuserdata(state, &runtime);
    lua_pushcclosure(state, function, 1);
}

int open_http_module(lua_State *state)
{
    auto &runtime = context(state);
    lua_newtable(state);

    push_http_function(state, runtime, lua_get);
    lua_setfield(state, -2, "get");
    push_http_function(state, runtime, lua_post);
    lua_setfield(state, -2, "post");

    return 1;
}
} // namespace

void register_http_module(lua_State *state, ScriptHttpContext &context)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");
    push_http_function(state, context, open_http_module);
    lua_setfield(state, -2, "http");
    lua_pop(state, 2);
}
} // namespace yaaf::script::modules
