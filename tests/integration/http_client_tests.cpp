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
} // namespace

TEST(HttpClientTests, GetReturnsHttpBinPayload)
{
    HttpClient client{yaaf::tests::runtime_http_options()};

    const auto response = client.get("https://httpbin.org/get?yaaf=copilot");

    EXPECT_EQ(response.status_code, 200);
    EXPECT_NE(response.content_type.find("application/json"), std::string::npos);

    const auto payload = parse_json_body(response);

    ASSERT_TRUE(payload.contains("args"));
    ASSERT_TRUE(payload["args"].contains("yaaf"));
    EXPECT_EQ(payload["args"]["yaaf"], "copilot");
    ASSERT_TRUE(payload.contains("url"));
    EXPECT_EQ(payload["url"], "https://httpbin.org/get?yaaf=copilot");
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
    HttpClient client{yaaf::tests::runtime_http_options()};
    const auto request_body = R"({"message":"hello","count":2})";

    const auto response = client.post("https://postman-echo.com/post", request_body, "application/json");

    EXPECT_EQ(response.status_code, 200);
    EXPECT_NE(response.content_type.find("application/json"), std::string::npos);

    const auto payload = parse_json_body(response);

    ASSERT_TRUE(payload.contains("data"));
    ASSERT_TRUE(payload["data"].is_object());
    EXPECT_EQ(payload["data"]["message"], "hello");
    EXPECT_EQ(payload["data"]["count"], 2);
    ASSERT_TRUE(payload.contains("json"));
    ASSERT_TRUE(payload["json"].is_object());
    EXPECT_EQ(payload["json"]["message"], "hello");
    EXPECT_EQ(payload["json"]["count"], 2);
    ASSERT_TRUE(payload.contains("headers"));
    ASSERT_TRUE(payload["headers"].contains("content-type"));
    EXPECT_EQ(payload["headers"]["content-type"], "application/json");
}

TEST(HttpClientTests, MoveConstructionPreservesUsability)
{
    HttpClient original{yaaf::tests::runtime_http_options()};
    HttpClient moved(std::move(original));

    const auto response = moved.get("https://httpbin.org/get?yaaf=move-ctor");

    EXPECT_EQ(response.status_code, 200);

    const auto payload = parse_json_body(response);

    ASSERT_TRUE(payload.contains("args"));
    ASSERT_TRUE(payload["args"].contains("yaaf"));
    EXPECT_EQ(payload["args"]["yaaf"], "move-ctor");
}

TEST(HttpClientTests, MoveAssignmentPreservesUsability)
{
    HttpClient source{yaaf::tests::runtime_http_options()};
    HttpClient destination{yaaf::tests::runtime_http_options()};

    destination = std::move(source);

    const auto response = destination.get("https://httpbin.org/get?yaaf=move-assign");

    EXPECT_EQ(response.status_code, 200);

    const auto payload = parse_json_body(response);

    ASSERT_TRUE(payload.contains("args"));
    ASSERT_TRUE(payload["args"].contains("yaaf"));
    EXPECT_EQ(payload["args"]["yaaf"], "move-assign");
}
