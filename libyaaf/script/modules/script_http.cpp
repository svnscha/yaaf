#include "script_http.h"
#include "lua_module_utils.h"

#include <cctype>

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

[[nodiscard]] std::string uppercase_ascii(std::string_view value)
{
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return normalized;
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

[[nodiscard]] std::optional<std::string> get_optional_string(lua_State *state, int table_index, const char *field)
{
    lua_getfield(state, table_index, field);
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        return std::nullopt;
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

[[nodiscard]] std::string get_string_or_default(lua_State *state, int table_index, const char *field,
                                                std::string fallback)
{
    if (auto value = get_optional_string(state, table_index, field))
    {
        return *value;
    }

    return fallback;
}

[[nodiscard]] std::optional<std::chrono::milliseconds> read_timeout(lua_State *state, int table_index)
{
    lua_getfield(state, table_index, "timeout");
    if (lua_isnil(state, -1))
    {
        lua_pop(state, 1);
        return std::nullopt;
    }

    if (!lua_isnumber(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument("field 'timeout' must be a number of milliseconds");
    }

    const auto timeout = static_cast<long long>(lua_tonumber(state, -1));
    lua_pop(state, 1);

    if (timeout < 0)
    {
        throw std::invalid_argument("field 'timeout' must be a non-negative number of milliseconds");
    }

    return std::chrono::milliseconds{timeout};
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

class LuaRegistryRefGuard
{
  public:
    explicit LuaRegistryRefGuard(lua_State *state) : state_(state)
    {
    }

    ~LuaRegistryRefGuard()
    {
        if (ref_.has_value())
        {
            luaL_unref(state_, LUA_REGISTRYINDEX, *ref_);
        }
    }

    [[nodiscard]] std::optional<int> ref() const
    {
        return ref_;
    }

    void capture_function_field(int table_index, const char *field)
    {
        lua_getfield(state_, table_index, field);
        if (lua_isnil(state_, -1))
        {
            lua_pop(state_, 1);
            return;
        }

        if (!lua_isfunction(state_, -1))
        {
            lua_pop(state_, 1);
            throw std::invalid_argument(fmt::format("field '{}' must be a function", field));
        }

        ref_ = luaL_ref(state_, LUA_REGISTRYINDEX);
    }

  private:
    lua_State *state_;
    std::optional<int> ref_;
};

[[nodiscard]] HttpClient::Request build_request(lua_State *state, int request_index,
                                                const std::optional<std::string_view> &forced_method,
                                                const bool use_post_defaults)
{
    HttpClient::Request request;
    request.method = forced_method.has_value() ? std::string(*forced_method)
                                               : get_string_or_default(state, request_index, "method", "GET");
    request.url = get_required_string(state, request_index, "url");
    request.headers = read_headers(state, request_index);
    request.timeout = read_timeout(state, request_index);

    if (const auto body = get_optional_string(state, request_index, "body"))
    {
        request.body = *body;
    }

    if (const auto content_type = get_optional_string(state, request_index, "content_type"))
    {
        request.content_type = *content_type;
    }

    if (use_post_defaults)
    {
        if (!request.body.has_value())
        {
            request.body = std::string{};
        }
        if (!request.content_type.has_value())
        {
            request.content_type = std::string{"application/json"};
        }
    }

    return request;
}

[[nodiscard]] HttpClient::Response run_request(ScriptHttpContext &runtime, const HttpClient::Request &request)
{
    const auto method = uppercase_ascii(request.method);

    if (runtime.services != nullptr)
    {
        if (runtime.services->http_request)
        {
            return runtime.services->http_request(request);
        }

        if (method == "GET" && runtime.services->http_get && !request.body.has_value() && !request.content_type.has_value() &&
            !request.timeout.has_value() && !request.on_response_chunk)
        {
            return runtime.services->http_get(request.url, request.headers);
        }

        if (method == "POST" && runtime.services->http_post && !request.timeout.has_value())
        {
            const auto *on_response_chunk = request.on_response_chunk ? &request.on_response_chunk : nullptr;
            return runtime.services->http_post(request.url, request.body.value_or(""),
                                               request.content_type.value_or("application/json"), request.headers,
                                               on_response_chunk);
        }
    }

    HttpClient client{runtime.http};
    return client.execute(request);
}

int lua_request_impl(lua_State *state, const std::optional<std::string_view> &forced_method, const bool use_post_defaults)
{
    auto &runtime = context(state);
    luaL_checktype(state, 1, LUA_TTABLE);
    const int request_index = absolute_index(state, 1);

    auto request = build_request(state, request_index, forced_method, use_post_defaults);

    LuaRegistryRefGuard callback_ref(state);
    callback_ref.capture_function_field(request_index, "on_response_chunk");
    if (callback_ref.ref().has_value())
    {
        const auto ref = *callback_ref.ref();
        request.on_response_chunk = [state, ref](std::string_view chunk) {
            lua_rawgeti(state, LUA_REGISTRYINDEX, ref);
            lua_pushlstring(state, chunk.data(), chunk.size());
            if (lua_pcall(state, 1, 0, 0) != LUA_OK)
            {
                const char *message = lua_tostring(state, -1);
                throw std::runtime_error(message != nullptr ? message : "Lua HTTP chunk callback failed");
            }
        };
    }

    const auto response = run_request(runtime, request);
    push_response(state, response);
    return 1;
}

int lua_request(lua_State *state)
{
    try
    {
        return lua_request_impl(state, std::nullopt, false);
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_get(lua_State *state)
{
    try
    {
        return lua_request_impl(state, std::string_view{"GET"}, false);
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
        return lua_request_impl(state, std::string_view{"POST"}, true);
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_put(lua_State *state)
{
    try
    {
        return lua_request_impl(state, std::string_view{"PUT"}, false);
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_patch(lua_State *state)
{
    try
    {
        return lua_request_impl(state, std::string_view{"PATCH"}, false);
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_delete(lua_State *state)
{
    try
    {
        return lua_request_impl(state, std::string_view{"DELETE"}, false);
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_head(lua_State *state)
{
    try
    {
        return lua_request_impl(state, std::string_view{"HEAD"}, false);
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

    push_http_function(state, runtime, lua_request);
    lua_setfield(state, -2, "request");
    push_http_function(state, runtime, lua_get);
    lua_setfield(state, -2, "get");
    push_http_function(state, runtime, lua_post);
    lua_setfield(state, -2, "post");
    push_http_function(state, runtime, lua_put);
    lua_setfield(state, -2, "put");
    push_http_function(state, runtime, lua_patch);
    lua_setfield(state, -2, "patch");
    push_http_function(state, runtime, lua_delete);
    lua_setfield(state, -2, "delete");
    push_http_function(state, runtime, lua_head);
    lua_setfield(state, -2, "head");

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
