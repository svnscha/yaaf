#include "../support/mcp_test_support.h"

#include "../../libyaaf/cli/cli.h"

using namespace yaaf::tests::mcp;

namespace
{
} // namespace

TEST(McpClientIntegrationTests, NativeClientListsAndCallsRealUvStdioServer)
{
    if (!executable_on_path("uv"))
    {
        GTEST_SKIP() << "uv is required for real MCP fixture server tests";
    }

    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_real_stdio_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", uv_stdio_server_config(root, "hello_stdio.py")}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    yaaf::mcp::Client client{options};
    expect_hello_tools(client, "hello");
}

TEST(McpClientIntegrationTests, NativeClientPassesEnvFileAndEnvOverridesToRealStdioServer)
{
    if (!executable_on_path("uv"))
    {
        GTEST_SKIP() << "uv is required for real MCP fixture server tests";
    }

    const auto workspace = make_workspace("assistant_mcp_stdio_env_test");
    const auto script_path = workspace / "env_stdio.py";
    {
        std::ofstream script{script_path};
        script << R"python(
from __future__ import annotations

import json
import os
import sys

TOOLS = [{
    "name": "env_values",
    "description": "Return selected environment values.",
    "inputSchema": {"type": "object"},
}]

def respond(request_id, result):
    print(json.dumps({"jsonrpc": "2.0", "id": request_id, "result": result}, separators=(",", ":")), flush=True)

for line in sys.stdin:
    if not line.strip():
        continue
    message = json.loads(line)
    request_id = message.get("id")
    if request_id is None:
        continue
    method = message.get("method")
    if method == "initialize":
        respond(request_id, {
            "protocolVersion": message.get("params", {}).get("protocolVersion", "2025-11-25"),
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "env-stdio", "version": "1"},
        })
    elif method == "tools/list":
        respond(request_id, {"tools": TOOLS})
    elif method == "tools/call":
        payload = "|".join([
            os.environ.get("YAAF_MCP_ENV_FILE", ""),
            os.environ.get("YAAF_MCP_ENV_INLINE", ""),
        ])
        respond(request_id, {"content": [{"type": "text", "text": payload}], "isError": False})
    else:
        print(json.dumps({"jsonrpc": "2.0", "id": request_id, "error": {"code": -32601, "message": method}}, separators=(",", ":")), flush=True)
)python";
    }

    const auto env_file = workspace / "stdio.env";
    {
        std::ofstream output{env_file};
        output << "# ignored\n";
        output << "YAAF_MCP_ENV_FILE=from-file\n";
        output << "=ignored\n";
        output << "ignored-without-separator\n";
    }

    nlohmann::json server_config;
    server_config["type"] = "stdio";
    server_config["command"] = "uv";
    server_config["args"] =
        nlohmann::json::array({"--directory", workspace.generic_string(), "run", "python", "env_stdio.py"});
    server_config["envFile"] = env_file.generic_string();
    server_config["env"] = {{"YAAF_MCP_ENV_INLINE", "from-env"}};
    write_mcp_config(workspace, nlohmann::json{{"servers", {{"env", server_config}}}});

    yaaf::mcp::ClientOptions options;
    options.workspace_root = workspace;
    options.config_path = workspace_mcp_config_path(workspace);
    yaaf::mcp::Client client{options};

    const auto result = client.call_tool("env", "env_values", nlohmann::json::object());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.content, "from-file|from-env");
}

TEST(McpClientIntegrationTests, LuaScriptUsesExplicitMcpConfigPath)
{
    if (!executable_on_path("uv"))
    {
        GTEST_SKIP() << "uv is required for real MCP fixture server tests";
    }

    const auto root = repository_root();
    const auto workspace = make_workspace("assistant_mcp_lua_script_config_test");
    write_mcp_config(workspace,
                     nlohmann::json{{"servers", {{"hello", uv_stdio_server_config(root, "hello_stdio.py")}}}});
    const auto script_path = write_lua_script(workspace, R"lua(
local tool = require("tool")
local result = tool.execute({ "hello.hello" }, "hello.hello", { name = "Lua" })
print(result.content)
)lua");
    const CurrentPathGuard current_path{root};

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error_output;

    const auto exit_code =
        yaaf::cli::run({"run", "--mcp", (workspace_mcp_config_path(workspace)).string(), script_path.string()}, input,
                       output, error_output);

    EXPECT_EQ(exit_code, EXIT_SUCCESS);
    EXPECT_TRUE(error_output.str().empty());
    EXPECT_EQ(output.str(), "Hello, Lua!\n");
}
