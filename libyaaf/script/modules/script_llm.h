#pragma once

#include "../../http/http_client.h"
#include "../../llm/llm.h"
#include "../../mcp/mcp_client.h"

struct lua_State;

namespace yaaf::script
{
struct Services
{
    std::function<HttpClient::Response(const HttpClient::Request &request)> http_request;
    std::function<HttpClient::Response(std::string_view url, const HttpClient::Headers &headers)> http_get;
    std::function<HttpClient::Response(std::string_view url, std::string_view body, std::string_view content_type,
                                       const HttpClient::Headers &headers,
                                       const HttpClient::ResponseChunkHandler *on_response_chunk)>
        http_post;
    std::function<yaaf::llm::GenerateResponse(const yaaf::llm::GenerateRequest &request,
                                              const yaaf::llm::StreamCallback *on_stream_event)>
        generate;
    std::function<yaaf::llm::ChatResponse(const yaaf::llm::ChatRequest &request,
                                          const yaaf::llm::ChatStreamCallback *on_stream_event)>
        chat;
    std::function<yaaf::llm::EmbedResponse(const yaaf::llm::EmbedRequest &request)> embed;
    yaaf::mcp::HttpPost mcp_http_post;
    yaaf::mcp::StdioProcessFactory mcp_stdio_process_factory;
    std::shared_ptr<const yaaf::mcp::schema::Registry> mcp_schema_registry;
};

struct ScriptLlmContext
{
    std::string default_endpoint;
    std::string default_model;
    HttpClient::Options http;
    const Services *services = nullptr;
};

namespace modules
{
/**
 * Registers the provider-neutral LLM bridge as `require("llm")`.
 *
 * The module dispatches generate, chat, and embed calls to a registered provider table.
 *
 * A native default provider is registered automatically so built-in commands keep working,
 * while Lua scripts can register custom providers that implement the same callbacks.
 */
void register_llm_module(lua_State *state, ScriptLlmContext &context);
} // namespace modules
} // namespace yaaf::script
