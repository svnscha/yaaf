#include "../../support/mcp_test_support.h"

#include "../../../libyaaf/cli/cli.h"

using namespace yaaf::tests::mcp;

namespace
{
constexpr std::string_view kDefaultOllamaEndpoint = "http://localhost:11434";
constexpr std::string_view kDefaultOllamaModel = "qwen3:0.6b";

[[nodiscard]] std::string runtime_ollama_endpoint(const yaaf::dotenv::EnvironmentFile &dotenv)
{
    return yaaf::tests::runtime_env_value(dotenv, "YAAF_OLLAMA_ENDPOINT").value_or(std::string(kDefaultOllamaEndpoint));
}

[[nodiscard]] std::string runtime_ollama_model(const yaaf::dotenv::EnvironmentFile &dotenv)
{
    return yaaf::tests::runtime_env_value(dotenv, "YAAF_OLLAMA_MODEL").value_or(std::string(kDefaultOllamaModel));
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

[[nodiscard]] std::string proxied_http_mcp_url(const std::filesystem::path &workspace)
{
    write_runtime_dotenv(workspace);
    const auto dotenv = runtime_dotenv(workspace);
    return configured_mcp_url(dotenv, "YAAF_MCP_HELLO_HTTP_PROXIED_URL", "http://host.docker.internal:39231/mcp");
}

[[nodiscard]] bool has_configured_proxy(const HttpClient::Options &http_options)
{
    return http_options.proxy.has_value() && !http_options.proxy->empty();
}

[[nodiscard]] bool has_http_mcp_fixture(std::string_view mcp_url)
{
    return http_fixture_available(mcp_url);
}

[[nodiscard]] HttpClient::Options workspace_http_options(const std::filesystem::path &workspace)
{
    const auto dotenv = runtime_dotenv(workspace);
    HttpClient::Options options;
    options.proxy = yaaf::tests::runtime_env_value(dotenv, "YAAF_PROXY");
    options.allow_invalid_proxy_certificates = options.proxy.has_value() && !options.proxy->empty();
    return options;
}

[[nodiscard]] bool proxied_http_fixture_available(std::string_view mcp_url, const HttpClient::Options &http_options)
{
    try
    {
        return HttpClient{http_options}.get(health_url_for_mcp_url(std::string(mcp_url))).status_code == 200;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

[[nodiscard]] bool ollama_available(std::string_view endpoint, const HttpClient::Options &http_options)
{
    try
    {
        const auto response = HttpClient{http_options}.get(ollama_tags_url(std::string(endpoint)));
        return response.status_code >= 200 && response.status_code < 300;
    }
    catch (const std::exception &)
    {
        return false;
    }
}
} // namespace

TEST(McpClientDockerIntegrationTests, NativeClientListsAndCallsPrestartedHttpServer)
{
    const auto workspace = make_workspace("assistant_mcp_real_http_test");
    write_runtime_dotenv(workspace);
    const auto dotenv = runtime_dotenv(workspace);
    const auto mcp_url = configured_mcp_url(dotenv, "YAAF_MCP_HELLO_HTTP_URL", "http://127.0.0.1:39231/mcp");
    if (!http_fixture_available(mcp_url))
    {
        GTEST_SKIP() << "start the hello HTTP MCP fixture with docker compose -f docker-compose.mitmproxy.yml up";
    }

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", mcp_url}}}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http = yaaf::tests::runtime_http_options_for_url(mcp_url);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

TEST(McpClientDockerIntegrationTests, NativeClientCallsPrestartedHttpServerThroughConfiguredProxy)
{
    const auto workspace = make_workspace("assistant_mcp_proxied_http_client_test");
    write_runtime_dotenv(workspace);
    const auto dotenv = runtime_dotenv(workspace);
    const auto mcp_url =
        configured_mcp_url(dotenv, "YAAF_MCP_HELLO_HTTP_PROXIED_URL", "http://host.docker.internal:39231/mcp");

    auto http_options = workspace_http_options(workspace);
    if (!http_options.proxy.has_value() || http_options.proxy->empty())
    {
        GTEST_SKIP() << "YAAF_PROXY is required for the proxied MCP client integration test";
    }
    if (!proxied_http_fixture_available(mcp_url, http_options))
    {
        GTEST_SKIP() << "start mitmproxy and the hello HTTP MCP fixture with docker compose -f "
                        "docker-compose.mitmproxy.yml up";
    }
    const auto ollama_endpoint = runtime_ollama_endpoint(dotenv);
    if (!ollama_available(ollama_endpoint, http_options))
    {
        GTEST_SKIP() << "start Ollama at " << ollama_endpoint;
    }

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", mcp_url}}}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http = std::move(http_options);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

TEST(McpClientDockerIntegrationTests, AskCommandSendsMcpToolsToRealModelThroughConfiguredProxy)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_real_ask_model_test");
    const auto mcp_url = proxied_http_mcp_url(workspace);
    const auto dotenv = runtime_dotenv(workspace);

    auto http_options = workspace_http_options(workspace);
    if (!has_configured_proxy(http_options))
    {
        GTEST_SKIP() << "YAAF_PROXY is required for proxied real-model MCP command integration tests";
    }
    if (!proxied_http_fixture_available(mcp_url, http_options))
    {
        GTEST_SKIP() << "start mitmproxy and the hello HTTP MCP fixture with docker compose -f "
                        "docker-compose.mitmproxy.yml up";
    }
    const auto ollama_endpoint = runtime_ollama_endpoint(dotenv);
    if (!ollama_available(ollama_endpoint, http_options))
    {
        GTEST_SKIP() << "start Ollama at " << ollama_endpoint;
    }

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", mcp_url}}}}}});
    const CurrentPathGuard current_path{root};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"--proxy", *http_options.proxy, "ask", "--endpoint", ollama_endpoint, "--model",
         runtime_ollama_model(dotenv), "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool", "hello.hello",
         "You must call the hello.hello tool with name MCP, then answer with the greeting."},
        input, output, error_output, nullptr);

    EXPECT_EQ(exit_code, EXIT_SUCCESS) << error_output.str();
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("assistant:"), std::string::npos);
}

TEST(McpClientDockerIntegrationTests, ChatCommandSendsMcpToolsToRealModelThroughConfiguredProxy)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_real_chat_model_test");
    const auto mcp_url = proxied_http_mcp_url(workspace);
    const auto dotenv = runtime_dotenv(workspace);

    auto http_options = workspace_http_options(workspace);
    if (!has_configured_proxy(http_options))
    {
        GTEST_SKIP() << "YAAF_PROXY is required for proxied real-model MCP command integration tests";
    }
    if (!proxied_http_fixture_available(mcp_url, http_options))
    {
        GTEST_SKIP() << "start mitmproxy and the hello HTTP MCP fixture with docker compose -f "
                        "docker-compose.mitmproxy.yml up";
    }
    const auto ollama_endpoint = runtime_ollama_endpoint(dotenv);
    if (!ollama_available(ollama_endpoint, http_options))
    {
        GTEST_SKIP() << "start Ollama at " << ollama_endpoint;
    }

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", mcp_url}}}}}});
    const CurrentPathGuard current_path{root};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"--proxy", *http_options.proxy, "chat", "--endpoint", ollama_endpoint, "--model",
         runtime_ollama_model(dotenv), "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool", "hello.repeat",
         "You must call the hello.repeat tool with text hi and count 3, then answer with the repeated text."},
        input, output, error_output, nullptr);

    EXPECT_EQ(exit_code, EXIT_SUCCESS) << error_output.str();
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("assistant:"), std::string::npos);
}

TEST(McpClientDockerIntegrationTests, NativeClientListsAndCallsPrestartedSseServer)
{
    const auto workspace = make_workspace("assistant_mcp_real_sse_test");
    write_runtime_dotenv(workspace);
    const auto dotenv = runtime_dotenv(workspace);
    const auto mcp_url = configured_mcp_url(dotenv, "YAAF_MCP_HELLO_SSE_URL", "http://127.0.0.1:39232/mcp");
    if (!http_fixture_available(mcp_url))
    {
        GTEST_SKIP() << "start the hello SSE MCP fixture with docker compose -f docker-compose.mitmproxy.yml up";
    }

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "sse"}, {"url", mcp_url}}}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http = yaaf::tests::runtime_http_options_for_url(mcp_url);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

