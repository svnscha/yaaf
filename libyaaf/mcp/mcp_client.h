#pragma once

#include "../http/http_client.h"
#include "mcp_schema.h"

namespace yaaf::mcp
{
namespace detail
{
class StdioPlatformProcess;
}

using Headers = std::vector<std::pair<std::string, std::string>>;
using HttpPost = std::function<HttpClient::Response(std::string_view url, std::string_view body,
                                                    std::string_view content_type, const Headers &headers)>;
using StdioProcessFactory =
    std::function<std::unique_ptr<detail::StdioPlatformProcess>(const nlohmann::json &raw)>;

struct ServerConfig
{
    std::string id;
    std::string type;
    nlohmann::json raw = nlohmann::json::object();
    std::vector<std::string> diagnostics;
    bool supported = false;
};

struct Config
{
    std::filesystem::path path;
    bool exists = false;
    std::vector<ServerConfig> servers;
    std::vector<std::string> diagnostics;
};

struct ToolDescriptor
{
    std::string server_id;
    std::string name;
    std::string local_name;
    std::string title;
    std::string description;
    nlohmann::json input_schema = nlohmann::json::object();
    nlohmann::json output_schema = nlohmann::json::object();
    nlohmann::json annotations = nlohmann::json::object();
};

struct ToolResult
{
    std::string tool_name;
    std::string content;
    bool success = false;
    nlohmann::json metadata = nlohmann::json::object();
};

struct ClientOptions
{
    std::filesystem::path workspace_root;
    std::filesystem::path config_path;
    HttpClient::Options http;
    HttpPost http_post;
    StdioProcessFactory stdio_process_factory;
    std::shared_ptr<const schema::Registry> schema_registry;
};

[[nodiscard]] Config load_config(const std::filesystem::path &workspace_root);
[[nodiscard]] Config load_config(const std::filesystem::path &workspace_root, const std::filesystem::path &config_path);
[[nodiscard]] nlohmann::json config_to_json(const Config &config);

class Client
{
  public:
    explicit Client(ClientOptions options);
    [[nodiscard]] Config config() const;
    [[nodiscard]] nlohmann::json diagnose_servers();
    [[nodiscard]] std::vector<ToolDescriptor> list_tools(const std::string &server_id);
    [[nodiscard]] ToolResult call_tool(const std::string &server_id, const std::string &tool_name,
                                       const nlohmann::json &arguments);

  private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace yaaf::mcp
