#include "mcp_client_stdio.h"

namespace yaaf::mcp::detail {

McpStdioProcessWrapper::McpStdioProcessWrapper(const nlohmann::json& config) {
    // Extract command (required)
    const auto command = json_string_value(config, "command");
    if (command.empty())
    {
        throw std::runtime_error("stdio MCP server requires command");
    }

    // Extract args
    std::vector<std::string> args;
    if (const auto args_field = config.find("args"); args_field != config.end() && args_field->is_array())
    {
        for (const auto &arg : *args_field)
        {
            if (arg.is_string())
            {
                args.push_back(arg.get<std::string>());
            }
        }
    }

    // Extract environment overrides (from both "env" and "envFile")
    auto env_overrides = read_environment_overrides(config);

    // Build process options and spawn
    yaaf::process::ProcessOptions options{
        .command = command,
        .args = args,
        .working_directory = {}, // Not used by MCP stdio
        .env_overrides = env_overrides,
        .inherit_parent_env = true, // MCP always merges with parent env
    };

    platform_process_ = yaaf::process::start_process(options);
}

McpStdioProcessWrapper::~McpStdioProcessWrapper()
{
    if (platform_process_)
    {
        platform_process_->shutdown(std::chrono::seconds(1));
    }
}

void McpStdioProcessWrapper::write_message(std::string_view line)
{
    if (!platform_process_)
    {
        throw std::runtime_error("MCP process not initialized");
    }
    platform_process_->write(line);
}

nlohmann::json McpStdioProcessWrapper::read_message(std::chrono::milliseconds timeout)
{
    if (!platform_process_)
    {
        throw std::runtime_error("MCP process not initialized");
    }

    auto result = platform_process_->read_line(timeout);

    if (result.timed_out)
    {
        throw std::runtime_error("timed out waiting for MCP stdio response");
    }

    if (result.process_exited)
    {
        throw std::runtime_error("MCP stdio server exited before replying");
    }

    return nlohmann::json::parse(result.data);
}

std::unique_ptr<StdioPlatformProcess> start_stdio_server(const nlohmann::json& raw) {
    return std::make_unique<McpStdioProcessWrapper>(raw);
}

}  // namespace yaaf::mcp::detail
