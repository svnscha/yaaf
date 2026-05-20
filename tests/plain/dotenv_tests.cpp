#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "../../libyaaf/config/dotenv.h"

namespace
{
class ScopedCurrentPath
{
  public:
    explicit ScopedCurrentPath(const std::filesystem::path &path) : original_(std::filesystem::current_path())
    {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath()
    {
        std::filesystem::current_path(original_);
    }

    ScopedCurrentPath(const ScopedCurrentPath &) = delete;
    ScopedCurrentPath &operator=(const ScopedCurrentPath &) = delete;

  private:
    std::filesystem::path original_;
};

[[nodiscard]] std::filesystem::path make_test_directory(std::string_view name)
{
    const auto path = std::filesystem::temp_directory_path() / fmt::format("yaaf-{}-{}", name, std::rand());
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}
} // namespace

TEST(DotenvTests, LoadParsesBasicEntriesQuotesAndOverrides)
{
    const auto directory = make_test_directory("dotenv-parse");
    const auto env_path = directory / ".env";

    {
        std::ofstream output(env_path);
        output << "# comment\n";
        output << "OLLAMA_ENDPOINT=http://localhost:11434\n";
        output << " export OLLAMA_MODEL = \"qwen3:0.6b\"\n";
        output << "OLLAMA_MODEL=nomic-embed-text:v1.5\n";
        output << "EMPTY_VALUE=\n";
    }

    const auto dotenv = yaaf::dotenv::EnvironmentFile::load(env_path.string());

    EXPECT_FALSE(dotenv.empty());
    EXPECT_EQ(dotenv.get("OLLAMA_ENDPOINT"), std::optional<std::string>{"http://localhost:11434"});
    EXPECT_EQ(dotenv.get("OLLAMA_MODEL"), std::optional<std::string>{"nomic-embed-text:v1.5"});
    EXPECT_EQ(dotenv.get("EMPTY_VALUE"), std::optional<std::string>{""});
    EXPECT_FALSE(dotenv.contains("MISSING_KEY"));

    std::filesystem::remove_all(directory);
}

TEST(DotenvTests, LoadFromParentsFindsNearestAncestorFile)
{
    const auto root = make_test_directory("dotenv-parent");
    const auto nested = root / "a" / "b" / "c";
    std::filesystem::create_directories(nested);

    {
        std::ofstream output(root / ".env");
        output << "OLLAMA_ENDPOINT=https://example.invalid\n";
    }

    {
        const ScopedCurrentPath scoped_path(nested);
        const auto dotenv = yaaf::dotenv::EnvironmentFile::load_from_parents();

        EXPECT_EQ(dotenv.get("OLLAMA_ENDPOINT"), std::optional<std::string>{"https://example.invalid"});
    }

    std::filesystem::remove_all(root);
}

TEST(DotenvTests, LoadMissingFileReturnsEmptyEnvironment)
{
    const auto dotenv = yaaf::dotenv::EnvironmentFile::load("this-file-does-not-exist.env");

    EXPECT_TRUE(dotenv.empty());
    EXPECT_FALSE(dotenv.contains("OLLAMA_ENDPOINT"));
    EXPECT_EQ(dotenv.get("OLLAMA_ENDPOINT"), std::nullopt);
}

TEST(DotenvTests, LoadIgnoresMalformedLinesAndEmptyKeys)
{
    const auto directory = make_test_directory("dotenv-malformed");
    const auto env_path = directory / ".env";

    {
        std::ofstream output(env_path);
        output << "MISSING_SEPARATOR\n";
        output << "=value-without-key\n";
        output << "VALID_KEY=expected\n";
    }

    const auto dotenv = yaaf::dotenv::EnvironmentFile::load(env_path.string());

    EXPECT_FALSE(dotenv.empty());
    EXPECT_EQ(dotenv.get("VALID_KEY"), std::optional<std::string>{"expected"});
    EXPECT_EQ(dotenv.get("MISSING_SEPARATOR"), std::nullopt);

    std::filesystem::remove_all(directory);
}

TEST(DotenvTests, LoadFromParentsReturnsEmptyWhenNoFileExists)
{
    const auto root = make_test_directory("dotenv-not-found");
    const auto nested = root / "a" / "b" / "c";
    std::filesystem::create_directories(nested);

    {
        const ScopedCurrentPath scoped_path(nested);
        const auto dotenv = yaaf::dotenv::EnvironmentFile::load_from_parents();

        EXPECT_TRUE(dotenv.empty());
        EXPECT_EQ(dotenv.get("OLLAMA_ENDPOINT"), std::nullopt);
    }

    std::filesystem::remove_all(root);
}
