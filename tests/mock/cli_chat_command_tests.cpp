#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/cli/cli.h"

TEST(CliChatCommandTests, HelpOmitsFormatOption)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"chat", "--help"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("--endpoint"), std::string::npos);
    EXPECT_NE(output.str().find("--model"), std::string::npos);
    EXPECT_NE(output.str().find("--stream"), std::string::npos);
    EXPECT_NE(output.str().find("--think"), std::string::npos);
    EXPECT_NE(output.str().find("--tool"), std::string::npos);
    EXPECT_NE(output.str().find("--mcp"), std::string::npos);
    EXPECT_EQ(output.str().find("--format"), std::string::npos);
}

TEST(CliChatCommandTests, InteractiveUsesInjectedStreams)
{
    yaaf::cli::Services services;
    services.chat = [](const yaaf::llm::ChatRequest &request,
                       const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        EXPECT_EQ(request.messages.size(), 1U);

        if (!request.messages.empty())
        {
            EXPECT_EQ(request.messages.front().role, "user");
            EXPECT_EQ(request.messages.front().content, "hello");
        }

        if (on_stream_event != nullptr)
        {
            yaaf::llm::ChatStreamEvent thinking_event;
            thinking_event.message.thinking = "reasoning";
            (*on_stream_event)(thinking_event);

            yaaf::llm::ChatStreamEvent content_event;
            content_event.message.content = "hi";
            (*on_stream_event)(content_event);
        }

        yaaf::llm::ChatResponse response;
        response.message.role = "assistant";
        response.message.content = "hi";
        response.message.thinking = "reasoning";
        return response;
    };

    std::istringstream input("hello\n");
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"chat", "--stream"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("user: yaaf thinking: reasoning"), std::string::npos);
    EXPECT_NE(output.str().find("assistant: hi"), std::string::npos);
}

TEST(CliChatCommandTests, WithoutStreamPrintsThinkingAndAssistantResponse)
{
    yaaf::cli::Services services;
    services.chat = [](const yaaf::llm::ChatRequest &request,
                       const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        EXPECT_EQ(request.stream, false);
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.messages.size(), 1U);
        EXPECT_EQ(request.messages.front().content, "hello");

        yaaf::llm::ChatResponse response;
        response.message.role = "assistant";
        response.message.content = "hi";
        response.message.thinking = "reasoning";
        return response;
    };

    std::istringstream input("hello\n");
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"chat"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "user: yaaf thinking: reasoning\nassistant: hi\nuser: ");
}

TEST(CliChatCommandTests, WithToolRunsToolLoop)
{
    yaaf::cli::Services services;
    std::size_t call_count = 0;
    services.chat = [&](const yaaf::llm::ChatRequest &request,
                        const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        ++call_count;
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_FALSE(request.stream);
        EXPECT_EQ(request.tools.size(), 1U);
        if (!request.tools.empty())
        {
            EXPECT_EQ(request.tools.front().function.name, "echo");
        }

        yaaf::llm::ChatResponse response;
        response.message.role = "assistant";

        if (call_count == 1)
        {
            EXPECT_EQ(request.messages.size(), 1U);
            if (!request.messages.empty())
            {
                EXPECT_EQ(request.messages.front().role, "user");
                EXPECT_EQ(request.messages.front().content, "echo hello");
            }

            yaaf::llm::Tool tool_call;
            tool_call.function.name = "echo";
            tool_call.function.arguments = nlohmann::json{{"text", "hello"}};
            response.message.tool_calls.push_back(std::move(tool_call));
        }
        else
        {
            EXPECT_GE(request.messages.size(), 3U);
            if (!request.messages.empty())
            {
                EXPECT_EQ(request.messages.back().role, "tool");
                EXPECT_EQ(request.messages.back().content, "hello");
            }
            response.message.content = "hello";
        }

        return response;
    };

    std::istringstream input("echo hello\n");
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"chat", "--tool", "echo"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(call_count, 2U);
    EXPECT_EQ(output.str(), "user: tool: echo {\"text\":\"hello\"}\n"
                            "observation: hello\n"
                            "assistant: hello\n"
                            "user: ");
}

