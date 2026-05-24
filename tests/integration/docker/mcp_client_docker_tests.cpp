#include "../../support/llm_provider_test_support.h"
#include "../../support/mcp_test_support.h"

#include "../../../libyaaf/cli/cli.h"

using namespace yaaf::tests::mcp;
using namespace yaaf::tests::llm;

namespace
{
constexpr std::string_view kOllamaEndpoint = "http://ollama.test";
constexpr std::string_view kOpenAiEndpoint = "http://openai.test/v1";
constexpr std::string_view kProxy = "http://proxy.test:8080";
constexpr std::string_view kModel = "qwen3:0.6b";
constexpr std::string_view kMcpUrl = "https://hello.test/mcp";

struct RecordedMcpPost
{
    std::string url;
    nlohmann::json json_body = nlohmann::json::object();
    yaaf::mcp::Headers headers;
};

[[nodiscard]] yaaf::mcp::HttpPost recording_mcp_post(std::vector<RecordedMcpPost> &requests,
                                                     ScriptedHttpTransport transport)
{
    const auto scripted = hello_http_post(transport);
    return [&requests, scripted](std::string_view url, std::string_view body, std::string_view content_type,
               const yaaf::mcp::Headers &headers) {
        requests.push_back(RecordedMcpPost{std::string(url), nlohmann::json::parse(body), headers});
        return scripted(url, body, content_type, headers);
    };
}

[[nodiscard]] yaaf::cli::Services provider_services(ScriptedProviderHttpFixture &provider_fixture,
                                                    std::vector<RecordedMcpPost> &mcp_requests,
                                                    ScriptedHttpTransport transport = ScriptedHttpTransport::Json)
{
    yaaf::cli::Services services;
    services.http_post = [&](std::string_view url, std::string_view body, std::string_view content_type,
                             const HttpClient::Headers &headers,
                             const HttpClient::ResponseChunkHandler *on_response_chunk) {
        return provider_fixture.post(url, body, content_type, headers, on_response_chunk);
    };
    services.mcp_http_post = recording_mcp_post(mcp_requests, transport);
    return services;
}
} // namespace

TEST(McpClientProviderIntegrationTests, NativeClientListsAndCallsScriptedHttpServer)
{
    const auto workspace = make_workspace("assistant_mcp_provider_http_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", kMcpUrl}}}}}});

    std::vector<RecordedMcpPost> requests;
    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http_post = recording_mcp_post(requests, ScriptedHttpTransport::Json);
    yaaf::mcp::Client client{options};

    expect_hello_tools(client, "hello");
    ASSERT_GE(requests.size(), 4U);
    EXPECT_EQ(requests.front().json_body.at("method"), "initialize");
}

TEST(McpClientProviderIntegrationTests, NativeClientListsAndCallsScriptedHttpServerWithConfiguredProxy)
{
    const auto workspace = make_workspace("assistant_mcp_provider_http_proxy_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", kMcpUrl}}}}}});

    std::vector<RecordedMcpPost> requests;
    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http.proxy = std::string(kProxy);
    options.http.allow_invalid_proxy_certificates = true;
    options.http_post = recording_mcp_post(requests, ScriptedHttpTransport::Json);
    yaaf::mcp::Client client{options};

    expect_hello_tools(client, "hello");
    ASSERT_GE(requests.size(), 4U);
    EXPECT_EQ(requests.back().json_body.at("method"), "tools/call");
}

TEST(McpClientProviderIntegrationTests, AskCommandUsesScriptedOllamaProviderWithHttpMcpTools)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_provider_ollama_ask_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", kMcpUrl}}}}}});
    const CurrentPathGuard current_path{root};

    ScriptedProviderHttpFixture provider_fixture;
    std::vector<RecordedMcpPost> mcp_requests;
    auto services = provider_services(provider_fixture, mcp_requests);

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"--proxy", std::string(kProxy), "ask", "--endpoint", std::string(kOllamaEndpoint), "--model", std::string(kModel),
         "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool", "hello.hello",
         "You must call the hello.hello tool with name MCP, then answer with the greeting."},
        input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS) << error_output.str();
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("assistant:"), std::string::npos);
    ASSERT_EQ(provider_fixture.requests().size(), 2U);
    EXPECT_EQ(provider_fixture.requests().front().url, std::string(kOllamaEndpoint) + "/api/chat");
    EXPECT_EQ(provider_fixture.requests().front().json_body.at("tools").at(0).at("function").at("name"), "hello.hello");
    EXPECT_EQ(provider_fixture.requests().back().json_body.at("messages").back().at("role"), "tool");
    EXPECT_EQ(provider_fixture.requests().back().json_body.at("messages").back().at("content"), "Hello, MCP!");
    ASSERT_FALSE(mcp_requests.empty());
    EXPECT_EQ(mcp_requests.back().json_body.at("method"), "tools/call");
}

TEST(McpClientProviderIntegrationTests, ChatCommandUsesScriptedOllamaProviderWithHttpMcpTools)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_provider_ollama_chat_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", kMcpUrl}}}}}});
    const CurrentPathGuard current_path{root};

    ScriptedProviderHttpFixture provider_fixture;
    std::vector<RecordedMcpPost> mcp_requests;
    auto services = provider_services(provider_fixture, mcp_requests);

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"--proxy", std::string(kProxy), "chat", "--endpoint", std::string(kOllamaEndpoint), "--model", std::string(kModel),
         "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool", "hello.repeat",
         "You must call the hello.repeat tool with text hi and count 3, then answer with the repeated text."},
        input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS) << error_output.str();
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("assistant:"), std::string::npos);
    ASSERT_EQ(provider_fixture.requests().size(), 2U);
    EXPECT_EQ(provider_fixture.requests().front().url, std::string(kOllamaEndpoint) + "/api/chat");
    EXPECT_EQ(provider_fixture.requests().front().json_body.at("tools").at(0).at("function").at("name"), "hello.repeat");
    EXPECT_EQ(provider_fixture.requests().back().json_body.at("messages").back().at("content"), "hi hi hi");
    ASSERT_FALSE(mcp_requests.empty());
    EXPECT_EQ(mcp_requests.back().json_body.at("method"), "tools/call");
}

TEST(McpClientProviderIntegrationTests, NativeClientListsAndCallsScriptedSseServer)
{
    const auto workspace = make_workspace("assistant_mcp_provider_sse_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", {{"type", "sse"}, {"url", kMcpUrl}}}}}});

    std::vector<RecordedMcpPost> requests;
    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http_post = recording_mcp_post(requests, ScriptedHttpTransport::Sse);
    yaaf::mcp::Client client{options};

    expect_hello_tools(client, "hello");
    ASSERT_GE(requests.size(), 4U);
    EXPECT_EQ(requests.front().json_body.at("method"), "initialize");
}

TEST(McpClientProviderIntegrationTests, AskCommandUsesScriptedOpenAiCompatibleProviderWithHttpMcpTools)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_provider_openai_ask_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", kMcpUrl}}}}}});
    const CurrentPathGuard current_path{root};

    ScriptedProviderHttpFixture provider_fixture;
    std::vector<RecordedMcpPost> mcp_requests;
    auto services = provider_services(provider_fixture, mcp_requests);

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"--proxy", std::string(kProxy), "ask", "--provider", "openai", "--endpoint", std::string(kOpenAiEndpoint),
         "--model", std::string(kModel), "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool",
         "hello.hello", "You must call the hello.hello tool with name MCP, then answer with the greeting."},
        input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS) << error_output.str();
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("assistant:"), std::string::npos);
    ASSERT_EQ(provider_fixture.requests().size(), 2U);
    EXPECT_EQ(provider_fixture.requests().front().url, std::string(kOpenAiEndpoint) + "/chat/completions");
    EXPECT_EQ(provider_fixture.requests().front().json_body.at("tools").at(0).at("function").at("name"), "hello.hello");
    EXPECT_EQ(provider_fixture.requests().back().json_body.at("messages").back().at("role"), "tool");
    EXPECT_EQ(provider_fixture.requests().back().json_body.at("messages").back().at("content"), "Hello, MCP!");
    ASSERT_FALSE(mcp_requests.empty());
    EXPECT_EQ(mcp_requests.back().json_body.at("method"), "tools/call");
}

TEST(McpClientProviderIntegrationTests, ChatCommandUsesScriptedOpenAiCompatibleProviderWithHttpMcpTools)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_provider_openai_chat_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", kMcpUrl}}}}}});
    const CurrentPathGuard current_path{root};

    ScriptedProviderHttpFixture provider_fixture;
    std::vector<RecordedMcpPost> mcp_requests;
    auto services = provider_services(provider_fixture, mcp_requests);

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"--proxy", std::string(kProxy), "chat", "--provider", "openai", "--endpoint", std::string(kOpenAiEndpoint),
         "--model", std::string(kModel), "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool",
         "hello.repeat", "You must call the hello.repeat tool with text hi and count 3, then answer with the repeated text."},
        input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS) << error_output.str();
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("assistant:"), std::string::npos);
    ASSERT_EQ(provider_fixture.requests().size(), 2U);
    EXPECT_EQ(provider_fixture.requests().front().url, std::string(kOpenAiEndpoint) + "/chat/completions");
    EXPECT_EQ(provider_fixture.requests().front().json_body.at("tools").at(0).at("function").at("name"), "hello.repeat");
    EXPECT_EQ(provider_fixture.requests().back().json_body.at("messages").back().at("content"), "hi hi hi");
    ASSERT_FALSE(mcp_requests.empty());
    EXPECT_EQ(mcp_requests.back().json_body.at("method"), "tools/call");
}
