#include "../support/mcp_test_support.h"

#include "../../libyaaf/cli/cli.h"

using namespace yaaf::tests::mcp;

TEST(McpCommandMockTests, AskCommandUsesRealStdioMcpToolFromWorkspaceConfig)
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

TEST(McpCommandMockTests, ChatCommandUsesRealStdioMcpToolFromWorkspaceConfig)
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

TEST(McpCommandMockTests, AgentCommandUsesRealStdioMcpToolFromWorkspaceConfig)
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

