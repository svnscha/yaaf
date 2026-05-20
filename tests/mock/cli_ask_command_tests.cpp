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

TEST(CliAskCommandTests, RejectsJsonFormatWithStream)
{
    yaaf::cli::Services services;
    bool generate_called = false;
    services.generate = [&](const yaaf::llm::GenerateRequest &, const yaaf::llm::StreamCallback *) {
        generate_called = true;
        return yaaf::llm::GenerateResponse{};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--format", "json", "--stream", "Why is the sky blue?"}, input,
                                          output, error_output, &services);

    EXPECT_NE(exit_code, EXIT_SUCCESS);
    EXPECT_FALSE(generate_called);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("--format json is not supported with --stream for ask"), std::string::npos);
}

TEST(CliAskCommandTests, HelpListsAskOnlyOptions)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--help"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("Ask the configured model a question"), std::string::npos);
    EXPECT_NE(output.str().find("POSITIONALS"), std::string::npos);
    EXPECT_NE(output.str().find("OPTIONS"), std::string::npos);
    EXPECT_NE(output.str().find("prompt"), std::string::npos);
    EXPECT_NE(output.str().find("--endpoint"), std::string::npos);
    EXPECT_NE(output.str().find("--model"), std::string::npos);
    EXPECT_NE(output.str().find("--stream"), std::string::npos);
    EXPECT_NE(output.str().find("--think"), std::string::npos);
    EXPECT_NE(output.str().find("--format"), std::string::npos);
    EXPECT_NE(output.str().find("--tool"), std::string::npos);
    EXPECT_NE(output.str().find("--mcp"), std::string::npos);
    EXPECT_NE(output.str().find("Endpoint used by this command"), std::string::npos);
    EXPECT_NE(output.str().find("qwen3:0.6b"), std::string::npos);
    EXPECT_EQ(output.str().find("--file"), std::string::npos);
}

TEST(CliAskCommandTests, HelpIsServedByNativeCliMetadata)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--help"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("ask [OPTIONS] prompt"), std::string::npos);
    EXPECT_NE(output.str().find("POSITIONALS"), std::string::npos);
    EXPECT_NE(output.str().find("OPTIONS"), std::string::npos);
    EXPECT_NE(output.str().find("Prompt to send to the selected model"), std::string::npos);
    EXPECT_NE(output.str().find("--endpoint"), std::string::npos);
    EXPECT_NE(output.str().find("--model"), std::string::npos);
    EXPECT_NE(output.str().find("--stream"), std::string::npos);
    EXPECT_NE(output.str().find("--think"), std::string::npos);
    EXPECT_NE(output.str().find("--format"), std::string::npos);
    EXPECT_NE(output.str().find("--tool"), std::string::npos);
    EXPECT_NE(output.str().find("--mcp"), std::string::npos);
    EXPECT_EQ(output.str().find("--file"), std::string::npos);
}

TEST(CliAskCommandTests, DiscoveredLuaCommandRunsGenerate)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.model, "lua-model");
        EXPECT_EQ(request.prompt, "Why sky blue?");
        EXPECT_FALSE(request.stream);
        EXPECT_FALSE(request.format.has_value());
        EXPECT_TRUE(request.think.has_value());
        if (request.think.has_value())
        {
            const auto *think = std::get_if<bool>(&*request.think);
            EXPECT_NE(think, nullptr);
            if (think != nullptr)
            {
                EXPECT_FALSE(*think);
            }
        }

        yaaf::llm::GenerateResponse response;
        response.model = request.model;
        response.response = "Rayleigh scattering";
        response.done = true;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--model", "lua-model", "Why", "sky", "blue?"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "assistant: Rayleigh scattering\n");
}

TEST(CliAskCommandTests, DiscoveredLuaCommandRunsChatTools)
{
    yaaf::cli::Services services;
    bool generate_called = false;
    std::size_t chat_call_count = 0;
    services.generate = [&](const yaaf::llm::GenerateRequest &, const yaaf::llm::StreamCallback *) {
        generate_called = true;
        return yaaf::llm::GenerateResponse{};
    };
    services.chat = [&](const yaaf::llm::ChatRequest &request,
                        const yaaf::llm::ChatStreamCallback *on_stream_event) -> yaaf::llm::ChatResponse {
        ++chat_call_count;
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.model, "lua-model");
        EXPECT_FALSE(request.stream);
        EXPECT_EQ(request.tools.size(), 1U);
        if (!request.tools.empty())
        {
            EXPECT_EQ(request.tools.front().function.name, "echo");
            EXPECT_TRUE(request.tools.front().function.arguments.is_object());
            if (request.tools.front().function.arguments.is_object())
            {
                EXPECT_EQ(request.tools.front().function.arguments.at("type"), "object");
            }
        }

        yaaf::llm::ChatResponse response;
        response.model = request.model;
        response.done = true;
        response.message.role = "assistant";

        if (chat_call_count == 1)
        {
            EXPECT_EQ(request.messages.size(), 1U);
            if (!request.messages.empty())
            {
                EXPECT_EQ(request.messages.front().content, "Echo hello");
            }

            yaaf::llm::Tool tool_call;
            tool_call.function.name = "echo";
            tool_call.function.arguments = nlohmann::json{{"text", "hello"}};
            response.message.tool_calls.push_back(std::move(tool_call));
        }
        else
        {
            EXPECT_FALSE(request.messages.empty());
            if (!request.messages.empty())
            {
                EXPECT_EQ(request.messages.back().role, "tool");
                EXPECT_EQ(request.messages.back().content, "hello");
            }
            response.message.content = "hello";
        }

        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--model", "lua-model", "--tool", "echo", "Echo", "hello"}, input,
                                          output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_FALSE(generate_called);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(chat_call_count, 2U);
    EXPECT_EQ(output.str(), "tool: echo {\"text\":\"hello\"}\n"
                            "observation: hello\n"
                            "assistant: hello\n");
}

TEST(CliAskCommandTests, DiscoveredLuaCommandWritesJsonOutput)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.model, "lua-model");
        EXPECT_EQ(request.prompt, "Question");
        EXPECT_TRUE(request.format.has_value());
        if (request.format.has_value())
        {
            const auto *format = std::get_if<std::string>(&*request.format);
            EXPECT_NE(format, nullptr);
            if (format != nullptr)
            {
                EXPECT_EQ(*format, "json");
            }
        }

        yaaf::llm::GenerateResponse response;
        response.model = request.model;
        response.response = R"({"answer":"ok"})";
        response.thinking = "kept";
        response.done = true;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--model", "lua-model", "--format", "json", "Question"}, input,
                                          output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("answer"), "ok");
    EXPECT_EQ(payload.at("thinking"), "kept");
}

TEST(CliAskCommandTests, DiscoveredLuaCommandPrettyPrintsJsonOutput)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.prompt, "Question");

        yaaf::llm::GenerateResponse response;
        response.model = request.model;
        response.response = R"({"answer":"ok"})";
        response.done = true;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--model", "lua-model", "--format", "json", "--pretty", "Question"},
                                          input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("\n  \"answer\": \"ok\"\n"), std::string::npos);
}

TEST(CliAskCommandTests, RejectsPrettyWithoutJsonFormat)
{
    yaaf::cli::Services services;
    bool generate_called = false;
    services.generate = [&](const yaaf::llm::GenerateRequest &, const yaaf::llm::StreamCallback *) {
        generate_called = true;
        return yaaf::llm::GenerateResponse{};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--pretty", "Question"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_FALSE(generate_called);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("--pretty is only supported with --format json for ask"), std::string::npos);
}

TEST(CliAskCommandTests, JsonOutputIncludesThinkingWhenThinkIsEnabled)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.prompt, "Question");
        EXPECT_FALSE(request.stream);
        EXPECT_FALSE(request.format.has_value());
        EXPECT_TRUE(request.think.has_value());
        if (request.think.has_value())
        {
            const auto *think = std::get_if<std::string>(&*request.think);
            EXPECT_NE(think, nullptr);
            if (think != nullptr)
            {
                EXPECT_EQ(*think, "low");
            }
        }

        yaaf::llm::GenerateResponse response;
        response.model = request.model;
        response.response = "ok";
        response.thinking = "kept private";
        response.done = true;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--format", "json", "--pretty", "--think", "low", "Question"}, input,
                                          output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("response"), "ok");
    EXPECT_EQ(payload.at("thinking"), "kept private");
}

TEST(CliAskCommandTests, DiscoveredLuaCommandWrapsNonObjectJsonResponse)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(on_stream_event, nullptr);
        EXPECT_EQ(request.prompt, "Question");

        yaaf::llm::GenerateResponse response;
        response.model = request.model;
        response.response = R"(["Rayleigh scattering",42])";
        response.done = true;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--model", "lua-model", "--format", "json", "Question"}, input,
                                          output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    ASSERT_TRUE(payload.contains("answer"));
    ASSERT_TRUE(payload.at("answer").is_array());
    ASSERT_EQ(payload.at("answer").size(), 2U);
    EXPECT_EQ(payload.at("answer").at(0), "Rayleigh scattering");
    EXPECT_EQ(payload.at("answer").at(1), 42);
}

TEST(CliAskCommandTests, DiscoveredLuaCommandRejectsJsonFormatWithStream)
{
    yaaf::cli::Services services;
    bool generate_called = false;
    services.generate = [&](const yaaf::llm::GenerateRequest &, const yaaf::llm::StreamCallback *) {
        generate_called = true;
        return yaaf::llm::GenerateResponse{};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"ask", "--model", "lua-model", "--stream", "--format", "json", "Question"},
                                          input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_FALSE(generate_called);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("--format json is not supported with --stream for ask"), std::string::npos);
}

TEST(CliAskCommandTests, DiscoveredLuaCommandStreamsThinkingAndResponse)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(request.model, "lua-model");
        EXPECT_EQ(request.prompt, "Question");
        EXPECT_TRUE(request.stream);
        EXPECT_NE(on_stream_event, nullptr);

        if (on_stream_event != nullptr)
        {
            yaaf::llm::GenerateStreamEvent thinking_event;
            thinking_event.thinking = "thinking";
            (*on_stream_event)(thinking_event);

            yaaf::llm::GenerateStreamEvent response_event;
            response_event.response = "answer";
            (*on_stream_event)(response_event);
        }

        yaaf::llm::GenerateResponse response;
        response.model = request.model;
        response.response = "answer";
        response.thinking = "thinking";
        response.done = true;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--model", "lua-model", "--stream", "Question"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "thinking: thinking\nassistant: answer\n");
}

TEST(CliAskCommandTests, DiscoveredLuaCommandStreamFallsBackToFinalResponse)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(request.model, "lua-model");
        EXPECT_EQ(request.prompt, "Question");
        EXPECT_TRUE(request.stream);
        EXPECT_NE(on_stream_event, nullptr);

        yaaf::llm::GenerateResponse response;
        response.model = request.model;
        response.response = "answer";
        response.done = true;
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--model", "lua-model", "--stream", "Question"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "assistant: answer\n");
}

TEST(CliAskCommandTests, PassesJsonSchemaFormatToService)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *) -> yaaf::llm::GenerateResponse {
        const auto *schema = std::get_if<nlohmann::json>(&*request.format);
        EXPECT_NE(schema, nullptr);

        if (schema != nullptr)
        {
            EXPECT_EQ(schema->at("type"), "object");
            EXPECT_EQ(schema->at("properties").at("answer").at("type"), "string");
        }

        yaaf::llm::GenerateResponse response;
        response.response = R"({"answer":"Rayleigh scattering"})";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run(
        {"ask", "--format", R"({"type":"object","properties":{"answer":{"type":"string"}}})", "Why is the sky blue?"},
        input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "assistant: {\"answer\":\"Rayleigh scattering\"}\n");
}

TEST(CliAskCommandTests, JsonOutputFlattensObjectResponse)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request, const yaaf::llm::StreamCallback *) {
        EXPECT_EQ(request.prompt, "Why is the sky blue?");
        yaaf::llm::GenerateResponse response;
        response.response = R"({"answer":"Rayleigh scattering"})";
        response.thinking = "kept private";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--format", "json", "Why is the sky blue?"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("answer"), "Rayleigh scattering");
    EXPECT_EQ(payload.at("thinking"), "kept private");
}

TEST(CliAskCommandTests, JsonOutputWrapsNonObjectJsonResponse)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &, const yaaf::llm::StreamCallback *) {
        yaaf::llm::GenerateResponse response;
        response.response = R"(["Rayleigh scattering",42])";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--format", "json", "Why is the sky blue?"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    ASSERT_TRUE(payload.contains("answer"));
    ASSERT_TRUE(payload.at("answer").is_array());
    ASSERT_EQ(payload.at("answer").size(), 2U);
    EXPECT_EQ(payload.at("answer")[0], "Rayleigh scattering");
    EXPECT_EQ(payload.at("answer")[1], 42);
}

TEST(CliAskCommandTests, JsonOutputWrapsPlainTextResponse)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &, const yaaf::llm::StreamCallback *) {
        yaaf::llm::GenerateResponse response;
        response.response = "Rayleigh scattering";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--format", "json", "Why is the sky blue?"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("answer"), "Rayleigh scattering");
}

TEST(CliAskCommandTests, StreamPrintsThinkingAndAssistantResponse)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(request.stream, true);

        if (on_stream_event != nullptr)
        {
            yaaf::llm::GenerateStreamEvent thinking_event;
            thinking_event.thinking = "reasoning";
            (*on_stream_event)(thinking_event);

            yaaf::llm::GenerateStreamEvent response_event;
            response_event.response = "Rayleigh scattering";
            (*on_stream_event)(response_event);
        }

        yaaf::llm::GenerateResponse response;
        response.response = "Rayleigh scattering";
        response.thinking = "reasoning";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--stream", "Why is the sky blue?"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "thinking: reasoning\nassistant: Rayleigh scattering\n");
}

TEST(CliAskCommandTests, StreamPrintsResponseWhenOnlyFinalMessageIsAvailable)
{
    yaaf::cli::Services services;
    services.generate = [](const yaaf::llm::GenerateRequest &request,
                           const yaaf::llm::StreamCallback *on_stream_event) -> yaaf::llm::GenerateResponse {
        EXPECT_EQ(request.stream, true);
        EXPECT_NE(on_stream_event, nullptr);

        yaaf::llm::GenerateResponse response;
        response.response = "Rayleigh scattering";
        response.thinking = "reasoning";
        return response;
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"ask", "--stream", "Why is the sky blue?"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "thinking: reasoning\nassistant: Rayleigh scattering\n");
}

