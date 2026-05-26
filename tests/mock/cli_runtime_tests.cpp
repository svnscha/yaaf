#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <CLI/CLI.hpp>
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
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() / fmt::format("yaaf-{}-{}", name, unique_suffix);
    std::filesystem::create_directories(path);
    return path;
}

void write_file(const std::filesystem::path &path, std::string_view contents)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file{path};
    file << contents;
}

class ScopedCurrentPath
{
  public:
    explicit ScopedCurrentPath(const std::filesystem::path &path) : original_(std::filesystem::current_path())
    {
        std::filesystem::current_path(path);
    }
    ~ScopedCurrentPath()
    {
        std::error_code ignored;
        std::filesystem::current_path(original_, ignored);
    }
    ScopedCurrentPath(const ScopedCurrentPath &) = delete;
    ScopedCurrentPath &operator=(const ScopedCurrentPath &) = delete;

  private:
    std::filesystem::path original_;
};

class ScopedUnsetEnvironmentVariable
{
  public:
    explicit ScopedUnsetEnvironmentVariable(std::string name) : name_(std::move(name))
    {
        if (const auto *current = std::getenv(name_.c_str()); current != nullptr)
        {
            original_ = current;
        }
#ifdef _WIN32
        _putenv_s(name_.c_str(), "");
#else
        unsetenv(name_.c_str());
#endif
    }

    ~ScopedUnsetEnvironmentVariable()
    {
        if (original_.has_value())
        {
#ifdef _WIN32
            _putenv_s(name_.c_str(), original_->c_str());
#else
            setenv(name_.c_str(), original_->c_str(), 1);
#endif
        }
    }

    ScopedUnsetEnvironmentVariable(const ScopedUnsetEnvironmentVariable &) = delete;
    ScopedUnsetEnvironmentVariable &operator=(const ScopedUnsetEnvironmentVariable &) = delete;

  private:
    std::string name_;
    std::optional<std::string> original_;
};

[[nodiscard]] std::filesystem::path path_from_output(const std::ostringstream &output)
{
    auto value = output.str();
    if (!value.empty() && value.back() == '\n')
    {
        value.pop_back();
    }
    return value;
}

void expect_equivalent_path_output(const std::ostringstream &output, const std::filesystem::path &expected)
{
    EXPECT_EQ(std::filesystem::weakly_canonical(path_from_output(output)), std::filesystem::weakly_canonical(expected));
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
    EXPECT_NE(output.str().find("run"), std::string::npos);
    EXPECT_EQ(output.str().find("example"), std::string::npos);
    EXPECT_NE(output.str().find("--proxy"), std::string::npos);
    EXPECT_NE(output.str().find("--pretty"), std::string::npos);
    EXPECT_EQ(output.str().find("--endpoint"), std::string::npos);
    EXPECT_EQ(output.str().find("--model"), std::string::npos);
    EXPECT_EQ(output.str().find("--stream"), std::string::npos);
    EXPECT_EQ(output.str().find("--think"), std::string::npos);
    EXPECT_EQ(output.str().find("--format"), std::string::npos);
}

TEST(CliTests, GlobalProxyOptionCanBeUsedWithGenericHttpRequestOverride)
{
    yaaf::cli::Services services;
    services.http_request = [](const HttpClient::Request &request) {
        EXPECT_EQ(request.method, "GET");
        EXPECT_EQ(request.url, "http://example.test");
        EXPECT_TRUE(request.headers.empty());
        EXPECT_FALSE(request.body.has_value());
        EXPECT_FALSE(request.timeout.has_value());
        return HttpClient::Response{200, "text/plain", "ok", {}};
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

TEST(CliTests, DefaultEndpointUsesYaafOllamaEndpointEnvironmentVariable)
{
    const ScopedEnvironmentVariable yaaf_endpoint{"YAAF_OLLAMA_ENDPOINT", "http://yaaf-prefixed.test:9999"};
    const ScopedEnvironmentVariable legacy_endpoint{"OLLAMA_ENDPOINT", "http://legacy-unprefixed.test:1111"};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"doctor", "--format", "json"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    EXPECT_EQ(payload.at("environment").at("endpoint"), "http://yaaf-prefixed.test:9999");
}

TEST(CliTests, LegacyUnprefixedOllamaEndpointEnvironmentVariableIsIgnored)
{
    const ScopedEnvironmentVariable legacy_endpoint{"OLLAMA_ENDPOINT", "http://legacy-unprefixed.test:1111"};
    const ScopedEnvironmentVariable yaaf_endpoint{"YAAF_OLLAMA_ENDPOINT", ""};
    const auto workspace = make_test_directory("legacy-ollama-endpoint");
    const ScopedCurrentPath current_path{workspace};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"doctor", "--format", "json"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = parse_json_output(output);
    const auto endpoint = payload.at("environment").at("endpoint").get<std::string>();
    EXPECT_NE(endpoint, "http://legacy-unprefixed.test:1111");
    EXPECT_EQ(endpoint, "http://localhost:11434");
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
}

TEST(CliTests, LegacyLuaFileEntryPointFailsWithStandardCliParseError)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"examples/example.lua", "one", "two"}, input, output, error_output);

    EXPECT_EQ(exit_code, static_cast<int>(CLI::ExitCodes::ExtrasError));
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("The following arguments were not expected"), std::string::npos);
    EXPECT_NE(error_output.str().find("examples/example.lua"), std::string::npos);
}

TEST(CliTests, RunSubcommandExecutesLuaFileEntryPoint)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", "examples/example.lua", "one", "two"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("yaaf example\n"), std::string::npos);
    EXPECT_NE(output.str().find("endpoint: "), std::string::npos);
    EXPECT_NE(output.str().find("model: qwen3:0.6b\n"), std::string::npos);
    EXPECT_NE(output.str().find("args: one, two\n"), std::string::npos);
}

TEST(CliTests, LegacyLuaFileEntryPointWithRootLevelMcpFailsWithStandardCliParseError)
{
    const auto workspace = make_test_directory("legacy-run-mcp");
    const auto script_path = workspace / "show_mcp.lua";
    const auto mcp_path = workspace / "tools.mcp.json";
    write_file(script_path, "local mcp = require(\"mcp\")\nlocal config = mcp.config()\nprint(config.path)\n");
    write_file(mcp_path, nlohmann::json{{"servers", {}}}.dump(2));

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"--mcp", mcp_path.string(), script_path.string()}, input, output, error_output);

    EXPECT_EQ(exit_code, static_cast<int>(CLI::ExitCodes::ExtrasError));
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("The following argument was not expected"), std::string::npos);
    EXPECT_NE(error_output.str().find(script_path.string()), std::string::npos);
}

TEST(CliTests, RunSubcommandForwardsExplicitMcpOption)
{
    const auto workspace = make_test_directory("run-subcommand-mcp");
    const auto script_path = workspace / "show_mcp.lua";
    const auto mcp_path = workspace / "tools.mcp.json";
    write_file(script_path, "local mcp = require(\"mcp\")\nlocal config = mcp.config()\nprint(config.path)\n");
    write_file(mcp_path, nlohmann::json{{"servers", {}}}.dump(2));

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"run", "--mcp", mcp_path.string(), script_path.string()}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    expect_equivalent_path_output(output, mcp_path);
}

TEST(CliTests, RunSubcommandFailsClearlyWhenScriptIsMissing)
{
    const auto workspace = make_test_directory("run-subcommand-missing-script");
    const auto missing_script = workspace / "missing.lua";

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", missing_script.string()}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_TRUE(output.str().empty());
    EXPECT_NE(error_output.str().find("Lua script not found"), std::string::npos);
    EXPECT_NE(error_output.str().find(missing_script.string()), std::string::npos);
}

TEST(CliTests, RunSubcommandFailsClearlyWhenScriptPathIsNotLua)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code = yaaf::cli::run({"run", "examples/example.txt"}, input, output, error_output);

    EXPECT_EQ(exit_code, EXIT_FAILURE);
    EXPECT_TRUE(output.str().empty());
    EXPECT_EQ(error_output.str(), "yaaf failed: run requires a .lua script path\n");
}

TEST(CliTests, DefaultDiscoveryFindsYaafMcpJsonInWorkspaceRoot)
{
    const auto workspace = make_test_directory("mcp-default-discovery");
    const auto script_path = workspace / "show_mcp.lua";
    const auto mcp_path = workspace / ".yaaf" / "mcp.json";
    write_file(script_path, "local mcp = require(\"mcp\")\nlocal config = mcp.config()\nprint(config.path)\n");
    write_file(mcp_path, nlohmann::json{{"servers", {}}}.dump(2));

    std::ostringstream output;
    {
        const ScopedUnsetEnvironmentVariable env_guard{"YAAF_MCP_FILE"};
        const ScopedCurrentPath scoped_path{workspace};

        std::istringstream input;
        std::ostringstream error_output;
        const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

        EXPECT_EQ(exit_code, EXIT_SUCCESS);
        EXPECT_TRUE(error_output.str().empty());
    }
    expect_equivalent_path_output(output, mcp_path);
}

TEST(CliTests, EnvironmentVariableOverridesYaafMcpJsonDiscovery)
{
    const auto workspace = make_test_directory("mcp-env-over-discovery");
    const auto script_path = workspace / "show_mcp.lua";
    const auto discovered_path = workspace / ".yaaf" / "mcp.json";
    const auto env_path = workspace / "env.mcp.json";
    write_file(script_path, "local mcp = require(\"mcp\")\nlocal config = mcp.config()\nprint(config.path)\n");
    write_file(discovered_path, nlohmann::json{{"servers", {}}}.dump(2));
    write_file(env_path, nlohmann::json{{"servers", {}}}.dump(2));

    std::ostringstream output;
    {
        const ScopedEnvironmentVariable mcp_file{"YAAF_MCP_FILE", env_path.string()};
        const ScopedCurrentPath scoped_path{workspace};

        std::istringstream input;
        std::ostringstream error_output;
        const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

        EXPECT_EQ(exit_code, EXIT_SUCCESS);
        EXPECT_TRUE(error_output.str().empty());
    }
    expect_equivalent_path_output(output, env_path);
}

TEST(CliTests, ExplicitMcpOptionOverridesYaafMcpJsonDiscovery)
{
    const auto workspace = make_test_directory("mcp-explicit-over-discovery");
    const auto script_path = workspace / "show_mcp.lua";
    const auto discovered_path = workspace / ".yaaf" / "mcp.json";
    const auto explicit_path = workspace / "explicit.mcp.json";
    write_file(script_path, "local mcp = require(\"mcp\")\nlocal config = mcp.config()\nprint(config.path)\n");
    write_file(discovered_path, nlohmann::json{{"servers", {}}}.dump(2));
    write_file(explicit_path, nlohmann::json{{"servers", {}}}.dump(2));

    std::ostringstream output;
    {
        const ScopedUnsetEnvironmentVariable env_guard{"YAAF_MCP_FILE"};
        const ScopedCurrentPath scoped_path{workspace};

        std::istringstream input;
        std::ostringstream error_output;
        const auto exit_code =
            yaaf::cli::run({"run", "--mcp", explicit_path.string(), script_path.string()}, input, output, error_output);

        EXPECT_EQ(exit_code, EXIT_SUCCESS);
        EXPECT_TRUE(error_output.str().empty());
    }
    expect_equivalent_path_output(output, explicit_path);
}

TEST(CliTests, RunSubcommandDiscoversYaafMcpJsonFromCurrentDirectory)
{
    const auto workspace = make_test_directory("run-mcp-discovery");
    const auto script_path = workspace / "show_mcp.lua";
    const auto mcp_path = workspace / ".yaaf" / "mcp.json";
    write_file(script_path, "local mcp = require(\"mcp\")\nlocal config = mcp.config()\nprint(config.path)\n");
    write_file(mcp_path, nlohmann::json{{"servers", {}}}.dump(2));

    std::ostringstream output;
    {
        const ScopedUnsetEnvironmentVariable env_guard{"YAAF_MCP_FILE"};
        const ScopedCurrentPath scoped_path{workspace};

        std::istringstream input;
        std::ostringstream error_output;
        const auto exit_code = yaaf::cli::run({"run", script_path.string()}, input, output, error_output);

        EXPECT_EQ(exit_code, EXIT_SUCCESS);
        EXPECT_TRUE(error_output.str().empty());
    }
    expect_equivalent_path_output(output, mcp_path);
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
