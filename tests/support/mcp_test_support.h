#pragma once

#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/config/dotenv.h"
#include "../../libyaaf/mcp/mcp_client.h"
#include "../../libyaaf/mcp/mcp_client_stdio.h"
#include "../../libyaaf/mcp/mcp_schema_generated.h"
#include "runtime_test_environment.h"

namespace yaaf::tests::mcp
{
[[nodiscard]] inline std::filesystem::path workspace_mcp_config_path(const std::filesystem::path &workspace)
{
    return workspace / ".yaaf" / "mcp.json";
}

class CurrentPathGuard
{
  public:
    explicit CurrentPathGuard(std::filesystem::path next) : previous_(std::filesystem::current_path())
    {
        std::filesystem::current_path(std::move(next));
    }

    ~CurrentPathGuard()
    {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
    }

  private:
    std::filesystem::path previous_;
};

class TemporaryMcpConfig
{
  public:
    TemporaryMcpConfig(std::filesystem::path workspace, const nlohmann::json &config)
        : path_(workspace_mcp_config_path(std::move(workspace)))
    {
        if (std::filesystem::exists(path_))
        {
            had_original_ = true;
            std::ifstream input{path_};
            original_.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        std::filesystem::create_directories(path_.parent_path());
        std::ofstream output{path_};
        output << config.dump(2);
    }

    ~TemporaryMcpConfig()
    {
        if (had_original_)
        {
            std::ofstream output{path_};
            output << original_;
        }
        else
        {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }

  private:
    std::filesystem::path path_;
    std::string original_;
    bool had_original_ = false;
};

[[nodiscard]] inline std::filesystem::path make_workspace(std::string_view name)
{
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path / ".yaaf");
    return path;
}

inline void write_mcp_config(const std::filesystem::path &workspace, const nlohmann::json &config)
{
    std::ofstream output{workspace_mcp_config_path(workspace)};
    output << config.dump(2);
}

[[nodiscard]] inline std::filesystem::path write_lua_script(const std::filesystem::path &workspace,
                                                            std::string_view body)
{
    const auto script_path = workspace / "script.lua";
    const auto lua_path = yaaf::tests::lua_root();
    const auto package_path =
        (lua_path / "?.lua").generic_string() + ";" + (lua_path / "?" / "init.lua").generic_string() + ";";

    std::ofstream script{script_path};
    script << "package.path = " << nlohmann::json(package_path).dump() << " .. package.path\n";
    script << body;
    return script_path;
}

[[nodiscard]] inline HttpClient::Response json_response(const nlohmann::json &payload)
{
    return HttpClient::Response{200, "application/json", payload.dump(), {}};
}

[[nodiscard]] inline HttpClient::Response sse_response(const nlohmann::json &payload, yaaf::mcp::Headers headers = {})
{
    HttpClient::Response response;
    response.status_code = 200;
    response.content_type = "text/event-stream";
    response.body = "event: message\ndata: " + payload.dump() + "\n\n";
    response.headers = std::move(headers);
    return response;
}

[[nodiscard]] inline std::filesystem::path repository_root()
{
    return yaaf::tests::repository_root();
}

[[nodiscard]] inline std::optional<std::string> environment_value(std::string_view name)
{
    const auto value = std::getenv(std::string(name).c_str());
    if (value == nullptr || std::string_view(value).empty())
    {
        return std::nullopt;
    }
    return std::string(value);
}

[[nodiscard]] inline std::vector<std::string> split_paths(std::string_view value)
{
    std::vector<std::string> paths;
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif

    std::size_t start = 0;
    while (start <= value.size())
    {
        const auto end = value.find(separator, start);
        paths.emplace_back(value.substr(start, end == std::string_view::npos ? value.size() - start : end - start));
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return paths;
}

[[nodiscard]] inline std::vector<std::string> executable_extensions()
{
#ifdef _WIN32
    const auto pathext = environment_value("PATHEXT").value_or(".COM;.EXE;.BAT;.CMD");
    auto extensions = split_paths(pathext);
    extensions.emplace_back("");
    return extensions;
#else
    return {""};
#endif
}

[[nodiscard]] inline bool executable_on_path(std::string_view name)
{
    auto path_value = environment_value("PATH");
    if (!path_value.has_value())
    {
        path_value = environment_value("Path");
    }
    if (!path_value.has_value())
    {
        return false;
    }

    for (const auto &directory : split_paths(*path_value))
    {
        if (directory.empty())
        {
            continue;
        }
        for (const auto &extension : executable_extensions())
        {
            if (std::filesystem::exists(std::filesystem::path(directory) / (std::string(name) + extension)))
            {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] inline nlohmann::json uv_stdio_server_config(const std::filesystem::path &root, std::string script)
{
    return nlohmann::json{{"type", "stdio"},
                          {"command", "uv"},
                          {"args", nlohmann::json::array({"--directory", (root / "mcp-servers").generic_string(), "run",
                                                          "python", script})}};
}

[[nodiscard]] inline nlohmann::json scripted_stdio_server_config(const nlohmann::json &fixture = nlohmann::json{
                                                                     {"kind", "hello"}})
{
    return nlohmann::json{{"type", "stdio"}, {"command", "yaaf-scripted-mcp"}, {"fixture", fixture}};
}

enum class ScriptedHttpTransport
{
    Json,
    Sse,
};

[[nodiscard]] inline std::string scripted_repeat_text(std::string_view text, int count)
{
    std::string joined;
    for (int index = 0; index < count; ++index)
    {
        if (!joined.empty())
        {
            joined += ' ';
        }
        joined += text;
    }
    return joined;
}

class ScriptedStdioProcess final : public yaaf::mcp::detail::StdioPlatformProcess
{
  public:
    explicit ScriptedStdioProcess(nlohmann::json raw)
        : raw_(std::move(raw)), environment_(yaaf::mcp::detail::read_environment_overrides(raw_))
    {
    }

    void write_message(std::string_view line) override
    {
        const auto message = nlohmann::json::parse(line);
        const auto method = message.value("method", std::string{});
        if (!message.contains("id"))
        {
            return;
        }

        if (method == "initialize")
        {
            const auto params = message.value("params", nlohmann::json::object());
            nlohmann::json result;
            result["protocolVersion"] = params.value("protocolVersion", std::string{});
            result["capabilities"]["tools"] = nlohmann::json::object();
            result["serverInfo"] = {{"name", server_name()}, {"version", "1"}};
            push_result(message.at("id"), std::move(result));
            return;
        }

        if (method == "tools/list")
        {
            nlohmann::json result;
            result["tools"] = tools();
            push_result(message.at("id"), std::move(result));
            return;
        }

        if (method == "tools/call")
        {
            const auto params = message.value("params", nlohmann::json::object());
            push_result(message.at("id"), call_result(params.value("name", std::string{}),
                                                      params.value("arguments", nlohmann::json::object())));
            return;
        }

        push_error(message.at("id"), method.empty() ? "unknown" : method);
    }

    [[nodiscard]] nlohmann::json read_message(std::chrono::milliseconds) override
    {
        if (responses_.empty())
        {
            throw std::runtime_error("scripted MCP fixture has no pending response");
        }

        auto response = std::move(responses_.front());
        responses_.erase(responses_.begin());
        return response;
    }

  private:
    [[nodiscard]] std::string fixture_kind() const
    {
        if (const auto fixture = raw_.find("fixture"); fixture != raw_.end() && fixture->is_object())
        {
            if (const auto kind = fixture->find("kind"); kind != fixture->end() && kind->is_string())
            {
                return kind->get<std::string>();
            }
        }
        return "hello";
    }

    [[nodiscard]] std::string server_name() const
    {
        return fixture_kind() == "env" ? "env-stdio" : "hello-stdio";
    }

    [[nodiscard]] std::string environment_override(std::string_view key) const
    {
        if (const auto found = environment_.find(std::string(key)); found != environment_.end())
        {
            return found->second;
        }
        return {};
    }

    [[nodiscard]] nlohmann::json tools() const
    {
        nlohmann::json entries = nlohmann::json::array();
        if (fixture_kind() == "env")
        {
            nlohmann::json tool;
            tool["name"] = "env_values";
            tool["description"] = "Return selected environment values.";
            tool["inputSchema"] = {{"type", "object"}};
            entries.push_back(std::move(tool));
            return entries;
        }

        if (fixture_kind() == "error")
        {
            nlohmann::json tool;
            tool["name"] = "fail";
            tool["description"] = "Return a scripted MCP error result.";
            tool["inputSchema"] = {{"type", "object"}, {"properties", {{"reason", {{"type", "string"}}}}}};
            entries.push_back(std::move(tool));
            return entries;
        }

        nlohmann::json hello;
        hello["name"] = "hello";
        hello["description"] = "Return a greeting.";
        hello["inputSchema"] = {{"type", "object"}, {"properties", {{"name", {{"type", "string"}}}}}};
        entries.push_back(std::move(hello));

        nlohmann::json repeat;
        repeat["name"] = "repeat";
        repeat["description"] = "Repeat text multiple times.";
        repeat["inputSchema"] = {{"type", "object"},
                                 {"properties", {{"text", {{"type", "string"}}}, {"count", {{"type", "integer"}}}}}};
        entries.push_back(std::move(repeat));
        return entries;
    }

    [[nodiscard]] nlohmann::json call_result(const std::string &tool_name, const nlohmann::json &arguments) const
    {
        nlohmann::json result;
        result["content"] = nlohmann::json::array();
        result["isError"] = false;

        if (fixture_kind() == "env")
        {
            result["content"].push_back({{"type", "text"},
                                         {"text", environment_override("YAAF_MCP_ENV_FILE") + "|" +
                                                      environment_override("YAAF_MCP_ENV_INLINE")}});
            return result;
        }

        if (fixture_kind() == "error")
        {
            result["content"].push_back(
                {{"type", "text"}, {"text", arguments.value("reason", std::string{"scripted failure"})}});
            result["isError"] = true;
            return result;
        }

        if (tool_name == "hello")
        {
            result["content"].push_back(
                {{"type", "text"}, {"text", fmt::format("Hello, {}!", arguments.value("name", std::string{}))}});
            return result;
        }

        if (tool_name == "repeat")
        {
            result["content"].push_back(
                {{"type", "text"},
                 {"text", scripted_repeat_text(arguments.value("text", std::string{}), arguments.value("count", 0))}});
            return result;
        }

        throw std::runtime_error(fmt::format("unknown scripted MCP tool: {}", tool_name));
    }

    void push_result(const nlohmann::json &id, nlohmann::json result)
    {
        responses_.push_back(nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
    }

    void push_error(const nlohmann::json &id, std::string_view method)
    {
        responses_.push_back(nlohmann::json{
            {"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32601}, {"message", std::string(method)}}}});
    }

    nlohmann::json raw_;
    std::map<std::string, std::string> environment_;
    std::vector<nlohmann::json> responses_;
};

[[nodiscard]] inline yaaf::mcp::StdioProcessFactory scripted_stdio_process_factory()
{
    return [](const nlohmann::json &raw) { return std::make_unique<ScriptedStdioProcess>(raw); };
}

[[nodiscard]] inline HttpClient::Response scripted_http_response(const nlohmann::json &payload,
                                                                 ScriptedHttpTransport transport,
                                                                 yaaf::mcp::Headers headers = {})
{
    if (transport == ScriptedHttpTransport::Sse)
    {
        return sse_response(payload, std::move(headers));
    }

    auto response = json_response(payload);
    response.headers = std::move(headers);
    return response;
}

[[nodiscard]] inline yaaf::mcp::HttpPost hello_http_post(ScriptedHttpTransport transport)
{
    return [transport](std::string_view, std::string_view body, std::string_view, const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        const auto method = request.at("method").get<std::string>();
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{202, "", "", {}};
        }

        nlohmann::json payload;
        payload["jsonrpc"] = "2.0";
        payload["id"] = request.at("id");

        if (method == "initialize")
        {
            payload["result"]["protocolVersion"] = request.at("params").at("protocolVersion");
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            payload["result"]["serverInfo"] = {{"name", "hello-http"}, {"version", "1"}};
            return scripted_http_response(payload, transport, {{"Mcp-Session-Id", "scripted-session"}});
        }

        if (method == "tools/list")
        {
            payload["result"]["tools"] = nlohmann::json::array();
            payload["result"]["tools"].push_back({{"name", "hello"}, {"description", "Return a greeting."}});
            payload["result"]["tools"].push_back({{"name", "repeat"}, {"description", "Repeat text multiple times."}});
            return scripted_http_response(payload, transport);
        }

        if (method == "tools/call")
        {
            const auto params = request.at("params");
            const auto tool_name = params.at("name").get<std::string>();
            const auto arguments = params.value("arguments", nlohmann::json::object());
            payload["result"]["content"] = nlohmann::json::array();
            payload["result"]["isError"] = false;
            if (tool_name == "hello")
            {
                payload["result"]["content"].push_back(
                    {{"type", "text"}, {"text", fmt::format("Hello, {}!", arguments.value("name", std::string{}))}});
                return scripted_http_response(payload, transport);
            }
            if (tool_name == "repeat")
            {
                payload["result"]["content"].push_back(
                    {{"type", "text"},
                     {"text",
                      scripted_repeat_text(arguments.value("text", std::string{}), arguments.value("count", 0))}});
                return scripted_http_response(payload, transport);
            }
        }

        payload["error"]["code"] = -32601;
        payload["error"]["message"] = method;
        return scripted_http_response(payload, transport);
    };
}

inline void write_runtime_dotenv(const std::filesystem::path &workspace)
{
    const auto root_dotenv = repository_root() / ".env";
    if (std::filesystem::exists(root_dotenv))
    {
        std::filesystem::copy_file(root_dotenv, workspace / ".env", std::filesystem::copy_options::overwrite_existing);
        return;
    }

    std::ofstream output{workspace / ".env"};
    output << "YAAF_OLLAMA_ENDPOINT=http://127.0.0.1:11434\n";
    output << "YAAF_PROXY=http://127.0.0.1:18080\n";
}

[[nodiscard]] inline yaaf::dotenv::EnvironmentFile runtime_dotenv(const std::filesystem::path &workspace)
{
    const CurrentPathGuard guard{workspace};
    return yaaf::dotenv::EnvironmentFile::load_from_parents();
}

[[nodiscard]] inline std::string configured_mcp_url(const yaaf::dotenv::EnvironmentFile &dotenv, std::string_view key,
                                                    std::string fallback)
{
    if (const auto value = environment_value(key); value.has_value())
    {
        return *value;
    }
    return dotenv.get(std::string(key)).value_or(std::move(fallback));
}

[[nodiscard]] inline std::string health_url_for_mcp_url(std::string url)
{
    if (url.ends_with("/mcp"))
    {
        url.resize(url.size() - std::string_view("/mcp").size());
    }
    return url + "/health";
}

[[nodiscard]] inline bool http_fixture_available(std::string_view mcp_url)
{
    try
    {
        return HttpClient{yaaf::tests::runtime_http_options_for_url(mcp_url)}
                   .get(health_url_for_mcp_url(std::string(mcp_url)))
                   .status_code == 200;
    }
    catch (const std::exception &)
    {
        return false;
    }
}

inline void expect_hello_tools(yaaf::mcp::Client &client, std::string_view server_id)
{
    const auto tools = client.list_tools(std::string(server_id));
    ASSERT_EQ(tools.size(), 2U);
    EXPECT_EQ(tools[0].server_id, server_id);
    EXPECT_TRUE(std::any_of(tools.begin(), tools.end(), [](const auto &tool) { return tool.name == "hello"; }));
    EXPECT_TRUE(std::any_of(tools.begin(), tools.end(), [](const auto &tool) { return tool.name == "repeat"; }));

    const auto hello = client.call_tool(std::string(server_id), "hello", nlohmann::json{{"name", "MCP"}});
    EXPECT_TRUE(hello.success);
    EXPECT_EQ(hello.content, "Hello, MCP!");

    const auto repeat =
        client.call_tool(std::string(server_id), "repeat", nlohmann::json{{"text", "hi"}, {"count", 3}});
    EXPECT_TRUE(repeat.success);
    EXPECT_EQ(repeat.content, "hi hi hi");
}

class TestSchemaBackend final : public yaaf::mcp::schema::Backend
{
  public:
    TestSchemaBackend(std::string_view version, std::vector<yaaf::mcp::schema::MethodInfo> methods)
        : version_(version), info_{version_, "", "", 0, methods.size()}, methods_(std::move(methods))
    {
    }

    [[nodiscard]] const yaaf::mcp::schema::VersionInfo &info() const override
    {
        return info_;
    }

    [[nodiscard]] const std::vector<yaaf::mcp::schema::MethodInfo> &methods() const override
    {
        return methods_;
    }

    [[nodiscard]] const std::vector<std::string_view> &definitions() const override
    {
        return definitions_;
    }

    [[nodiscard]] bool has_definition(std::string_view definition) const override
    {
        return std::find(definitions_.begin(), definitions_.end(), definition) != definitions_.end();
    }

    [[nodiscard]] std::optional<yaaf::mcp::schema::MethodInfo> method(std::string_view method) const override
    {
        const auto found = std::find_if(methods_.begin(), methods_.end(),
                                        [method](const auto &entry) { return entry.method == method; });
        return found == methods_.end() ? std::nullopt : std::optional<yaaf::mcp::schema::MethodInfo>{*found};
    }

  private:
    std::string version_;
    yaaf::mcp::schema::VersionInfo info_;
    std::vector<yaaf::mcp::schema::MethodInfo> methods_;
    std::vector<std::string_view> definitions_;
};

class TestSchemaRegistry final : public yaaf::mcp::schema::Registry
{
  public:
    explicit TestSchemaRegistry(std::shared_ptr<const yaaf::mcp::schema::Backend> backend)
        : backend_(std::move(backend)), versions_{backend_->info()}
    {
    }

    [[nodiscard]] std::string_view latest_protocol_version() const override
    {
        return backend_->info().version;
    }

    [[nodiscard]] const std::vector<yaaf::mcp::schema::VersionInfo> &supported_versions() const override
    {
        return versions_;
    }

    [[nodiscard]] std::shared_ptr<const yaaf::mcp::schema::Backend> backend(std::string_view version) const override
    {
        return backend_->info().version == version ? backend_ : nullptr;
    }

    [[nodiscard]] bool is_supported_protocol_version(std::string_view version) const override
    {
        return backend_->info().version == version;
    }

  private:
    std::shared_ptr<const yaaf::mcp::schema::Backend> backend_;
    std::vector<yaaf::mcp::schema::VersionInfo> versions_;
};
} // namespace yaaf::tests::mcp
