#include "../mcp_schema_generated.h"

namespace yaaf::mcp::schema
{
namespace
{
const VersionInfo kVersionInfo = {
    "2024-11-05",
    "https://raw.githubusercontent.com/modelcontextprotocol/modelcontextprotocol/main/schema/2024-11-05/schema.json",
    "third_party/mcp/schema/2024-11-05/schema.json", 79, 24};

const std::vector<std::string_view> kDefinitions = {
    "Annotated",
    "BlobResourceContents",
    "CallToolRequest",
    "CallToolResult",
    "CancelledNotification",
    "ClientCapabilities",
    "ClientNotification",
    "ClientRequest",
    "ClientResult",
    "CompleteRequest",
    "CompleteResult",
    "CreateMessageRequest",
    "CreateMessageResult",
    "Cursor",
    "EmbeddedResource",
    "EmptyResult",
    "GetPromptRequest",
    "GetPromptResult",
    "ImageContent",
    "Implementation",
    "InitializedNotification",
    "InitializeRequest",
    "InitializeResult",
    "JSONRPCError",
    "JSONRPCMessage",
    "JSONRPCNotification",
    "JSONRPCRequest",
    "JSONRPCResponse",
    "ListPromptsRequest",
    "ListPromptsResult",
    "ListResourcesRequest",
    "ListResourcesResult",
    "ListResourceTemplatesRequest",
    "ListResourceTemplatesResult",
    "ListRootsRequest",
    "ListRootsResult",
    "ListToolsRequest",
    "ListToolsResult",
    "LoggingLevel",
    "LoggingMessageNotification",
    "ModelHint",
    "ModelPreferences",
    "Notification",
    "PaginatedRequest",
    "PaginatedResult",
    "PingRequest",
    "ProgressNotification",
    "ProgressToken",
    "Prompt",
    "PromptArgument",
    "PromptListChangedNotification",
    "PromptMessage",
    "PromptReference",
    "ReadResourceRequest",
    "ReadResourceResult",
    "Request",
    "RequestId",
    "Resource",
    "ResourceContents",
    "ResourceListChangedNotification",
    "ResourceReference",
    "ResourceTemplate",
    "ResourceUpdatedNotification",
    "Result",
    "Role",
    "Root",
    "RootsListChangedNotification",
    "SamplingMessage",
    "ServerCapabilities",
    "ServerNotification",
    "ServerRequest",
    "ServerResult",
    "SetLevelRequest",
    "SubscribeRequest",
    "TextContent",
    "TextResourceContents",
    "Tool",
    "ToolListChangedNotification",
    "UnsubscribeRequest",
};

const std::vector<MethodInfo> kMethods = {
    {"completion/complete", "CompleteRequest", MessageKind::request},
    {"initialize", "InitializeRequest", MessageKind::request},
    {"logging/setLevel", "SetLevelRequest", MessageKind::request},
    {"notifications/cancelled", "CancelledNotification", MessageKind::notification},
    {"notifications/initialized", "InitializedNotification", MessageKind::notification},
    {"notifications/message", "LoggingMessageNotification", MessageKind::notification},
    {"notifications/progress", "ProgressNotification", MessageKind::notification},
    {"notifications/prompts/list_changed", "PromptListChangedNotification", MessageKind::notification},
    {"notifications/resources/list_changed", "ResourceListChangedNotification", MessageKind::notification},
    {"notifications/resources/updated", "ResourceUpdatedNotification", MessageKind::notification},
    {"notifications/roots/list_changed", "RootsListChangedNotification", MessageKind::notification},
    {"notifications/tools/list_changed", "ToolListChangedNotification", MessageKind::notification},
    {"ping", "PingRequest", MessageKind::request},
    {"prompts/get", "GetPromptRequest", MessageKind::request},
    {"prompts/list", "ListPromptsRequest", MessageKind::request},
    {"resources/list", "ListResourcesRequest", MessageKind::request},
    {"resources/read", "ReadResourceRequest", MessageKind::request},
    {"resources/subscribe", "SubscribeRequest", MessageKind::request},
    {"resources/templates/list", "ListResourceTemplatesRequest", MessageKind::request},
    {"resources/unsubscribe", "UnsubscribeRequest", MessageKind::request},
    {"roots/list", "ListRootsRequest", MessageKind::request},
    {"sampling/createMessage", "CreateMessageRequest", MessageKind::request},
    {"tools/call", "CallToolRequest", MessageKind::request},
    {"tools/list", "ListToolsRequest", MessageKind::request},
};

class SchemaBackend2024_11_05 final : public Backend
{
  public:
    [[nodiscard]] const VersionInfo &info() const override
    {
        return kVersionInfo;
    }

    [[nodiscard]] const std::vector<MethodInfo> &methods() const override
    {
        return kMethods;
    }

    [[nodiscard]] const std::vector<std::string_view> &definitions() const override
    {
        return kDefinitions;
    }

    [[nodiscard]] bool has_definition(std::string_view definition) const override
    {
        return std::find(kDefinitions.begin(), kDefinitions.end(), definition) != kDefinitions.end();
    }

    [[nodiscard]] std::optional<MethodInfo> method(std::string_view method) const override
    {
        const auto found = std::find_if(kMethods.begin(), kMethods.end(),
                                        [method](const MethodInfo &entry) { return entry.method == method; });
        if (found == kMethods.end())
        {
            return std::nullopt;
        }
        return *found;
    }
};
} // namespace

std::shared_ptr<const Backend> generated_backend_2024_11_05()
{
    static const auto instance = std::make_shared<SchemaBackend2024_11_05>();
    return instance;
}
} // namespace yaaf::mcp::schema
