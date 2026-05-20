#pragma once

namespace yaaf::mcp::schema
{
enum class MessageKind
{
    request,
    notification,
};

struct VersionInfo
{
    std::string_view version;
    std::string_view schema_url;
    std::string_view schema_path;
    std::size_t definition_count = 0;
    std::size_t method_count = 0;
};

struct MethodInfo
{
    std::string_view method;
    std::string_view definition;
    MessageKind kind = MessageKind::request;
};

class Backend
{
  public:
    virtual ~Backend() = default;

    [[nodiscard]] virtual const VersionInfo &info() const = 0;
    [[nodiscard]] virtual const std::vector<MethodInfo> &methods() const = 0;
    [[nodiscard]] virtual const std::vector<std::string_view> &definitions() const = 0;
    [[nodiscard]] virtual bool has_definition(std::string_view definition) const = 0;
    [[nodiscard]] virtual std::optional<MethodInfo> method(std::string_view method) const = 0;
};

class Registry
{
  public:
    virtual ~Registry() = default;

    [[nodiscard]] virtual std::string_view latest_protocol_version() const = 0;
    [[nodiscard]] virtual const std::vector<VersionInfo> &supported_versions() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const Backend> backend(std::string_view version) const = 0;
    [[nodiscard]] virtual bool is_supported_protocol_version(std::string_view version) const = 0;
};

class BackendFactory
{
  public:
    virtual ~BackendFactory() = default;

    [[nodiscard]] virtual std::shared_ptr<const Backend> create(std::string_view version) const = 0;
    [[nodiscard]] virtual std::shared_ptr<const Backend> create_latest() const = 0;
    [[nodiscard]] virtual std::shared_ptr<const Registry> create_registry() const = 0;
};

[[nodiscard]] const std::shared_ptr<const Registry> &default_registry();
[[nodiscard]] std::shared_ptr<const BackendFactory> default_factory();
[[nodiscard]] std::string_view latest_protocol_version();
[[nodiscard]] const std::vector<VersionInfo> &supported_versions();
[[nodiscard]] const std::vector<std::string_view> &known_methods();
[[nodiscard]] const std::vector<MethodInfo> &methods(std::string_view version);
[[nodiscard]] const std::vector<std::string_view> &definitions(std::string_view version);
[[nodiscard]] bool is_supported_protocol_version(std::string_view version);
[[nodiscard]] bool has_definition(std::string_view version, std::string_view definition);
[[nodiscard]] std::optional<MethodInfo> method(std::string_view version, std::string_view method);
} // namespace yaaf::mcp::schema