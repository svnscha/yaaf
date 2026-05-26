#pragma once

#include "../http/http_client.h"
#include "../llm/llm.h"
#include "../mcp/mcp_client.h"
#include "../mcp/mcp_schema.h"

namespace yaaf::cli
{
struct Services
{
    std::function<HttpClient::Response(const HttpClient::Request &request)> http_request;
    std::function<HttpClient::Response(std::string_view url, const HttpClient::Headers &headers)> http_get;
    std::function<HttpClient::Response(std::string_view url, std::string_view body, std::string_view content_type,
                                       const HttpClient::Headers &headers,
                                       const HttpClient::ResponseChunkHandler *on_response_chunk)>
        http_post;
    yaaf::mcp::HttpPost mcp_http_post;
    yaaf::mcp::StdioProcessFactory mcp_stdio_process_factory;
    std::shared_ptr<const yaaf::mcp::schema::Registry> mcp_schema_registry;
    std::function<yaaf::llm::GenerateResponse(const yaaf::llm::GenerateRequest &request,
                                              const yaaf::llm::StreamCallback *on_stream_event)>
        generate;
    std::function<yaaf::llm::ChatResponse(const yaaf::llm::ChatRequest &request,
                                          const yaaf::llm::ChatStreamCallback *on_stream_event)>
        chat;
    std::function<yaaf::llm::EmbedResponse(const yaaf::llm::EmbedRequest &request)> embed;
};

/**
 * Runs the yaaf CLI using the provided argument list and streams.
 *
 * @param args Command line arguments excluding the executable name.
 * @param input Input stream used for interactive chat prompts.
 * @param output Output stream used for normal command output.
 * @param error_output Error stream used for user-facing failures.
 * @param services Optional overrides for command backends used by unit tests.
 * @return Process-style exit code.
 */
[[nodiscard]] int run(std::vector<std::string> args, std::istream &input, std::ostream &output,
                      std::ostream &error_output, const Services *services = nullptr);

/**
 * Runs the yaaf CLI using argc/argv.
 *
 * @param argc Number of command line arguments.
 * @param argv Command line argument array including the executable name.
 * @param input Input stream used for interactive chat prompts.
 * @param output Output stream used for normal command output.
 * @param error_output Error stream used for user-facing failures.
 * @param services Optional overrides for command backends used by unit tests.
 * @return Process-style exit code.
 */
[[nodiscard]] int run(int argc, const char *const *argv, std::istream &input, std::ostream &output,
                      std::ostream &error_output, const Services *services = nullptr);
} // namespace yaaf::cli
