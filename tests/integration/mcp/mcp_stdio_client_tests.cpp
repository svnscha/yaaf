#include "../../support/mcp_test_support.h"

#include "../../../libyaaf/cli/cli.h"

using namespace yaaf::tests::mcp;

TEST(McpStdioClientIntegrationTests, NativeClientListsAndCallsScriptedStdioServer)
{
    const auto workspace = make_workspace("assistant_mcp_real_stdio_test");
    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", scripted_stdio_server_config()}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.stdio_process_factory = scripted_stdio_process_factory();
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

TEST(McpStdioClientIntegrationTests, NativeClientPassesEnvFileAndEnvOverridesToScriptedStdioServer)
{
    const auto workspace = make_workspace("assistant_mcp_stdio_env_test");
    const auto env_file = workspace / "stdio.env";
    {
        std::ofstream output{env_file};
        output << "# ignored\n";
        output << "YAAF_MCP_ENV_FILE=from-file\n";
        output << "=ignored\n";
        output << "ignored-without-separator\n";
    }

    auto server_config = scripted_stdio_server_config({{"kind", "env"}});
    server_config["envFile"] = env_file.generic_string();
    server_config["env"] = {{"YAAF_MCP_ENV_INLINE", "from-env"}};
    write_mcp_config(workspace, nlohmann::json{{"servers", {{"env", server_config}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.stdio_process_factory = scripted_stdio_process_factory();
    yaaf::mcp::Client client{options};

    const auto result = client.call_tool("env", "env_values", nlohmann::json::object());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.content, "from-file|from-env");
}

TEST(McpStdioClientIntegrationTests, NativeClientMapsScriptedStdioToolFailures)
{
    const auto workspace = make_workspace("assistant_mcp_stdio_error_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", scripted_stdio_server_config({{"kind", "error"}})}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    options.stdio_process_factory = scripted_stdio_process_factory();
    yaaf::mcp::Client client{options};

    const auto tools = client.list_tools("hello");
    ASSERT_EQ(tools.size(), 1U);
    EXPECT_EQ(tools.front().name, "fail");
    EXPECT_EQ(tools.front().input_schema.at("type"), "object");

    const auto result = client.call_tool("hello", "fail", nlohmann::json{{"reason", "denied"}});

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.content, "denied");
}

TEST(McpStdioClientIntegrationTests, LuaMcpModuleUsesExplicitMcpConfigPath)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_lua_direct_module_test");
    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", scripted_stdio_server_config()}}}});
    const auto script_path = write_lua_script(workspace, R"lua(
local mcp = require("mcp")
local result = mcp.call_tool("hello", "repeat", { text = "Lua", count = 2 })
print(result.content)
)lua");
    const CurrentPathGuard current_path{root};

    yaaf::cli::Services services;
    services.mcp_stdio_process_factory = scripted_stdio_process_factory();

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"run", "--mcp", (workspace_mcp_config_path(workspace)).string(), script_path.string()}, input,
                       output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "Lua Lua\n");
}

TEST(McpStdioClientIntegrationTests, LuaScriptUsesExplicitMcpConfigPath)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_lua_script_config_test");
    write_mcp_config(workspace, nlohmann::json{{"servers", {{"hello", scripted_stdio_server_config()}}}});
    const auto script_path = write_lua_script(workspace, R"lua(
local tool = require("tool")
local result = tool.execute({ "hello.hello" }, "hello.hello", { name = "Lua" })
print(result.content)
)lua");
    const CurrentPathGuard current_path{root};

    yaaf::cli::Services services;
    services.mcp_stdio_process_factory = scripted_stdio_process_factory();

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"run", "--mcp", (workspace_mcp_config_path(workspace)).string(), script_path.string()}, input,
                       output, error_output, &services);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "Hello, Lua!\n");
}

