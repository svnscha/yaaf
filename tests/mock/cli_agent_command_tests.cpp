#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/cli/cli.h"

TEST(CliAgentCommandTests, HelpListsAgentOnlyOptions)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"agent", "--help"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("--name"), std::string::npos);
    EXPECT_NE(output.str().find("--endpoint"), std::string::npos);
    EXPECT_NE(output.str().find("--model"), std::string::npos);
    EXPECT_NE(output.str().find("--think"), std::string::npos);
    EXPECT_NE(output.str().find("--max-turns"), std::string::npos);
    EXPECT_NE(output.str().find("--tool"), std::string::npos);
    EXPECT_NE(output.str().find("--mcp"), std::string::npos);
    EXPECT_EQ(output.str().find("--stream"), std::string::npos);
    EXPECT_EQ(output.str().find("--format"), std::string::npos);
}

TEST(CliAgentCommandTests, RunsLuaReactAgent)
{
    yaaf::cli::Services services;
    std::size_t call_count = 0;
    services.chat = [&](const yaaf::llm::ChatRequest &request,
                        const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        ++call_count;
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.model, "ministral-3:14b");
        EXPECT_FALSE(request.messages.empty());

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";
        response.message.content = nlohmann::json{{"type", "final_answer"},
                                                  {"thought", "I can answer directly."},
                                                  {"final_answer", "Rayleigh scattering"}}
                                       .dump();
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"agent", "--name", "react", "Why is the sky blue?"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(call_count, 1U);
    EXPECT_EQ(output.str(), "thought: I can answer directly.\n"
                            "assistant: Rayleigh scattering\n");
}

TEST(CliAgentCommandTests, RejectsNonPositiveMaxTurns)
{
    yaaf::cli::Services services;
    bool chat_called = false;
    services.chat = [&](const yaaf::llm::ChatRequest &, const yaaf::llm::ChatStreamCallback *) {
        chat_called = true;
        return yaaf::llm::ChatResponse{};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"agent", "--name", "react", "--max-turns", "0", "Why is the sky blue?"},
                                          input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_FALSE(chat_called);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("--max-turns must be greater than zero for agent"), std::string::npos);
}

TEST(CliAgentCommandTests, RejectsUnknownAgentName)
{
    yaaf::cli::Services services;
    bool chat_called = false;
    services.chat = [&](const yaaf::llm::ChatRequest &, const yaaf::llm::ChatStreamCallback *) {
        chat_called = true;
        return yaaf::llm::ChatResponse{};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"agent", "--name", "unknown", "Why is the sky blue?"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_FALSE(chat_called);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("unknown agent: unknown"), std::string::npos);
    EXPECT_NE(error_output.str().find("available agents: react"), std::string::npos);
}

TEST(CliAgentCommandTests, WithEchoToolRunsThoughtActionObservationLoop)
{
    yaaf::cli::Services services;
    std::size_t call_count = 0;
    services.chat = [&](const yaaf::llm::ChatRequest &request,
                        const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        ++call_count;
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_FALSE(request.messages.empty());

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";

        if (call_count == 1)
        {
            EXPECT_NE(request.messages.front().content.find("echo"), std::string::npos);
            response.message.content =
                nlohmann::json{
                    {"type", "action"},
                    {"thought", "I need to echo the requested text."},
                    {"action", nlohmann::json{{"name", "echo"}, {"arguments", nlohmann::json{{"text", "hello"}}}}}}
                    .dump();
        }
        else
        {
            EXPECT_EQ(request.messages.back().role, "tool");
            EXPECT_EQ(request.messages.back().content, "hello");
            response.message.content = nlohmann::json{{"type", "final_answer"},
                                                      {"thought", "Now I can answer from the echoed result."},
                                                      {"final_answer", "hello"}}
                                           .dump();
        }

        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"agent", "--name", "react", "--tool", "echo", "Echo hello."}, input, output,
                                          error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(call_count, 2U);
    EXPECT_EQ(output.str(), "thought: I need to echo the requested text.\n"
                            "tool: echo {\"text\":\"hello\"}\n"
                            "observation: hello\n"
                            "thought: Now I can answer from the echoed result.\n"
                            "assistant: hello\n");
}

TEST(CliAgentCommandTests, RejectsUnknownTool)
{
    yaaf::cli::Services services;
    bool chat_called = false;
    services.chat = [&](const yaaf::llm::ChatRequest &, const yaaf::llm::ChatStreamCallback *) {
        chat_called = true;
        return yaaf::llm::ChatResponse{};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"agent", "--name", "react", "--tool", "unknown", "Echo hello."}, input,
                                          output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_FALSE(chat_called);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("unknown tool: unknown"), std::string::npos);
    EXPECT_NE(error_output.str().find("available tools: echo"), std::string::npos);
}

