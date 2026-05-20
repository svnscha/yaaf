#include "../../../libyaaf/pch/pch_dependencies.h"
#include "../../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../../libyaaf/config/dotenv.h"
#include "../../../libyaaf/script/lua_runtime.h"
#include "../../support/runtime_test_environment.h"

namespace
{
constexpr std::string_view kDefaultOllamaEndpoint = "http://localhost:11434";
constexpr std::string_view kTestModel = "qwen3:0.6b";
constexpr std::string_view kEmbedModel = "nomic-embed-text:v1.5";

[[nodiscard]] std::string ollama_endpoint()
{
    static const std::string endpoint =
        yaaf::tests::runtime_env_value(yaaf::tests::load_runtime_dotenv(), "OLLAMA_ENDPOINT")
            .value_or(std::string(kDefaultOllamaEndpoint));
    return endpoint;
}

[[nodiscard]] std::string ollama_tags_url(std::string endpoint)
{
    if (endpoint.empty() || endpoint.back() != '/')
    {
        endpoint += '/';
    }
    endpoint += "api/tags";
    return endpoint;
}

void require_reachable_ollama()
{
    const auto endpoint = ollama_endpoint();
    try
    {
        const auto response =
            HttpClient{yaaf::tests::runtime_http_options_for_url(endpoint)}.get(ollama_tags_url(endpoint));
        if (response.status_code >= 200 && response.status_code < 300)
        {
            return;
        }
    }
    catch (const std::exception &)
    {
    }

    GTEST_SKIP() << "Ollama endpoint is not reachable at " << endpoint;
}

class LuaOllamaProviderTests : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        require_reachable_ollama();
    }
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
                                                                  std::string model = std::string(kTestModel))
{
    yaaf::script::LuaRuntimeOptions options;
    options.file_path = script_path.string();
    options.endpoint = ollama_endpoint();
    options.model = std::move(model);
    options.http = yaaf::tests::runtime_http_options_for_url(options.endpoint);
    return options;
}

[[nodiscard]] std::string run_lua_script(const TemporaryScript &script, std::string model = std::string(kTestModel))
{
    auto options = lua_runtime_options(script.path, std::move(model));
    std::ostringstream output;
    options.output = &output;

    EXPECT_EQ(yaaf::script::run_file(options), EXIT_SUCCESS);
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

    const auto payload = parse_json_output(run_lua_script(script));
    EXPECT_EQ(json_string(payload, "model"), std::string(kTestModel));
    EXPECT_EQ(payload.at("done"), true);
    EXPECT_FALSE(json_string(payload, "created_at").empty());
    EXPECT_FALSE(json_string(payload, "response").empty());
    EXPECT_GT(json_integer(payload, "eval_count"), 0);
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

    const auto payload = parse_json_output(run_lua_script(script));
    EXPECT_GT(json_integer(payload, "event_count"), 0);
    EXPECT_EQ(payload.at("saw_done"), true);
    EXPECT_EQ(payload.at("streamed"), payload.at("response"));
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

    const auto payload = parse_json_output(run_lua_script(script));
    EXPECT_EQ(json_string(payload, "model"), std::string(kTestModel));
    EXPECT_EQ(payload.at("done"), true);
    EXPECT_FALSE(json_string(payload, "created_at").empty());
    EXPECT_EQ(json_string(payload, "role"), "assistant");
    EXPECT_FALSE(json_string(payload, "content").empty());
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

    const auto payload = parse_json_output(run_lua_script(script));
    EXPECT_GT(json_integer(payload, "event_count"), 0);
    EXPECT_EQ(payload.at("saw_done"), true);
    EXPECT_EQ(json_string(payload, "role"), "assistant");
    EXPECT_EQ(payload.at("streamed"), payload.at("content"));
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

    const auto payload = parse_json_output(run_lua_script(script, std::string(kEmbedModel)));
    EXPECT_EQ(json_string(payload, "model"), std::string(kEmbedModel));
    EXPECT_EQ(json_integer(payload, "embedding_count"), 1);
    EXPECT_GT(json_integer(payload, "width"), 0);
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

    const auto payload = parse_json_output(run_lua_script(script, std::string(kEmbedModel)));
    EXPECT_EQ(json_string(payload, "model"), std::string(kEmbedModel));
    EXPECT_EQ(json_integer(payload, "embedding_count"), 2);
    EXPECT_GT(json_integer(payload, "first_width"), 0);
    EXPECT_GT(json_integer(payload, "second_width"), 0);
}
