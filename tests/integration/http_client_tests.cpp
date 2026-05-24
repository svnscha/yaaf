#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include <gtest/gtest.h>

#include "../../libyaaf/http/http_client.h"
#include "../support/http_test_server.h"

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

[[nodiscard]] std::optional<std::string> response_header_value_case_insensitive(const HttpClient::Response &response,
                                                                                std::string_view key)
{
    const auto matches_key = [key](std::string_view candidate) {
        return candidate.size() == key.size() &&
               std::equal(candidate.begin(), candidate.end(), key.begin(),
                          [](const char lhs, const char rhs) {
                              return std::tolower(static_cast<unsigned char>(lhs)) ==
                                     std::tolower(static_cast<unsigned char>(rhs));
                          });
    };

    for (const auto &[name, value] : response.headers)
    {
        if (matches_key(name))
        {
            return value;
        }
    }

    return std::nullopt;
}

[[nodiscard]] yaaf::tests::http::Response json_response(const nlohmann::json &payload)
{
    yaaf::tests::http::Response response;
    response.status_code = 200;
    response.content_type = "application/json";
    response.body = payload.dump();
    return response;
}
} // namespace

TEST(HttpClientTests, GetReturnsHttpBinPayload)
{
    std::string request_url;
    yaaf::tests::http::LocalHttpServer server{[&](const yaaf::tests::http::Request &request) {
        EXPECT_EQ(request.method, "GET");
        EXPECT_EQ(request.target, "/get?yaaf=copilot");
        return json_response({{"args", {{"yaaf", "copilot"}}}, {"url", request_url}});
    }};
    request_url = server.base_url() + "/get?yaaf=copilot";
    HttpClient client;

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
    yaaf::tests::http::LocalHttpServer server{[](const yaaf::tests::http::Request &request) {
        EXPECT_EQ(request.method, "POST");
        EXPECT_EQ(request.target, "/post");
        nlohmann::json headers = nlohmann::json::object();
        for (const auto &[name, value] : request.headers)
        {
            headers[name] = value;
        }
        return json_response({{"data", request.body}, {"json", nlohmann::json::parse(request.body)}, {"headers", headers}});
    }};
    const auto request_url = server.base_url() + "/post";
    HttpClient client;
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

TEST(HttpClientTests, ExecuteSupportsPatchHeadersAndBody)
{
    yaaf::tests::http::LocalHttpServer server{[](const yaaf::tests::http::Request &request) {
        EXPECT_EQ(request.method, "PATCH");
        EXPECT_EQ(request.target, "/patch");
        nlohmann::json headers = nlohmann::json::object();
        for (const auto &[name, value] : request.headers)
        {
            headers[name] = value;
        }
        return json_response({{"data", request.body}, {"json", nlohmann::json::parse(request.body)}, {"headers", headers}});
    }};
    const auto request_url = server.base_url() + "/patch";
    HttpClient client;

    HttpClient::Request request;
    request.method = "PATCH";
    request.url = request_url;
    request.body = R"({"message":"patched","count":3})";
    request.content_type = "application/json";
    request.headers = {{"X-Yaaf-Request", "patch"}};

    const auto response = client.execute(request);

    EXPECT_EQ(response.status_code, 200);
    EXPECT_NE(response.content_type.find("application/json"), std::string::npos);

    const auto payload = parse_json_body(response);
    ASSERT_TRUE(payload.contains("data"));
    ASSERT_TRUE(payload["data"].is_string());
    EXPECT_EQ(payload["data"], *request.body);
    ASSERT_TRUE(payload.contains("json"));
    ASSERT_TRUE(payload["json"].is_object());
    EXPECT_EQ(payload["json"]["message"], "patched");
    EXPECT_EQ(payload["json"]["count"], 3);
    ASSERT_TRUE(payload.contains("headers"));
    const auto content_type = json_string_value_case_insensitive(payload["headers"], "content-type");
    ASSERT_TRUE(content_type.has_value());
    EXPECT_EQ(*content_type, "application/json");
    const auto forwarded_header = json_string_value_case_insensitive(payload["headers"], "x-yaaf-request");
    ASSERT_TRUE(forwarded_header.has_value());
    EXPECT_EQ(*forwarded_header, "patch");
}

TEST(HttpClientTests, ExecuteSupportsHeadResponses)
{
    yaaf::tests::http::LocalHttpServer server{[](const yaaf::tests::http::Request &request) {
        EXPECT_EQ(request.method, "HEAD");
        EXPECT_EQ(request.target, "/response-headers?X-Yaaf-Head=yes");
        yaaf::tests::http::Response response;
        response.status_code = 200;
        response.content_type.clear();
        response.headers = {{"X-Yaaf-Head", "yes"}};
        return response;
    }};
    const auto request_url = server.base_url() + "/response-headers?X-Yaaf-Head=yes";
    HttpClient client;

    HttpClient::Request request;
    request.method = "HEAD";
    request.url = request_url;

    const auto response = client.execute(request);

    EXPECT_EQ(response.status_code, 200);
    EXPECT_TRUE(response.body.empty());
    const auto head_header = response_header_value_case_insensitive(response, "X-Yaaf-Head");
    ASSERT_TRUE(head_header.has_value());
    EXPECT_EQ(*head_header, "yes");
}

TEST(HttpClientTests, ExecuteAppliesPerRequestTimeout)
{
    yaaf::tests::http::LocalHttpServer server{[](const yaaf::tests::http::Request &request) {
        EXPECT_EQ(request.method, "GET");
        EXPECT_EQ(request.target, "/delay/3");
        yaaf::tests::http::Response response;
        response.status_code = 200;
        response.content_type = "text/plain";
        response.body = "delayed";
        response.delay = std::chrono::seconds{3};
        return response;
    }};
    const auto request_url = server.base_url() + "/delay/3";
    HttpClient client;

    HttpClient::Request request;
    request.method = "GET";
    request.url = request_url;
    request.timeout = std::chrono::milliseconds{100};

    EXPECT_THROW(static_cast<void>(client.execute(request)), std::runtime_error);
}

TEST(HttpClientTests, MoveConstructionPreservesUsability)
{
    yaaf::tests::http::LocalHttpServer server{[](const yaaf::tests::http::Request &request) {
        EXPECT_EQ(request.method, "GET");
        EXPECT_EQ(request.target, "/get?yaaf=move-ctor");
        return json_response({{"args", {{"yaaf", "move-ctor"}}}});
    }};
    const auto request_url = server.base_url() + "/get?yaaf=move-ctor";
    HttpClient original;
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
    yaaf::tests::http::LocalHttpServer server{[](const yaaf::tests::http::Request &request) {
        EXPECT_EQ(request.method, "GET");
        EXPECT_EQ(request.target, "/get?yaaf=move-assign");
        return json_response({{"args", {{"yaaf", "move-assign"}}}});
    }};
    const auto request_url = server.base_url() + "/get?yaaf=move-assign";
    HttpClient source;
    HttpClient destination;

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
