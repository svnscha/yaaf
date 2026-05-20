#pragma once

#include "mcp_client.h"

namespace yaaf::mcp::detail
{
class StdioPlatformProcess
{
  public:
    virtual ~StdioPlatformProcess() = default;
    virtual void write_message(std::string_view line) = 0;
    [[nodiscard]] virtual nlohmann::json read_message(std::chrono::milliseconds timeout) = 0;
};

[[nodiscard]] std::unique_ptr<StdioPlatformProcess> start_stdio_server(const nlohmann::json &raw);

[[nodiscard]] inline std::string json_string_value(const nlohmann::json &raw, const char *key)
{
    if (const auto entry = raw.find(key); entry != raw.end() && entry->is_string())
    {
        return entry->get<std::string>();
    }
    return {};
}

[[nodiscard]] inline std::map<std::string, std::string> read_environment_overrides(const nlohmann::json &raw)
{
    std::map<std::string, std::string> overrides;
    if (const auto env_file = raw.find("envFile"); env_file != raw.end() && env_file->is_string())
    {
        std::ifstream input{env_file->get<std::string>()};
        std::string line;
        while (std::getline(input, line))
        {
            const auto separator = line.find('=');
            if (separator == std::string::npos || separator == 0 || line.front() == '#')
            {
                continue;
            }
            overrides[line.substr(0, separator)] = line.substr(separator + 1);
        }
    }

    if (const auto env = raw.find("env"); env != raw.end() && env->is_object())
    {
        for (auto it = env->begin(); it != env->end(); ++it)
        {
            if (it.value().is_string())
            {
                overrides[it.key()] = it.value().get<std::string>();
            }
        }
    }

    return overrides;
}
} // namespace yaaf::mcp::detail
