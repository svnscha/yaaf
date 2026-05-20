#include "mcp_schema_generated.h"

namespace yaaf::mcp::schema
{
namespace
{
const std::vector<VersionInfo> kSupportedVersions = {
    {"2024-11-05",
     "https://raw.githubusercontent.com/modelcontextprotocol/modelcontextprotocol/main/schema/2024-11-05/schema.json",
     "third_party/mcp/schema/2024-11-05/schema.json", 79, 24},
    {"2025-03-26",
     "https://raw.githubusercontent.com/modelcontextprotocol/modelcontextprotocol/main/schema/2025-03-26/schema.json",
     "third_party/mcp/schema/2025-03-26/schema.json", 83, 24},
    {"2025-06-18",
     "https://raw.githubusercontent.com/modelcontextprotocol/modelcontextprotocol/main/schema/2025-06-18/schema.json",
     "third_party/mcp/schema/2025-06-18/schema.json", 91, 25},
    {"2025-11-25",
     "https://raw.githubusercontent.com/modelcontextprotocol/modelcontextprotocol/main/schema/2025-11-25/schema.json",
     "third_party/mcp/schema/2025-11-25/schema.json", 145, 31},
};

const std::vector<std::string_view> kKnownMethods = {
    "completion/complete",
    "elicitation/create",
    "initialize",
    "logging/setLevel",
    "notifications/cancelled",
    "notifications/elicitation/complete",
    "notifications/initialized",
    "notifications/message",
    "notifications/progress",
    "notifications/prompts/list_changed",
    "notifications/resources/list_changed",
    "notifications/resources/updated",
    "notifications/roots/list_changed",
    "notifications/tasks/status",
    "notifications/tools/list_changed",
    "ping",
    "prompts/get",
    "prompts/list",
    "resources/list",
    "resources/read",
    "resources/subscribe",
    "resources/templates/list",
    "resources/unsubscribe",
    "roots/list",
    "sampling/createMessage",
    "tasks/cancel",
    "tasks/get",
    "tasks/list",
    "tasks/result",
    "tools/call",
    "tools/list",
};

const std::vector<MethodInfo> kEmptyMethods;
const std::vector<std::string_view> kEmptyDefinitions;

class GeneratedRegistry final : public Registry
{
  public:
    GeneratedRegistry()
        : backends_{
              generated_backend_2024_11_05(),
              generated_backend_2025_03_26(),
              generated_backend_2025_06_18(),
              generated_backend_2025_11_25(),
          }
    {
    }

    [[nodiscard]] std::string_view latest_protocol_version() const override
    {
        return kSupportedVersions.back().version;
    }

    [[nodiscard]] const std::vector<VersionInfo> &supported_versions() const override
    {
        return kSupportedVersions;
    }

    [[nodiscard]] std::shared_ptr<const Backend> backend(std::string_view version) const override
    {
        const auto found = std::find_if(backends_.begin(), backends_.end(),
                                        [version](const auto &entry) { return entry->info().version == version; });
        return found == backends_.end() ? nullptr : *found;
    }

    [[nodiscard]] bool is_supported_protocol_version(std::string_view version) const override
    {
        return backend(version) != nullptr;
    }

  private:
    std::vector<std::shared_ptr<const Backend>> backends_;
};
} // namespace

std::shared_ptr<const Backend> GeneratedBackendFactory::create(std::string_view version) const
{
    if (version == "2024-11-05")
    {
        return generated_backend_2024_11_05();
    }
    if (version == "2025-03-26")
    {
        return generated_backend_2025_03_26();
    }
    if (version == "2025-06-18")
    {
        return generated_backend_2025_06_18();
    }
    if (version == "2025-11-25")
    {
        return generated_backend_2025_11_25();
    }
    return nullptr;
}

std::shared_ptr<const Backend> GeneratedBackendFactory::create_latest() const
{
    return create(kSupportedVersions.back().version);
}

std::shared_ptr<const Registry> GeneratedBackendFactory::create_registry() const
{
    return std::make_shared<GeneratedRegistry>();
}

std::shared_ptr<const BackendFactory> generated_factory()
{
    static const auto instance = std::make_shared<GeneratedBackendFactory>();
    return instance;
}

std::shared_ptr<const BackendFactory> default_factory()
{
    return generated_factory();
}

const std::shared_ptr<const Registry> &default_registry()
{
    static const auto instance = generated_factory()->create_registry();
    return instance;
}

std::string_view latest_protocol_version()
{
    return default_registry()->latest_protocol_version();
}

const std::vector<VersionInfo> &supported_versions()
{
    return default_registry()->supported_versions();
}

const std::vector<std::string_view> &known_methods()
{
    return kKnownMethods;
}

const std::vector<MethodInfo> &methods(std::string_view version)
{
    if (const auto selected = default_registry()->backend(version); selected != nullptr)
    {
        return selected->methods();
    }
    return kEmptyMethods;
}

const std::vector<std::string_view> &definitions(std::string_view version)
{
    if (const auto selected = default_registry()->backend(version); selected != nullptr)
    {
        return selected->definitions();
    }
    return kEmptyDefinitions;
}

bool is_supported_protocol_version(std::string_view version)
{
    return default_registry()->is_supported_protocol_version(version);
}

bool has_definition(std::string_view version, std::string_view definition)
{
    if (const auto selected = default_registry()->backend(version); selected != nullptr)
    {
        return selected->has_definition(definition);
    }
    return false;
}

std::optional<MethodInfo> method(std::string_view version, std::string_view method)
{
    if (const auto selected = default_registry()->backend(version); selected != nullptr)
    {
        return selected->method(method);
    }
    return std::nullopt;
}
} // namespace yaaf::mcp::schema
