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
            return HttpClient::Response{202, "", ""};
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
        return HttpClient::Response{500, "text/plain", "unexpected"};
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
            return HttpClient::Response{503, "text/plain", "unavailable"};
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
            return HttpClient::Response{200, "application/json", ""};
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
            return HttpClient::Response{200, "text/event-stream", "event: message\n\n"};
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
            return HttpClient::Response{200, "text/event-stream",
                                        "event: message\r\n"
                                        "data: {\"jsonrpc\":\"2.0\",\r\n"
                                        "data: \"id\":" +
                                            std::to_string(request.at("id").get<int>()) +
                                            ",\r\n"
                                            "data: \"result\":{\"protocolVersion\":\"2025-06-18\",\r\n"
                                            "data: \"capabilities\":{\"tools\":{}},\r\n"
                                            "data: \"serverInfo\":{\"name\":\"docs\",\"version\":\"1\"}}}"};
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{500, "text/plain", "notification failed"};
        }
        return HttpClient::Response{500, "text/plain", "unexpected"};
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
            return HttpClient::Response{202, "", ""};
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
        return HttpClient::Response{202, "", ""};
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
            return HttpClient::Response{202, "", ""};
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
            return HttpClient::Response{202, "", ""};
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
                                      {"local",
                                       {{"type", "stdio"},
                                        {"env", {{"API_TOKEN", "stdio-secret"}}}}}}}});

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
            payload["result"]["serverInfo"] = {{"name", url_string.find("docs") != std::string::npos ? "docs" : "broken"},
                                                {"version", "1"}};
            return json_response(payload);
        }
        if (method == "notifications/initialized")
        {
            return HttpClient::Response{202, "", ""};
        }
        if (method == "tools/list" && url_string.find("docs") != std::string::npos)
        {
            nlohmann::json payload;
            payload["jsonrpc"] = "2.0";
            payload["id"] = request.at("id");
            payload["result"]["tools"] = nlohmann::json::array(
                {{{"name", "lookup"}, {"title", "Lookup"}, {"description", "Look up docs"}}});
            return json_response(payload);
        }
        if (method == "tools/list" && url_string.find("broken") != std::string::npos)
        {
            return HttpClient::Response{503, "text/plain", "broken"};
        }
        return HttpClient::Response{500, "text/plain", "unexpected"};
    };

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;
    const auto exit_code =
        yaaf::cli::run({"--mcp", mcp_path.string(), "doctor", "--format", "json"}, input, output, error_output,
                       &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());

    const auto payload = nlohmann::json::parse(output.str());
    ASSERT_TRUE(payload.contains("mcp"));
    EXPECT_TRUE(payload.at("mcp").at("exists"));
    ASSERT_EQ(payload.at("mcp").at("servers").size(), 3U);

    const auto &servers = payload.at("mcp").at("servers");
    const auto find_server = [&](std::string_view id) -> const nlohmann::json & {
        const auto found = std::find_if(servers.begin(), servers.end(), [&](const auto &server) {
            return server.at("id").get<std::string>() == id;
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
    write_mcp_config(workspace,
                     nlohmann::json{{"servers",
                                     {{"docs",
                                       {{"type", "http"},
                                        {"url", "https://docs.example.test/mcp"},
                                        {"headers", {{"Authorization", "Bearer docs-secret"}}}}}}}});

    yaaf::cli::Services services;
    services.mcp_http_post = [](std::string_view, std::string_view body, std::string_view,
                                const yaaf::mcp::Headers &) {
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
            return HttpClient::Response{202, "", ""};
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
    const auto exit_code = yaaf::cli::run({"--mcp", mcp_path.string(), "doctor"}, input, output, error_output,
                                          &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_NE(output.str().find("initialize: ok (protocol 2025-06-18)"), std::string::npos);
    EXPECT_NE(output.str().find("tools: 1 discovered: docs.lookup"), std::string::npos);
}
