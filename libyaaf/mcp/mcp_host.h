#pragma once

#include "mcp_schema.h"

namespace yaaf::mcp
{
/// JSON-RPC request from client.
struct HostRequest
{
    std::string jsonrpc = "2.0";
    std::string method;
    nlohmann::json params = nlohmann::json::object();
    std::optional<std::int64_t> id;
};

/// JSON-RPC response to send back to client.
struct HostResponse
{
    std::string jsonrpc = "2.0";
    std::optional<nlohmann::json> result;
    std::optional<nlohmann::json> error;
    std::optional<std::int64_t> id;
};

/// Prompt argument descriptor in MCP format.
struct PromptArgument
{
    std::string name;
    std::string description;
    bool required = false;
};

/// Prompt descriptor in MCP format.
struct PromptDescriptor
{
    std::string name;
    std::string description;
    std::vector<PromptArgument> arguments;
};

/// Tool result from tool executor callback.
struct ToolExecutorResult
{
    std::string content;
    bool is_error = false;
};

/// Prompt result from prompt executor callback.
struct PromptMessage
{
    std::string role;  // "user" or "assistant"
    std::string content;
};

/// Represents the negotiated MCP session.
struct Session
{
    std::string protocol_version;
    nlohmann::json server_info = nlohmann::json::object();
};

using ToolExecutor = std::function<ToolExecutorResult(const std::string &tool_name, const nlohmann::json &arguments)>;
using PromptExecutor = std::function<std::vector<PromptMessage>(const std::string &prompt_name, const nlohmann::json &arguments)>;

/// Manages the hosted MCP server session.
/**
 * Host negotiates protocol version with the client, exposes tool and prompt
 * registries, and dispatches tool calls and prompt requests to Lua callbacks.
 *
 * All operations are synchronous. The host assumes a single client connection
 * and does not handle concurrent requests.
 */
class Host
{
  public:
    /// Construct a host with the given schema backend and callbacks.
    /**
     * @param schema_backend Schema backend for protocol version gating
     * @param tool_executor Callback to execute tool calls (Lua-provided)
     * @param prompt_executor Callback to execute prompt requests (Lua-provided)
     * @throws std::invalid_argument if schema_backend is null
     */
    Host(std::shared_ptr<const schema::Backend> schema_backend, ToolExecutor tool_executor = nullptr,
         PromptExecutor prompt_executor = nullptr);

    /// Initialize session and negotiate protocol version with client.
    /**
     * @param client_info Client info object with name and version
     * @return ServerInfo with negotiated protocol version and server capabilities
     * @throws std::runtime_error if protocol version negotiation fails
     */
    [[nodiscard]] nlohmann::json initialize(const nlohmann::json &client_info);

    /// List all available tools.
    /**
     * @return Vector of tools in MCP ToolInfo schema format
     * @throws std::runtime_error if tool executor callback fails
     */
    [[nodiscard]] std::vector<nlohmann::json> list_tools();

    /// Call a tool with the given arguments.
    /**
     * @param name Tool name from MCP server registry
     * @param arguments JSON object with tool parameters
     * @return JSON result from tool executor
     * @throws std::runtime_error if tool name not found or executor fails
     */
    [[nodiscard]] nlohmann::json call_tool(const std::string &name, const nlohmann::json &arguments);

    /// List all available prompts.
    /**
     * @return Vector of prompts in MCP PromptDescriptor schema format
     * @throws std::runtime_error if prompt executor callback fails
     */
    [[nodiscard]] std::vector<nlohmann::json> list_prompts();

    /// Get a prompt with the given arguments.
    /**
     * @param name Prompt name from MCP server registry
     * @param arguments JSON object with prompt parameters
     * @return Vector of prompt messages in MCP format
     * @throws std::runtime_error if prompt name not found or executor fails
     */
    [[nodiscard]] std::vector<nlohmann::json> get_prompt(const std::string &name, const nlohmann::json &arguments);

    /// Access the negotiated session information.
    /**
     * @return Const reference to the current session state
     */
    [[nodiscard]] const Session &session() const;

  private:
    std::shared_ptr<const schema::Backend> schema_backend_;
    Session session_;
    ToolExecutor tool_executor_;
    PromptExecutor prompt_executor_;
};

} // namespace yaaf::mcp
