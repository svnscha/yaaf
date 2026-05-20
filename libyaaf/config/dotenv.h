#pragma once

#include <filesystem>

namespace yaaf::dotenv
{
    class EnvironmentFile
    {
    public:
        [[nodiscard]] static EnvironmentFile load(std::string_view file_path);
        [[nodiscard]] static EnvironmentFile load_from_parents(std::string_view file_name = ".env");

        [[nodiscard]] bool contains(std::string_view key) const;
        [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
        [[nodiscard]] bool empty() const;

    private:
        std::vector<std::pair<std::string, std::string>> entries_;
    };
}
