#include "config/dotenv.h"

#include <fstream>

namespace
{
    [[nodiscard]] std::string trim(std::string_view value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
        {
            return {};
        }

        const auto last = value.find_last_not_of(" \t\r\n");
        return std::string(value.substr(first, last - first + 1));
    }

    [[nodiscard]] std::string strip_quotes(std::string value)
    {
        if (value.size() < 2)
        {
            return value;
        }

        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
        {
            return value.substr(1, value.size() - 2);
        }

        return value;
    }

    void parse_line(
        const std::string &line,
        std::vector<std::pair<std::string, std::string>> &entries)
    {
        const std::string trimmed_line = trim(line);
        if (trimmed_line.empty() || trimmed_line.front() == '#')
        {
            return;
        }

        constexpr std::string_view export_prefix = "export ";
        const std::string_view line_view = trimmed_line;
        const std::string_view without_export = line_view.starts_with(export_prefix)
                                                    ? line_view.substr(export_prefix.size())
                                                    : line_view;

        const auto separator = without_export.find('=');
        if (separator == std::string_view::npos)
        {
            return;
        }

        std::string key = trim(without_export.substr(0, separator));
        if (key.empty())
        {
            return;
        }

        std::string value = strip_quotes(trim(without_export.substr(separator + 1)));

        const auto existing = std::find_if(
            entries.begin(),
            entries.end(),
            [&key](const auto &entry)
            {
                return entry.first == key;
            });

        if (existing != entries.end())
        {
            existing->second = std::move(value);
            return;
        }

        entries.emplace_back(std::move(key), std::move(value));
    }
}

namespace yaaf::dotenv
{
    EnvironmentFile EnvironmentFile::load(std::string_view file_path)
    {
        EnvironmentFile result;

        std::ifstream input{std::filesystem::path(file_path)};
        if (!input)
        {
            return result;
        }

        std::string line;
        while (std::getline(input, line))
        {
            parse_line(line, result.entries_);
        }

        return result;
    }

    EnvironmentFile EnvironmentFile::load_from_parents(std::string_view file_name)
    {
        auto current = std::filesystem::current_path();
        const auto env_name = std::filesystem::path(file_name);

        while (true)
        {
            const auto candidate = current / env_name;
            if (std::filesystem::exists(candidate))
            {
                return load(candidate.string());
            }

            if (!current.has_parent_path() || current == current.parent_path())
            {
                break;
            }

            current = current.parent_path();
        }

        return {};
    }

    bool EnvironmentFile::contains(std::string_view key) const
    {
        return std::any_of(
            entries_.begin(),
            entries_.end(),
            [key](const auto &entry)
            {
                return entry.first == key;
            });
    }

    std::optional<std::string> EnvironmentFile::get(std::string_view key) const
    {
        const auto it = std::find_if(
            entries_.begin(),
            entries_.end(),
            [key](const auto &entry)
            {
                return entry.first == key;
            });

        if (it == entries_.end())
        {
            return std::nullopt;
        }

        return it->second;
    }

    bool EnvironmentFile::empty() const
    {
        return entries_.empty();
    }
}
