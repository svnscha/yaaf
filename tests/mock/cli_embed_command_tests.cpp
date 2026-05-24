#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/cli/cli.h"

namespace
{
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

