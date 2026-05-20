#include "../../support/mcp_test_support.h"

#include "../../../libyaaf/cli/cli.h"

using namespace yaaf::tests::mcp;

namespace
{
constexpr std::string_view kDefaultOllamaEndpoint = "http://localhost:11434";
constexpr std::string_view kDefaultOllamaModel = "qwen3:0.6b";

[[nodiscard]] std::string runtime_ollama_endpoint()
{
    const auto dotenv = yaaf::tests::load_runtime_dotenv();
    return yaaf::tests::runtime_env_value(dotenv, "OLLAMA_ENDPOINT").value_or(std::string(kDefaultOllamaEndpoint));
}

[[nodiscard]] std::string runtime_ollama_model()
{
    const auto dotenv = yaaf::tests::load_runtime_dotenv();
    return yaaf::tests::runtime_env_value(dotenv, "OLLAMA_MODEL").value_or(std::string(kDefaultOllamaModel));
}

[[nodiscard]] std::string proxied_http_mcp_url(const std::filesystem::path &workspace)
{
    write_runtime_dotenv(workspace);
    const auto dotenv = runtime_dotenv(workspace);
    return configured_mcp_url(dotenv, "YAAF_MCP_HELLO_HTTP_PROXIED_URL", "http://host.docker.internal:39231/mcp");
}

void require_configured_proxy(const HttpClient::Options &http_options)
{
    if (!http_options.proxy.has_value() || http_options.proxy->empty())
    {
        GTEST_SKIP() << "YAAF_PROXY is required for proxied real-model MCP command integration tests";
    }
}

void require_http_mcp_fixture(std::string_view mcp_url)
{
    if (!http_fixture_available(mcp_url))
    {
        GTEST_SKIP() << "start mitmproxy and the hello HTTP MCP fixture with docker compose -f "
                        "docker-compose.mitmproxy.yml up";
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
    options.config_path = workspace / ".vscode" / "mcp.json";
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

    auto http_options = yaaf::tests::runtime_http_options();
    if (!http_options.proxy.has_value() || http_options.proxy->empty())
    {
        GTEST_SKIP() << "YAAF_PROXY is required for the proxied MCP client integration test";
    }
    if (!http_fixture_available(mcp_url))
    {
        GTEST_SKIP() << "start mitmproxy and the hello HTTP MCP fixture with docker compose -f "
                        "docker-compose.mitmproxy.yml up";
    }

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", mcp_url}}}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace / ".vscode" / "mcp.json";
    options.http = std::move(http_options);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

TEST(McpClientDockerIntegrationTests, AskCommandSendsMcpToolsToRealModelThroughConfiguredProxy)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_real_ask_model_test");
    const auto mcp_url = proxied_http_mcp_url(workspace);

    auto http_options = yaaf::tests::runtime_http_options();
    require_configured_proxy(http_options);
    require_http_mcp_fixture(mcp_url);

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", mcp_url}}}}}});
    const CurrentPathGuard current_path{root};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--endpoint", runtime_ollama_endpoint(), "--model", runtime_ollama_model(), "--mcp",
                        (workspace / ".vscode" / "mcp.json").string(), "--tool", "hello.hello",
                        "You must call the hello.hello tool with name MCP, then answer with the greeting."},
                       input, output, error_output, nullptr);

    EXPECT_EQ(exit_code, EXIT_SUCCESS) << error_output.str();
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("yaaf:"), std::string::npos);
}

TEST(McpClientDockerIntegrationTests, ChatCommandSendsMcpToolsToRealModelThroughConfiguredProxy)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_real_chat_model_test");
    const auto mcp_url = proxied_http_mcp_url(workspace);

    auto http_options = yaaf::tests::runtime_http_options();
    require_configured_proxy(http_options);
    require_http_mcp_fixture(mcp_url);

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", mcp_url}}}}}});
    const CurrentPathGuard current_path{root};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"chat", "--endpoint", runtime_ollama_endpoint(), "--model", runtime_ollama_model(), "--mcp",
         (workspace / ".vscode" / "mcp.json").string(), "--tool", "hello.repeat",
         "You must call the hello.repeat tool with text hi and count 3, then answer with the repeated text."},
        input, output, error_output, nullptr);

    EXPECT_EQ(exit_code, EXIT_SUCCESS) << error_output.str();
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("yaaf:"), std::string::npos);
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
    options.config_path = workspace / ".vscode" / "mcp.json";
    options.http = yaaf::tests::runtime_http_options_for_url(mcp_url);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}
