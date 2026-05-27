#include "mcp_client.h"

#include "mcp_client_stdio.h"
#include "mcp_schema_generated.h"

#include <cctype>
#include <chrono>
#include <map>

namespace yaaf::mcp
{
namespace
{
[[nodiscard]] std::string as_string(const nlohmann::json &value, std::string_view fallback = {})
{
    return value.is_string() ? value.get<std::string>() : std::string(fallback);
}

[[nodiscard]] bool equals_ignore_case(std::string_view left, std::string_view right)
{
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char left_char, char right_char) {
               return std::tolower(static_cast<unsigned char>(left_char)) ==
                      std::tolower(static_cast<unsigned char>(right_char));
           });
}

[[nodiscard]] std::optional<std::string> response_header(const HttpClient::Response &response, std::string_view name)
{
    for (const auto &[header_name, header_value] : response.headers)
    {
        if (equals_ignore_case(header_name, name))
        {
            return header_value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string sse_data_payload(std::string_view body)
{
    std::string payload;
    std::size_t position = 0;
    while (position < body.size())
    {
        auto end = body.find('\n', position);
        if (end == std::string_view::npos)
        {
            end = body.size();
        }
        auto line = body.substr(position, end - position);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (line.rfind("data:", 0) == 0)
        {
            auto data = line.substr(5);
            if (!data.empty() && data.front() == ' ')
            {
                data.remove_prefix(1);
            }
            if (!payload.empty())
            {
                payload += '\n';
            }
            payload += data;
        }
        position = end + 1;
    }
    return payload;
}

[[nodiscard]] std::string replace_all(std::string value, std::string_view needle, std::string_view replacement)
{
    std::size_t position = 0;
    while ((position = value.find(needle, position)) != std::string::npos)
    {
        value.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
    return value;
}

[[nodiscard]] std::string getenv_string(const std::string &name)
{
    const char *value = yaaf::platform::safe_getenv(name.c_str());
    return value != nullptr ? std::string(value) : std::string{};
}

[[nodiscard]] std::string expand_variables(std::string value, const std::filesystem::path &workspace_root,
                                           std::vector<std::string> &diagnostics)
{
    value = replace_all(std::move(value), "${workspaceFolder}", workspace_root.generic_string());

    std::size_t position = 0;
    while ((position = value.find("${env:", position)) != std::string::npos)
    {
        const auto end = value.find('}', position);
        if (end == std::string::npos)
        {
            diagnostics.push_back("unterminated ${env:...} variable");
            break;
        }

        const auto name = value.substr(position + 6, end - position - 6);
        value.replace(position, end - position + 1, getenv_string(name));
    }

    position = 0;
    while ((position = value.find("${input:", position)) != std::string::npos)
    {
        const auto end = value.find('}', position);
        if (end == std::string::npos)
        {
            diagnostics.push_back("unsupported variable ${input:...}");
            break;
        }

        diagnostics.push_back("unsupported variable ${input:...}");
        position = end + 1;
    }

    return value;
}

[[nodiscard]] nlohmann::json expand_json(const nlohmann::json &value, const std::filesystem::path &workspace_root,
                                         std::vector<std::string> &diagnostics)
{
    if (value.is_string())
    {
        return expand_variables(value.get<std::string>(), workspace_root, diagnostics);
    }

    if (value.is_array())
    {
        auto result = nlohmann::json::array();
        for (const auto &entry : value)
        {
            result.push_back(expand_json(entry, workspace_root, diagnostics));
        }
        return result;
    }

    if (value.is_object())
    {
        auto result = nlohmann::json::object();
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            result[it.key()] = expand_json(it.value(), workspace_root, diagnostics);
        }
        return result;
    }

    return value;
}

[[nodiscard]] Headers read_headers(const nlohmann::json &raw)
{
    Headers headers;
    const auto header_entry = raw.find("headers");
    if (header_entry == raw.end() || !header_entry->is_object())
    {
        return headers;
    }

    for (auto it = header_entry->begin(); it != header_entry->end(); ++it)
    {
        if (it.value().is_string())
        {
            headers.emplace_back(it.key(), it.value().get<std::string>());
        }
    }
    return headers;
}

[[nodiscard]] nlohmann::json redact_config(const nlohmann::json &raw)
{
    auto redacted = raw;
    if (auto headers = redacted.find("headers"); headers != redacted.end() && headers->is_object())
    {
        for (auto it = headers->begin(); it != headers->end(); ++it)
        {
            it.value() = "<redacted>";
        }
    }
    if (auto env = redacted.find("env"); env != redacted.end() && env->is_object())
    {
        for (auto it = env->begin(); it != env->end(); ++it)
        {
            it.value() = "<redacted>";
        }
    }
    return redacted;
}

[[nodiscard]] nlohmann::json make_request(std::int64_t id, std::string method, nlohmann::json params)
{
    nlohmann::json request = {{"jsonrpc", "2.0"}, {"id", id}, {"method", std::move(method)}};
    if (!params.is_null())
    {
        request["params"] = std::move(params);
    }
    return request;
}

[[nodiscard]] nlohmann::json make_notification(std::string method)
{
    return nlohmann::json{{"jsonrpc", "2.0"}, {"method", std::move(method)}};
}

class Transport
{
  public:
    virtual ~Transport() = default;
    virtual nlohmann::json request(const nlohmann::json &message) = 0;
    virtual void notify(const nlohmann::json &message) = 0;
};

class HttpTransport final : public Transport
{
  public:
    HttpTransport(std::string url, Headers headers, HttpClient::Options options, HttpPost post)
        : url_(std::move(url)), headers_(std::move(headers)), options_(std::move(options)), post_(std::move(post))
    {
        headers_.emplace_back("Accept", "application/json, text/event-stream");
    }

    nlohmann::json request(const nlohmann::json &message) override
    {
        const auto response = post(message.dump());
        if (response.status_code < 200 || response.status_code >= 300)
        {
            throw std::runtime_error(fmt::format("MCP HTTP request failed with status {}", response.status_code));
        }
        if (response.body.empty())
        {
            throw std::runtime_error("MCP HTTP request returned an empty body");
        }
        if (const auto session_id = response_header(response, "Mcp-Session-Id"); session_id.has_value())
        {
            session_id_ = *session_id;
        }
        if (response.content_type.find("text/event-stream") != std::string::npos)
        {
            const auto payload = sse_data_payload(response.body);
            if (payload.empty())
            {
                throw std::runtime_error("MCP SSE response did not contain data");
            }
            return nlohmann::json::parse(payload);
        }
        return nlohmann::json::parse(response.body);
    }

    void notify(const nlohmann::json &message) override
    {
        const auto response = post(message.dump());
        if (response.status_code < 200 || response.status_code >= 300)
        {
            throw std::runtime_error(fmt::format("MCP HTTP notification failed with status {}", response.status_code));
        }
    }

    void set_protocol_version(const std::string &version)
    {
        protocol_version_ = version;
    }

    void set_session_id(const std::string &session_id)
    {
        session_id_ = session_id;
    }

  private:
    [[nodiscard]] HttpClient::Response post(std::string body)
    {
        Headers headers = headers_;
        if (!protocol_version_.empty())
        {
            headers.emplace_back("MCP-Protocol-Version", protocol_version_);
        }
        if (!session_id_.empty())
        {
            headers.emplace_back("Mcp-Session-Id", session_id_);
        }

        if (post_)
        {
            return post_(url_, body, "application/json", headers);
        }

        HttpClient client{options_};
        HttpClient::Headers http_headers(headers.begin(), headers.end());
        return client.post(url_, body, "application/json", http_headers);
    }

    std::string url_;
    Headers headers_;
    HttpClient::Options options_;
    HttpPost post_;
    std::string protocol_version_;
    std::string session_id_;
};

class StdioTransport final : public Transport
{
  public:
    StdioTransport(const nlohmann::json &raw, StdioProcessFactory factory)
        : process_(factory ? factory(raw) : detail::start_stdio_server(raw))
    {
    }

    nlohmann::json request(const nlohmann::json &message) override
    {
        write_message(message);
        const auto expected_id = message.at("id");
        while (true)
        {
            auto response = read_message();
            if (response.contains("id") && response.at("id") == expected_id)
            {
                return response;
            }
        }
    }

    void notify(const nlohmann::json &message) override
    {
        write_message(message);
    }

  private:
    void write_message(const nlohmann::json &message)
    {
        process_->write_message(message.dump() + "\n");
    }

    [[nodiscard]] nlohmann::json read_message()
    {
        return process_->read_message(std::chrono::seconds(30));
    }

    std::unique_ptr<detail::StdioPlatformProcess> process_;
};

[[nodiscard]] nlohmann::json response_result(const nlohmann::json &response)
{
    if (response.contains("error"))
    {
        const auto &error = response.at("error");
        throw std::runtime_error(as_string(error.value("message", nlohmann::json{}), "MCP protocol error"));
    }
    if (!response.contains("result"))
    {
        throw std::runtime_error("MCP response did not contain result");
    }
    return response.at("result");
}

[[nodiscard]] std::string content_to_text(const nlohmann::json &result)
{
    std::vector<std::string> parts;
    if (const auto content = result.find("content"); content != result.end() && content->is_array())
    {
        for (const auto &entry : *content)
        {
            const auto type = entry.find("type");
            if (type != entry.end() && type->is_string() && *type == "text" && entry.contains("text") &&
                entry.at("text").is_string())
            {
                parts.push_back(entry.at("text").get<std::string>());
            }
            else if (entry.is_object())
            {
                parts.push_back(entry.dump());
            }
        }
    }
    if (parts.empty() && result.contains("structuredContent"))
    {
        return result.at("structuredContent").dump();
    }
    std::string joined;
    for (const auto &part : parts)
    {
        if (!joined.empty())
        {
            joined += '\n';
        }
        joined += part;
    }
    return joined;
}

[[nodiscard]] nlohmann::json tool_to_json(const ToolDescriptor &tool)
{
    return nlohmann::json{{"server_id", tool.server_id},
                          {"name", tool.name},
                          {"local_name", tool.local_name},
                          {"title", tool.title},
                          {"description", tool.description},
                          {"inputSchema", tool.input_schema},
                          {"outputSchema", tool.output_schema},
                          {"annotations", tool.annotations}};
}

[[nodiscard]] std::filesystem::path resolve_config_path(const std::filesystem::path &workspace_root,
                                                        const std::filesystem::path &config_path)
{
    if (config_path.is_absolute())
    {
        return config_path;
    }
    return workspace_root / config_path;
}

[[nodiscard]] bool has_unsupported_input_variable(const std::vector<std::string> &diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const std::string &diagnostic) { return diagnostic.find("${input:") != std::string::npos; });
}
} // namespace

Config load_config(const std::filesystem::path &workspace_root)
{
    return load_config(workspace_root, {});
}

Config load_config(const std::filesystem::path &workspace_root, const std::filesystem::path &config_path)
{
    Config config;
    if (config_path.empty())
    {
        return config;
    }

    config.path = resolve_config_path(workspace_root, config_path);
    if (!std::filesystem::exists(config.path))
    {
        config.diagnostics.push_back("MCP config not found");
        return config;
    }

    config.exists = true;
    std::ifstream input{config.path};
    if (!input)
    {
        config.diagnostics.push_back("MCP config could not be opened");
        return config;
    }

    nlohmann::json payload;
    try
    {
        input >> payload;
    }
    catch (const std::exception &error)
    {
        config.diagnostics.push_back(fmt::format("MCP config parse failed: {}", error.what()));
        return config;
    }

    const auto servers = payload.find("servers");
    if (servers == payload.end() || !servers->is_object())
    {
        config.diagnostics.push_back("MCP config must contain a servers object");
        return config;
    }

    for (auto it = servers->begin(); it != servers->end(); ++it)
    {
        ServerConfig server;
        server.id = it.key();
        if (!it.value().is_object())
        {
            server.diagnostics.push_back("server config must be an object");
            config.servers.push_back(std::move(server));
            continue;
        }

        server.raw = expand_json(it.value(), workspace_root, server.diagnostics);
        server.type = as_string(server.raw.value("type", nlohmann::json{}), "stdio");
        if (server.type == "http" || server.type == "sse" || server.type == "stdio")
        {
            server.supported = true;
        }
        else
        {
            server.diagnostics.push_back(fmt::format("unsupported MCP server type '{}'", server.type));
        }

        if ((server.type == "http" || server.type == "sse") &&
            as_string(server.raw.value("url", nlohmann::json{})).empty())
        {
            server.supported = false;
            server.diagnostics.push_back("HTTP MCP server requires url");
        }
        if (server.type == "stdio" && as_string(server.raw.value("command", nlohmann::json{})).empty())
        {
            server.supported = false;
            server.diagnostics.push_back("stdio MCP server requires command");
        }
        if (has_unsupported_input_variable(server.diagnostics))
        {
            server.supported = false;
        }
        config.servers.push_back(std::move(server));
    }

    std::sort(config.servers.begin(), config.servers.end(),
              [](const ServerConfig &left, const ServerConfig &right) { return left.id < right.id; });
    return config;
}

nlohmann::json config_to_json(const Config &config)
{
    auto protocol_versions = nlohmann::json::array();
    for (const auto &version : schema::supported_versions())
    {
        protocol_versions.push_back({{"version", version.version},
                                     {"schema", version.schema_path},
                                     {"definitions", version.definition_count},
                                     {"methods", version.method_count}});
    }

    auto payload =
        nlohmann::json{{"path", config.path.generic_string()},
                       {"exists", config.exists},
                       {"diagnostics", config.diagnostics},
                       {"protocol", {{"latest", schema::latest_protocol_version()}, {"supported", protocol_versions}}},
                       {"servers", nlohmann::json::array()}};
    for (const auto &server : config.servers)
    {
        payload["servers"].push_back({{"id", server.id},
                                      {"type", server.type},
                                      {"supported", server.supported},
                                      {"diagnostics", server.diagnostics},
                                      {"config", redact_config(server.raw)}});
    }
    return payload;
}

class Client::Impl
{
  public:
    explicit Impl(ClientOptions options)
        : options_(std::move(options)),
          registry_(options_.schema_registry ? options_.schema_registry : schema::default_registry()),
          config_(load_config(options_.workspace_root, options_.config_path))
    {
    }

    [[nodiscard]] Config config() const
    {
        return config_;
    }

    [[nodiscard]] nlohmann::json diagnose_servers()
    {
        auto payload = nlohmann::json::array();
        for (const auto &server : config_.servers)
        {
            auto entry = nlohmann::json{{"id", server.id},
                                        {"type", server.type},
                                        {"supported", server.supported},
                                        {"initialize",
                                         {{"status", "failed"},
                                          {"error", ""},
                                          {"protocol_version", ""},
                                          {"server_info", nlohmann::json::object()}}},
                                        {"tools",
                                         {{"status", "failed"},
                                          {"error", ""},
                                          {"count", 0},
                                          {"names", nlohmann::json::array()},
                                          {"discovered", nlohmann::json::array()}}}};

            try
            {
                auto &session = ensure_session(server.id);
                entry["initialize"]["status"] = "ok";
                entry["initialize"]["protocol_version"] = session.protocol_version;
                entry["initialize"]["server_info"] = session.server_info;

                try
                {
                    const auto tools = list_tools(server.id);
                    auto names = nlohmann::json::array();
                    auto discovered = nlohmann::json::array();
                    for (const auto &tool : tools)
                    {
                        names.push_back(tool.local_name);
                        discovered.push_back(tool_to_json(tool));
                    }
                    entry["tools"]["status"] = "ok";
                    entry["tools"]["count"] = tools.size();
                    entry["tools"]["names"] = std::move(names);
                    entry["tools"]["discovered"] = std::move(discovered);
                }
                catch (const std::exception &error)
                {
                    entry["tools"]["error"] = error.what();
                }
            }
            catch (const std::exception &error)
            {
                entry["initialize"]["error"] = error.what();
                entry["tools"]["error"] = fmt::format("initialize failed: {}", error.what());
            }

            payload.push_back(std::move(entry));
        }
        return payload;
    }

    [[nodiscard]] std::vector<ToolDescriptor> list_tools(const std::string &server_id)
    {
        auto &session = ensure_session(server_id);
        require_method(session, "tools/list");
        std::vector<ToolDescriptor> tools;
        std::optional<std::string> cursor;
        do
        {
            nlohmann::json params = nlohmann::json::object();
            if (cursor.has_value())
            {
                params["cursor"] = *cursor;
            }
            const auto result =
                response_result(session.transport->request(make_request(next_id_++, "tools/list", params)));
            if (const auto entries = result.find("tools"); entries != result.end() && entries->is_array())
            {
                for (const auto &entry : *entries)
                {
                    if (!entry.is_object() || !entry.contains("name") || !entry.at("name").is_string())
                    {
                        continue;
                    }
                    ToolDescriptor tool;
                    tool.server_id = server_id;
                    tool.name = entry.at("name").get<std::string>();
                    tool.local_name = server_id + "." + tool.name;
                    tool.title = as_string(entry.value("title", nlohmann::json{}));
                    tool.description = as_string(entry.value("description", nlohmann::json{}));
                    tool.input_schema = entry.value("inputSchema", nlohmann::json::object());
                    tool.output_schema = entry.value("outputSchema", nlohmann::json::object());
                    tool.annotations = entry.value("annotations", nlohmann::json::object());
                    tools.push_back(std::move(tool));
                }
            }
            cursor.reset();
            if (auto next = result.find("nextCursor"); next != result.end() && next->is_string() && !next->empty())
            {
                cursor = next->get<std::string>();
            }
        } while (cursor.has_value());
        return tools;
    }

    [[nodiscard]] ToolResult call_tool(const std::string &server_id, const std::string &tool_name,
                                       const nlohmann::json &arguments)
    {
        auto &session = ensure_session(server_id);
        require_method(session, "tools/call");
        ToolResult result;
        result.tool_name = server_id + "." + tool_name;
        result.metadata = {{"server", server_id}, {"mcp_tool", tool_name}};

        try
        {
            const auto response = response_result(session.transport->request(
                make_request(next_id_++, "tools/call", nlohmann::json{{"name", tool_name}, {"arguments", arguments}})));
            result.success = !response.value("isError", false);
            result.content = content_to_text(response);
            result.metadata["raw"] = response;
        }
        catch (const std::exception &error)
        {
            result.success = false;
            result.content = fmt::format("MCP tool failed: {}", error.what());
            result.metadata["error"] = error.what();
        }
        return result;
    }

  private:
    struct Session
    {
        std::unique_ptr<Transport> transport;
        std::shared_ptr<const schema::Backend> schema_backend;
        std::string protocol_version;
        nlohmann::json server_info = nlohmann::json::object();
    };

    static void require_method(const Session &session, std::string_view method)
    {
        if (session.schema_backend == nullptr || !session.schema_backend->method(method).has_value())
        {
            const auto version = session.schema_backend != nullptr ? session.schema_backend->info().version : "unknown";
            throw std::runtime_error(fmt::format("MCP protocol {} does not define method {}", version, method));
        }
    }

    [[nodiscard]] const ServerConfig &server_config(const std::string &server_id) const
    {
        const auto found = std::find_if(config_.servers.begin(), config_.servers.end(),
                                        [&](const ServerConfig &server) { return server.id == server_id; });
        if (found == config_.servers.end())
        {
            throw std::runtime_error(fmt::format("unknown MCP server: {}", server_id));
        }
        if (!found->supported)
        {
            throw std::runtime_error(fmt::format("unsupported MCP server: {}", server_id));
        }
        return *found;
    }

    [[nodiscard]] Session &ensure_session(const std::string &server_id)
    {
        if (auto found = sessions_.find(server_id); found != sessions_.end())
        {
            return found->second;
        }

        const auto &server = server_config(server_id);
        Session session;
        if (server.type == "http" || server.type == "sse")
        {
            auto transport =
                std::make_unique<HttpTransport>(as_string(server.raw.value("url", nlohmann::json{})),
                                                read_headers(server.raw), options_.http, options_.http_post);
            const auto initialize = response_result(transport->request(make_request(
                next_id_++, "initialize",
                nlohmann::json{{"protocolVersion", std::string(registry_->latest_protocol_version())},
                               {"capabilities", nlohmann::json::object()},
                               {"clientInfo", nlohmann::json{{"name", "yaaf"}, {"version", "0.1.0"}}}})));
            const auto protocol_version =
                as_string(initialize.value("protocolVersion", nlohmann::json{}), registry_->latest_protocol_version());
            session.schema_backend = registry_->backend(protocol_version);
            if (session.schema_backend == nullptr)
            {
                throw std::runtime_error(fmt::format("unsupported MCP protocol version: {}", protocol_version));
            }
            session.protocol_version = protocol_version;
            session.server_info = initialize.value("serverInfo", nlohmann::json::object());
            transport->set_protocol_version(protocol_version);
            transport->notify(make_notification("notifications/initialized"));
            session.transport = std::move(transport);
        }
        else if (server.type == "stdio")
        {
            session.transport = std::make_unique<StdioTransport>(server.raw, options_.stdio_process_factory);
            const auto initialize = response_result(session.transport->request(make_request(
                next_id_++, "initialize",
                nlohmann::json{{"protocolVersion", std::string(registry_->latest_protocol_version())},
                               {"capabilities", nlohmann::json::object()},
                               {"clientInfo", nlohmann::json{{"name", "yaaf"}, {"version", "0.1.0"}}}})));
            const auto protocol_version =
                as_string(initialize.value("protocolVersion", nlohmann::json{}), registry_->latest_protocol_version());
            session.schema_backend = registry_->backend(protocol_version);
            if (session.schema_backend == nullptr)
            {
                throw std::runtime_error(fmt::format("unsupported MCP protocol version: {}", protocol_version));
            }
            session.protocol_version = protocol_version;
            session.server_info = initialize.value("serverInfo", nlohmann::json::object());
            session.transport->notify(make_notification("notifications/initialized"));
        }

        auto [inserted, _] = sessions_.emplace(server_id, std::move(session));
        return inserted->second;
    }

    ClientOptions options_;
    std::shared_ptr<const schema::Registry> registry_;
    Config config_;
    std::map<std::string, Session> sessions_;
    std::int64_t next_id_ = 1;
};

Client::Client(ClientOptions options) : impl_(std::make_shared<Impl>(std::move(options)))
{
}

Config Client::config() const
{
    return impl_->config();
}

nlohmann::json Client::diagnose_servers()
{
    return impl_->diagnose_servers();
}

std::vector<ToolDescriptor> Client::list_tools(const std::string &server_id)
{
    return impl_->list_tools(server_id);
}

ToolResult Client::call_tool(const std::string &server_id, const std::string &tool_name,
                             const nlohmann::json &arguments)
{
    return impl_->call_tool(server_id, tool_name, arguments);
}
} // namespace yaaf::mcp




