#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/cli/cli.h"

namespace
{
class ScopedEnvironmentVariable
{
  public:
    ScopedEnvironmentVariable(std::string name, std::string value) : name_(std::move(name))
    {
        if (const auto *current = std::getenv(name_.c_str()); current != nullptr)
        {
            original_ = current;
        }
        set(value);
    }

    ~ScopedEnvironmentVariable()
    {
        if (original_.has_value())
        {
            set(*original_);
        }
        else
        {
            unset();
        }
    }

  private:
    void set(const std::string &value) const
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    void unset() const
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        unsetenv(name_.c_str());
#endif
    }

    std::string name_;
    std::optional<std::string> original_;
};

nlohmann::json parse_json_output(const std::ostringstream &output)
{
    return nlohmann::json::parse(output.str());
}

[[nodiscard]] std::filesystem::path make_test_directory(std::string_view name)
{
    const auto path = std::filesystem::temp_directory_path() / fmt::format("yaaf-{}-{}", name, std::rand());
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}
} // namespace

TEST(CliTests, RootHelpOmitsOllamaSubcommandOptions)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"--help"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("ask"), std::string::npos);
    EXPECT_NE(output.str().find("doctor"), std::string::npos);
    EXPECT_EQ(output.str().find("example"), std::string::npos);
    EXPECT_NE(output.str().find("--proxy"), std::string::npos);
    EXPECT_NE(output.str().find("--pretty"), std::string::npos);
    EXPECT_EQ(output.str().find("--endpoint"), std::string::npos);
    EXPECT_EQ(output.str().find("--model"), std::string::npos);
    EXPECT_EQ(output.str().find("--stream"), std::string::npos);
    EXPECT_EQ(output.str().find("--think"), std::string::npos);
    EXPECT_EQ(output.str().find("--format"), std::string::npos);
}

TEST(CliTests, GlobalProxyOptionCanBeUsedWithHttpGet)
{
    yaaf::cli::Services services;
    services.http_get = [](std::string_view url, const HttpClient::Headers &headers) {
        EXPECT_EQ(url, "http://example.test");
        EXPECT_TRUE(headers.empty());
        return HttpClient::Response{200, "text/plain", "ok"};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"--proxy", "http://127.0.0.1:8080", "--get", "http://example.test"}, input,
                                          output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("method"), "GET");
    EXPECT_EQ(payload.at("url"), "http://example.test");
    EXPECT_EQ(payload.at("status_code"), 200);
    EXPECT_EQ(payload.at("body"), "ok");
}

TEST(CliTests, DoctorPrintsEnvironmentAndRegistries)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"doctor"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("yaaf doctor"), std::string::npos);
    EXPECT_NE(output.str().find("environment:"), std::string::npos);
    EXPECT_NE(output.str().find("registered agents:"), std::string::npos);
    EXPECT_NE(output.str().find("  - react"), std::string::npos);
    EXPECT_NE(output.str().find("registered tools:"), std::string::npos);
    EXPECT_NE(output.str().find("  - echo:"), std::string::npos);
    EXPECT_NE(output.str().find("app modes:"), std::string::npos);
    EXPECT_NE(output.str().find("  ask: tools enabled"), std::string::npos);
    EXPECT_NE(output.str().find("  chat: tools enabled"), std::string::npos);
    EXPECT_NE(output.str().find("  agent: tools enabled"), std::string::npos);
    EXPECT_NE(output.str().find("  embed: tools disabled"), std::string::npos);
}

TEST(CliTests, DoctorCanWriteJsonReport)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"doctor", "--format", "json"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("environment").at("model"), "qwen3:0.6b");
    ASSERT_TRUE(payload.at("registries").at("agents").is_array());
    ASSERT_FALSE(payload.at("registries").at("agents").empty());
    EXPECT_EQ(payload.at("registries").at("agents").at(0).at("name"), "react");
    ASSERT_TRUE(payload.at("registries").at("tools").is_array());
    ASSERT_FALSE(payload.at("registries").at("tools").empty());
    EXPECT_EQ(payload.at("registries").at("tools").at(0).at("name"), "echo");
    EXPECT_TRUE(payload.at("app_modes").at("ask").at("tools"));
    EXPECT_TRUE(payload.at("app_modes").at("chat").at("tools"));
    EXPECT_TRUE(payload.at("app_modes").at("agent").at("tools"));
    EXPECT_FALSE(payload.at("app_modes").at("embed").at("tools"));
    EXPECT_FALSE(payload.at("mcp").at("exists"));
    EXPECT_TRUE(payload.at("mcp").at("path").get<std::string>().empty());
}

TEST(CliTests, McpFileEnvironmentLoadsConfigWithoutImplicitWorkspaceDiscovery)
{
    const auto workspace = make_test_directory("mcp-file-env");
    const auto mcp_path = workspace / "custom.mcp.json";
    {
        std::ofstream config{mcp_path};
        config << nlohmann::json{{"servers", {{"envServer", {{"type", "named-pipe"}}}}}}.dump(2);
    }
    const ScopedEnvironmentVariable mcp_file{"YAAF_MCP_FILE", mcp_path.string()};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"doctor", "--format", "json"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    const auto payload = parse_json_output(output);
    EXPECT_TRUE(payload.at("mcp").at("exists"));
    EXPECT_EQ(payload.at("mcp").at("path"), mcp_path.generic_string());
    ASSERT_EQ(payload.at("mcp").at("servers").size(), 1U);
    EXPECT_EQ(payload.at("mcp").at("servers").at(0).at("id"), "envServer");

    std::filesystem::remove_all(workspace);
}

TEST(CliTests, ExplicitMcpOptionOverridesMcpFileEnvironment)
{
    const auto workspace = make_test_directory("mcp-file-override");
    const auto env_mcp_path = workspace / "env.mcp.json";
    const auto explicit_mcp_path = workspace / "explicit.mcp.json";
    {
        std::ofstream config{env_mcp_path};
        config << nlohmann::json{{"servers", {{"envServer", {{"type", "named-pipe"}}}}}}.dump(2);
    }
    {
        std::ofstream config{explicit_mcp_path};
        config << nlohmann::json{{"servers", {{"explicitServer", {{"type", "named-pipe"}}}}}}.dump(2);
    }
    const ScopedEnvironmentVariable mcp_file{"YAAF_MCP_FILE", env_mcp_path.string()};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"--mcp", explicit_mcp_path.string(), "doctor", "--format", "json"}, input,
                                          output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    const auto payload = parse_json_output(output);
    EXPECT_TRUE(payload.at("mcp").at("exists"));
    EXPECT_EQ(payload.at("mcp").at("path"), explicit_mcp_path.generic_string());
    ASSERT_EQ(payload.at("mcp").at("servers").size(), 1U);
    EXPECT_EQ(payload.at("mcp").at("servers").at(0).at("id"), "explicitServer");

    std::filesystem::remove_all(workspace);
}

TEST(CliTests, RunsLuaFileEntryPointOutsideBuiltinCommands)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"examples/example.lua", "one", "two"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("yaaf example\n"), std::string::npos);
    EXPECT_NE(output.str().find("endpoint: "), std::string::npos);
    EXPECT_NE(output.str().find("model: qwen3:0.6b\n"), std::string::npos);
    EXPECT_NE(output.str().find("args: one, two\n"), std::string::npos);
}

TEST(CliTests, BuiltinCommandsAreLoadedFromLuaCliDirectory)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"doctor", "--format", "json"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("registries").at("agents").at(0).at("name"), "react");
    EXPECT_EQ(payload.at("registries").at("tools").at(0).at("name"), "echo");
}
