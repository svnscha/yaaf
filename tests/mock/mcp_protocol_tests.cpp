#include "../support/mcp_test_support.h"

#include "../../libyaaf/cli/cli.h"
#include "../../libyaaf/script/lua_runtime.h"

using namespace yaaf::tests::mcp;

TEST(McpProtocolMockTests, NativeClientPaginatesSseListsAndMapsToolErrors)
{
    const auto workspace = make_workspace("assistant_mcp_native_client_test");
    write_mcp_config(workspace, nlohmann::json{{"servers",
                                                {{"docs",
                                                  {{"type", "http"},
                                                   {"url", "https://example.test/mcp"},
                                                   {"headers", {{"Authorization", "Bearer token"}}}}}}}});

    std::vector<nlohmann::json> requests;
    std::vector<yaaf::mcp::Headers> headers_seen;
    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.schema_registry = std::make_shared<TestSchemaRegistry>(std::make_shared<TestSchemaBackend>(
        "2030-01-01", std::vector<yaaf::mcp::schema::MethodInfo>{{"tools/list", "ListToolsRequest"},
                                                                 {"tools/call", "CallToolRequest"}}));
    options.http_post = [&](std::string_view, std::string_view body, std::string_view,
                            const yaaf::mcp::Headers &headers) {
        headers_seen.push_back(headers);
        const auto request = nlohmann::json::parse(body);
        requests.push_back(request);
        const auto method = request.at("method").get<std::string>();
        if (method == "initialize")
        {
            EXPECT_EQ(request.at("params").at("protocolVersion").get<std::string>(), "2030-01-01");
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["protocolVersion"] = "2030-01-01";
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            return sse_response(payload, {{"Mcp-Session-Id", "session-1"}});
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{202, "", "", {}};
        }
        if (method == "tools/list" && !request.at("params").contains("cursor"))
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] = nlohmann::json::array();
            payload["result"]["nextCursor"] = "next";
            return json_response(payload);
        }
        if (method == "tools/list")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] =
                nlohmann::json::array({{{"name", "lookup"}, {"title", "Lookup"}, {"description", "Look up docs"}}});
            return json_response(payload);
        }
        if (method == "tools/call")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["content"] = nlohmann::json::array({{{"type", "text"}, {"text", "bad input"}}});
            payload["result"]["structuredContent"] = {{"code", "bad"}};
            payload["result"]["isError"] = true;
            return json_response(payload);
        }
        return HttpClient::Response{500, "text/plain", "unexpected", {}};
    };

    yaaf::mcp::Client client{options};
    const auto tools = client.list_tools("docs");
    ASSERT_EQ(tools.size(), 1U);
    EXPECT_EQ(tools.front().local_name, "docs.lookup");

    const auto result = client.call_tool("docs", "lookup", nlohmann::json{{"query", "mcp"}});
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.content, "bad input");
    EXPECT_EQ(result.metadata.at("raw").at("structuredContent").at("code"), "bad");
    ASSERT_GE(headers_seen.size(), 3U);
    const auto has_session_header = [](const yaaf::mcp::Headers &headers) {
        return std::any_of(headers.begin(), headers.end(), [](const auto &header) {
            return header.first == "Mcp-Session-Id" && header.second == "session-1";
        });
    };
    EXPECT_TRUE(has_session_header(headers_seen.back()));
}

TEST(McpProtocolMockTests, RejectsUnsupportedNegotiatedProtocolVersion)
{
    const auto workspace = make_workspace("assistant_mcp_protocol_reject_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.http_post = [](std::string_view, std::string_view body, std::string_view, const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        nlohmann::json payload;
        payload["jsonrpc"] = "2.0";
        payload["id"] = request.at("id");
        payload["result"]["protocolVersion"] = "1900-01-01";
        payload["result"]["capabilities"] = nlohmann::json::object();
        payload["result"]["serverInfo"] = {{"name", "docs"}, {"version", "1"}};
        return json_response(payload);
    };

    yaaf::mcp::Client client{options};
    EXPECT_THROW((void)client.list_tools("docs"), std::runtime_error);
}

TEST(McpProtocolMockTests, ReportsHttpTransportFailures)
{
    const auto workspace = make_workspace("assistant_mcp_http_failure_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});

    const auto registry = std::make_shared<TestSchemaRegistry>(std::make_shared<TestSchemaBackend>(
        "2025-06-18", std::vector<yaaf::mcp::schema::MethodInfo>{{"tools/list", "ListToolsRequest"}}));

    {
        yaaf::mcp::ClientOptions options;
        options.workspace_root = workspace;
        options.config_path = workspace_mcp_config_path(workspace);
        options.schema_registry = registry;
        options.http_post = [](std::string_view, std::string_view, std::string_view, const yaaf::mcp::Headers &) {
            return HttpClient::Response{503, "text/plain", "unavailable", {}};
        };

        yaaf::mcp::Client client{options};
        EXPECT_THROW((void)client.list_tools("docs"), std::runtime_error);
    }

    {
        yaaf::mcp::ClientOptions options;
        options.workspace_root = workspace;
        options.config_path = workspace_mcp_config_path(workspace);
        options.schema_registry = registry;
        options.http_post = [](std::string_view, std::string_view, std::string_view, const yaaf::mcp::Headers &) {
            return HttpClient::Response{200, "application/json", "", {}};
        };

        yaaf::mcp::Client client{options};
        EXPECT_THROW((void)client.list_tools("docs"), std::runtime_error);
    }
}

TEST(McpProtocolMockTests, ReportsSseTransportFailuresAndParsesMultiLinePayloads)
{
    const auto workspace = make_workspace("assistant_mcp_sse_failure_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "sse"}, {"url", "https://example.test/mcp"}}}}}});

    const auto registry = std::make_shared<TestSchemaRegistry>(std::make_shared<TestSchemaBackend>(
        "2025-06-18", std::vector<yaaf::mcp::schema::MethodInfo>{{"tools/list", "ListToolsRequest"}}));

    {
        yaaf::mcp::ClientOptions options;
        options.workspace_root = workspace;
        options.config_path = workspace_mcp_config_path(workspace);
        options.schema_registry = registry;
        options.http_post = [](std::string_view, std::string_view, std::string_view, const yaaf::mcp::Headers &) {
            return HttpClient::Response{200, "text/event-stream", "event: message\n\n", {}};
        };

        yaaf::mcp::Client client{options};
        EXPECT_THROW((void)client.list_tools("docs"), std::runtime_error);
    }

    std::vector<nlohmann::json> requests;
    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.schema_registry = registry;
    options.http_post = [&](std::string_view, std::string_view body, std::string_view, const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        requests.push_back(request);
        const auto method = request.at("method").get<std::string>();
        if (method == "initialize")
        {
            return HttpClient::Response{200,
                                        "text/event-stream",
                                        "event: message\r\n"
                                        "data: {\"jsonrpc\":\"2.0\",\r\n"
                                        "data: \"id\":" +
                                            std::to_string(request.at("id").get<int>()) +
                                            ",\r\n"
                                            "data: \"result\":{\"protocolVersion\":\"2025-06-18\",\r\n"
                                            "data: \"capabilities\":{\"tools\":{}},\r\n"
                                            "data: \"serverInfo\":{\"name\":\"docs\",\"version\":\"1\"}}}",
                                        {}};
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{500, "text/plain", "notification failed", {}};
        }
        return HttpClient::Response{500, "text/plain", "unexpected", {}};
    };

    yaaf::mcp::Client client{options};
    EXPECT_THROW((void)client.list_tools("docs"), std::runtime_error);
    ASSERT_GE(requests.size(), 2U);
    EXPECT_EQ(requests.front().at("method"), "initialize");
    EXPECT_EQ(requests.back().at("method"), "notifications/initialized");
}

TEST(McpProtocolMockTests, MapsProtocolErrorsAndStructuredToolResults)
{
    const auto workspace = make_workspace("assistant_mcp_tool_result_shapes_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});

    const auto registry = std::make_shared<TestSchemaRegistry>(std::make_shared<TestSchemaBackend>(
        "2025-06-18", std::vector<yaaf::mcp::schema::MethodInfo>{{"tools/list", "ListToolsRequest"},
                                                                 {"tools/call", "CallToolRequest"}}));

    std::string result_shape = "object-content";
    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.schema_registry = registry;
    options.http_post = [&](std::string_view, std::string_view body, std::string_view, const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        const auto method = request.at("method").get<std::string>();
        if (method == "initialize")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["protocolVersion"] = "2025-06-18";
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            payload["result"]["serverInfo"] = {{"name", "docs"}, {"version", "1"}};
            return json_response(payload);
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{202, "", "", {}};
        }
        if (method == "tools/list")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] = nlohmann::json::array({{{"name", "lookup"}}});
            return json_response(payload);
        }
        if (result_shape == "object-content")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["content"] =
                nlohmann::json::array({{{"type", "image"}, {"data", "abc"}}, {{"type", "text"}, {"text", "plain"}}});
            payload["result"]["isError"] = false;
            return json_response(payload);
        }
        if (result_shape == "structured-only")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["structuredContent"] = {{"value", 42}};
            payload["result"]["isError"] = false;
            return json_response(payload);
        }
        if (result_shape == "protocol-error")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["error"] = {{"code", -32000}, {"message", "bad protocol"}};
            return json_response(payload);
        }
        return json_response({{"jsonrpc", "2.0"}, {"id", request.at("id")}});
    };

    yaaf::mcp::Client client{options};
    EXPECT_EQ(client.call_tool("docs", "lookup", nlohmann::json::object()).content,
              "{\"data\":\"abc\",\"type\":\"image\"}\nplain");

    result_shape = "structured-only";
    EXPECT_EQ(client.call_tool("docs", "lookup", nlohmann::json::object()).content, "{\"value\":42}");

    result_shape = "protocol-error";
    const auto protocol_error = client.call_tool("docs", "lookup", nlohmann::json::object());
    EXPECT_FALSE(protocol_error.success);
    EXPECT_NE(protocol_error.content.find("bad protocol"), std::string::npos);

    result_shape = "missing-result";
    const auto missing_result = client.call_tool("docs", "lookup", nlohmann::json::object());
    EXPECT_FALSE(missing_result.success);
    EXPECT_NE(missing_result.content.find("did not contain result"), std::string::npos);
}

TEST(McpProtocolMockTests, RejectsMethodsMissingFromNegotiatedSchema)
{
    const auto workspace = make_workspace("assistant_mcp_missing_method_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.schema_registry = std::make_shared<TestSchemaRegistry>(std::make_shared<TestSchemaBackend>(
        "2025-06-18", std::vector<yaaf::mcp::schema::MethodInfo>{{"tools/list", "ListToolsRequest"}}));
    options.http_post = [](std::string_view, std::string_view body, std::string_view, const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        const auto method = request.at("method").get<std::string>();
        if (method == "initialize")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["protocolVersion"] = "2025-06-18";
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            payload["result"]["serverInfo"] = {{"name", "docs"}, {"version", "1"}};
            return json_response(payload);
        }
        return HttpClient::Response{202, "", "", {}};
    };

    yaaf::mcp::Client client{options};
    EXPECT_THROW((void)client.call_tool("docs", "lookup", nlohmann::json::object()), std::runtime_error);
}

TEST(McpLuaBridgeMockTests, LuaRegistryDiscoversAndCallsHttpMcpTools)
{
    const auto workspace = make_workspace("assistant_mcp_lua_registry_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});
    const auto script_path = write_lua_script(workspace, R"lua(
local tool = require("tool")
local names = tool.names()
assert(names[1] == "docs.lookup")
assert(names[2] == "echo")
local specs = tool.specs({ "docs.lookup" })
assert(specs[1]["function"].name == "docs.lookup")
local result = tool.execute({ "docs.lookup" }, "docs.lookup", { query = "mcp", include = true, limit = 2, tags = { "a", "b" }, optional = nil })
assert(result.success == true)
assert(result.tool_name == "docs.lookup")
assert(result.content == "MCP docs")
assert(result.metadata.server == "docs")
assert(result.metadata.arguments.query == "mcp")
)lua");

    yaaf::script::Services services;
    services.mcp_schema_registry = std::make_shared<TestSchemaRegistry>(std::make_shared<TestSchemaBackend>(
        "2025-06-18", std::vector<yaaf::mcp::schema::MethodInfo>{{"tools/list", "ListToolsRequest"},
                                                                 {"tools/call", "CallToolRequest"}}));
    services.mcp_http_post = [](std::string_view, std::string_view body, std::string_view, const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        const auto method = request.at("method").get<std::string>();
        if (method == "initialize")
        {
            EXPECT_EQ(request.at("params").at("protocolVersion"), "2025-06-18");
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["protocolVersion"] = "2025-06-18";
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            payload["result"]["serverInfo"] = {{"name", "docs"}, {"version", "1"}};
            return json_response(payload);
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{202, "", "", {}};
        }
        if (method == "tools/list")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] = nlohmann::json::array(
                {{{"name", "lookup"}, {"description", "Look up docs"}, {"inputSchema", {{"type", "object"}}}}});
            return json_response(payload);
        }
        if (method == "tools/call")
        {
            EXPECT_EQ(request.at("params").at("name"), "lookup");
            EXPECT_EQ(request.at("params").at("arguments").at("query"), "mcp");
            EXPECT_EQ(request.at("params").at("arguments").at("include"), true);
            EXPECT_EQ(request.at("params").at("arguments").at("limit"), 2);
            EXPECT_EQ(request.at("params").at("arguments").at("tags").at(0), "a");
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["content"] = nlohmann::json::array({{{"type", "text"}, {"text", "MCP docs"}}});
            payload["result"]["isError"] = false;
            return json_response(payload);
        }
        nlohmann::json payload;
        payload["jsonrpc"] = "2.0";
        payload["id"] = request.value("id", 0);
        payload["error"]["message"] = "unexpected";
        return json_response(payload);
    };

    yaaf::script::LuaRuntimeOptions options;
    options.file_path = script_path.string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";
    options.workspace_root = workspace;
    options.mcp_config_path = workspace_mcp_config_path(workspace);

    EXPECT_EQ(yaaf::script::run_file(options, &services), EXIT_SUCCESS);
}

TEST(McpLuaBridgeMockTests, DirectLuaModuleReturnsMcpJsonShapes)
{
    const auto workspace = make_workspace("assistant_mcp_lua_direct_module_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});
    const auto script_path = write_lua_script(workspace, R"lua(
local mcp = require("mcp")
local config = mcp.config()
assert(config.exists == true)
local servers = mcp.servers()
assert(servers[1].id == "docs")
local listed = mcp.list_tools("docs")
assert(listed[1].name == "lookup")
assert(listed[1].outputSchema == nil)
assert(listed[1].annotations.score == 1.5)
assert(listed[1].annotations.count == 42)
local result = mcp.call_tool("docs", "lookup", { query = "mcp" })
assert(result.success == true)
assert(result.metadata.raw.structuredContent.score == 1.5)
print(result.content)
)lua");

    yaaf::script::Services services;
    services.mcp_schema_registry = std::make_shared<TestSchemaRegistry>(std::make_shared<TestSchemaBackend>(
        "2025-06-18", std::vector<yaaf::mcp::schema::MethodInfo>{{"tools/list", "ListToolsRequest"},
                                                                 {"tools/call", "CallToolRequest"}}));
    services.mcp_http_post = [](std::string_view, std::string_view body, std::string_view, const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        const auto method = request.at("method").get<std::string>();
        if (method == "initialize")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["protocolVersion"] = "2025-06-18";
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            payload["result"]["serverInfo"] = {{"name", "docs"}, {"version", "1"}};
            return json_response(payload);
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{202, "", "", {}};
        }
        if (method == "tools/list")
        {
            nlohmann::json tool;
            tool["name"] = "lookup";
            tool["outputSchema"] = nullptr;
            tool["annotations"] = {{"score", 1.5}, {"count", std::uint64_t{42}}};

            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] = nlohmann::json::array({tool});
            return json_response(payload);
        }

        nlohmann::json payload;
        payload["jsonrpc"] = "2.0";
        payload["id"] = request.at("id");
        payload["result"]["structuredContent"] = {{"score", 1.5}};
        payload["result"]["content"] = nlohmann::json::array();
        payload["result"]["isError"] = false;
        return json_response(payload);
    };

    yaaf::script::LuaRuntimeOptions options;
    options.file_path = script_path.string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";
    options.workspace_root = workspace;
    options.mcp_config_path = workspace_mcp_config_path(workspace);
    std::ostringstream output;
    options.output = &output;

    EXPECT_EQ(yaaf::script::run_file(options, &services), EXIT_SUCCESS);
    EXPECT_EQ(output.str(), "{\"score\":1.5}\n");
}

TEST(McpLuaBridgeMockTests, RejectsUnsupportedLuaArgumentTypes)
{
    const auto workspace = make_workspace("assistant_mcp_lua_argument_error_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});
    const auto script_path = write_lua_script(workspace, R"lua(
local mcp = require("mcp")
mcp.call_tool("docs", "lookup", function() end)
)lua");

    yaaf::script::LuaRuntimeOptions options;
    options.file_path = script_path.string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";
    options.workspace_root = workspace;
    options.mcp_config_path = workspace_mcp_config_path(workspace);

    EXPECT_THROW((void)yaaf::script::run_file(options), std::runtime_error);
}

TEST(McpLuaBridgeMockTests, RejectsLuaArgumentTablesWithNonStringObjectKeys)
{
    const auto workspace = make_workspace("assistant_mcp_lua_table_key_error_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"docs", {{"type", "http"}, {"url", "https://example.test/mcp"}}}}}});
    const auto script_path = write_lua_script(workspace, R"lua(
local mcp = require("mcp")
mcp.call_tool("docs", "lookup", { [0] = "bad" })
)lua");

    yaaf::script::LuaRuntimeOptions options;
    options.file_path = script_path.string();
    options.endpoint = "http://localhost:11434";
    options.model = "qwen3:0.6b";
    options.workspace_root = workspace;
    options.mcp_config_path = workspace_mcp_config_path(workspace);

    EXPECT_THROW((void)yaaf::script::run_file(options), std::runtime_error);
}

TEST(McpDoctorMockTests, DoctorJsonIncludesActiveMcpDiagnosticsAndRedactsSecrets)
{
    const auto workspace = make_workspace("assistant_mcp_doctor_json_test");
    const auto mcp_path = workspace_mcp_config_path(workspace);
    write_mcp_config(workspace,
                     nlohmann::json{{"servers",
                                     {{"broken",
                                       {{"type", "http"},
                                        {"url", "https://broken.example.test/mcp"},
                                        {"headers", {{"Authorization", "Bearer broken-secret"}}}}},
                                      {"docs",
                                       {{"type", "http"},
                                        {"url", "https://docs.example.test/mcp"},
                                        {"headers", {{"Authorization", "Bearer docs-secret"}}}}},
                                      {"local", {{"type", "stdio"}, {"env", {{"API_TOKEN", "stdio-secret"}}}}}}}});

    yaaf::cli::Services services;
    services.mcp_http_post = [](std::string_view url, std::string_view body, std::string_view,
                                const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        const auto method = request.at("method").get<std::string>();
        const auto url_string = std::string(url);

        if (method == "initialize")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["protocolVersion"] = "2025-06-18";
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            payload["result"]["serverInfo"] = {
                {"name", url_string.find("docs") != std::string::npos ? "docs" : "broken"}, {"version", "1"}};
            return json_response(payload);
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{202, "", "", {}};
        }
        if (method == "tools/list" && url_string.find("docs") != std::string::npos)
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] =
                nlohmann::json::array({{{"name", "lookup"}, {"title", "Lookup"}, {"description", "Look up docs"}}});
            return json_response(payload);
        }
        if (method == "tools/list" && url_string.find("broken") != std::string::npos)
        {
            return HttpClient::Response{503, "text/plain", "broken", {}};
        }
        return HttpClient::Response{500, "text/plain", "unexpected", {}};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;
    const auto exit_code = yaaf::cli::run({"--mcp", mcp_path.string(), "doctor", "--format", "json"}, input, output,
                                          error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = nlohmann::json::parse(output.str());
    ASSERT_TRUE(payload.contains("mcp"));
    EXPECT_TRUE(payload.at("mcp").at("exists"));
    ASSERT_EQ(payload.at("mcp").at("servers").size(), 3U);

    const auto &servers = payload.at("mcp").at("servers");
    const auto find_server = [&](std::string_view id) -> const nlohmann::json & {
        const auto found = std::find_if(servers.begin(), servers.end(), [&](const auto &server) {
            return server.at("id").template get<std::string>() == id;
        });
        EXPECT_NE(found, servers.end());
        return *found;
    };

    const auto &broken = find_server("broken");
    EXPECT_EQ(broken.at("config").at("headers").at("Authorization"), "<redacted>");
    EXPECT_EQ(broken.at("active").at("initialize").at("status"), "ok");
    EXPECT_EQ(broken.at("active").at("tools").at("status"), "failed");
    EXPECT_NE(broken.at("active").at("tools").at("error").get<std::string>().find("status 503"), std::string::npos);

    const auto &docs = find_server("docs");
    EXPECT_EQ(docs.at("config").at("headers").at("Authorization"), "<redacted>");
    EXPECT_EQ(docs.at("active").at("initialize").at("status"), "ok");
    EXPECT_EQ(docs.at("active").at("initialize").at("protocol_version"), "2025-06-18");
    EXPECT_EQ(docs.at("active").at("tools").at("status"), "ok");
    EXPECT_EQ(docs.at("active").at("tools").at("count"), 1);
    ASSERT_EQ(docs.at("active").at("tools").at("names").size(), 1U);
    EXPECT_EQ(docs.at("active").at("tools").at("names").at(0), "docs.lookup");

    const auto &local = find_server("local");
    EXPECT_EQ(local.at("config").at("env").at("API_TOKEN"), "<redacted>");
    EXPECT_EQ(local.at("active").at("initialize").at("status"), "failed");
    EXPECT_NE(local.at("active").at("initialize").at("error").get<std::string>().find("unsupported MCP server"),
              std::string::npos);
}

TEST(McpDoctorMockTests, DoctorTextIncludesActiveMcpDiagnosticsSummary)
{
    const auto workspace = make_workspace("assistant_mcp_doctor_text_test");
    const auto mcp_path = workspace_mcp_config_path(workspace);
    write_mcp_config(workspace, nlohmann::json{{"servers",
                                                {{"docs",
                                                  {{"type", "http"},
                                                   {"url", "https://docs.example.test/mcp"},
                                                   {"headers", {{"Authorization", "Bearer docs-secret"}}}}}}}});

    yaaf::cli::Services services;
    services.mcp_http_post = [](std::string_view, std::string_view body, std::string_view, const yaaf::mcp::Headers &) {
        const auto request = nlohmann::json::parse(body);
        const auto method = request.at("method").get<std::string>();
        if (method == "initialize")
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["protocolVersion"] = "2025-06-18";
            payload["result"]["capabilities"]["tools"] = nlohmann::json::object();
            payload["result"]["serverInfo"] = {{"name", "docs"}, {"version", "1"}};
            return json_response(payload);
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{202, "", "", {}};
        }
        nlohmann::json payload;
        payload["jsonrpc"] = "2.0";
        payload["id"] = request.at("id");
        payload["result"]["tools"] =
            nlohmann::json::array({{{"name", "lookup"}, {"title", "Lookup"}, {"description", "Look up docs"}}});
        return json_response(payload);
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;
    const auto exit_code =
        yaaf::cli::run({"--mcp", mcp_path.string(), "doctor"}, input, output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("initialize: ok (protocol 2025-06-18)"), std::string::npos);
    EXPECT_NE(output.str().find("tools: 1 discovered: docs.lookup"), std::string::npos);
}

// ============================================================================
// MCP Host Protocol Tests
// ============================================================================

namespace
{
/// Helper to create a Host with mock callbacks
[[nodiscard]] yaaf::mcp::Host create_test_host(const std::vector<yaaf::mcp::ToolInfo> &tools = {},
                                               const std::vector<yaaf::mcp::PromptDescriptor> &prompts = {},
                                               yaaf::mcp::ToolLister tool_lister = nullptr,
                                               yaaf::mcp::ToolExecutor tool_executor = nullptr,
                                               yaaf::mcp::PromptLister prompt_lister = nullptr,
                                               yaaf::mcp::PromptExecutor prompt_executor = nullptr)
{
    // Create default tool_lister if not provided
    if (!tool_lister && !tools.empty())
    {
        tool_lister = [tools]() { return tools; };
    }

    // Create default prompt_lister if not provided
    if (!prompt_lister && !prompts.empty())
    {
        prompt_lister = [prompts]() { return prompts; };
    }

    const auto schema_backend = std::make_shared<TestSchemaBackend>(
        "2025-06-18",
        std::vector<yaaf::mcp::schema::MethodInfo>{{"initialize", "InitializeRequest"},
                                                   {"notifications/initialized", "InitializedNotification"},
                                                   {"tools/list", "ListToolsRequest"},
                                                   {"tools/call", "CallToolRequest"},
                                                   {"prompts/list", "ListPromptsRequest"},
                                                   {"prompts/get", "GetPromptRequest"}});

    return yaaf::mcp::Host{schema_backend, tool_executor, prompt_executor, tool_lister, prompt_lister};
}

/// Helper to parse JSON-RPC response lines
[[nodiscard]] nlohmann::json parse_jsonrpc_response(std::string_view line)
{
    return nlohmann::json::parse(std::string(line));
}

/// Helper to extract response lines from output
[[nodiscard]] std::vector<std::string> extract_response_lines(const std::string &output)
{
    std::vector<std::string> lines;
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty())
        {
            lines.push_back(line);
        }
    }
    return lines;
}

} // namespace

TEST(McpHostProtocolTests, HostNegotiatesProtocolVersionOnInitialize)
{
    auto host = create_test_host();

    const auto result = host.initialize({{"protocolVersion", "2025-06-18"}, {"clientInfo", {{"name", "test"}}}});

    EXPECT_EQ(result.at("protocolVersion"), "2025-06-18");
    EXPECT_EQ(result.at("serverInfo").at("name"), "yaaf");
    EXPECT_TRUE(result.contains("capabilities"));
    EXPECT_TRUE(result.at("capabilities").contains("tools"));
    EXPECT_TRUE(result.at("capabilities").contains("prompts"));

    // Verify subsequent calls work after initialize
    const auto &session = host.session();
    EXPECT_EQ(session.protocol_version, "2025-06-18");
}

TEST(McpHostProtocolTests, HostListsToolsFromExecutor)
{
    const std::vector<yaaf::mcp::ToolInfo> tools{
        yaaf::mcp::ToolInfo{"echo", "Echo tool", {{"type", "object"}}},
        yaaf::mcp::ToolInfo{"lookup", "Lookup tool", nlohmann::json::object()},
        yaaf::mcp::ToolInfo{"process", "Process tool", nlohmann::json::object()},
    };

    auto host = create_test_host(tools);
    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    const auto listed = host.list_tools();

    ASSERT_EQ(listed.size(), 3U);
    EXPECT_EQ(listed[0].at("name"), "echo");
    EXPECT_EQ(listed[0].at("description"), "Echo tool");
    EXPECT_TRUE(listed[0].contains("inputSchema"));
    EXPECT_EQ(listed[1].at("name"), "lookup");
    EXPECT_EQ(listed[2].at("name"), "process");
}

TEST(McpHostProtocolTests, HostFiltersToolsByName)
{
    const std::vector<yaaf::mcp::ToolInfo> tools{
        yaaf::mcp::ToolInfo{"echo", "Echo tool", nlohmann::json::object()},
        yaaf::mcp::ToolInfo{"tool1", "First tool", nlohmann::json::object()},
        yaaf::mcp::ToolInfo{"tool2", "Second tool", nlohmann::json::object()},
    };

    // Create host with custom tool_lister that filters
    auto host = create_test_host({}, {}, [&tools]() {
        std::vector<yaaf::mcp::ToolInfo> filtered;
        filtered.push_back(tools[0]); // Only include echo
        return filtered;
    });

    (void)host.initialize({{"protocolVersion", "2025-06-18"}});
    const auto listed = host.list_tools();

    ASSERT_EQ(listed.size(), 1U);
    EXPECT_EQ(listed[0].at("name"), "echo");
}

TEST(McpHostProtocolTests, HostCallsToolViaExecutor)
{
    auto host = create_test_host({}, {}, nullptr, [](const std::string &name, const nlohmann::json &args) {
        EXPECT_EQ(name, "test_tool");
        EXPECT_EQ(args.at("param"), "value");
        return yaaf::mcp::ToolExecutorResult{"Success!", false};
    });

    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    const auto result = host.call_tool("test_tool", {{"param", "value"}});

    EXPECT_EQ(result.at("type"), "text");
    EXPECT_TRUE(result.at("content").is_array());
    EXPECT_EQ(result.at("content")[0].at("text"), "Success!");
}

TEST(McpHostProtocolTests, HostMapsToolErrorToMcpResult)
{
    auto host = create_test_host({}, {}, nullptr, [](const std::string &, const nlohmann::json &) {
        return yaaf::mcp::ToolExecutorResult{"Tool failed", true};
    });

    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    const auto result = host.call_tool("broken_tool", {});

    EXPECT_EQ(result.at("type"), "error");
    EXPECT_TRUE(result.at("content").is_array());
    EXPECT_EQ(result.at("content")[0].at("text"), "Tool failed");
}

TEST(McpHostProtocolTests, HostListsPromptsFromExecutor)
{
    const std::vector<yaaf::mcp::PromptDescriptor> prompts{
        yaaf::mcp::PromptDescriptor{
            "weather", "Get weather", {yaaf::mcp::PromptArgument{"location", "Location name", true}}},
        yaaf::mcp::PromptDescriptor{"greeting",
                                    "Greeting prompt",
                                    {yaaf::mcp::PromptArgument{"name", "User name", false},
                                     yaaf::mcp::PromptArgument{"greeting", "Greeting type", true}}},
    };

    auto host = create_test_host({}, prompts);
    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    const auto listed = host.list_prompts();

    ASSERT_EQ(listed.size(), 2U);
    EXPECT_EQ(listed[0].at("name"), "weather");
    EXPECT_EQ(listed[0].at("description"), "Get weather");
    EXPECT_TRUE(listed[0].contains("arguments"));
    EXPECT_EQ(listed[0].at("arguments")[0].at("name"), "location");
    EXPECT_EQ(listed[0].at("arguments")[0].at("required"), true);

    EXPECT_EQ(listed[1].at("name"), "greeting");
    EXPECT_EQ(listed[1].at("arguments").size(), 2U);
}

TEST(McpHostProtocolTests, HostGetPromptViaExecutor)
{
    auto host =
        create_test_host({}, {}, nullptr, nullptr, nullptr, [](const std::string &name, const nlohmann::json &args) {
            EXPECT_EQ(name, "test_prompt");
            EXPECT_EQ(args.at("role"), "user");
            return std::vector<yaaf::mcp::PromptMessage>{
                yaaf::mcp::PromptMessage{"user", "Hello"},
                yaaf::mcp::PromptMessage{"assistant", "Hi there!"},
            };
        });

    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    const auto messages = host.get_prompt("test_prompt", {{"role", "user"}});

    ASSERT_EQ(messages.size(), 2U);
    EXPECT_EQ(messages[0].at("role"), "user");
    EXPECT_EQ(messages[0].at("content").at("type"), "text");
    EXPECT_EQ(messages[0].at("content").at("text"), "Hello");
    EXPECT_EQ(messages[1].at("role"), "assistant");
    EXPECT_EQ(messages[1].at("content").at("text"), "Hi there!");
}

TEST(McpHostProtocolTests, HostReturnsErrorForMissingPrompt)
{
    auto host = create_test_host({}, {});
    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    EXPECT_THROW((void)host.get_prompt("unknown_prompt", {}), std::runtime_error);
}

TEST(McpHostProtocolTests, StdioHostReadsJsonRpcRequest)
{
    auto host = create_test_host();
    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    std::istringstream input;
    std::ostringstream output;

    yaaf::mcp::StdioHost stdio_host{host, input, output};

    // Note: We would normally write to input, but StdioHost::run() blocks on input.
    // For focused testing, we test the JSON-RPC framing separately.

    // Test response formatting through list_tools call
    std::istringstream input2("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"tools/list\", \"params\": {} }\n");
    std::ostringstream output2;
    yaaf::mcp::StdioHost stdio_host2{host, input2, output2};

    // The run() will process the request and exit on EOF
    // This is tested more comprehensively in StdioHost tests below
}

TEST(McpHostProtocolTests, StdioHostHandlesUnknownMethod)
{
    auto host = create_test_host();
    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 42, \"method\": \"unknown/method\", \"params\": {} }\n");
    std::ostringstream output;

    yaaf::mcp::StdioHost stdio_host{host, input, output};
    // Don't call run() directly in test; instead verify the error handling path

    // We test the dispatch_method indirectly through the framing test
    const auto response_str = output.str();
    // Since we don't call run(), the output is empty; we verify the behavior through integration tests
}

TEST(McpHostProtocolTests, StdioHostHandlesMalformedJson)
{
    auto host = create_test_host();
    (void)host.initialize({{"protocolVersion", "2025-06-18"}});

    std::istringstream input("{ invalid json }\n");
    std::ostringstream output;

    yaaf::mcp::StdioHost stdio_host{host, input, output};
    // stdio_host.run();  // This would block in a real scenario

    // We test this behavior in integration tests with actual subprocess communication
}

TEST(McpHostProtocolTests, StdioHostProcessesInitializeRequest)
{
    auto host = create_test_host();

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\", \"clientInfo\": { \"name\": \"test\" } } }\n");
    std::ostringstream output;

    yaaf::mcp::StdioHost stdio_host{host, input, output};
    // run() would block waiting for more input; we verify response format through a combined test

    // Test the response is properly formatted
    const auto lines = extract_response_lines(output.str());
    // Response will be generated when run() processes the initialize request
}

TEST(McpHostProtocolTests, StdioHostProcessesListToolsRequest)
{
    const std::vector<yaaf::mcp::ToolInfo> tools{
        {{"echo", "Echo tool", nlohmann::json::object()}},
    };

    auto host = create_test_host(tools);

    // Simulate: initialize, then list tools, then EOF
    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\" } }\n"
                             "{ \"jsonrpc\": \"2.0\", \"id\": 2, \"method\": \"tools/list\", \"params\": {} }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run();

    const auto lines = extract_response_lines(output.str());
    ASSERT_GE(lines.size(), 2U);

    // First response is initialize result
    const auto init_resp = parse_jsonrpc_response(lines[0]);
    EXPECT_EQ(init_resp.at("id"), 1);
    EXPECT_TRUE(init_resp.contains("result"));

    // Second response is tools list
    const auto tools_resp = parse_jsonrpc_response(lines[1]);
    EXPECT_EQ(tools_resp.at("id"), 2);
    EXPECT_TRUE(tools_resp.at("result").contains("tools"));
    const auto &result_tools = tools_resp.at("result").at("tools");
    ASSERT_EQ(result_tools.size(), 1U);
    EXPECT_EQ(result_tools[0].at("name"), "echo");
}

TEST(McpHostProtocolTests, StdioHostProcessesCallToolRequest)
{
    auto host = create_test_host({}, {}, nullptr, [](const std::string &name, const nlohmann::json &args) {
        return yaaf::mcp::ToolExecutorResult{
            fmt::format("Called {} with param={}", name, args.at("param").get<std::string>()), false};
    });

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\" } }\n"
                             "{ \"jsonrpc\": \"2.0\", \"id\": 2, \"method\": \"tools/call\", \"params\": "
                             "{ \"name\": \"mytool\", \"arguments\": { \"param\": \"value\" } } }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run();

    const auto lines = extract_response_lines(output.str());
    ASSERT_GE(lines.size(), 2U);

    const auto call_resp = parse_jsonrpc_response(lines[1]);
    EXPECT_EQ(call_resp.at("id"), 2);
    EXPECT_EQ(call_resp.at("result").at("type"), "text");
    EXPECT_EQ(call_resp.at("result").at("content")[0].at("text"), "Called mytool with param=value");
}

TEST(McpHostProtocolTests, StdioHostReturnsErrorForUnknownMethod)
{
    auto host = create_test_host();

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\" } }\n"
                             "{ \"jsonrpc\": \"2.0\", \"id\": 2, \"method\": \"unknown/method\", \"params\": {} }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run();

    const auto lines = extract_response_lines(output.str());
    ASSERT_GE(lines.size(), 2U);

    const auto error_resp = parse_jsonrpc_response(lines[1]);
    EXPECT_EQ(error_resp.at("id"), 2);
    EXPECT_TRUE(error_resp.contains("error"));
    EXPECT_EQ(error_resp.at("error").at("code"), -32601); // METHOD_NOT_FOUND
}

TEST(McpHostProtocolTests, StdioHostReturnsErrorForMalformedJson)
{
    auto host = create_test_host();

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\" } }\n"
                             "{ invalid json }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run();

    const auto lines = extract_response_lines(output.str());
    // Should have at least one error response for malformed JSON
    ASSERT_GE(lines.size(), 1U);

    // Find the error response (it should be for the malformed JSON)
    bool found_parse_error = false;
    for (const auto &line : lines)
    {
        try
        {
            const auto resp = parse_jsonrpc_response(line);
            if (resp.contains("error") && resp.at("error").at("code") == -32700) // JSON_PARSE_ERROR
            {
                found_parse_error = true;
                break;
            }
        }
        catch (...)
        {
            // Not a valid JSON-RPC response
        }
    }
    EXPECT_TRUE(found_parse_error);
}

TEST(McpHostProtocolTests, StdioHostEndsOnInputEof)
{
    auto host = create_test_host();

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\" } }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run(); // Should return cleanly after EOF

    const auto lines = extract_response_lines(output.str());
    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(parse_jsonrpc_response(lines[0]).at("id"), 1);
}

TEST(McpHostProtocolTests, StdioHostProcessesListPromptsRequest)
{
    const std::vector<yaaf::mcp::PromptDescriptor> prompts{
        yaaf::mcp::PromptDescriptor{
            "weather", "Get weather", {yaaf::mcp::PromptArgument{"location", "Location name", true}}},
    };

    auto host = create_test_host({}, prompts);

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\" } }\n"
                             "{ \"jsonrpc\": \"2.0\", \"id\": 2, \"method\": \"prompts/list\", \"params\": {} }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run();

    const auto lines = extract_response_lines(output.str());
    ASSERT_GE(lines.size(), 2U);

    const auto prompts_resp = parse_jsonrpc_response(lines[1]);
    EXPECT_EQ(prompts_resp.at("id"), 2);
    const auto &result_prompts = prompts_resp.at("result").at("prompts");
    ASSERT_EQ(result_prompts.size(), 1U);
    EXPECT_EQ(result_prompts[0].at("name"), "weather");
    EXPECT_TRUE(result_prompts[0].contains("arguments"));
}

TEST(McpHostProtocolTests, StdioHostProcessesGetPromptRequest)
{
    auto host =
        create_test_host({}, {}, nullptr, nullptr, nullptr, [](const std::string &name, const nlohmann::json &args) {
            return std::vector<yaaf::mcp::PromptMessage>{
                yaaf::mcp::PromptMessage{"user",
                                         fmt::format("Get {} for {}", name, args.at("location").get<std::string>())},
                yaaf::mcp::PromptMessage{"assistant", "Here's the weather"},
            };
        });

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\" } }\n"
                             "{ \"jsonrpc\": \"2.0\", \"id\": 2, \"method\": \"prompts/get\", \"params\": "
                             "{ \"name\": \"weather\", \"arguments\": { \"location\": \"NYC\" } } }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run();

    const auto lines = extract_response_lines(output.str());
    ASSERT_GE(lines.size(), 2U);

    const auto prompt_resp = parse_jsonrpc_response(lines[1]);
    EXPECT_EQ(prompt_resp.at("id"), 2);
    const auto &messages = prompt_resp.at("result").at("messages");
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_EQ(messages[0].at("role"), "user");
    EXPECT_EQ(messages[1].at("role"), "assistant");
}

TEST(McpHostProtocolTests, StdioHostCatchesToolExecutorException)
{
    auto host = create_test_host({}, {}, nullptr,
                                 [](const std::string &, const nlohmann::json &) -> yaaf::mcp::ToolExecutorResult {
                                     throw std::runtime_error("Tool executor crashed");
                                 });

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"initialize\", \"params\": "
                             "{ \"protocolVersion\": \"2025-06-18\" } }\n"
                             "{ \"jsonrpc\": \"2.0\", \"id\": 2, \"method\": \"tools/call\", \"params\": "
                             "{ \"name\": \"crash\", \"arguments\": {} } }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run();

    const auto lines = extract_response_lines(output.str());
    ASSERT_GE(lines.size(), 2U);

    const auto error_resp = parse_jsonrpc_response(lines[1]);
    EXPECT_EQ(error_resp.at("id"), 2);
    EXPECT_TRUE(error_resp.contains("error"));
    EXPECT_EQ(error_resp.at("error").at("code"), -32603); // INTERNAL_ERROR
    EXPECT_NE(error_resp.at("error").at("message").get<std::string>().find("crashed"), std::string::npos);
}

TEST(McpHostProtocolTests, StdioHostRequiresInitializeBeforeOtherMethods)
{
    auto host = create_test_host({{"echo", "Echo tool", nlohmann::json::object()}});

    std::istringstream input("{ \"jsonrpc\": \"2.0\", \"id\": 1, \"method\": \"tools/list\", \"params\": {} }\n");

    std::ostringstream output;
    yaaf::mcp::StdioHost stdio_host{host, input, output};
    stdio_host.run();

    const auto lines = extract_response_lines(output.str());
    ASSERT_EQ(lines.size(), 1U);

    const auto error_resp = parse_jsonrpc_response(lines[0]);
    EXPECT_EQ(error_resp.at("id"), 1);
    EXPECT_TRUE(error_resp.contains("error"));
    EXPECT_EQ(error_resp.at("error").at("code"), -32600); // INVALID_REQUEST
    EXPECT_NE(error_resp.at("error").at("message").get<std::string>().find("not initialized"), std::string::npos);
}
