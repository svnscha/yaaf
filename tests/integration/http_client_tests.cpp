#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "../../libyaaf/http/http_client.h"
#include "../support/runtime_test_environment.h"

namespace
{
[[nodiscard]] nlohmann::json parse_json_body(const HttpClient::Response &response)
{
    EXPECT_FALSE(response.body.empty());
    return nlohmann::json::parse(response.body);
}

[[nodiscard]] std::optional<std::string> json_string_or_singleton_array_value(const nlohmann::json &value)
{
    if (value.is_string())
    {
        return value.get<std::string>();
    }
    if (value.is_array() && value.size() == 1 && value.front().is_string())
    {
        return value.front().get<std::string>();
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> json_string_value_case_insensitive(const nlohmann::json &object,
                                                                            std::string_view key)
{
    if (!object.is_object())
    {
        return std::nullopt;
    }

    const auto matches_key = [key](std::string_view candidate) {
        return candidate.size() == key.size() &&
               std::equal(candidate.begin(), candidate.end(), key.begin(),
                          [](const char lhs, const char rhs) {
                              return std::tolower(static_cast<unsigned char>(lhs)) ==
                                     std::tolower(static_cast<unsigned char>(rhs));
                          });
    };

    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (matches_key(it.key()))
        {
            return json_string_or_singleton_array_value(it.value());
        }
    }

    return std::nullopt;
}

[[nodiscard]] bool httpbin_fixture_available(std::string_view base_url)
{
    try
    {
        const auto status_url = yaaf::tests::join_fixture_url(std::string(base_url), "/status/200");
        const auto response = HttpClient{yaaf::tests::runtime_http_options_for_url(status_url)}.get(status_url);
        if (response.status_code == 200)
        {
            return true;
        }
    }
    catch (const std::exception &)
    {
    }
    return false;
}
} // namespace

TEST(HttpClientTests, GetReturnsHttpBinPayload)
{
    const auto base_url = yaaf::tests::runtime_httpbin_base_url();
    if (!httpbin_fixture_available(base_url))
    {
        GTEST_SKIP() << "start the local test stack with docker compose -f docker-compose.test-stack.yml up";
        return;
    }
    const auto request_url = yaaf::tests::join_fixture_url(base_url, "/get?yaaf=copilot");
    HttpClient client{yaaf::tests::runtime_http_options_for_url(request_url)};

    const auto response = client.get(request_url);

    EXPECT_EQ(response.status_code, 200);
    EXPECT_NE(response.content_type.find("application/json"), std::string::npos);

    const auto payload = parse_json_body(response);

    ASSERT_TRUE(payload.contains("args"));
    ASSERT_TRUE(payload["args"].contains("yaaf"));
    const auto arg_value = json_string_or_singleton_array_value(payload["args"]["yaaf"]);
    ASSERT_TRUE(arg_value.has_value());
    EXPECT_EQ(*arg_value, "copilot");
    ASSERT_TRUE(payload.contains("url"));
    EXPECT_EQ(payload["url"], request_url);
}

TEST(HttpClientTests, ProxyOptionIsAppliedToRequests)
{
    HttpClient::Options options;
    options.proxy = "http://127.0.0.1:1";
    HttpClient client{std::move(options)};

    EXPECT_THROW(static_cast<void>(client.get("http://example.com")), std::runtime_error);
}

TEST(HttpClientTests, PostReturnsSubmittedJson)
{
    const auto base_url = yaaf::tests::runtime_httpbin_base_url();
    if (!httpbin_fixture_available(base_url))
    {
        GTEST_SKIP() << "start the local test stack with docker compose -f docker-compose.test-stack.yml up";
        return;
    }
    const auto request_url = yaaf::tests::join_fixture_url(base_url, "/post");
    HttpClient client{yaaf::tests::runtime_http_options_for_url(request_url)};
    const auto request_body = R"({"message":"hello","count":2})";

    const auto response = client.post(request_url, request_body, "application/json");

    EXPECT_EQ(response.status_code, 200);
    EXPECT_NE(response.content_type.find("application/json"), std::string::npos);

    const auto payload = parse_json_body(response);

    ASSERT_TRUE(payload.contains("data"));
    ASSERT_TRUE(payload["data"].is_string());
    EXPECT_EQ(payload["data"], request_body);
    ASSERT_TRUE(payload.contains("json"));
    ASSERT_TRUE(payload["json"].is_object());
    EXPECT_EQ(payload["json"]["message"], "hello");
    EXPECT_EQ(payload["json"]["count"], 2);
    ASSERT_TRUE(payload.contains("headers"));
    const auto content_type = json_string_value_case_insensitive(payload["headers"], "content-type");
    ASSERT_TRUE(content_type.has_value());
    EXPECT_EQ(*content_type, "application/json");
}

TEST(HttpClientTests, MoveConstructionPreservesUsability)
{
    const auto base_url = yaaf::tests::runtime_httpbin_base_url();
    if (!httpbin_fixture_available(base_url))
    {
        GTEST_SKIP() << "start the local test stack with docker compose -f docker-compose.test-stack.yml up";
        return;
    }
    const auto request_url = yaaf::tests::join_fixture_url(base_url, "/get?yaaf=move-ctor");
    HttpClient original{yaaf::tests::runtime_http_options_for_url(request_url)};
    HttpClient moved(std::move(original));

    const auto response = moved.get(request_url);

    EXPECT_EQ(response.status_code, 200);

    const auto payload = parse_json_body(response);

    ASSERT_TRUE(payload.contains("args"));
    ASSERT_TRUE(payload["args"].contains("yaaf"));
    const auto arg_value = json_string_or_singleton_array_value(payload["args"]["yaaf"]);
    ASSERT_TRUE(arg_value.has_value());
    EXPECT_EQ(*arg_value, "move-ctor");
}

TEST(HttpClientTests, MoveAssignmentPreservesUsability)
{
    const auto base_url = yaaf::tests::runtime_httpbin_base_url();
    if (!httpbin_fixture_available(base_url))
    {
        GTEST_SKIP() << "start the local test stack with docker compose -f docker-compose.test-stack.yml up";
        return;
    }
    const auto request_url = yaaf::tests::join_fixture_url(base_url, "/get?yaaf=move-assign");
    HttpClient source{yaaf::tests::runtime_http_options_for_url(request_url)};
    HttpClient destination{yaaf::tests::runtime_http_options_for_url(request_url)};

    destination = std::move(source);

    const auto response = destination.get(request_url);

    EXPECT_EQ(response.status_code, 200);

    const auto payload = parse_json_body(response);

    ASSERT_TRUE(payload.contains("args"));
    ASSERT_TRUE(payload["args"].contains("yaaf"));
    const auto arg_value = json_string_or_singleton_array_value(payload["args"]["yaaf"]);
    ASSERT_TRUE(arg_value.has_value());
    EXPECT_EQ(*arg_value, "move-assign");
}
