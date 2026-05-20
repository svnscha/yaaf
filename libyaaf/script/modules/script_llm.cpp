#include "script_llm.h"
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
using lua_module_utils::lua_error_message;
using lua_module_utils::lua_to_json;
using lua_module_utils::push_json;
using lua_module_utils::require_module;
using lua_module_utils::throw_lua_error;

struct LuaRegistryRef
{
    lua_State *state = nullptr;
    int ref = LUA_NOREF;

    ~LuaRegistryRef()
    {
        if (state != nullptr && ref != LUA_NOREF)
        {
            luaL_unref(state, LUA_REGISTRYINDEX, ref);
        }
    }
};

[[nodiscard]] ScriptLlmContext &context(lua_State *state)
{
    return *static_cast<ScriptLlmContext *>(lua_touserdata(state, lua_upvalueindex(1)));
}

[[nodiscard]] int push_module_table(lua_State *state)
{
    lua_pushvalue(state, lua_upvalueindex(2));
    return absolute_index(state, -1);
}

[[nodiscard]] const char *dispatch_method_name(lua_State *state)
{
    return lua_tostring(state, lua_upvalueindex(3));
}

[[nodiscard]] nlohmann::json request_json(lua_State *state, int index)
{
    index = absolute_index(state, index);
    nlohmann::json payload = nlohmann::json::object();

    lua_pushnil(state);
    while (lua_next(state, index) != 0)
    {
        if (!lua_isstring(state, -2))
        {
            lua_pop(state, 2);
            throw std::invalid_argument("Lua table keys must be strings for llm request conversion");
        }

        const std::string key = lua_tostring(state, -2);
        if (key != "on_stream")
        {
            payload[key] =
                lua_to_json(state, -1,
                            {.object_key_mode = lua_module_utils::JsonObjectKeyMode::StringOnly,
                             .unsupported_value_error = "unsupported Lua value type for llm request conversion",
                             .invalid_key_error = "Lua table keys must be strings for llm request conversion"});
        }

        lua_pop(state, 1);
    }

    return payload;
}

[[nodiscard]] std::optional<llm::Think> normalize_think(std::optional<llm::Think> think)
{
    if (!think.has_value())
    {
        return think;
    }

    if (const auto *value = std::get_if<std::string>(&*think); value != nullptr)
    {
        if (value->empty() || *value == "none")
        {
            return llm::Think{false};
        }
    }

    return think;
}

[[nodiscard]] std::optional<llm::Think> decode_think(const nlohmann::json &payload)
{
    const auto entry = payload.find("think");
    if (entry == payload.end() || entry->is_null())
    {
        return std::nullopt;
    }

    if (entry->is_boolean())
    {
        return llm::Think{entry->get<bool>()};
    }

    if (entry->is_string())
    {
        return llm::Think{entry->get<std::string>()};
    }

    throw std::invalid_argument("field 'think' must be a boolean or string");
}

[[nodiscard]] std::optional<llm::Format> decode_format(const nlohmann::json &payload)
{
    const auto entry = payload.find("format");
    if (entry == payload.end() || entry->is_null())
    {
        return std::nullopt;
    }

    if (entry->is_string())
    {
        return llm::Format{entry->get<std::string>()};
    }

    return llm::Format{*entry};
}

[[nodiscard]] std::vector<std::string> decode_string_array(const nlohmann::json &payload, std::string_view field)
{
    std::vector<std::string> values;
    const auto entry = payload.find(std::string(field));
    if (entry == payload.end() || entry->is_null())
    {
        return values;
    }

    if (!entry->is_array())
    {
        throw std::invalid_argument(fmt::format("field '{}' must be an array", field));
    }

    values.reserve(entry->size());
    for (const auto &value : *entry)
    {
        if (!value.is_string())
        {
            throw std::invalid_argument(fmt::format("field '{}' entries must be strings", field));
        }
        values.push_back(value.get<std::string>());
    }

    return values;
}

[[nodiscard]] std::vector<llm::Tool> decode_tools(const nlohmann::json &payload, std::string_view field)
{
    std::vector<llm::Tool> tools;
    const auto entry = payload.find(std::string(field));
    if (entry == payload.end() || entry->is_null())
    {
        return tools;
    }

    if (!entry->is_array())
    {
        throw std::invalid_argument(fmt::format("field '{}' must be an array", field));
    }

    tools.reserve(entry->size());
    for (const auto &tool_payload : *entry)
    {
        llm::Tool tool;
        tool.type = tool_payload.value("type", "function");
        const auto &function_payload = tool_payload.contains("function") ? tool_payload.at("function") : tool_payload;
        tool.function.name = function_payload.value("name", "");
        if (function_payload.contains("description") && function_payload.at("description").is_string())
        {
            tool.function.description = function_payload.at("description").get<std::string>();
        }
        if (function_payload.contains("arguments"))
        {
            tool.function.arguments = function_payload.at("arguments");
        }
        else if (function_payload.contains("parameters"))
        {
            tool.function.arguments = function_payload.at("parameters");
        }
        tools.push_back(std::move(tool));
    }

    return tools;
}

[[nodiscard]] std::vector<llm::ChatMessage> decode_messages(const nlohmann::json &payload)
{
    std::vector<llm::ChatMessage> messages;
    if (payload.contains("messages") && !payload.at("messages").is_null())
    {
        const auto &entry = payload.at("messages");
        if (!entry.is_array())
        {
            throw std::invalid_argument("field 'messages' must be an array");
        }

        messages.reserve(entry.size());
        for (const auto &message_payload : entry)
        {
            llm::ChatMessage message;
            message.role = message_payload.value("role", "user");
            message.content = message_payload.value("content", "");
            if (message_payload.contains("thinking") && message_payload.at("thinking").is_string())
            {
                message.thinking = message_payload.at("thinking").get<std::string>();
            }
            message.images = decode_string_array(message_payload, "images");
            message.tool_calls = decode_tools(message_payload, "tool_calls");
            messages.push_back(std::move(message));
        }
    }

    return messages;
}

[[nodiscard]] llm::EmbedInput decode_embed_input(const nlohmann::json &payload)
{
    const auto entry = payload.find("input");
    if (entry == payload.end() || entry->is_null())
    {
        throw std::invalid_argument("field 'input' is required");
    }

    if (entry->is_string())
    {
        return llm::EmbedInput{entry->get<std::string>()};
    }

    if (!entry->is_array())
    {
        throw std::invalid_argument("field 'input' must be a string or array of strings");
    }

    std::vector<std::string> values;
    values.reserve(entry->size());
    for (const auto &value : *entry)
    {
        if (!value.is_string())
        {
            throw std::invalid_argument("field 'input' entries must be strings");
        }
        values.push_back(value.get<std::string>());
    }

    return llm::EmbedInput{std::move(values)};
}

[[nodiscard]] nlohmann::json encode_chat_message(const llm::ChatMessage &message)
{
    nlohmann::json payload = {{"role", message.role}, {"content", message.content}};
    if (message.thinking.has_value())
    {
        payload["thinking"] = *message.thinking;
    }
    if (!message.images.empty())
    {
        payload["images"] = message.images;
    }
    if (!message.tool_calls.empty())
    {
        payload["tool_calls"] = nlohmann::json::array();
        for (const auto &tool_call : message.tool_calls)
        {
            payload["tool_calls"].push_back(
                nlohmann::json{{"type", tool_call.type},
                               {"function", nlohmann::json{{"name", tool_call.function.name},
                                                           {"arguments", tool_call.function.arguments}}}});
        }
    }
    return payload;
}

[[nodiscard]] nlohmann::json encode_generate_response(const llm::GenerateResponse &response)
{
    nlohmann::json payload = {{"model", response.model},
                              {"created_at", response.created_at},
                              {"response", response.response},
                              {"done", response.done},
                              {"done_reason", response.done_reason}};
    if (response.thinking.has_value())
    {
        payload["thinking"] = *response.thinking;
    }
    return payload;
}

[[nodiscard]] nlohmann::json encode_generate_stream_event(const llm::GenerateStreamEvent &event)
{
    nlohmann::json payload = {{"model", event.model},
                              {"created_at", event.created_at},
                              {"response", event.response},
                              {"done", event.done},
                              {"done_reason", event.done_reason}};
    if (event.thinking.has_value())
    {
        payload["thinking"] = *event.thinking;
    }
    return payload;
}

[[nodiscard]] nlohmann::json encode_chat_response(const llm::ChatResponse &response)
{
    return nlohmann::json{{"model", response.model},
                          {"created_at", response.created_at},
                          {"message", encode_chat_message(response.message)},
                          {"done", response.done},
                          {"done_reason", response.done_reason}};
}

[[nodiscard]] nlohmann::json encode_chat_stream_event(const llm::ChatStreamEvent &event)
{
    return nlohmann::json{{"model", event.model},
                          {"created_at", event.created_at},
                          {"message", encode_chat_message(event.message)},
                          {"done", event.done},
                          {"done_reason", event.done_reason}};
}

[[nodiscard]] nlohmann::json encode_embed_response(const llm::EmbedResponse &response)
{
    return nlohmann::json{{"model", response.model},
                          {"embeddings", response.embeddings},
                          {"total_duration", response.total_duration},
                          {"load_duration", response.load_duration},
                          {"prompt_eval_count", response.prompt_eval_count}};
}

void call_lua_callback(lua_State *state, int callback_ref, const nlohmann::json &payload)
{
    lua_rawgeti(state, LUA_REGISTRYINDEX, callback_ref);
    push_json(state, payload);
    if (lua_pcall(state, 1, 0, 0) != 0)
    {
        auto message = lua_error_message(state);
        lua_pop(state, 1);
        throw std::runtime_error(fmt::format("llm stream callback failed: {}", message));
    }
}

int lua_native_generate(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        if (runtime.services == nullptr || !runtime.services->generate)
        {
            throw std::runtime_error("native llm generate provider is unavailable");
        }

        luaL_checktype(state, 1, LUA_TTABLE);
        const int request_index = absolute_index(state, 1);
        const auto payload = request_json(state, request_index);

        llm::GenerateRequest request;
        request.model = payload.value("model", runtime.default_model);
        request.prompt = payload.value("prompt", "");
        if (payload.contains("suffix") && payload.at("suffix").is_string())
        {
            request.suffix = payload.at("suffix").get<std::string>();
        }
        request.images = decode_string_array(payload, "images");
        if (payload.contains("system") && payload.at("system").is_string())
        {
            request.system = payload.at("system").get<std::string>();
        }
        request.format = decode_format(payload);
        request.stream = payload.value("stream", false);
        request.think = normalize_think(decode_think(payload));
        request.raw = payload.value("raw", false);

        LuaRegistryRef callback_ref{state};
        lua_getfield(state, request_index, "on_stream");
        if (lua_isfunction(state, -1))
        {
            callback_ref.ref = luaL_ref(state, LUA_REGISTRYINDEX);
        }
        else
        {
            lua_pop(state, 1);
        }

        llm::StreamCallback on_stream_event;
        if (callback_ref.ref != LUA_NOREF)
        {
            on_stream_event = [state, ref = callback_ref.ref](const llm::GenerateStreamEvent &event) {
                call_lua_callback(state, ref, encode_generate_stream_event(event));
            };
        }

        const auto response =
            runtime.services->generate(request, callback_ref.ref != LUA_NOREF ? &on_stream_event : nullptr);
        push_json(state, encode_generate_response(response));
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_native_chat(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        if (runtime.services == nullptr || !runtime.services->chat)
        {
            throw std::runtime_error("native llm chat provider is unavailable");
        }

        luaL_checktype(state, 1, LUA_TTABLE);
        const int request_index = absolute_index(state, 1);
        const auto payload = request_json(state, request_index);

        llm::ChatRequest request;
        request.model = payload.value("model", runtime.default_model);
        request.messages = decode_messages(payload);
        request.tools = decode_tools(payload, "tools");
        request.format = decode_format(payload);
        request.stream = payload.value("stream", false);
        request.think = normalize_think(decode_think(payload));

        LuaRegistryRef callback_ref{state};
        lua_getfield(state, request_index, "on_stream");
        if (lua_isfunction(state, -1))
        {
            callback_ref.ref = luaL_ref(state, LUA_REGISTRYINDEX);
        }
        else
        {
            lua_pop(state, 1);
        }

        llm::ChatStreamCallback on_stream_event;
        if (callback_ref.ref != LUA_NOREF)
        {
            on_stream_event = [state, ref = callback_ref.ref](const llm::ChatStreamEvent &event) {
                call_lua_callback(state, ref, encode_chat_stream_event(event));
            };
        }

        const auto response =
            runtime.services->chat(request, callback_ref.ref != LUA_NOREF ? &on_stream_event : nullptr);
        push_json(state, encode_chat_response(response));
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_native_embed(lua_State *state)
{
    try
    {
        auto &runtime = context(state);
        if (runtime.services == nullptr || !runtime.services->embed)
        {
            throw std::runtime_error("native llm embed provider is unavailable");
        }

        luaL_checktype(state, 1, LUA_TTABLE);
        const auto payload = request_json(state, 1);

        llm::EmbedRequest request;
        request.model = payload.value("model", runtime.default_model);
        request.input = decode_embed_input(payload);
        request.truncate = payload.value("truncate", true);
        if (payload.contains("dimensions") && !payload.at("dimensions").is_null())
        {
            request.dimensions = payload.at("dimensions").get<std::int64_t>();
        }

        const auto response = runtime.services->embed(request);
        push_json(state, encode_embed_response(response));
        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

[[nodiscard]] int push_provider_registry(lua_State *state, int module_index)
{
    lua_getfield(state, module_index, "_providers");
    if (!lua_istable(state, -1))
    {
        lua_pop(state, 1);
        throw std::runtime_error("llm provider registry is unavailable");
    }

    return absolute_index(state, -1);
}

[[nodiscard]] std::string resolve_provider_name(lua_State *state, int request_index, int module_index)
{
    (void)module_index;
    auto read_provider_field = [&](const char *field) -> std::optional<std::string> {
        lua_getfield(state, request_index, field);
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
        if (value.empty())
        {
            throw std::invalid_argument(fmt::format("field '{}' must not be empty", field));
        }
        return value;
    };

    if (const auto provider = read_provider_field("provider"); provider.has_value())
    {
        return *provider;
    }

    throw std::invalid_argument(
        "field 'provider' is required; create an llm client with llm.create(name) or pass request.provider explicitly");
}

void validate_provider_table(lua_State *state, int provider_index)
{
    provider_index = absolute_index(state, provider_index);

    bool has_method = false;
    for (const auto *method_name : {"generate", "chat", "embed"})
    {
        lua_getfield(state, provider_index, method_name);
        if (!lua_isnil(state, -1))
        {
            if (!lua_isfunction(state, -1))
            {
                lua_pop(state, 1);
                throw std::invalid_argument(fmt::format("llm provider field '{}' must be a function", method_name));
            }

            has_method = true;
        }
        lua_pop(state, 1);
    }

    if (!has_method)
    {
        throw std::invalid_argument("llm provider must define at least one of generate, chat, or embed");
    }
}

int lua_register_provider(lua_State *state)
{
    try
    {
        const int module_index = push_module_table(state);

        const char *name = luaL_checkstring(state, 1);
        if (std::string_view(name).empty())
        {
            throw std::invalid_argument("llm provider name must not be empty");
        }

        luaL_checktype(state, 2, LUA_TTABLE);
        validate_provider_table(state, 2);

        const int providers_index = push_provider_registry(state, module_index);
        lua_pushvalue(state, 2);
        lua_setfield(state, providers_index, name);
        return 0;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

[[nodiscard]] int push_provider(lua_State *state, int module_index, const char *name)
{
    const int providers_index = push_provider_registry(state, module_index);
    lua_getfield(state, providers_index, name);
    if (!lua_istable(state, -1))
    {
        lua_pop(state, 1);
        throw std::invalid_argument(fmt::format("unknown llm provider '{}'", name));
    }

    return absolute_index(state, -1);
}

int lua_provider_names(lua_State *state)
{
    try
    {
        const int module_index = push_module_table(state);
        const int providers_index = push_provider_registry(state, module_index);

        lua_newtable(state);
        int output_index = 1;

        lua_pushnil(state);
        while (lua_next(state, providers_index) != 0)
        {
            if (lua_type(state, -2) == LUA_TSTRING)
            {
                lua_pushvalue(state, -2);
                lua_rawseti(state, -4, output_index++);
            }
            lua_pop(state, 1);
        }

        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

int lua_dispatch(lua_State *state)
{
    try
    {
        [[maybe_unused]] auto &runtime = context(state);
        luaL_checktype(state, 1, LUA_TTABLE);
        const int request_index = absolute_index(state, 1);
        const char *method_name = dispatch_method_name(state);

        const int module_index = push_module_table(state);
        const auto provider_name = resolve_provider_name(state, request_index, module_index);

        const int provider_index = push_provider(state, module_index, provider_name.c_str());
        lua_getfield(state, provider_index, method_name);
        if (!lua_isfunction(state, -1))
        {
            lua_pop(state, 1);
            throw std::invalid_argument(
                fmt::format("llm provider '{}' does not implement {}", provider_name, method_name));
        }

        lua_pushvalue(state, request_index);
        if (lua_pcall(state, 1, 1, 0) != 0)
        {
            auto message = lua_error_message(state);
            lua_pop(state, 1);
            throw std::runtime_error(
                fmt::format("llm provider '{}' {} failed: {}", provider_name, method_name, message));
        }

        return 1;
    }
    catch (const std::exception &error)
    {
        throw_lua_error(state, error.what());
    }
}

void push_llm_function(lua_State *state, ScriptLlmContext &runtime, int module_index, lua_CFunction function)
{
    lua_pushlightuserdata(state, &runtime);
    lua_pushvalue(state, module_index);
    lua_pushcclosure(state, function, 2);
}

void push_native_provider_function(lua_State *state, ScriptLlmContext &runtime, lua_CFunction function)
{
    lua_pushlightuserdata(state, &runtime);
    lua_pushcclosure(state, function, 1);
}

void push_llm_dispatch_function(lua_State *state, ScriptLlmContext &runtime, int module_index, lua_CFunction function,
                                const char *method_name)
{
    lua_pushlightuserdata(state, &runtime);
    lua_pushvalue(state, module_index);
    lua_pushstring(state, method_name);
    lua_pushcclosure(state, function, 3);
}

void install_create_function(lua_State *state, int module_index)
{
    constexpr std::string_view source = R"lua(
        local module = ...
        return function(name, defaults)
            if type(name) ~= "string" or name == "" then
                error("llm provider name must not be empty")
            end
            if type(module._providers[name]) ~= "table" then
                error(string.format("unknown llm provider '%s'", name))
            end
            if defaults ~= nil and type(defaults) ~= "table" then
                error("llm client defaults must be a table")
            end

            defaults = defaults or {}

            local client = {
                provider = name,
                defaults = defaults,
            }

            local function normalize_request(self_or_request, maybe_request)
                if maybe_request ~= nil then
                    return maybe_request
                end
                return self_or_request
            end

            local function dispatch(method, self_or_request, maybe_request)
                local request = normalize_request(self_or_request, maybe_request) or {}
                local merged = { provider = name }
                for key, value in pairs(defaults) do
                    merged[key] = value
                end
                for key, value in pairs(request) do
                    merged[key] = value
                end
                return module[method](merged)
            end

            function client.generate(self_or_request, maybe_request)
                return dispatch("generate", self_or_request, maybe_request)
            end

            function client.chat(self_or_request, maybe_request)
                return dispatch("chat", self_or_request, maybe_request)
            end

            function client.embed(self_or_request, maybe_request)
                return dispatch("embed", self_or_request, maybe_request)
            end

            return client
        end
    )lua";

    if (luaL_loadbuffer(state, source.data(), source.size(), "llm.create") != 0)
    {
        auto message = lua_error_message(state);
        lua_pop(state, 1);
        throw std::runtime_error(fmt::format("failed to compile llm.create helper: {}", message));
    }

    lua_pushvalue(state, module_index);
    if (lua_pcall(state, 1, 1, 0) != 0)
    {
        auto message = lua_error_message(state);
        lua_pop(state, 1);
        throw std::runtime_error(fmt::format("failed to initialize llm.create helper: {}", message));
    }

    lua_setfield(state, module_index, "create");
}

int open_llm_module(lua_State *state)
{
    auto &runtime = context(state);
    lua_newtable(state);
    const int module_index = absolute_index(state, -1);

    lua_newtable(state);
    const int providers_index = absolute_index(state, -1);

    require_module(state, "llm.providers.echo");
    lua_setfield(state, providers_index, "echo");

    require_module(state, "llm.providers.ollama");
    const int ollama_provider_index = absolute_index(state, -1);
    if (runtime.services != nullptr)
    {
        if (runtime.services->generate)
        {
            push_native_provider_function(state, runtime, lua_native_generate);
            lua_setfield(state, ollama_provider_index, "generate");
        }
        if (runtime.services->chat)
        {
            push_native_provider_function(state, runtime, lua_native_chat);
            lua_setfield(state, ollama_provider_index, "chat");
        }
        if (runtime.services->embed)
        {
            push_native_provider_function(state, runtime, lua_native_embed);
            lua_setfield(state, ollama_provider_index, "embed");
        }
    }
    lua_setfield(state, providers_index, "ollama");

    lua_setfield(state, module_index, "_providers");

    push_llm_function(state, runtime, module_index, lua_register_provider);
    lua_setfield(state, module_index, "register");

    push_llm_function(state, runtime, module_index, lua_provider_names);
    lua_setfield(state, module_index, "names");

    install_create_function(state, module_index);

    push_llm_dispatch_function(state, runtime, module_index, lua_dispatch, "generate");
    lua_setfield(state, module_index, "generate");

    push_llm_dispatch_function(state, runtime, module_index, lua_dispatch, "chat");
    lua_setfield(state, module_index, "chat");

    push_llm_dispatch_function(state, runtime, module_index, lua_dispatch, "embed");
    lua_setfield(state, module_index, "embed");

    return 1;
}
} // namespace

void register_llm_module(lua_State *state, ScriptLlmContext &context)
{
    lua_getglobal(state, "package");
    lua_getfield(state, -1, "preload");

    lua_pushlightuserdata(state, &context);
    lua_pushcclosure(state, open_llm_module, 1);
    lua_setfield(state, -2, "llm");

    lua_pop(state, 2);
}
} // namespace yaaf::script::modules
