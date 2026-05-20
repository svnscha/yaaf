#include "../support/mcp_test_support.h"

using namespace yaaf::tests::mcp;

TEST(McpConfigTests, EmptyPathLeavesMcpUnconfigured)
{
    const auto workspace = make_workspace("assistant_mcp_unconfigured_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});

    const auto loaded_config = yaaf::mcp::load_config(workspace);

    EXPECT_FALSE(loaded_config.exists);
    EXPECT_TRUE(loaded_config.path.empty());
    EXPECT_TRUE(loaded_config.servers.empty());
    EXPECT_TRUE(loaded_config.diagnostics.empty());
}

TEST(McpConfigTests, LoadsExplicitVsCodeMcpJsonShape)
{
    const auto workspace = make_workspace("assistant_mcp_config_test");
    nlohmann::json config_json;
    config_json["servers"]["github"] = {{"type", "http"},
                                        {"url", "https://example.test/mcp"},
                                        {"headers", {{"Authorization", "Bearer ${env:GITHUB_TOKEN}"}}}};
    config_json["servers"]["playwright"] = {
        {"type", "stdio"}, {"command", "npx"}, {"args", nlohmann::json::array({"-y", "@playwright/mcp"})}};
    write_mcp_config(workspace, config_json);

    const auto loaded_config = yaaf::mcp::load_config(workspace, workspace / ".vscode" / "mcp.json");

    ASSERT_TRUE(loaded_config.exists);
    ASSERT_EQ(loaded_config.servers.size(), 2U);
    EXPECT_EQ(loaded_config.servers[0].id, "github");
    EXPECT_EQ(loaded_config.servers[0].type, "http");
    EXPECT_TRUE(loaded_config.servers[0].supported);
    EXPECT_TRUE(loaded_config.servers[0].diagnostics.empty());
    EXPECT_EQ(loaded_config.servers[1].id, "playwright");
    EXPECT_EQ(loaded_config.servers[1].type, "stdio");
    EXPECT_TRUE(loaded_config.servers[1].supported);
}

TEST(McpConfigTests, LoadsExplicitMcpJsonPath)
{
    const auto workspace = make_workspace("assistant_mcp_explicit_config_test");
    const auto config_directory = workspace / "configs";
    std::filesystem::create_directories(config_directory);
    const auto config_path = config_directory / "custom-mcp.json";
    {
        std::ofstream output{config_path};
        output << nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", "${workspaceFolder}/mcp"}}}}}}.dump(
            2);
    }

    const auto loaded_config = yaaf::mcp::load_config(workspace, config_path);

    EXPECT_TRUE(loaded_config.exists);
    EXPECT_EQ(loaded_config.path, config_path);
    ASSERT_EQ(loaded_config.servers.size(), 1U);
    EXPECT_EQ(loaded_config.servers.front().raw.at("url"), (workspace / "mcp").generic_string());
}

TEST(McpConfigTests, LoadsRelativeMcpJsonPath)
{
    const auto workspace = make_workspace("assistant_mcp_relative_config_test");
    std::filesystem::create_directories(workspace / "configs");
    {
        std::ofstream output{workspace / "configs" / "mcp.json"};
        output << nlohmann::json{{"servers", {{"hello", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}};
    }

    const auto loaded_config = yaaf::mcp::load_config(workspace, std::filesystem::path{"configs/mcp.json"});

    EXPECT_TRUE(loaded_config.exists);
    EXPECT_EQ(loaded_config.path, workspace / "configs" / "mcp.json");
    ASSERT_EQ(loaded_config.servers.size(), 1U);
    EXPECT_EQ(loaded_config.servers.front().id, "hello");
}

TEST(McpConfigTests, ReportsMalformedConfigAndServerDiagnostics)
{
    const auto workspace = make_workspace("assistant_mcp_malformed_config_test");

    {
        std::ofstream output{workspace / ".vscode" / "mcp.json"};
        output << R"json({ "servers": )json";
    }
    auto loaded_config = yaaf::mcp::load_config(workspace, workspace / ".vscode" / "mcp.json");
    ASSERT_FALSE(loaded_config.diagnostics.empty());
    EXPECT_NE(loaded_config.diagnostics.front().find("parse failed"), std::string::npos);

    write_mcp_config(workspace, nlohmann::json{{"notServers", true}});
    loaded_config = yaaf::mcp::load_config(workspace, workspace / ".vscode" / "mcp.json");
    ASSERT_FALSE(loaded_config.diagnostics.empty());
    EXPECT_EQ(loaded_config.diagnostics.front(), "MCP config must contain a servers object");

    nlohmann::json config_json;
    config_json["servers"]["notObject"] = true;
    config_json["servers"]["httpMissingUrl"] = {{"type", "http"}};
    config_json["servers"]["stdioMissingCommand"] = {{"type", "stdio"}};
    config_json["servers"]["unterminatedEnv"] = {{"type", "http"}, {"url", "https://example.test/${env:MISSING"}};
    config_json["servers"]["unsupportedInputVariable"] = {{"type", "http"},
                                                          {"url", "https://example.test/${input:TOKEN}"}};
    write_mcp_config(workspace, config_json);

    loaded_config = yaaf::mcp::load_config(workspace, workspace / ".vscode" / "mcp.json");

    ASSERT_EQ(loaded_config.servers.size(), 5U);
    EXPECT_NE(loaded_config.servers[0].diagnostics.front().find("HTTP MCP server requires url"), std::string::npos);
    EXPECT_NE(loaded_config.servers[1].diagnostics.front().find("server config must be an object"), std::string::npos);
    EXPECT_NE(loaded_config.servers[2].diagnostics.front().find("stdio MCP server requires command"),
              std::string::npos);
    EXPECT_FALSE(loaded_config.servers[3].supported);
    EXPECT_NE(loaded_config.servers[3].diagnostics.front().find("unsupported variable ${input"), std::string::npos);
    EXPECT_NE(loaded_config.servers[4].diagnostics.front().find("unterminated ${env"), std::string::npos);
}

TEST(McpConfigTests, ExpandsVariablesRedactsSecretsAndReportsInvalidServers)
{
    const auto workspace = make_workspace("assistant_mcp_config_diagnostics_test");
#ifdef _WIN32
    _putenv_s("YAAF_MCP_TEST_VALUE", "expanded-env");
#else
    setenv("YAAF_MCP_TEST_VALUE", "expanded-env", 1);
#endif

    nlohmann::json config_json;
    config_json["servers"]["bad"] = {{"type", "named-pipe"}};
    config_json["servers"]["envServer"] = {{"type", "http"},
                                           {"url", "https://example.test/${env:YAAF_MCP_TEST_VALUE}"},
                                           {"headers", {{"Authorization", "secret"}}},
                                           {"env", {{"TOKEN", "secret-env"}}}};
    config_json["servers"]["workspaceServer"] = {{"type", "http"}, {"url", "${workspaceFolder}/mcp"}};
    write_mcp_config(workspace, config_json);

    const auto loaded_config = yaaf::mcp::load_config(workspace, workspace / ".vscode" / "mcp.json");
    const auto report = yaaf::mcp::config_to_json(loaded_config);

    ASSERT_EQ(loaded_config.servers.size(), 3U);
    EXPECT_FALSE(loaded_config.servers[0].supported);
    EXPECT_NE(loaded_config.servers[0].diagnostics.front().find("unsupported MCP server type"), std::string::npos);
    EXPECT_EQ(loaded_config.servers[1].raw.at("url"), "https://example.test/expanded-env");
    EXPECT_EQ(loaded_config.servers[2].raw.at("url"), (workspace / "mcp").generic_string());
    EXPECT_EQ(report.at("servers").at(1).at("config").at("headers").at("Authorization"), "<redacted>");
    EXPECT_EQ(report.at("servers").at(1).at("config").at("env").at("TOKEN"), "<redacted>");
}

TEST(McpSchemaTests, GeneratedSchemaMetadataSupportsPublishedVersions)
{
    EXPECT_EQ(yaaf::mcp::schema::latest_protocol_version(), "2025-11-25");
    EXPECT_TRUE(yaaf::mcp::schema::is_supported_protocol_version("2024-11-05"));
    EXPECT_TRUE(yaaf::mcp::schema::is_supported_protocol_version("2025-03-26"));
    EXPECT_TRUE(yaaf::mcp::schema::is_supported_protocol_version("2025-06-18"));
    EXPECT_TRUE(yaaf::mcp::schema::is_supported_protocol_version("2025-11-25"));
    EXPECT_FALSE(yaaf::mcp::schema::is_supported_protocol_version("1900-01-01"));

    const auto &methods = yaaf::mcp::schema::known_methods();
    EXPECT_NE(std::find(methods.begin(), methods.end(), "initialize"), methods.end());
    EXPECT_NE(std::find(methods.begin(), methods.end(), "tools/list"), methods.end());
    EXPECT_NE(std::find(methods.begin(), methods.end(), "tools/call"), methods.end());

    const auto factory = yaaf::mcp::schema::generated_factory();
    ASSERT_NE(factory, nullptr);
    const auto registry = factory->create_registry();
    ASSERT_NE(registry, nullptr);

    const auto latest = yaaf::mcp::schema::latest_protocol_version();
    const auto versions = registry->supported_versions();
    ASSERT_FALSE(versions.empty());
    EXPECT_TRUE(std::filesystem::exists(versions.back().schema_path));
    EXPECT_GT(versions.back().definition_count, 100U);
    EXPECT_EQ(versions.back().method_count, registry->backend(latest)->methods().size());

    for (const auto &version : versions)
    {
        const auto backend = factory->create(version.version);
        ASSERT_NE(backend, nullptr);
        EXPECT_EQ(backend->info().version, version.version);
        EXPECT_EQ(backend->methods().size(), version.method_count);
        EXPECT_EQ(backend->definitions().size(), version.definition_count);
        EXPECT_TRUE(backend->has_definition("CallToolRequest"));
        EXPECT_FALSE(backend->has_definition("MissingDefinition"));

        const auto list_tools = backend->method("tools/list");
        ASSERT_TRUE(list_tools.has_value());
        EXPECT_EQ(list_tools->definition, "ListToolsRequest");
        EXPECT_FALSE(backend->method("missing/method").has_value());
    }
    EXPECT_EQ(factory->create("1900-01-01"), nullptr);
    EXPECT_NE(yaaf::mcp::schema::default_factory(), nullptr);
    EXPECT_FALSE(yaaf::mcp::schema::methods(latest).empty());
    EXPECT_TRUE(yaaf::mcp::schema::methods("1900-01-01").empty());
    EXPECT_FALSE(yaaf::mcp::schema::definitions(latest).empty());
    EXPECT_TRUE(yaaf::mcp::schema::definitions("1900-01-01").empty());
    EXPECT_TRUE(yaaf::mcp::schema::has_definition(latest, "CallToolRequest"));
    EXPECT_FALSE(yaaf::mcp::schema::has_definition("1900-01-01", "CallToolRequest"));
    EXPECT_FALSE(yaaf::mcp::schema::method(latest, "missing/method").has_value());
    EXPECT_FALSE(yaaf::mcp::schema::method("1900-01-01", "tools/call").has_value());

    const auto oldest_backend = factory->create("2024-11-05");
    ASSERT_NE(oldest_backend, nullptr);
    EXPECT_EQ(oldest_backend->info().version, "2024-11-05");

    const auto latest_backend = factory->create_latest();
    ASSERT_NE(latest_backend, nullptr);
    EXPECT_TRUE(latest_backend->has_definition("CallToolRequest"));
    EXPECT_TRUE(latest_backend->has_definition("CallToolResult"));

    const auto call_tool = latest_backend->method("tools/call");
    ASSERT_TRUE(call_tool.has_value());
    EXPECT_EQ(call_tool->definition, "CallToolRequest");
    EXPECT_EQ(call_tool->kind, yaaf::mcp::schema::MessageKind::request);

    const auto initialized = latest_backend->method("notifications/initialized");
    ASSERT_TRUE(initialized.has_value());
    EXPECT_EQ(initialized->kind, yaaf::mcp::schema::MessageKind::notification);
}
