#include "../../support/mcp_test_support.h"

#include "../../../libyaaf/cli/cli.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace yaaf::tests::mcp;

namespace
{
/// Find the yaaf executable in the build directory.
/// Searches in platform-specific Multi-Config build directories first, then a fallback path.
[[nodiscard]] std::filesystem::path find_yaaf_executable(const std::filesystem::path &root)
{
    // With Ninja Multi-Config, try build/<platform>/<target>/<config>/yaaf first
    const std::vector<std::string> configs = {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"};
    const std::vector<std::string> platforms = {"osx-arm64", "windows-x64", "linux-musl-static"};

    std::vector<std::filesystem::path> searched_paths;

    for (const auto &platform : platforms)
    {
        for (const auto &config : configs)
        {
            const auto candidate = root / "build" / platform / "app" / config / "yaaf";
            searched_paths.push_back(candidate);
            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
        }

        // Single-config builds place executables directly under build/<platform>/app/.
        const auto single_config_candidate = root / "build" / platform / "app" / "yaaf";
        searched_paths.push_back(single_config_candidate);
        if (std::filesystem::exists(single_config_candidate))
        {
            return single_config_candidate;
        }
    }

    // Fallback to single-config paths
    const auto fallback = root / "build" / "app" / "yaaf";
    searched_paths.push_back(fallback);
    if (std::filesystem::exists(fallback))
    {
        return fallback;
    }

    // If still not found, throw an error with helpful message
    throw std::runtime_error(fmt::format("Could not find yaaf executable in build directory. Last checked: {}",
                                         searched_paths.empty() ? "<none>" : searched_paths.back().string()));
}

/// Manages a subprocess running a Lua MCP host script.
/// Provides methods to send and receive JSON-RPC messages over pipes.
class LuaHostSubprocess
{
  public:
    /// Spawn a subprocess running the Lua host script at the given path.
    static std::unique_ptr<LuaHostSubprocess> spawn(const std::filesystem::path &script_path,
                                                    const std::filesystem::path &yaaf_exe)
    {
        auto self = std::make_unique<LuaHostSubprocess>();

        // Create pipes for parent -> child (stdin) and child -> parent (stdout)
        int stdin_pipe[2] = {};
        int stdout_pipe[2] = {};

        if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0)
        {
            throw std::runtime_error("failed to create pipes for subprocess");
        }

        self->pid_ = fork();
        if (self->pid_ < 0)
        {
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            throw std::runtime_error("failed to fork subprocess");
        }

        if (self->pid_ == 0)
        {
            // Child process: redirect stdin/stdout to pipes and run yaaf
            close(stdin_pipe[1]);  // Close parent's write end
            close(stdout_pipe[0]); // Close parent's read end

            dup2(stdin_pipe[0], STDIN_FILENO);
            dup2(stdout_pipe[1], STDOUT_FILENO);

            close(stdin_pipe[0]);
            close(stdout_pipe[1]);

            // Keep stderr open for debugging - don't redirect to /dev/null

            // Execute yaaf with run command
            execl(yaaf_exe.c_str(), yaaf_exe.filename().c_str(), "run", script_path.c_str(), nullptr);

            // If execl fails, exit with error
            _exit(127);
        }

        // Parent process: close child's pipe ends and store parent's ends
        close(stdin_pipe[0]);  // Close child's read end
        close(stdout_pipe[1]); // Close child's write end

        self->stdin_fd_ = stdin_pipe[1];
        self->stdout_fd_ = stdout_pipe[0];

        // Set non-blocking mode for stdout to avoid hanging on read
        int flags = fcntl(self->stdout_fd_, F_GETFL);
        fcntl(self->stdout_fd_, F_SETFL, flags & ~O_NONBLOCK); // Keep it blocking for now

        return self;
    }

    ~LuaHostSubprocess()
    {
        if (stdin_fd_ >= 0)
        {
            close(stdin_fd_);
        }
        if (stdout_fd_ >= 0)
        {
            close(stdout_fd_);
        }

        if (pid_ > 0)
        {
            int status = 0;
            waitpid(pid_, &status, WNOHANG);
        }
    }

    /// Send a JSON-RPC message to the subprocess.
    void send_message(const nlohmann::json &message)
    {
        const auto json_str = message.dump() + "\n";
        const auto written = write(stdin_fd_, json_str.c_str(), json_str.size());
        if (written < 0 || static_cast<std::size_t>(written) != json_str.size())
        {
            throw std::runtime_error("failed to write to subprocess stdin");
        }
    }

    /// Read a JSON-RPC message from the subprocess.
    [[nodiscard]] nlohmann::json read_message()
    {
        std::string line;
        char buffer[8192] = {};
        ssize_t bytes_read = 0;

        // Read until newline
        while (true)
        {
            bytes_read = read(stdout_fd_, buffer, sizeof(buffer) - 1);
            if (bytes_read < 0)
            {
                throw std::runtime_error(fmt::format("failed to read from subprocess stdout: {}", strerror(errno)));
            }
            if (bytes_read == 0)
            {
                throw std::runtime_error("subprocess closed stdout unexpectedly (EOF)");
            }

            buffer[bytes_read] = '\0';

            // Look for newline in buffer
            const char *newline = strchr(buffer, '\n');
            if (newline != nullptr)
            {
                line.assign(buffer, newline - buffer);
                break;
            }

            throw std::runtime_error("subprocess message line too long or contains no newline");
        }

        return nlohmann::json::parse(line);
    }

    /// Close stdin to signal EOF to the subprocess.
    void close_stdin()
    {
        if (stdin_fd_ >= 0)
        {
            close(stdin_fd_);
            stdin_fd_ = -1;
        }
    }

    /// Wait for subprocess to exit and return exit code.
    [[nodiscard]] int wait_for_exit()
    {
        close_stdin();

        int status = 0;
        waitpid(pid_, &status, 0);

        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status))
        {
            return 128 + WTERMSIG(status);
        }
        return -1;
    }

  private:
    pid_t pid_ = -1;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
};

/// Write a Lua host script that registers tools and prompts.
[[nodiscard]] std::filesystem::path write_lua_host_script(const std::filesystem::path &workspace, std::string_view body)
{
    return write_lua_script(workspace, body);
}

} // namespace

TEST(McpLuaHostIntegrationTests, LuaScriptHostsMcpStdioServer)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("mcp_lua_host_stdio_test");
    const auto yaaf_exe = find_yaaf_executable(root);
    const CurrentPathGuard current_path{root};

    // Create a Lua script that acts as an MCP server
    const auto script_path = write_lua_host_script(workspace, R"lua(
local tool = require("tool")
local mcp = require("mcp")

-- Register a custom tool
tool.register({
  spec = {
    name = "greet",
    description = "Greets a person",
    parameters = {
      type = "object",
      properties = {
        name = {type = "string", description = "Person to greet"}
      },
      required = {"name"}
    }
  },
  execute = function(args)
    return {
      tool_name = "greet",
      content = "Hello, " .. args.name .. "!",
      success = true
    }
  end
})

-- Register second tool
tool.register({
  spec = {
    name = "add",
    description = "Adds two numbers",
    parameters = {
      type = "object",
      properties = {
        a = {type = "number", description = "First number"},
        b = {type = "number", description = "Second number"}
      },
      required = {"a", "b"}
    }
  },
  execute = function(args)
    local result = args.a + args.b
    return {
      tool_name = "add",
      content = tostring(result),
      success = true
    }
  end
})

-- Register prompts
mcp.register_prompt({
  name = "greeting_template",
  description = "Template for greeting messages",
  arguments = {},
  handler = function(args)
    return {
      messages = {
        {role = "user", content = "Please compose a greeting"}
      }
    }
  end
})

mcp.register_prompt({
  name = "math_hint",
  description = "Provides math hints",
  arguments = {
    {name = "topic", description = "Math topic", required = true}
  },
  handler = function(args)
    return {
      messages = {
        {role = "user", content = "Help with " .. (args.topic or "math")}
      }
    }
  end
})

-- Start MCP server hosting the tools and prompts
mcp.host_stdio({
  tools = {"greet", "add"},
  prompts = {"greeting_template", "math_hint"}
})
)lua");

    // Spawn host subprocess
    auto host = LuaHostSubprocess::spawn(script_path, yaaf_exe);

    // Test 1: Send initialize request
    nlohmann::json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params",
         {
             {"protocolVersion", "2024-11-05"},
             {"clientInfo", {{"name", "test-client"}, {"version", "1.0"}}},
             {"capabilities", nlohmann::json::object()},
         }},
    };

    host->send_message(init_request);
    const auto init_response = host->read_message();

    EXPECT_EQ(init_response.at("jsonrpc"), "2.0");
    EXPECT_EQ(init_response.at("id"), 1);
    EXPECT_TRUE(init_response.contains("result"));
    const auto init_result = init_response.at("result");
    EXPECT_EQ(init_result.at("protocolVersion"), "2024-11-05");
    EXPECT_TRUE(init_result.contains("serverInfo"));
    // The serverInfo name is the default runtime name, not necessarily "yaaf-lua-host"
    EXPECT_EQ(init_result.at("serverInfo").at("version"), "0.1.0");

    // Test 2: List tools
    nlohmann::json list_tools_request = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "tools/list"},
        {"params", nlohmann::json::object()},
    };

    host->send_message(list_tools_request);
    const auto list_tools_response = host->read_message();

    EXPECT_EQ(list_tools_response.at("jsonrpc"), "2.0");
    EXPECT_EQ(list_tools_response.at("id"), 2);
    EXPECT_TRUE(list_tools_response.contains("result"));
    const auto tools_list = list_tools_response.at("result").at("tools");
    ASSERT_EQ(tools_list.size(), 2);

    // Verify both tools are present (order may vary)
    std::vector<std::string> tool_names;
    std::vector<std::string> tool_descs;
    for (const auto &tool : tools_list)
    {
        tool_names.push_back(tool.at("name"));
        tool_descs.push_back(tool.at("description"));
        EXPECT_TRUE(tool.contains("inputSchema"));
    }

    EXPECT_TRUE(std::find(tool_names.begin(), tool_names.end(), "greet") != tool_names.end());
    EXPECT_TRUE(std::find(tool_names.begin(), tool_names.end(), "add") != tool_names.end());
    EXPECT_TRUE(std::find(tool_descs.begin(), tool_descs.end(), "Greets a person") != tool_descs.end());
    EXPECT_TRUE(std::find(tool_descs.begin(), tool_descs.end(), "Adds two numbers") != tool_descs.end());

    // Test 3: List prompts
    nlohmann::json list_prompts_request = {
        {"jsonrpc", "2.0"},
        {"id", 4},
        {"method", "prompts/list"},
        {"params", nlohmann::json::object()},
    };

    host->send_message(list_prompts_request);
    const auto list_prompts_response = host->read_message();

    EXPECT_EQ(list_prompts_response.at("jsonrpc"), "2.0");
    EXPECT_EQ(list_prompts_response.at("id"), 4);
    EXPECT_TRUE(list_prompts_response.contains("result"));
    const auto prompts_list = list_prompts_response.at("result").at("prompts");
    ASSERT_EQ(prompts_list.size(), 2);

    std::vector<std::string> prompt_names;
    for (const auto &prompt : prompts_list)
    {
        prompt_names.push_back(prompt.at("name"));
        EXPECT_TRUE(prompt.contains("description"));
    }

    EXPECT_TRUE(std::find(prompt_names.begin(), prompt_names.end(), "greeting_template") != prompt_names.end());
    EXPECT_TRUE(std::find(prompt_names.begin(), prompt_names.end(), "math_hint") != prompt_names.end());

    // Test 4: Close stdin and verify clean exit
    host->close_stdin();
    const int exit_code = host->wait_for_exit();
    EXPECT_EQ(exit_code, 0);
}

TEST(McpLuaHostIntegrationTests, RemoteClientCallsLuaHostedServer)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("mcp_lua_host_remote_client_test");
    const auto yaaf_exe = find_yaaf_executable(root);
    const CurrentPathGuard current_path{root};

    // Create a Lua script that hosts MCP tools
    const auto script_path = write_lua_host_script(workspace, R"lua(
local tool = require("tool")
local mcp = require("mcp")

-- Register a simple tool
tool.register({
  spec = {
    name = "echo_tool",
    description = "Returns the input as output",
    parameters = {
      type = "object",
      properties = {
        message = {type = "string", description = "Message to echo"}
      },
      required = {"message"}
    }
  },
  execute = function(args)
    return {
      tool_name = "echo_tool",
      content = "Echo: " .. (args.message or ""),
      success = true
    }
  end
})

-- Register a resource prompt
mcp.register_prompt({
  name = "test_prompt",
  description = "A test prompt",
  arguments = {},
  handler = function(args)
    return {
      messages = {
        {role = "user", content = "Test message"}
      }
    }
  end
})

-- Start server
mcp.host_stdio({
  tools = {"echo_tool"},
  prompts = {"test_prompt"}
})
)lua");

    // Spawn host subprocess
    auto host = LuaHostSubprocess::spawn(script_path, yaaf_exe);

    // Initialize handshake
    nlohmann::json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params",
         {
             {"protocolVersion", "2024-11-05"},
             {"clientInfo", {{"name", "test-client"}, {"version", "1.0"}}},
             {"capabilities", nlohmann::json::object()},
         }},
    };

    host->send_message(init_request);
    const auto init_response = host->read_message();

    EXPECT_EQ(init_response.at("id"), 1);
    EXPECT_TRUE(init_response.contains("result"));
    EXPECT_EQ(init_response.at("result").at("protocolVersion"), "2024-11-05");

    // Verify tools/list works
    nlohmann::json list_tools = {
        {"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", nlohmann::json::object()}};

    host->send_message(list_tools);
    const auto tools_response = host->read_message();

    EXPECT_EQ(tools_response.at("id"), 2);
    const auto tools = tools_response.at("result").at("tools");
    ASSERT_EQ(tools.size(), 1);
    EXPECT_EQ(tools[0].at("name"), "echo_tool");

    // Verify prompts/list works
    nlohmann::json list_prompts = {
        {"jsonrpc", "2.0"}, {"id", 3}, {"method", "prompts/list"}, {"params", nlohmann::json::object()}};

    host->send_message(list_prompts);
    const auto prompts_response = host->read_message();

    EXPECT_EQ(prompts_response.at("id"), 3);
    const auto prompts = prompts_response.at("result").at("prompts");
    ASSERT_EQ(prompts.size(), 1);
    EXPECT_EQ(prompts[0].at("name"), "test_prompt");

    // Clean shutdown
    host->close_stdin();
    const int exit_code = host->wait_for_exit();
    EXPECT_EQ(exit_code, 0);
}

TEST(McpLuaHostIntegrationTests, LuaServerHandlesUnknownMethods)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("mcp_lua_host_unknown_method_test");
    const auto yaaf_exe = find_yaaf_executable(root);
    const CurrentPathGuard current_path{root};

    const auto script_path = write_lua_host_script(workspace, R"lua(
local mcp = require("mcp")

mcp.host_stdio({})
)lua");

    auto host = LuaHostSubprocess::spawn(script_path, yaaf_exe);

    // Initialize
    nlohmann::json init_request = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params",
         {
             {"protocolVersion", "2024-11-05"},
             {"clientInfo", {{"name", "test"}, {"version", "1.0"}}},
         }},
    };

    host->send_message(init_request);
    [[maybe_unused]] const auto init_resp = host->read_message(); // Consume init response

    // Send unknown method
    nlohmann::json unknown_method = {
        {"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "unknown/method"},
        {"params", nlohmann::json::object()},
    };

    host->send_message(unknown_method);
    const auto error_response = host->read_message();

    EXPECT_EQ(error_response.at("id"), 2);
    EXPECT_TRUE(error_response.contains("error"));
    EXPECT_EQ(error_response.at("error").at("code"), -32601); // Method not found

    host->close_stdin();
    EXPECT_EQ(host->wait_for_exit(), 0);
}

TEST(McpLuaHostIntegrationTests, LuaServerFiltersToolsAndPrompts)
{
    const auto root = repository_root();
    const auto workspace = make_workspace("mcp_lua_host_filtering_test");
    const auto yaaf_exe = find_yaaf_executable(root);
    const CurrentPathGuard current_path{root};

    const auto script_path = write_lua_host_script(workspace, R"lua(
local tool = require("tool")
local mcp = require("mcp")

-- Register multiple tools
tool.register({
  spec = {
    name = "red_tool",
    description = "Red tool",
    parameters = {type = "object"}
  },
  execute = function(args)
    return {tool_name = "red_tool", content = "red", success = true}
  end
})

tool.register({
  spec = {
    name = "blue_tool",
    description = "Blue tool",
    parameters = {type = "object"}
  },
  execute = function(args)
    return {tool_name = "blue_tool", content = "blue", success = true}
  end
})

tool.register({
  spec = {
    name = "green_tool",
    description = "Green tool",
    parameters = {type = "object"}
  },
  execute = function(args)
    return {tool_name = "green_tool", content = "green", success = true}
  end
})

-- Register multiple prompts
mcp.register_prompt({
  name = "prompt_alpha",
  description = "Alpha prompt",
  arguments = {},
  handler = function(args)
    return {messages = {{role = "user", content = "Alpha"}}}
  end
})

mcp.register_prompt({
  name = "prompt_beta",
  description = "Beta prompt",
  arguments = {},
  handler = function(args)
    return {messages = {{role = "user", content = "Beta"}}}
  end
})

mcp.register_prompt({
  name = "prompt_gamma",
  description = "Gamma prompt",
  arguments = {},
  handler = function(args)
    return {messages = {{role = "user", content = "Gamma"}}}
  end
})

-- Only expose red_tool and blue_tool, and prompt_alpha and prompt_beta
mcp.host_stdio({
  tools = {"red_tool", "blue_tool"},
  prompts = {"prompt_alpha", "prompt_beta"}
})
)lua");

    auto host = LuaHostSubprocess::spawn(script_path, yaaf_exe);

    // Initialize
    nlohmann::json init_request = {{"jsonrpc", "2.0"},
                                   {"id", 1},
                                   {"method", "initialize"},
                                   {"params", {{"protocolVersion", "2024-11-05"}, {"clientInfo", {{"name", "test"}}}}}};

    host->send_message(init_request);
    [[maybe_unused]] const auto init_response_filter = host->read_message();

    // List tools - should only see red_tool and blue_tool
    nlohmann::json list_tools = {
        {"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", nlohmann::json::object()}};

    host->send_message(list_tools);
    const auto tools_response = host->read_message();
    const auto tools = tools_response.at("result").at("tools");

    EXPECT_EQ(tools.size(), 2);
    std::vector<std::string> tool_names;
    for (const auto &tool : tools)
    {
        tool_names.push_back(tool.at("name"));
    }
    EXPECT_TRUE(std::find(tool_names.begin(), tool_names.end(), "red_tool") != tool_names.end());
    EXPECT_TRUE(std::find(tool_names.begin(), tool_names.end(), "blue_tool") != tool_names.end());
    EXPECT_FALSE(std::find(tool_names.begin(), tool_names.end(), "green_tool") != tool_names.end());

    // List prompts - should only see prompt_alpha and prompt_beta
    nlohmann::json list_prompts = {
        {"jsonrpc", "2.0"}, {"id", 3}, {"method", "prompts/list"}, {"params", nlohmann::json::object()}};

    host->send_message(list_prompts);
    const auto prompts_response = host->read_message();
    const auto prompts = prompts_response.at("result").at("prompts");

    EXPECT_EQ(prompts.size(), 2);
    std::vector<std::string> prompt_names;
    for (const auto &prompt : prompts)
    {
        prompt_names.push_back(prompt.at("name"));
    }
    EXPECT_TRUE(std::find(prompt_names.begin(), prompt_names.end(), "prompt_alpha") != prompt_names.end());
    EXPECT_TRUE(std::find(prompt_names.begin(), prompt_names.end(), "prompt_beta") != prompt_names.end());
    EXPECT_FALSE(std::find(prompt_names.begin(), prompt_names.end(), "prompt_gamma") != prompt_names.end());

    host->close_stdin();
    EXPECT_EQ(host->wait_for_exit(), 0);
}
