#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/cli/cli.h"
#include "../../libyaaf/script/lua_runtime.h"

namespace
{
yaaf::script::LuaRuntimeOptions lua_runtime_options(const std::filesystem::path &script_path)
{
    yaaf::script::LuaRuntimeOptions options;
    options.file_path = script_path.string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";
    return options;
}
} // namespace

TEST(ScriptLuaTests, ReadsCommandMetadataWithoutRunningCommandBody)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_command_metadata_test.lua";
    {
        std::ofstream script{script_path};
        script << "local yaaf = require('yaaf')\n";
        script << "return yaaf.command({\n";
        script << "  description = 'Example command',\n";
        script << "  options = {{ name = 'value', flags = {'--value'}, type = 'string', default = 'fallback', "
                  "description = 'Value option' }},\n";
        script << "  positionals = {{ name = 'prompt', multiple = true, required = true, description = 'Prompt text' "
                  "}},\n";
        script << "  run = function() error('command body should not run while reading metadata') end\n";
        script << "})\n";
    }

    const auto metadata = yaaf::script::read_command_metadata(lua_runtime_options(script_path));

    std::filesystem::remove(script_path);

    EXPECT_EQ(metadata.at("description"), "Example command");
    ASSERT_TRUE(metadata.at("options").is_array());
    ASSERT_EQ(metadata.at("options").size(), 1U);
    EXPECT_EQ(metadata.at("options").at(0).at("name"), "value");
    ASSERT_TRUE(metadata.at("positionals").is_array());
    EXPECT_EQ(metadata.at("positionals").at(0).at("name"), "prompt");
}

TEST(ScriptLuaTests, CommandReceivesParsedOptionsAndPositionals)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_command_options_test.lua";
    {
        std::ofstream script{script_path};
        script << "local yaaf = require('yaaf')\n";
        script << "return yaaf.command({\n";
        script << "  description = 'Example command',\n";
        script << "  run = function(command)\n";
        script << "    print(command.options.value)\n";
        script << "    print(command.options.enabled)\n";
        script << "    print(table.concat(command.positionals.prompt, ' '))\n";
        script << "  end\n";
        script << "})\n";
    }

    auto options = lua_runtime_options(script_path);
    options.options = nlohmann::json{{"value", "parsed"}, {"enabled", true}};
    options.positionals = nlohmann::json{{"prompt", std::vector<std::string>{"hello", "lua"}}};
    std::ostringstream output;
    options.output = &output;

    const auto exit_code = yaaf::script::run_file(options);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_EQ(output.str(), "parsed\ntrue\nhello lua\n");
}

TEST(ScriptLuaTests, RunsLuaFileWithLlmOllamaProviderChat)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_chat_test.lua";
    {
        std::ofstream script{script_path};
        script << "local llm = require('llm')\n";
        script << "local response = llm.chat({\n";
        script << "  provider = 'ollama',\n";
        script << "  messages = {{ role = 'user', content = 'hello from lua' }},\n";
        script << "  think = 'none'\n";
        script << "})\n";
        script << "print(response.message.content)\n";
    }

    yaaf::cli::Services services;
    services.chat = [](const yaaf::llm::ChatRequest &request,
                       const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.model, "qwen3:0.6b");
        EXPECT_FALSE(request.stream);
        EXPECT_EQ(request.messages.size(), 1U);
        EXPECT_EQ(request.messages.front().role, "user");
        EXPECT_EQ(request.messages.front().content, "hello from lua");
        EXPECT_TRUE(request.think.has_value());
        if (request.think.has_value())
        {
            const auto *think = std::get_if<bool>(&*request.think);
            EXPECT_NE(think, nullptr);
            if (think != nullptr)
            {
                EXPECT_FALSE(*think);
            }
        }

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.done_reason = "stop";
        response.message.role = "assistant";
        response.message.content = "hello from yaaf";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output, &services);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "hello from yaaf\n");
}

TEST(ScriptLuaTests, RunsLuaFileWithRequiredLlmChatModule)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_llm_chat_test.lua";
    {
        std::ofstream script{script_path};
        script << "local llm = require('llm')\n";
        script << "local ollama = llm.create('ollama')\n";
        script << "local response = ollama.chat({\n";
        script << "  messages = {{ role = 'user', content = 'hello from llm' }},\n";
        script << "  think = 'none'\n";
        script << "})\n";
        script << "print(response.message.content)\n";
    }

    yaaf::cli::Services services;
    services.chat = [](const yaaf::llm::ChatRequest &request,
                       const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.model, "qwen3:0.6b");
        EXPECT_EQ(request.messages.size(), 1U);
        EXPECT_EQ(request.messages.front().content, "hello from llm");

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        response.message.content = "hello from llm bridge";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output, &services);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "hello from llm bridge\n");
}

TEST(ScriptLuaTests, LlmOllamaProviderSupportsToolsAndToolCalls)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_chat_tools_test.lua";
    {
        std::ofstream script{script_path};
        script << "local llm = require('llm')\n";
        script << "local response = llm.chat({\n";
        script << "  provider = 'ollama',\n";
        script << "  messages = {\n";
        script << "    { role = 'user', content = 'echo hello' },\n";
        script << "    { role = 'assistant', content = '', tool_calls = {{ ['function'] = { name = 'echo', "
                  "arguments = { text = 'hello' } } }} },\n";
        script << "    { role = 'tool', content = 'hello' },\n";
        script << "  },\n";
        script << "  tools = {{ type = 'function', ['function'] = { name = 'echo', description = 'Echo text', "
                  "parameters = { type = 'object' } } }},\n";
        script << "})\n";
        script << "print(response.message.tool_calls[1]['function'].name)\n";
        script << "print(response.message.tool_calls[1]['function'].arguments.text)\n";
    }

    yaaf::cli::Services services;
    services.chat = [](const yaaf::llm::ChatRequest &request,
                       const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.tools.size(), 1U);
        if (!request.tools.empty())
        {
            EXPECT_EQ(request.tools.front().type, "function");
            EXPECT_EQ(request.tools.front().function.name, "echo");
            EXPECT_TRUE(request.tools.front().function.description.has_value());
            if (request.tools.front().function.description.has_value())
            {
                EXPECT_EQ(*request.tools.front().function.description, "Echo text");
            }
            EXPECT_EQ(request.tools.front().function.arguments.at("type"), "object");
        }

        EXPECT_EQ(request.messages.size(), 3U);
        if (request.messages.size() > 1)
        {
            EXPECT_EQ(request.messages.at(1).tool_calls.size(), 1U);
            if (!request.messages.at(1).tool_calls.empty())
            {
                EXPECT_EQ(request.messages.at(1).tool_calls.front().function.name, "echo");
                EXPECT_EQ(request.messages.at(1).tool_calls.front().function.arguments.at("text"), "hello");
            }
        }

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";

        yaaf::llm::Tool tool_call;
        tool_call.function.name = "echo";
        tool_call.function.arguments = nlohmann::json{{"text", "goodbye"}};
        response.message.tool_calls.push_back(std::move(tool_call));
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output, &services);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "echo\ngoodbye\n");
}

TEST(ScriptLuaTests, ExposesStandaloneJsonHelpers)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_json_test.lua";
    {
        std::ofstream script{script_path};
        script << "local json = require('json')\n";
        script << "local payload = json.decode('{\"answer\":\"ok\"}')\n";
        script << "print(json.encode({ answer = payload.answer }))\n";
        script << "local llm = require('llm')\n";
        script << "print(type(llm.json))\n";
    }

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "{\"answer\":\"ok\"}\nnil\n");
}

TEST(ScriptLuaTests, JsonHelpersEncodeNilAndNumericObjectKeys)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_json_edge_test.lua";
    {
        std::ofstream script{script_path};
        script << "local json = require('json')\n";
        script << "print(json.encode(nil))\n";
        script << "local payload = json.decode(json.encode({ [1.5] = 'half', [2] = 'two' }))\n";
        script << "print(payload['1.5'])\n";
        script << "print(payload['2'])\n";
    }

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "null\nhalf\ntwo\n");
}

TEST(ScriptLuaTests, JsonHelpersPreserveIntegerValues)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_json_integer_test.lua";
    {
        std::ofstream script{script_path};
        script << "local json = require('json')\n";
        script << "local payload = json.decode('{\"index\":0,\"count\":3,\"fraction\":1.5}')\n";
        script << "print(json.encode(payload))\n";
    }

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "{\"count\":3,\"fraction\":1.5,\"index\":0}\n");
}

TEST(ScriptLuaTests, JsonHelpersReportUnsupportedValuesAndInvalidKeys)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_json_error_test.lua";
    {
        std::ofstream script{script_path};
        script << "local json = require('json')\n";
        script << "local ok_value, err_value = pcall(function() return json.encode(function() end) end)\n";
        script << "print(ok_value)\n";
        script << "print(string.find(err_value, 'unsupported Lua value type for JSON conversion', 1, true) ~= nil)\n";
        script << "local invalid_key = {}\n";
        script << "local ok_key, err_key = pcall(function() return json.encode({ [invalid_key] = 'value' }) end)\n";
        script << "print(ok_key)\n";
        script << "print(string.find(err_key, 'Lua table keys must be strings or numbers for JSON conversion', 1, "
                  "true) ~= nil)\n";
    }

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "false\ntrue\nfalse\ntrue\n");
}

TEST(ScriptLuaTests, RunsLuaFileWithRequiredLlmGenerateAndEmbedModule)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_generate_embed_test.lua";
    {
        std::ofstream script{script_path};
        script << "local llm = require('llm')\n";
        script << "local ollama = llm.create('ollama')\n";
        script << "local generated = ollama.generate({ prompt = 'say hi', format = { type = 'object' } })\n";
        script << "local embedded = ollama.embed({ input = {'one', 'two'}, truncate = false, dimensions = 64 })\n";
        script << "print(generated.response)\n";
        script << "print(#embedded.embeddings)\n";
        script << "print(embedded.prompt_eval_count)\n";
    }

    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.model, "qwen3:0.6b");
        EXPECT_EQ(request.prompt, "say hi");
        EXPECT_FALSE(request.stream);
        EXPECT_TRUE(request.format.has_value());
        if (request.format.has_value())
        {
            const auto *schema = std::get_if<nlohmann::json>(&*request.format);
            EXPECT_NE(schema, nullptr);
            if (schema != nullptr)
            {
                EXPECT_EQ(schema->at("type"), "object");
            }
        }

        yaaf::llm::GenerateResponse response;
        response.model = request.model;
        response.response = "hello";
        response.done = true;
        return response;
    };
    services.embed = [](const yaaf::llm::EmbedRequest &request) -> yaaf::llm::EmbedResponse {
        EXPECT_EQ(request.model, "qwen3:0.6b");
        EXPECT_FALSE(request.truncate);
        EXPECT_TRUE(request.dimensions.has_value());
        if (request.dimensions.has_value())
        {
            EXPECT_EQ(*request.dimensions, 64);
        }
        const auto *inputs = std::get_if<std::vector<std::string>>(&request.input);
        EXPECT_NE(inputs, nullptr);
        if (inputs != nullptr)
        {
            EXPECT_EQ(*inputs, (std::vector<std::string>{"one", "two"}));
        }

        yaaf::llm::EmbedResponse response;
        response.model = request.model;
        response.embeddings = {{1.0, 2.0}, {3.0, 4.0}};
        response.prompt_eval_count = 2;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output, &services);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "hello\n2\n2\n");
}

TEST(ScriptLuaTests, LlmModuleRegistersLuaProviderCallbacks)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_custom_llm_test.lua";
    {
        std::ofstream script{script_path};
        script << "local http = require('http')\n";
        script << "local llm = require('llm')\n";
        script << "llm.register('custom', {\n";
        script << "  generate = function(request)\n";
        script << "    local response = http.post({\n";
        script << "      url = request.endpoint .. '/generate',\n";
        script << "      body = request.prompt,\n";
        script << "      content_type = 'text/plain',\n";
        script << "      headers = { ['X-Model'] = request.model },\n";
        script << "    })\n";
        script << "    return { model = request.model, response = response.body, done = true }\n";
        script << "  end,\n";
        script << "  chat = function(request)\n";
        script << "    local first = request.messages[1] or {}\n";
        script << "    return { model = request.model, message = { role = 'yaaf', content = 'chat:' .. (first.content "
                  "or '') }, done = true }\n";
        script << "  end,\n";
        script << "  embed = function(request)\n";
        script << "    local response = http.get({ url = request.endpoint .. '/embed', headers = { ['X-Model'] = "
                  "request.model } })\n";
        script << "    return { model = request.model, embeddings = {{ response.status_code / 100 }}, "
                  "prompt_eval_count = 1 }\n";
        script << "  end,\n";
        script << "})\n";
        script << "local names = llm.names()\n";
        script << "table.sort(names)\n";
        script << "assert(names[1] == 'custom')\n";
        script << "assert(names[2] == 'echo')\n";
        script << "assert(names[3] == 'ollama')\n";
        script << "local client = llm.create('custom', { endpoint = 'http://llm.test', model = 'lua-client' })\n";
        script << "local generated = client.generate({ prompt = 'hello' })\n";
        script << "local chatted = client.chat({ messages = {{ role = 'user', content = 'hi' }} })\n";
        script << "local embedded = client.embed({ input = 'one' })\n";
        script << "print(generated.response)\n";
        script << "print(chatted.message.content)\n";
        script << "print(embedded.embeddings[1][1])\n";
    }

    yaaf::cli::Services services;
    services.http_get = [](std::string_view url, const HttpClient::Headers &headers) -> HttpClient::Response {
        EXPECT_EQ(url, "http://llm.test/embed");
        EXPECT_EQ(headers.size(), 1U);
        if (!headers.empty())
        {
            EXPECT_EQ(headers.front().first, "X-Model");
            EXPECT_EQ(headers.front().second, "lua-client");
        }

        HttpClient::Response response;
        response.status_code = 250;
        return response;
    };
    services.http_post = [](std::string_view url, std::string_view body, std::string_view content_type,
                            const HttpClient::Headers &headers,
                            const HttpClient::ResponseChunkHandler *on_response_chunk) -> HttpClient::Response {
        EXPECT_EQ(url, "http://llm.test/generate");
        EXPECT_EQ(body, "hello");
        EXPECT_EQ(content_type, "text/plain");
        EXPECT_EQ(headers.size(), 1U);
        if (!headers.empty())
        {
            EXPECT_EQ(headers.front().first, "X-Model");
            EXPECT_EQ(headers.front().second, "lua-client");
        }
        EXPECT_EQ(on_response_chunk, nullptr);

        HttpClient::Response response;
        response.body = "generated-from-lua";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output, &services);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "generated-from-lua\nchat:hi\n2.5\n");
}

TEST(ScriptLuaTests, BuiltInEchoProviderMirrorsInputs)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_echo_provider_test.lua";
    {
        std::ofstream script{script_path};
        script << "local llm = require('llm')\n";
        script
            << "local generated = llm.generate({ provider = 'echo', model = 'echo-model', prompt = 'hello echo' })\n";
        script << "local chatted = llm.chat({ provider = 'echo', model = 'echo-model', messages = {{ role = 'user', "
                  "content = 'chat echo' }} })\n";
        script << "local embedded = llm.embed({ provider = 'echo', model = 'echo-model', input = { 'a', 'abcd' } })\n";
        script << "print(generated.response)\n";
        script << "print(chatted.message.content)\n";
        script << "print(embedded.embeddings[1][1])\n";
        script << "print(embedded.embeddings[2][1])\n";
    }

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "hello echo\nchat echo\n1\n4\n");
}

TEST(ScriptLuaTests, DirectLlmCallsRequireExplicitProvider)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_llm_provider_required_test.lua";
    {
        std::ofstream script{script_path};
        script << "local llm = require('llm')\n";
        script << "llm.generate({ prompt = 'missing provider' })\n";
    }

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("field 'provider' is required"), std::string::npos);
}

TEST(ScriptLuaTests, RunsLuaFileWithRequiredHttpModule)
{
    const auto script_path = std::filesystem::temp_directory_path() / "assistant_lua_http_test.lua";
    {
        std::ofstream script{script_path};
        script << "local http = require('http')\n";
        script << "local get_response = http.get({ url = 'http://example.test/get', headers = { Accept = "
                  "'application/json' } })\n";
        script << "local chunks = {}\n";
        script << "local post_response = http.post({\n";
        script << "  url = 'http://example.test/post',\n";
        script << "  body = '{\\\"hello\\\":\\\"world\\\"}',\n";
        script << "  content_type = 'application/json',\n";
        script << "  headers = { ['X-Test'] = 'yes' },\n";
        script << "  on_response_chunk = function(chunk) table.insert(chunks, chunk) end,\n";
        script << "})\n";
        script << "print(get_response.status_code)\n";
        script << "print(get_response.headers['X-Reply'])\n";
        script << "print(post_response.status_code)\n";
        script << "print(table.concat(chunks, ''))\n";
        script << "print(post_response.headers['X-Result'])\n";
    }

    yaaf::cli::Services services;
    services.http_get = [](std::string_view url, const HttpClient::Headers &headers) -> HttpClient::Response {
        EXPECT_EQ(url, "http://example.test/get");
        EXPECT_EQ(headers.size(), 1U);
        if (!headers.empty())
        {
            EXPECT_EQ(headers.front().first, "Accept");
            EXPECT_EQ(headers.front().second, "application/json");
        }

        HttpClient::Response response;
        response.status_code = 200;
        response.content_type = "application/json";
        response.body = "{\"ok\":true}";
        response.headers = {{"X-Reply", "json"}};
        return response;
    };
    services.http_post = [](std::string_view url, std::string_view body, std::string_view content_type,
                            const HttpClient::Headers &headers,
                            const HttpClient::ResponseChunkHandler *on_response_chunk) -> HttpClient::Response {
        EXPECT_EQ(url, "http://example.test/post");
        EXPECT_EQ(body, "{\"hello\":\"world\"}");
        EXPECT_EQ(content_type, "application/json");
        EXPECT_EQ(headers.size(), 1U);
        if (!headers.empty())
        {
            EXPECT_EQ(headers.front().first, "X-Test");
            EXPECT_EQ(headers.front().second, "yes");
        }
        EXPECT_NE(on_response_chunk, nullptr);
        if (on_response_chunk != nullptr)
        {
            (*on_response_chunk)("chunk-");
            (*on_response_chunk)("stream");
        }

        HttpClient::Response response;
        response.status_code = 201;
        response.content_type = "text/plain";
        response.body = "created";
        response.headers = {{"X-Result", "ok"}};
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output, &services);

    std::filesystem::remove(script_path);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "200\njson\n201\nchunk-stream\nok\n");
}
