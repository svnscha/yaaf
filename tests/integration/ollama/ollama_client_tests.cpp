#include "../../../libyaaf/pch/pch_dependencies.h"
#include "../../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../../libyaaf/script/lua_runtime.h"
#include "../../support/llm_provider_test_support.h"

namespace
{
constexpr std::string_view kOllamaEndpoint = "http://ollama.test";
constexpr std::string_view kOpenAiEndpoint = "http://openai.test/v1";
constexpr std::string_view kTestModel = "qwen3:0.6b";
constexpr std::string_view kEmbedModel = "nomic-embed-text:v1.5";

class LuaOllamaProviderTests : public ::testing::Test
{
};

class LuaOpenAiCompatibleProviderTests : public ::testing::Test
{
};

struct TemporaryScript
{
    std::filesystem::path path;

    explicit TemporaryScript(std::string_view file_name, std::string_view contents)
        : path(std::filesystem::temp_directory_path() / std::string(file_name))
    {
        std::ofstream script{path};
        script << contents;
    }

    ~TemporaryScript()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

[[nodiscard]] yaaf::script::LuaRuntimeOptions lua_runtime_options(const std::filesystem::path &script_path,
                                                                  std::string endpoint = std::string(kOllamaEndpoint),
                                                                  std::string model = std::string(kTestModel))
{
    yaaf::script::LuaRuntimeOptions options;
    options.file_path = script_path.string();
    options.endpoint = std::move(endpoint);
    options.model = std::move(model);
    options.http = HttpClient::Options{};
    return options;
}

[[nodiscard]] std::string run_lua_script(const TemporaryScript &script, const yaaf::script::Services *services = nullptr,
                                         std::string endpoint = std::string(kOllamaEndpoint),
                                         std::string model = std::string(kTestModel))
{
    auto options = lua_runtime_options(script.path, std::move(endpoint), std::move(model));
    std::ostringstream output;
    options.output = &output;

    EXPECT_EQ(yaaf::script::run_file(options, services), EXIT_SUCCESS);
    return output.str();
}

[[nodiscard]] nlohmann::json parse_json_output(const std::string &output)
{
    return nlohmann::json::parse(output);
}

[[nodiscard]] std::string json_string(const nlohmann::json &payload, std::string_view key)
{
    return payload.at(std::string(key)).get<std::string>();
}

[[nodiscard]] std::int64_t json_integer(const nlohmann::json &payload, std::string_view key)
{
    return payload.at(std::string(key)).get<std::int64_t>();
}

[[nodiscard]] yaaf::script::Services provider_services(yaaf::tests::llm::ScriptedProviderHttpFixture &fixture)
{
    yaaf::script::Services services;
    services.http_post = [&](std::string_view url, std::string_view body, std::string_view content_type,
                             const HttpClient::Headers &headers,
                             const HttpClient::ResponseChunkHandler *on_response_chunk) {
        return fixture.post(url, body, content_type, headers, on_response_chunk);
    };
    return services;
}
} // namespace

TEST_F(LuaOllamaProviderTests, GenerateReturnsCompletedResponse)
{
    const TemporaryScript script{"assistant_lua_ollama_generate_test.lua", R"(
local json = require("json")
local llm = require("llm")

local response = llm.generate({
  provider = "ollama",
  prompt = "Respond with a short greeting in plain text.",
  think = "none"
})

print(json.encode({
  model = response.model,
  done = response.done,
  created_at = response.created_at,
  response = response.response,
  eval_count = response.eval_count
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services));
    EXPECT_EQ(json_string(payload, "model"), std::string(kTestModel));
    EXPECT_EQ(payload.at("done"), true);
    EXPECT_FALSE(json_string(payload, "created_at").empty());
    EXPECT_FALSE(json_string(payload, "response").empty());
    EXPECT_GT(json_integer(payload, "eval_count"), 0);
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_EQ(fixture.requests().front().url, std::string(kOllamaEndpoint) + "/api/generate");
}

TEST_F(LuaOllamaProviderTests, GenerateStreamingCallbackPublishesLiveEvents)
{
    const TemporaryScript script{"assistant_lua_ollama_generate_stream_test.lua", R"(
local json = require("json")
local llm = require("llm")

local event_count = 0
local saw_done = false
local streamed = ""
local response = llm.generate({
  provider = "ollama",
  prompt = "Reply with hi only.",
  stream = true,
  think = "none",
  on_stream = function(event)
    event_count = event_count + 1
    streamed = streamed .. (event.response or "")
    saw_done = saw_done or (event.done == true)
  end,
})

print(json.encode({
  event_count = event_count,
  saw_done = saw_done,
  streamed = streamed,
  response = response.response
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services));
    EXPECT_GT(json_integer(payload, "event_count"), 0);
    EXPECT_EQ(payload.at("saw_done"), true);
    EXPECT_EQ(payload.at("streamed"), payload.at("response"));
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_TRUE(fixture.requests().front().streamed);
}

TEST_F(LuaOllamaProviderTests, ChatReturnsCompletedResponse)
{
    const TemporaryScript script{"assistant_lua_ollama_chat_test.lua", R"(
local json = require("json")
local llm = require("llm")

local response = llm.chat({
  provider = "ollama",
  think = "none",
  messages = {
    { role = "user", content = "Respond with a short greeting in plain text." }
  }
})

print(json.encode({
  model = response.model,
  done = response.done,
  created_at = response.created_at,
  role = response.message.role,
  content = response.message.content
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services));
    EXPECT_EQ(json_string(payload, "model"), std::string(kTestModel));
    EXPECT_EQ(payload.at("done"), true);
    EXPECT_FALSE(json_string(payload, "created_at").empty());
    EXPECT_EQ(json_string(payload, "role"), "assistant");
    EXPECT_FALSE(json_string(payload, "content").empty());
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_EQ(fixture.requests().front().url, std::string(kOllamaEndpoint) + "/api/chat");
}

TEST_F(LuaOllamaProviderTests, ChatStreamingCallbackPublishesLiveEvents)
{
    const TemporaryScript script{"assistant_lua_ollama_chat_stream_test.lua", R"(
local json = require("json")
local llm = require("llm")

local event_count = 0
local saw_done = false
local streamed = ""
local response = llm.chat({
  provider = "ollama",
  stream = true,
  think = "none",
  messages = {
    { role = "user", content = "Reply with hi only." }
  },
  on_stream = function(event)
    event_count = event_count + 1
    local message = event.message or {}
    streamed = streamed .. (message.content or "")
    saw_done = saw_done or (event.done == true)
  end,
})

print(json.encode({
  event_count = event_count,
  saw_done = saw_done,
  streamed = streamed,
  content = response.message.content,
  role = response.message.role
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services));
    EXPECT_GT(json_integer(payload, "event_count"), 0);
    EXPECT_EQ(payload.at("saw_done"), true);
    EXPECT_EQ(json_string(payload, "role"), "assistant");
    EXPECT_EQ(payload.at("streamed"), payload.at("content"));
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_TRUE(fixture.requests().front().streamed);
}

TEST_F(LuaOllamaProviderTests, EmbedReturnsSingleEmbedding)
{
    const TemporaryScript script{"assistant_lua_ollama_embed_single_test.lua", R"(
local json = require("json")
local llm = require("llm")

local response = llm.embed({
  provider = "ollama",
  model = "nomic-embed-text:v1.5",
  input = "hello from lua"
})

print(json.encode({
  model = response.model,
  embedding_count = #response.embeddings,
  width = #response.embeddings[1]
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services, std::string(kOllamaEndpoint), std::string(kEmbedModel)));
    EXPECT_EQ(json_string(payload, "model"), std::string(kEmbedModel));
    EXPECT_EQ(json_integer(payload, "embedding_count"), 1);
    EXPECT_GT(json_integer(payload, "width"), 0);
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_EQ(fixture.requests().front().url, std::string(kOllamaEndpoint) + "/api/embed");
}

TEST_F(LuaOllamaProviderTests, EmbedReturnsBatchEmbeddings)
{
    const TemporaryScript script{"assistant_lua_ollama_embed_batch_test.lua", R"(
local json = require("json")
local llm = require("llm")

local response = llm.embed({
  provider = "ollama",
  model = "nomic-embed-text:v1.5",
  input = { "hello from lua", "goodbye from lua" }
})

print(json.encode({
  model = response.model,
  embedding_count = #response.embeddings,
  first_width = #response.embeddings[1],
  second_width = #response.embeddings[2]
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services, std::string(kOllamaEndpoint), std::string(kEmbedModel)));
    EXPECT_EQ(json_string(payload, "model"), std::string(kEmbedModel));
    EXPECT_EQ(json_integer(payload, "embedding_count"), 2);
    EXPECT_GT(json_integer(payload, "first_width"), 0);
    EXPECT_GT(json_integer(payload, "second_width"), 0);
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_EQ(fixture.requests().front().url, std::string(kOllamaEndpoint) + "/api/embed");
}

TEST_F(LuaOpenAiCompatibleProviderTests, GenerateReturnsCompletedResponse)
{
    const TemporaryScript script{"assistant_lua_openai_generate_test.lua", R"(
local json = require("json")
local llm = require("llm")

local response = llm.generate({
  provider = "openai",
  endpoint = "http://openai.test/v1",
  model = "qwen3:0.6b",
  prompt = "Respond with a short greeting in plain text.",
  think = "none"
})

print(json.encode({
  model = response.model,
  done = response.done,
  created_at = response.created_at,
  response = response.response
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services, std::string(kOpenAiEndpoint)));
    EXPECT_EQ(json_string(payload, "model"), std::string(kTestModel));
    EXPECT_EQ(payload.at("done"), true);
    EXPECT_FALSE(json_string(payload, "created_at").empty());
    EXPECT_FALSE(json_string(payload, "response").empty());
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_EQ(fixture.requests().front().url, std::string(kOpenAiEndpoint) + "/chat/completions");
}

TEST_F(LuaOpenAiCompatibleProviderTests, GenerateStreamingCallbackPublishesLiveEvents)
{
    const TemporaryScript script{"assistant_lua_openai_generate_stream_test.lua", R"(
local json = require("json")
local llm = require("llm")

local event_count = 0
local saw_done = false
local streamed = ""
local response = llm.generate({
  provider = "openai",
  endpoint = "http://openai.test/v1",
  model = "qwen3:0.6b",
  prompt = "Reply with hi only.",
  stream = true,
  think = "none",
  on_stream = function(event)
    event_count = event_count + 1
    streamed = streamed .. (event.response or "")
    saw_done = saw_done or (event.done == true)
  end,
})

print(json.encode({
  event_count = event_count,
  saw_done = saw_done,
  streamed = streamed,
  response = response.response
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services, std::string(kOpenAiEndpoint)));
    EXPECT_GT(json_integer(payload, "event_count"), 0);
    EXPECT_EQ(payload.at("saw_done"), true);
    EXPECT_EQ(payload.at("streamed"), payload.at("response"));
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_TRUE(fixture.requests().front().streamed);
}

TEST_F(LuaOpenAiCompatibleProviderTests, ChatReturnsCompletedResponse)
{
    const TemporaryScript script{"assistant_lua_openai_chat_test.lua", R"(
local json = require("json")
local llm = require("llm")

local response = llm.chat({
  provider = "openai",
  endpoint = "http://openai.test/v1",
  model = "qwen3:0.6b",
  think = "none",
  messages = {
    { role = "user", content = "Respond with a short greeting in plain text." }
  }
})

print(json.encode({
  model = response.model,
  done = response.done,
  created_at = response.created_at,
  role = response.message.role,
  content = response.message.content
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services, std::string(kOpenAiEndpoint)));
    EXPECT_EQ(json_string(payload, "model"), std::string(kTestModel));
    EXPECT_EQ(payload.at("done"), true);
    EXPECT_FALSE(json_string(payload, "created_at").empty());
    EXPECT_EQ(json_string(payload, "role"), "assistant");
    EXPECT_FALSE(json_string(payload, "content").empty());
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_EQ(fixture.requests().front().url, std::string(kOpenAiEndpoint) + "/chat/completions");
}

TEST_F(LuaOpenAiCompatibleProviderTests, ChatStreamingCallbackPublishesLiveEvents)
{
    const TemporaryScript script{"assistant_lua_openai_chat_stream_test.lua", R"(
local json = require("json")
local llm = require("llm")

local event_count = 0
local saw_done = false
local streamed = ""
local response = llm.chat({
  provider = "openai",
  endpoint = "http://openai.test/v1",
  model = "qwen3:0.6b",
  stream = true,
  think = "none",
  messages = {
    { role = "user", content = "Reply with hi only." }
  },
  on_stream = function(event)
    event_count = event_count + 1
    local message = event.message or {}
    streamed = streamed .. (message.content or "")
    saw_done = saw_done or (event.done == true)
  end,
})

print(json.encode({
  event_count = event_count,
  saw_done = saw_done,
  streamed = streamed,
  content = response.message.content,
  role = response.message.role
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services, std::string(kOpenAiEndpoint)));
    EXPECT_GT(json_integer(payload, "event_count"), 0);
    EXPECT_EQ(payload.at("saw_done"), true);
    EXPECT_EQ(json_string(payload, "role"), "assistant");
    EXPECT_EQ(payload.at("streamed"), payload.at("content"));
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_TRUE(fixture.requests().front().streamed);
}

TEST_F(LuaOpenAiCompatibleProviderTests, EmbedReturnsSingleEmbedding)
{
    const TemporaryScript script{"assistant_lua_openai_embed_single_test.lua", R"(
local json = require("json")
local llm = require("llm")

local response = llm.embed({
  provider = "openai",
  endpoint = "http://openai.test/v1",
  api_key = "sk-test",
  model = "nomic-embed-text:v1.5",
  input = "hello from lua"
})

print(json.encode({
  model = response.model,
  embedding_count = #response.embeddings,
  width = #response.embeddings[1]
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services, std::string(kOpenAiEndpoint), std::string(kEmbedModel)));
    EXPECT_EQ(json_string(payload, "model"), std::string(kEmbedModel));
    EXPECT_EQ(json_integer(payload, "embedding_count"), 1);
    EXPECT_GT(json_integer(payload, "width"), 0);
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_EQ(fixture.requests().front().url, std::string(kOpenAiEndpoint) + "/embeddings");
}

TEST_F(LuaOpenAiCompatibleProviderTests, EmbedReturnsBatchEmbeddings)
{
    const TemporaryScript script{"assistant_lua_openai_embed_batch_test.lua", R"(
local json = require("json")
local llm = require("llm")

local response = llm.embed({
  provider = "openai",
  endpoint = "http://openai.test/v1",
  api_key = "sk-test",
  model = "nomic-embed-text:v1.5",
  input = { "hello from lua", "goodbye from lua" }
})

print(json.encode({
  model = response.model,
  embedding_count = #response.embeddings,
  first_width = #response.embeddings[1],
  second_width = #response.embeddings[2]
}))
)"};

    yaaf::tests::llm::ScriptedProviderHttpFixture fixture;
    const auto services = provider_services(fixture);

    const auto payload = parse_json_output(run_lua_script(script, &services, std::string(kOpenAiEndpoint), std::string(kEmbedModel)));
    EXPECT_EQ(json_string(payload, "model"), std::string(kEmbedModel));
    EXPECT_EQ(json_integer(payload, "embedding_count"), 2);
    EXPECT_GT(json_integer(payload, "first_width"), 0);
    EXPECT_GT(json_integer(payload, "second_width"), 0);
    ASSERT_EQ(fixture.requests().size(), 1U);
    EXPECT_EQ(fixture.requests().front().url, std::string(kOpenAiEndpoint) + "/embeddings");
}


