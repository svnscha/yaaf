#include "libyaaf/pch/pch_std.h"
#include "libyaaf/pch/pch_dependencies.h"

#include "mcp_host.h"

namespace yaaf::mcp
{
namespace
{
[[nodiscard]] std::string as_string(const nlohmann::json &value, std::string_view fallback = {})
{
    return value.is_string() ? value.get<std::string>() : std::string(fallback);
}
} // namespace

Host::Host(std::shared_ptr<const schema::Backend> schema_backend, ToolExecutor tool_executor,
           PromptExecutor prompt_executor)
    : schema_backend_(std::move(schema_backend)), tool_executor_(std::move(tool_executor)),
      prompt_executor_(std::move(prompt_executor))
{
    if (!schema_backend_)
    {
        throw std::invalid_argument("schema_backend must not be null");
    }
    session_.protocol_version = std::string(schema_backend_->info().version);
}

nlohmann::json Host::initialize(const nlohmann::json &client_info)
{
    // Validate that initialize method is supported
    if (!schema_backend_->method("initialize").has_value())
    {
        throw std::runtime_error("initialize method not supported in protocol version");
    }

    // Extract client protocol version
    const auto client_version = as_string(client_info.value("protocolVersion", nlohmann::json{}),
                                          std::string(schema_backend_->info().version));

    // Store negotiated protocol version (for now, accept client's version or use backend)
    session_.protocol_version = client_version;

    // Build server response with negotiated version and capabilities
    session_.server_info = {{"name", "yaaf"}, {"version", "0.1.0"}};

    nlohmann::json response = {{"protocolVersion", session_.protocol_version}, {"serverInfo", session_.server_info}};

    // Add capabilities for v1+
    if (!client_version.empty())
    {
        response["capabilities"] = {{"tools", nlohmann::json::object()}, {"prompts", nlohmann::json::object()}};
    }

    return response;
}

std::vector<nlohmann::json> Host::list_tools()
{
    if (!schema_backend_->method("tools/list").has_value())
    {
        throw std::runtime_error("tools/list method not supported in protocol version");
    }

    // For now, return empty list. In Phase 2.2, this will call the tool executor callback.
    // Tools would be fetched from a Lua registry via the executor callback.
    if (!tool_executor_)
    {
        return {};
    }

    // Call executor callback to get tool list
    // This is a placeholder for Phase 2.2 integration
    return {};
}

nlohmann::json Host::call_tool(const std::string &name, const nlohmann::json &arguments)
{
    if (!schema_backend_->method("tools/call").has_value())
    {
        throw std::runtime_error("tools/call method not supported in protocol version");
    }

    if (!tool_executor_)
    {
        throw std::runtime_error(fmt::format("tool '{}' not found", name));
    }

    // Call executor callback with tool name and arguments
    const auto result = tool_executor_(name, arguments);

    // Return MCP tool result format
    return {{"type", result.is_error ? "error" : "text"}, {"text", result.content}};
}

std::vector<nlohmann::json> Host::list_prompts()
{
    if (!schema_backend_->method("prompts/list").has_value())
    {
        throw std::runtime_error("prompts/list method not supported in protocol version");
    }

    // For now, return empty list. In Phase 2.2, this will call the prompt executor callback.
    if (!prompt_executor_)
    {
        return {};
    }

    // Call executor callback to get prompt list
    // This is a placeholder for Phase 2.2 integration
    return {};
}

std::vector<nlohmann::json> Host::get_prompt(const std::string &name, const nlohmann::json &arguments)
{
    if (!schema_backend_->method("prompts/get").has_value())
    {
        throw std::runtime_error("prompts/get method not supported in protocol version");
    }

    if (!prompt_executor_)
    {
        throw std::runtime_error(fmt::format("prompt '{}' not found", name));
    }

    // Call executor callback with prompt name and arguments
    const auto messages = prompt_executor_(name, arguments);

    // Convert to MCP message format
    std::vector<nlohmann::json> result;
    for (const auto &msg : messages)
    {
        result.push_back({{"role", msg.role}, {"content", {{"type", "text"}, {"text", msg.content}}}});
    }
    return result;
}

const Session &Host::session() const
{
    return session_;
}

} // namespace yaaf::mcp
