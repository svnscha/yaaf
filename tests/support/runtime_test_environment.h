#pragma once

#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include "../../libyaaf/config/dotenv.h"
#include "../../libyaaf/http/http_client.h"

namespace yaaf::tests
{
[[nodiscard]] inline std::filesystem::path repository_root()
{
    auto path = std::filesystem::current_path();
    while (!path.empty())
    {
        if (std::filesystem::exists(path / "CMakeLists.txt") && std::filesystem::exists(path / "libyaaf") &&
            std::filesystem::exists(path / "lua"))
        {
            return path;
        }

        const auto parent = path.parent_path();
        if (parent == path)
        {
            break;
        }
        path = parent;
    }

    auto source_path = std::filesystem::path{__FILE__};
    if (source_path.is_relative())
    {
        source_path = std::filesystem::absolute(source_path);
    }
    return source_path.parent_path().parent_path().parent_path();
}

[[nodiscard]] inline yaaf::dotenv::EnvironmentFile load_runtime_dotenv()
{
    return yaaf::dotenv::EnvironmentFile::load((repository_root() / ".env").string());
}

[[nodiscard]] inline std::optional<std::string> runtime_env_value(const yaaf::dotenv::EnvironmentFile &dotenv,
                                                                  std::string_view key)
{
    const auto value = std::getenv(std::string(key).c_str());
    if (value != nullptr && !std::string_view(value).empty())
    {
        return std::string(value);
    }
    return dotenv.get(key);
}

[[nodiscard]] inline HttpClient::Options runtime_http_options()
{
    const auto dotenv = load_runtime_dotenv();
    HttpClient::Options options;
    options.proxy = runtime_env_value(dotenv, "YAAF_PROXY");
    options.allow_invalid_proxy_certificates = options.proxy.has_value() && !options.proxy->empty();
    return options;
}

[[nodiscard]] inline std::string runtime_fixture_url(std::string_view key, std::string fallback)
{
    const auto dotenv = load_runtime_dotenv();
    return runtime_env_value(dotenv, key).value_or(std::move(fallback));
}

[[nodiscard]] inline std::string join_fixture_url(std::string base_url, std::string_view path)
{
    if (base_url.empty())
    {
        return std::string(path);
    }

    if (!path.empty() && path.front() != '/' && !base_url.ends_with('/'))
    {
        base_url.push_back('/');
    }
    else if (!path.empty() && path.front() == '/' && base_url.ends_with('/'))
    {
        base_url.pop_back();
    }

    base_url.append(path);
    return base_url;
}

[[nodiscard]] inline std::string runtime_httpbin_base_url()
{
    return runtime_fixture_url("YAAF_HTTPBIN_URL", "http://127.0.0.1:18082");
}

[[nodiscard]] inline std::string runtime_httpbin_proxied_base_url()
{
    return runtime_fixture_url("YAAF_HTTPBIN_PROXIED_URL", "http://host.docker.internal:18082");
}

[[nodiscard]] inline std::string runtime_httpbin_url(std::string_view path)
{
    return join_fixture_url(runtime_httpbin_base_url(), path);
}

[[nodiscard]] inline bool is_loopback_url(std::string_view url)
{
    return url.starts_with("http://127.") || url.starts_with("https://127.") || url.starts_with("http://localhost") ||
           url.starts_with("https://localhost") || url.starts_with("http://[::1]") || url.starts_with("https://[::1]");
}

[[nodiscard]] inline HttpClient::Options runtime_http_options_for_url(std::string_view url)
{
    return is_loopback_url(url) ? HttpClient::Options{} : runtime_http_options();
}

[[nodiscard]] inline std::filesystem::path lua_root()
{
    return repository_root() / "lua";
}
} // namespace yaaf::tests
