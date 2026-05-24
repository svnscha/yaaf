#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/cli/cli.h"

namespace
{
class ScopedEnvironmentVariable
{
  public:
    ScopedEnvironmentVariable(std::string name, std::string value) : name_(std::move(name))
    {
        if (const auto *current = std::getenv(name_.c_str()); current != nullptr)
        {
            original_ = current;
        }
        set(value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (original_.has_value())
        {
            set(*original_);
        }
        else
        {
            unset();
        }
    }

  private:
    void set(const std::string &value) const
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void unset() const
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    std::optional<std::string> original_;
};

class ScopedCurrentPath
{
  public:
    explicit ScopedCurrentPath(const std::filesystem::path &path) : original_(std::filesystem::current_path())
    {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath()
    {
        std::error_code ignored;
        std::filesystem::current_path(original_, ignored);
    }

  private:
    std::filesystem::path original_;
};

[[nodiscard]] std::filesystem::path make_test_directory(std::string_view name)
{
    const auto path = std::filesystem::temp_directory_path() / fmt::format("yaaf-{}-{}", name, std::rand());
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

void write_file(const std::filesystem::path &path, std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file{path};
    file << contents;
}

nlohmann::json parse_json_output(const std::ostringstream &output)
{
    return nlohmann::json::parse(output.str());
}
} // namespace

TEST(CliEmbedCommandTests, WritesJsonPayload)
{
    yaaf::cli::Services services;
    services.embed = [](const yaaf::llm::EmbedRequest &request) {
        EXPECT_EQ(std::get<std::string>(request.input), "hello world");

        yaaf::llm::EmbedResponse response;
        response.model = "nomic-embed-text:v1.5";
        response.embeddings = {{0.1, 0.2, 0.3}};
        response.prompt_eval_count = 3;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"embed", "hello world"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("model"), "nomic-embed-text:v1.5");
    ASSERT_EQ(payload.at("embeddings").size(), 1U);
    EXPECT_EQ(payload.at("prompt_eval_count"), 3);
}

TEST(CliEmbedCommandTests, HelpIsServedByNativeCliMetadata)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"embed", "--help"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("Generate embeddings for one or more input texts"), std::string::npos);
    EXPECT_NE(output.str().find("POSITIONALS"), std::string::npos);
    EXPECT_NE(output.str().find("OPTIONS"), std::string::npos);
    EXPECT_NE(output.str().find("--provider"), std::string::npos);
    EXPECT_NE(output.str().find("--endpoint"), std::string::npos);
    EXPECT_NE(output.str().find("--model"), std::string::npos);
    EXPECT_NE(output.str().find("--format"), std::string::npos);
    EXPECT_NE(output.str().find("--dimensions"), std::string::npos);
    EXPECT_NE(output.str().find("--no-truncate"), std::string::npos);
    EXPECT_NE(output.str().find("--pretty"), std::string::npos);
}

TEST(CliEmbedCommandTests, PassesDimensionsNoTruncateAndMultipleInputs)
{
    yaaf::cli::Services services;
    services.embed = [](const yaaf::llm::EmbedRequest &request) {
        EXPECT_EQ(request.model, "embed-model");
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
        response.embeddings = {{0.1}, {0.2}};
        response.total_duration = 10;
        response.load_duration = 2;
        response.prompt_eval_count = 4;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"embed", "--model", "embed-model", "--dimensions", "64", "--no-truncate", "one", "two"}, input,
                       output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("model"), "embed-model");
    ASSERT_EQ(payload.at("embeddings").size(), 2U);
    EXPECT_EQ(payload.at("total_duration"), 10);
    EXPECT_EQ(payload.at("load_duration"), 2);
    EXPECT_EQ(payload.at("prompt_eval_count"), 4);
}

TEST(CliEmbedCommandTests, PrettyPrintsJsonPayload)
{
    yaaf::cli::Services services;
    services.embed = [](const yaaf::llm::EmbedRequest &) {
        yaaf::llm::EmbedResponse response;
        response.model = "embed-model";
        response.embeddings = {{0.1}};
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"embed", "--pretty", "hello world"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("\n  \"model\": \"embed-model\""), std::string::npos);
}

TEST(CliEmbedCommandTests, RejectsNonJsonFormat)
{
    yaaf::cli::Services services;
    bool embed_called = false;
    services.embed = [&](const yaaf::llm::EmbedRequest &) {
        embed_called = true;
        return yaaf::llm::EmbedResponse{};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"embed", "--format", "xml", "hello world"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_FALSE(embed_called);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("embed only supports --format json"), std::string::npos);
}



TEST(CliEmbedCommandTests, OpenAiProviderUsesEmbeddingsEndpoint)
{
    std::string captured_body;
    HttpClient::Headers captured_headers;

    yaaf::cli::Services services;
    services.http_post = [&](std::string_view url, std::string_view body, std::string_view content_type,
                             const HttpClient::Headers &headers,
                             const HttpClient::ResponseChunkHandler *on_response_chunk) -> HttpClient::Response {
        EXPECT_EQ(url, "http://openai.test/v1/embeddings");
        EXPECT_EQ(content_type, "application/json");
        EXPECT_EQ(on_response_chunk, nullptr);
        captured_body = std::string(body);
        captured_headers = headers;

        HttpClient::Response response;
        response.status_code = 200;
        response.body = nlohmann::json{{"model", "text-embedding-3-small"},
                                       {"data", {{{"index", 0}, {"embedding", {0.1, 0.2, 0.3}}}}},
                                       {"usage", {{"prompt_tokens", 4}}}}
                            .dump();
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"embed", "--provider", "openai", "--endpoint", "http://openai.test/v1",
                                           "--model", "text-embedding-3-small", "--dimensions", "64",
                                           "hello world"},
                                          input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(captured_headers.size(), 1U);
    if (!captured_headers.empty())
    {
        EXPECT_EQ(captured_headers.front().first, "Accept");
        EXPECT_EQ(captured_headers.front().second, "application/json");
    }

    const auto request_payload = nlohmann::json::parse(captured_body, nullptr, false);
    ASSERT_FALSE(request_payload.is_discarded());
    EXPECT_EQ(request_payload.at("model"), "text-embedding-3-small");
    EXPECT_EQ(request_payload.at("input"), "hello world");
    EXPECT_EQ(request_payload.at("dimensions"), 64);

    const auto response_payload = parse_json_output(output);
    EXPECT_EQ(response_payload.at("model"), "text-embedding-3-small");
    EXPECT_EQ(response_payload.at("prompt_eval_count"), 4);
}

TEST(CliEmbedCommandTests, OpenAiProviderLoadsEmbeddingModelFromDotenv)
{
    const ScopedEnvironmentVariable ollama_endpoint{"YAAF_OLLAMA_ENDPOINT", ""};
    const ScopedEnvironmentVariable openai_endpoint{"YAAF_OPENAI_ENDPOINT", ""};
    const ScopedEnvironmentVariable openai_model{"YAAF_OPENAI_MODEL", ""};
    const ScopedEnvironmentVariable openai_embed_model{"YAAF_OPENAI_EMBED_MODEL", ""};

    const auto test_directory = make_test_directory("cli-openai-embed-dotenv");
    write_file(test_directory / ".env",
               "YAAF_OPENAI_ENDPOINT=http://openai.test/v1\n"
               "YAAF_OPENAI_MODEL=gpt-4o-mini\n"
               "YAAF_OPENAI_EMBED_MODEL=text-embedding-3-small\n");
    const ScopedCurrentPath current_path{test_directory};

    yaaf::cli::Services services;
    services.http_post = [&](std::string_view url, std::string_view body, std::string_view content_type,
                             const HttpClient::Headers &headers,
                             const HttpClient::ResponseChunkHandler *on_response_chunk) -> HttpClient::Response {
        EXPECT_EQ(url, "http://openai.test/v1/embeddings");
        EXPECT_EQ(content_type, "application/json");
        EXPECT_EQ(on_response_chunk, nullptr);
        EXPECT_EQ(headers.size(), 1U);

        const auto request_payload = nlohmann::json::parse(body, nullptr, false);
        EXPECT_FALSE(request_payload.is_discarded());
        if (request_payload.is_discarded())
        {
            return HttpClient::Response{};
        }
        EXPECT_EQ(request_payload.at("model"), "text-embedding-3-small");

        HttpClient::Response response;
        response.status_code = 200;
        response.body = nlohmann::json{{"model", "text-embedding-3-small"},
                                       {"data", {{{"index", 0}, {"embedding", {0.1, 0.2, 0.3}}}}}}
                            .dump();
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"embed", "--provider", "openai", "hello world"}, input, output, error_output,
                       &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto response_payload = parse_json_output(output);
    EXPECT_EQ(response_payload.at("model"), "text-embedding-3-small");
}

