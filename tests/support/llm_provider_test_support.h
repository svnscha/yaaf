#pragma once

#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include "../../libyaaf/http/http_client.h"

namespace yaaf::tests::llm
{
struct RecordedHttpPost
{
    std::string url;
    std::string body;
    nlohmann::json json_body = nlohmann::json::object();
    HttpClient::Headers headers;
    bool streamed = false;
};

class ScriptedProviderHttpFixture
{
  public:
    [[nodiscard]] const std::vector<RecordedHttpPost> &requests() const
    {
        return requests_;
    }

    [[nodiscard]] HttpClient::Response post(std::string_view url, std::string_view body, std::string_view content_type,
                                            const HttpClient::Headers &headers,
                                            const HttpClient::ResponseChunkHandler *on_response_chunk)
    {
        if (content_type != "application/json")
        {
            throw std::runtime_error("provider test fixture only supports application/json requests");
        }

        const auto payload = nlohmann::json::parse(body, nullptr, false);
        if (payload.is_discarded())
        {
            throw std::runtime_error("provider test fixture received invalid JSON body");
        }

        requests_.push_back(RecordedHttpPost{std::string(url), std::string(body), payload, headers,
                                             on_response_chunk != nullptr});

        const auto request_url = std::string(url);
        if (ends_with(request_url, "/api/generate"))
        {
            return ollama_generate(payload, on_response_chunk);
        }
        if (ends_with(request_url, "/api/chat"))
        {
            return ollama_chat(payload, on_response_chunk);
        }
        if (ends_with(request_url, "/api/embed"))
        {
            return ollama_embed(payload);
        }
        if (ends_with(request_url, "/chat/completions"))
        {
            return openai_chat(payload, on_response_chunk);
        }
        if (ends_with(request_url, "/embeddings"))
        {
            return openai_embed(payload);
        }

        throw std::runtime_error("unsupported provider test fixture URL: " + request_url);
    }

  private:
    std::vector<RecordedHttpPost> requests_;

    [[nodiscard]] static bool ends_with(std::string_view value, std::string_view suffix)
    {
        return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
    }

    [[nodiscard]] static std::string completion_text()
    {
        return "Hello world";
    }

    [[nodiscard]] static std::string completion_prefix()
    {
        return "Hello";
    }

    [[nodiscard]] static std::string completion_suffix()
    {
        return " world";
    }

    [[nodiscard]] static nlohmann::json embedding_for_index(std::size_t index)
    {
        const double base = 0.1 * static_cast<double>(index + 1);
        return nlohmann::json::array({base, base + 0.1, base + 0.2});
    }

    [[nodiscard]] static HttpClient::Response json_response(const nlohmann::json &payload)
    {
        return HttpClient::Response{200, "application/json", payload.dump(), {}};
    }

    static void emit_chunk(const HttpClient::ResponseChunkHandler *on_response_chunk, const std::string &chunk)
    {
        if (on_response_chunk != nullptr)
        {
            (*on_response_chunk)(chunk);
        }
    }

    [[nodiscard]] static bool has_tools(const nlohmann::json &payload)
    {
        return payload.contains("tools") && payload.at("tools").is_array() && !payload.at("tools").empty();
    }

    [[nodiscard]] static std::optional<std::string> last_tool_message_content(const nlohmann::json &payload)
    {
        const auto messages = payload.find("messages");
        if (messages == payload.end() || !messages->is_array() || messages->empty())
        {
            return std::nullopt;
        }

        const auto &last = messages->back();
        if (!last.is_object() || last.value("role", std::string{}) != "tool")
        {
            return std::nullopt;
        }

        if (const auto content = last.find("content"); content != last.end() && content->is_string())
        {
            return content->get<std::string>();
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::string first_tool_name(const nlohmann::json &payload)
    {
        if (!has_tools(payload))
        {
            return {};
        }

        const auto &tool = payload.at("tools").front();
        if (!tool.is_object())
        {
            return {};
        }
        if (const auto function_payload = tool.find("function"); function_payload != tool.end() && function_payload->is_object())
        {
            return function_payload->value("name", std::string{});
        }
        return {};
    }

    [[nodiscard]] static nlohmann::json scripted_tool_arguments(std::string_view tool_name)
    {
        if (tool_name.ends_with("hello"))
        {
            return nlohmann::json{{"name", "MCP"}};
        }
        if (tool_name.ends_with("repeat"))
        {
            return nlohmann::json{{"text", "hi"}, {"count", 3}};
        }
        return nlohmann::json::object();
    }

    [[nodiscard]] static std::string tool_loop_final_text(const nlohmann::json &payload)
    {
        if (const auto content = last_tool_message_content(payload); content.has_value())
        {
            return "Tool result: " + *content;
        }
        return completion_text();
    }

    [[nodiscard]] static HttpClient::Response ollama_generate(const nlohmann::json &payload,
                                                              const HttpClient::ResponseChunkHandler *on_response_chunk)
    {
        const auto model = payload.value("model", "");
        if (on_response_chunk != nullptr || payload.value("stream", false))
        {
            emit_chunk(on_response_chunk,
                       nlohmann::json{{"model", model}, {"created_at", "2026-05-24T00:00:00Z"},
                                      {"response", completion_prefix()}, {"done", false}}
                           .dump() + "\n");
            emit_chunk(on_response_chunk,
                       nlohmann::json{{"model", model}, {"created_at", "2026-05-24T00:00:01Z"},
                                      {"response", completion_suffix()}, {"done", false}}
                           .dump() + "\n");
            emit_chunk(on_response_chunk,
                       nlohmann::json{{"model", model}, {"created_at", "2026-05-24T00:00:02Z"},
                                      {"done", true}, {"done_reason", "stop"}, {"eval_count", 5}}
                           .dump() + "\n");
            return HttpClient::Response{200, "application/json", "", {}};
        }

        return json_response({{"model", model},
                              {"created_at", "2026-05-24T00:00:00Z"},
                              {"response", completion_text()},
                              {"done", true},
                              {"done_reason", "stop"},
                              {"eval_count", 5}});
    }

    [[nodiscard]] static HttpClient::Response ollama_chat(const nlohmann::json &payload,
                                                          const HttpClient::ResponseChunkHandler *on_response_chunk)
    {
        const auto model = payload.value("model", "");
        if (on_response_chunk != nullptr || payload.value("stream", false))
        {
            emit_chunk(on_response_chunk,
                       nlohmann::json{{"model", model},
                                      {"created_at", "2026-05-24T00:00:00Z"},
                                      {"message", {{"role", "assistant"}, {"content", completion_prefix()}}},
                                      {"done", false}}
                           .dump() + "\n");
            emit_chunk(on_response_chunk,
                       nlohmann::json{{"model", model},
                                      {"created_at", "2026-05-24T00:00:01Z"},
                                      {"message", {{"role", "assistant"}, {"content", completion_suffix()}}},
                                      {"done", false}}
                           .dump() + "\n");
            emit_chunk(on_response_chunk,
                       nlohmann::json{{"model", model},
                                      {"created_at", "2026-05-24T00:00:02Z"},
                                      {"message", {{"role", "assistant"}}},
                                      {"done", true},
                                      {"done_reason", "stop"}}
                           .dump() + "\n");
            return HttpClient::Response{200, "application/json", "", {}};
        }

        if (has_tools(payload) && !last_tool_message_content(payload).has_value())
        {
            const auto tool_name = first_tool_name(payload);
            return json_response({{"model", model},
                                  {"created_at", "2026-05-24T00:00:00Z"},
                                  {"message",
                                   {{"role", "assistant"},
                                    {"content", ""},
                                    {"tool_calls", nlohmann::json::array({{{"type", "function"},
                                                                              {"function", {{"name", tool_name},
                                                                                            {"arguments", scripted_tool_arguments(tool_name)}}}}})}}},
                                  {"done", true},
                                  {"done_reason", "stop"}});
        }

        return json_response({{"model", model},
                              {"created_at", "2026-05-24T00:00:00Z"},
                              {"message", {{"role", "assistant"}, {"content", tool_loop_final_text(payload)}}},
                              {"done", true},
                              {"done_reason", "stop"}});
    }

    [[nodiscard]] static HttpClient::Response ollama_embed(const nlohmann::json &payload)
    {
        nlohmann::json embeddings = nlohmann::json::array();
        if (payload.contains("input") && payload.at("input").is_array())
        {
            for (std::size_t index = 0; index < payload.at("input").size(); ++index)
            {
                embeddings.push_back(embedding_for_index(index));
            }
        }
        else
        {
            embeddings.push_back(embedding_for_index(0));
        }

        return json_response({{"model", payload.value("model", "")}, {"embeddings", embeddings}});
    }

    [[nodiscard]] static HttpClient::Response openai_chat(const nlohmann::json &payload,
                                                          const HttpClient::ResponseChunkHandler *on_response_chunk)
    {
        const auto model = payload.value("model", "");
        if (on_response_chunk != nullptr || payload.value("stream", false))
        {
            emit_chunk(on_response_chunk,
                       "data: " +
                           nlohmann::json{{"model", model},
                                          {"created", 1712345678},
                                          {"choices", {{{"index", 0},
                                                         {"delta", {{"role", "assistant"},
                                                                     {"content", completion_prefix()}}}}}}}
                               .dump() +
                           "\n\n");
            emit_chunk(on_response_chunk,
                       "data: " +
                           nlohmann::json{{"model", model},
                                          {"created", 1712345678},
                                          {"choices", {{{"index", 0},
                                                         {"delta", {{"content", completion_suffix()}}}}}}}
                               .dump() +
                           "\n\n");
            emit_chunk(on_response_chunk,
                       "data: " +
                           nlohmann::json{{"model", model},
                                          {"created", 1712345678},
                                          {"choices", {{{"index", 0},
                                                         {"delta", nlohmann::json::object()},
                                                         {"finish_reason", "stop"}}}},
                                          {"usage", {{"prompt_tokens", 5}}}}
                               .dump() +
                           "\n\n");
            emit_chunk(on_response_chunk, "data: [DONE]\n\n");
            return HttpClient::Response{200, "application/json", "", {}};
        }

        if (has_tools(payload) && !last_tool_message_content(payload).has_value())
        {
            const auto tool_name = first_tool_name(payload);
            return json_response({{"model", model},
                                  {"created", 1712345678},
                                  {"choices",
                                   {{{"index", 0},
                                     {"finish_reason", "tool_calls"},
                                     {"message",
                                      {{"role", "assistant"},
                                       {"content", ""},
                                       {"tool_calls", nlohmann::json::array({{{"type", "function"},
                                                                                 {"function", {{"name", tool_name},
                                                                                               {"arguments", scripted_tool_arguments(tool_name).dump()}}}}})}}}}}},
                                  {"usage", {{"prompt_tokens", 5}}}});
        }

        return json_response({{"model", model},
                              {"created", 1712345678},
                              {"choices",
                               {{{"index", 0},
                                 {"finish_reason", "stop"},
                                 {"message", {{"role", "assistant"}, {"content", tool_loop_final_text(payload)}}}}}},
                              {"usage", {{"prompt_tokens", 5}}}});
    }

    [[nodiscard]] static HttpClient::Response openai_embed(const nlohmann::json &payload)
    {
        nlohmann::json data = nlohmann::json::array();
        if (payload.contains("input") && payload.at("input").is_array())
        {
            for (std::size_t index = 0; index < payload.at("input").size(); ++index)
            {
                data.push_back({{"index", static_cast<int>(index)}, {"embedding", embedding_for_index(index)}});
            }
        }
        else
        {
            data.push_back({{"index", 0}, {"embedding", embedding_for_index(0)}});
        }

        return json_response({{"model", payload.value("model", "")}, {"data", data}});
    }
};
} // namespace yaaf::tests::llm
