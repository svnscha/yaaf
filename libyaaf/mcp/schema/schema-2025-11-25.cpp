#include "../mcp_schema_generated.h"

namespace yaaf::mcp::schema
{
namespace
{
const VersionInfo kVersionInfo = {
    "2025-11-25",
    "https://raw.githubusercontent.com/modelcontextprotocol/modelcontextprotocol/main/schema/2025-11-25/schema.json",
    "third_party/mcp/schema/2025-11-25/schema.json", 145, 31};

const std::vector<std::string_view> kDefinitions = {
    "Annotations",
    "AudioContent",
    "BaseMetadata",
    "BlobResourceContents",
    "BooleanSchema",
    "CallToolRequest",
    "CallToolRequestParams",
    "CallToolResult",
    "CancelledNotification",
    "CancelledNotificationParams",
    "CancelTaskRequest",
    "CancelTaskResult",
    "ClientCapabilities",
    "ClientNotification",
    "ClientRequest",
    "ClientResult",
    "CompleteRequest",
    "CompleteRequestParams",
    "CompleteResult",
    "ContentBlock",
    "CreateMessageRequest",
    "CreateMessageRequestParams",
    "CreateMessageResult",
    "CreateTaskResult",
    "Cursor",
    "ElicitationCompleteNotification",
    "ElicitRequest",
    "ElicitRequestFormParams",
    "ElicitRequestParams",
    "ElicitRequestURLParams",
    "ElicitResult",
    "EmbeddedResource",
    "EmptyResult",
    "EnumSchema",
    "Error",
    "GetPromptRequest",
    "GetPromptRequestParams",
    "GetPromptResult",
    "GetTaskPayloadRequest",
    "GetTaskPayloadResult",
    "GetTaskRequest",
    "GetTaskResult",
    "Icon",
    "Icons",
    "ImageContent",
    "Implementation",
    "InitializedNotification",
    "InitializeRequest",
    "InitializeRequestParams",
    "InitializeResult",
    "JSONRPCErrorResponse",
    "JSONRPCMessage",
    "JSONRPCNotification",
    "JSONRPCRequest",
    "JSONRPCResponse",
    "JSONRPCResultResponse",
    "LegacyTitledEnumSchema",
    "ListPromptsRequest",
    "ListPromptsResult",
    "ListResourcesRequest",
    "ListResourcesResult",
    "ListResourceTemplatesRequest",
    "ListResourceTemplatesResult",
    "ListRootsRequest",
    "ListRootsResult",
    "ListTasksRequest",
    "ListTasksResult",
    "ListToolsRequest",
    "ListToolsResult",
    "LoggingLevel",
    "LoggingMessageNotification",
    "LoggingMessageNotificationParams",
    "ModelHint",
    "ModelPreferences",
    "MultiSelectEnumSchema",
    "Notification",
    "NotificationParams",
    "NumberSchema",
    "PaginatedRequest",
    "PaginatedRequestParams",
    "PaginatedResult",
    "PingRequest",
    "PrimitiveSchemaDefinition",
    "ProgressNotification",
    "ProgressNotificationParams",
    "ProgressToken",
    "Prompt",
    "PromptArgument",
    "PromptListChangedNotification",
    "PromptMessage",
    "PromptReference",
    "ReadResourceRequest",
    "ReadResourceRequestParams",
    "ReadResourceResult",
    "RelatedTaskMetadata",
    "Request",
    "RequestId",
    "RequestParams",
    "Resource",
    "ResourceContents",
    "ResourceLink",
    "ResourceListChangedNotification",
    "ResourceRequestParams",
    "ResourceTemplate",
    "ResourceTemplateReference",
    "ResourceUpdatedNotification",
    "ResourceUpdatedNotificationParams",
    "Result",
    "Role",
    "Root",
    "RootsListChangedNotification",
    "SamplingMessage",
    "SamplingMessageContentBlock",
    "ServerCapabilities",
    "ServerNotification",
    "ServerRequest",
    "ServerResult",
    "SetLevelRequest",
    "SetLevelRequestParams",
    "SingleSelectEnumSchema",
    "StringSchema",
    "SubscribeRequest",
    "SubscribeRequestParams",
    "Task",
    "TaskAugmentedRequestParams",
    "TaskMetadata",
    "TaskStatus",
    "TaskStatusNotification",
    "TaskStatusNotificationParams",
    "TextContent",
    "TextResourceContents",
    "TitledMultiSelectEnumSchema",
    "TitledSingleSelectEnumSchema",
    "Tool",
    "ToolAnnotations",
    "ToolChoice",
    "ToolExecution",
    "ToolListChangedNotification",
    "ToolResultContent",
    "ToolUseContent",
    "UnsubscribeRequest",
    "UnsubscribeRequestParams",
    "UntitledMultiSelectEnumSchema",
    "UntitledSingleSelectEnumSchema",
    "URLElicitationRequiredError",
};

const std::vector<MethodInfo> kMethods = {
    {"completion/complete", "CompleteRequest", MessageKind::request},
    {"elicitation/create", "ElicitRequest", MessageKind::request},
    {"initialize", "InitializeRequest", MessageKind::request},
    {"logging/setLevel", "SetLevelRequest", MessageKind::request},
    {"notifications/cancelled", "CancelledNotification", MessageKind::notification},
    {"notifications/elicitation/complete", "ElicitationCompleteNotification", MessageKind::notification},
    {"notifications/initialized", "InitializedNotification", MessageKind::notification},
    {"notifications/message", "LoggingMessageNotification", MessageKind::notification},
    {"notifications/progress", "ProgressNotification", MessageKind::notification},
    {"notifications/prompts/list_changed", "PromptListChangedNotification", MessageKind::notification},
    {"notifications/resources/list_changed", "ResourceListChangedNotification", MessageKind::notification},
    {"notifications/resources/updated", "ResourceUpdatedNotification", MessageKind::notification},
    {"notifications/roots/list_changed", "RootsListChangedNotification", MessageKind::notification},
    {"notifications/tasks/status", "TaskStatusNotification", MessageKind::notification},
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
    {"tasks/cancel", "CancelTaskRequest", MessageKind::request},
    {"tasks/get", "GetTaskRequest", MessageKind::request},
    {"tasks/list", "ListTasksRequest", MessageKind::request},
    {"tasks/result", "GetTaskPayloadRequest", MessageKind::request},
    {"tools/call", "CallToolRequest", MessageKind::request},
    {"tools/list", "ListToolsRequest", MessageKind::request},
};

class SchemaBackend2025_11_25 final : public Backend
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

std::shared_ptr<const Backend> generated_backend_2025_11_25()
{
    static const auto instance = std::make_shared<SchemaBackend2025_11_25>();
    return instance;
}
} // namespace yaaf::mcp::schema
