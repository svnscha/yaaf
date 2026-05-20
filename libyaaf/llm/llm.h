#pragma once

#include <functional>
#include <variant>

#include "../http/http_client.h"

namespace yaaf::llm
{
/** Request format selector: either `json` or a JSON schema object. */
using Format = std::variant<std::string, nlohmann::json>;

/** Thinking mode selector: either enabled/disabled or a named level such as `high`. */
using Think = std::variant<bool, std::string>;

/** Model keep-alive selector, for example `5m`, `0`, or another numeric duration value. */
using KeepAlive = std::variant<std::string, std::int64_t, double>;

/** Stop sequence selector: either one stop string or multiple stop strings. */
using Stop = std::variant<std::string, std::vector<std::string>>;

/** Embed input selector: either one input string or multiple input strings. */
using EmbedInput = std::variant<std::string, std::vector<std::string>>;

/** Log probability information for a single token alternative. */
struct TokenLogprob
{
    std::string token;
    double logprob = 0.0;
    std::vector<std::int64_t> bytes;
};

/** Log probability information for one generated token. */
struct Logprob
{
    std::string token;
    double logprob = 0.0;
    std::vector<std::int64_t> bytes;
    std::vector<TokenLogprob> top_logprobs;
};

/** A tool function argument or schema payload represented as JSON. */
using ToolJson = nlohmann::json;

/** Function tool metadata used in chat tool definitions and tool-call responses. */
struct ToolFunction
{
    std::string name;
    std::optional<std::string> description;
    ToolJson arguments = nlohmann::json::object();
};

/** One tool entry for chat requests or responses. */
struct Tool
{
    std::string type = "function";
    ToolFunction function;
};

/** One chat message in the conversation history or response. */
struct ChatMessage
{
    std::string role;
    std::string content;
    std::optional<std::string> thinking;
    std::vector<std::string> images;
    std::vector<Tool> tool_calls;
};

/** Shared runtime model options such as temperature, seed, stop, or token limits. */
struct ModelOptions
{
    std::optional<std::int64_t> seed;
    std::optional<double> temperature;
    std::optional<std::int64_t> top_k;
    std::optional<double> top_p;
    std::optional<double> min_p;
    std::optional<Stop> stop;
    std::optional<std::int64_t> num_ctx;
    std::optional<std::int64_t> num_predict;
    std::optional<nlohmann::json> extra;
};

/** One streamed generate event emitted by an LLM provider. */
struct GenerateStreamEvent
{
    std::string model;
    std::string created_at;
    std::string response;
    std::optional<std::string> thinking;
    bool done = false;
    std::string done_reason;
    std::optional<std::int64_t> total_duration;
    std::optional<std::int64_t> load_duration;
    std::optional<std::int64_t> prompt_eval_count;
    std::optional<std::int64_t> prompt_eval_duration;
    std::optional<std::int64_t> eval_count;
    std::optional<std::int64_t> eval_duration;
    std::vector<Logprob> logprobs;
};

/** Callback invoked for each streamed generate event. */
using StreamCallback = std::function<void(const GenerateStreamEvent &event)>;

/** One streamed chat event emitted by an LLM provider. */
struct ChatStreamEvent
{
    std::string model;
    std::string created_at;
    ChatMessage message;
    bool done = false;
    std::string done_reason;
    std::optional<std::int64_t> total_duration;
    std::optional<std::int64_t> load_duration;
    std::optional<std::int64_t> prompt_eval_count;
    std::optional<std::int64_t> prompt_eval_duration;
    std::optional<std::int64_t> eval_count;
    std::optional<std::int64_t> eval_duration;
    std::vector<Logprob> logprobs;
};

/** Callback invoked for each streamed chat event. */
using ChatStreamCallback = std::function<void(const ChatStreamEvent &event)>;

/** Provider-neutral generate request. */
struct GenerateRequest
{
    std::string model;
    std::string prompt;
    std::optional<std::string> suffix;
    std::vector<std::string> images;
    std::optional<Format> format;
    std::optional<std::string> system;
    bool stream = false;
    std::optional<Think> think;
    bool raw = false;
    std::optional<KeepAlive> keep_alive;
    std::optional<ModelOptions> options;
    std::optional<bool> logprobs;
    std::optional<std::int64_t> top_logprobs;
};

/** Provider-neutral embed request. */
struct EmbedRequest
{
    std::string model;
    EmbedInput input;
    bool truncate = true;
    std::optional<std::int64_t> dimensions;
    std::optional<KeepAlive> keep_alive;
    std::optional<ModelOptions> options;
};

/** Provider-neutral chat request. */
struct ChatRequest
{
    std::string model;
    std::vector<ChatMessage> messages;
    std::vector<Tool> tools;
    std::optional<Format> format;
    bool stream = true;
    std::optional<Think> think;
    std::optional<KeepAlive> keep_alive;
    std::optional<ModelOptions> options;
    std::optional<bool> logprobs;
    std::optional<std::int64_t> top_logprobs;
};

/** Final aggregated generate response. */
struct GenerateResponse
{
    std::string model;
    std::string created_at;
    std::string response;
    std::optional<std::string> thinking;
    bool done = false;
    std::string done_reason;
    std::optional<std::int64_t> total_duration;
    std::optional<std::int64_t> load_duration;
    std::optional<std::int64_t> prompt_eval_count;
    std::optional<std::int64_t> prompt_eval_duration;
    std::optional<std::int64_t> eval_count;
    std::optional<std::int64_t> eval_duration;
    std::vector<Logprob> logprobs;
};

/** Provider-neutral embed response. */
struct EmbedResponse
{
    std::string model;
    std::vector<std::vector<double>> embeddings;
    std::optional<std::int64_t> total_duration;
    std::optional<std::int64_t> load_duration;
    std::optional<std::int64_t> prompt_eval_count;
};

/** Final aggregated chat response. */
struct ChatResponse
{
    std::string model;
    std::string created_at;
    ChatMessage message;
    bool done = false;
    std::string done_reason;
    std::optional<std::int64_t> total_duration;
    std::optional<std::int64_t> load_duration;
    std::optional<std::int64_t> prompt_eval_count;
    std::optional<std::int64_t> prompt_eval_duration;
    std::optional<std::int64_t> eval_count;
    std::optional<std::int64_t> eval_duration;
    std::vector<Logprob> logprobs;
};
} // namespace yaaf::llm
