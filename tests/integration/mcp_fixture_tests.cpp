#include "../support/mcp_test_support.h"

#include "../../libyaaf/cli/cli.h"

using namespace yaaf::tests::mcp;

TEST(McpFixtureIntegrationTests, NativeClientListsAndCallsRealUvStdioServer)
{
    if (!executable_on_path("uv"))
    {
        GTEST_SKIP() << "uv is required for real MCP fixture server tests";
    }

    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_real_stdio_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", uv_stdio_server_config(root, "hello_stdio.py")}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

TEST(McpFixtureIntegrationTests, AskCommandUsesRealStdioMcpToolFromWorkspaceConfig)
{
    if (!executable_on_path("uv"))
    {
        GTEST_SKIP() << "uv is required for real MCP fixture server tests";
    }

    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_ask_stdio_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", uv_stdio_server_config(root, "hello_stdio.py")}}}});
    const CurrentPathGuard current_path{root};

    yaaf::cli::Services services;
    std::size_t chat_call_count = 0;
    services.chat = [&](const yaaf::llm::ChatRequest &request,
                        const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        ++chat_call_count;
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.model, "lua-model");
        EXPECT_FALSE(request.stream);
        EXPECT_EQ(request.tools.size(), 1U);
        if (!request.tools.empty())
        {
            EXPECT_EQ(request.tools.front().function.name, "hello.hello");
        }

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        if (chat_call_count == 1)
        {
            yaaf::llm::Tool tool_call;
            tool_call.function.name = "hello.hello";
            tool_call.function.arguments = nlohmann::json{{"name", "MCP"}};
            response.message.tool_calls.push_back(std::move(tool_call));
        }
        else
        {
            EXPECT_FALSE(request.messages.empty());
            if (!request.messages.empty())
            {
                EXPECT_EQ(request.messages.back().role, "tool");
                EXPECT_EQ(request.messages.back().content, "Hello, MCP!");
            }
            response.message.content = "The MCP server said hello.";
        }
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--model", "lua-model", "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool",
                        "hello.hello", "Say", "hello", "through", "MCP"},
                       input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(chat_call_count, 2U);
    EXPECT_EQ(output.str(), "tool: hello.hello {\"name\":\"MCP\"}\n"
                            "observation: Hello, MCP!\n"
                            "assistant: The MCP server said hello.\n");
}

TEST(McpFixtureIntegrationTests, ChatCommandUsesRealStdioMcpToolFromWorkspaceConfig)
{
    if (!executable_on_path("uv"))
    {
        GTEST_SKIP() << "uv is required for real MCP fixture server tests";
    }

    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_chat_stdio_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", uv_stdio_server_config(root, "hello_stdio.py")}}}});
    const CurrentPathGuard current_path{root};

    yaaf::cli::Services services;
    std::size_t chat_call_count = 0;
    services.chat = [&](const yaaf::llm::ChatRequest &request,
                        const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        ++chat_call_count;
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_FALSE(request.stream);
        EXPECT_EQ(request.tools.size(), 1U);
        if (!request.tools.empty())
        {
            EXPECT_EQ(request.tools.front().function.name, "hello.repeat");
        }

        yaaf::llm::ChatResponse response;
        response.message.role = "assistant";
        if (chat_call_count == 1)
        {
            yaaf::llm::Tool tool_call;
            tool_call.function.name = "hello.repeat";
            tool_call.function.arguments = nlohmann::json{{"text", "hi"}, {"count", 3}};
            response.message.tool_calls.push_back(std::move(tool_call));
        }
        else
        {
            EXPECT_FALSE(request.messages.empty());
            if (!request.messages.empty())
            {
                EXPECT_EQ(request.messages.back().role, "tool");
                EXPECT_EQ(request.messages.back().content, "hi hi hi");
            }
            response.message.content = "Repeated through MCP.";
        }
        return response;
    };

    std::istringstream input("repeat hi\n");
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"chat", "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool", "hello.repeat"},
                       input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(chat_call_count, 2U);
    EXPECT_EQ(output.str(), "user: tool: hello.repeat {\"count\":3,\"text\":\"hi\"}\n"
                            "observation: hi hi hi\n"
                            "assistant: Repeated through MCP.\n"
                            "user: ");
}

TEST(McpFixtureIntegrationTests, AgentCommandUsesRealStdioMcpToolFromWorkspaceConfig)
{
    if (!executable_on_path("uv"))
    {
        GTEST_SKIP() << "uv is required for real MCP fixture server tests";
    }

    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_agent_stdio_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", uv_stdio_server_config(root, "hello_stdio.py")}}}});
    const CurrentPathGuard current_path{root};

    yaaf::cli::Services services;
    std::size_t chat_call_count = 0;
    services.chat = [&](const yaaf::llm::ChatRequest &request,
                        const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        ++chat_call_count;
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_FALSE(request.messages.empty());

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        if (chat_call_count == 1)
        {
            if (!request.messages.empty())
            {
                EXPECT_NE(request.messages.front().content.find("hello.repeat"), std::string::npos);
            }
            response.message.content =
                nlohmann::json{{"type", "action"},
                               {"thought", "I should ask the MCP server to repeat the text."},
                               {"action", nlohmann::json{{"name", "hello.repeat"},
                                                         {"arguments", nlohmann::json{{"text", "hi"}, {"count", 3}}}}}}
                    .dump();
        }
        else
        {
            EXPECT_EQ(request.messages.back().role, "tool");
            EXPECT_EQ(request.messages.back().content, "hi hi hi");
            response.message.content = nlohmann::json{{"type", "final_answer"},
                                                      {"thought", "The MCP tool returned the repeated text."},
                                                      {"final_answer", "hi hi hi"}}
                                           .dump();
        }
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"agent", "--name", "react", "--mcp", (workspace_mcp_config_path(workspace)).string(), "--tool",
                        "hello.repeat", "Repeat hi through MCP"},
                       input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(chat_call_count, 2U);
    EXPECT_EQ(output.str(), "thought: I should ask the MCP server to repeat the text.\n"
                            "tool: hello.repeat {\"count\":3,\"text\":\"hi\"}\n"
                            "observation: hi hi hi\n"
                            "thought: The MCP tool returned the repeated text.\n"
                            "assistant: hi hi hi\n");
}

TEST(McpFixtureIntegrationTests, LuaScriptUsesExplicitMcpConfigPath)
{
    if (!executable_on_path("uv"))
    {
        GTEST_SKIP() << "uv is required for real MCP fixture server tests";
    }

    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_lua_script_config_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", uv_stdio_server_config(root, "hello_stdio.py")}}}});
    const auto script_path = write_lua_script(workspace, R"lua(
local tool = require("tool")
local result = tool.execute({ "hello.hello" }, "hello.hello", { name = "Lua" })
print(result.content)
)lua");
    const CurrentPathGuard current_path{root};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"run", "--mcp", (workspace_mcp_config_path(workspace)).string(), script_path.string()}, input,
                       output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "Hello, Lua!\n");
}

TEST(McpFixtureIntegrationTests, NativeClientListsAndCallsPrestartedHttpServer)
{
    const auto workspace = make_workspace("assistant_mcp_real_http_test");
    write_runtime_dotenv(workspace);
    const auto dotenv = runtime_dotenv(workspace);
    const auto mcp_url = configured_mcp_url(dotenv, "YAAF_MCP_HELLO_HTTP_URL", "http://127.0.0.1:39231/mcp");
    if (!http_fixture_available(mcp_url))
    {
        GTEST_SKIP() << "start the local test stack with docker compose -f docker-compose.test-stack.yml up";
    }

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", mcp_url}}}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http = yaaf::tests::runtime_http_options_for_url(mcp_url);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

TEST(McpFixtureIntegrationTests, NativeClientListsAndCallsPrestartedSseServer)
{
    const auto workspace = make_workspace("assistant_mcp_real_sse_test");
    write_runtime_dotenv(workspace);
    const auto dotenv = runtime_dotenv(workspace);
    const auto mcp_url = configured_mcp_url(dotenv, "YAAF_MCP_HELLO_SSE_URL", "http://127.0.0.1:39232/mcp");
    if (!http_fixture_available(mcp_url))
    {
        GTEST_SKIP() << "start the local test stack with docker compose -f docker-compose.test-stack.yml up";
    }

    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", {{"type", "sse"}, {"url", mcp_url}}}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http = yaaf::tests::runtime_http_options_for_url(mcp_url);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}
