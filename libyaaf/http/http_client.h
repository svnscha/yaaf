#pragma once

#include <chrono>
#include <functional>

class HttpClient
{
  public:
    using Headers = std::vector<std::pair<std::string, std::string>>;
    using ResponseChunkHandler = std::function<void(std::string_view chunk)>;

    struct Response
    {
        long status_code = 0;
        std::string content_type;
        std::string body;
        Headers headers;
    };

    struct Request
    {
        std::string method = "GET";
        std::string url;
        Headers headers;
        std::optional<std::string> body;
        std::optional<std::string> content_type;
        std::optional<std::chrono::milliseconds> timeout;
        ResponseChunkHandler on_response_chunk;
    };

    struct Options
    {
        std::optional<std::string> proxy;
        bool allow_invalid_proxy_certificates = false;
    };

    HttpClient();
    explicit HttpClient(Options options);
    ~HttpClient();

    HttpClient(HttpClient &&other) noexcept;
    HttpClient &operator=(HttpClient &&other) noexcept;

    HttpClient(const HttpClient &other) = delete;
    HttpClient &operator=(const HttpClient &other) = delete;

    /**
     * Executes a single HTTP request.
     *
     * The optional timeout is a total request timeout, including connection and response transfer.
     *
     * @param request Request configuration including method, URL, headers, optional body, and optional timeout.
     * @return Completed HTTP response including status, response body, and parsed headers.
     * @throws std::runtime_error if libcurl setup or execution fails.
     */
    [[nodiscard]] Response execute(const Request &request) const;
    [[nodiscard]] Response get(std::string_view url) const;
    [[nodiscard]] Response get(std::string_view url, const Headers &headers) const;
    [[nodiscard]] Response post(std::string_view url, std::string_view body,
                                std::string_view content_type = "application/json") const;
    [[nodiscard]] Response post(std::string_view url, std::string_view body, std::string_view content_type,
                                const Headers &headers) const;
    [[nodiscard]] Response post(std::string_view url, std::string_view body, std::string_view content_type,
                                const ResponseChunkHandler &on_response_chunk) const;
    [[nodiscard]] Response post(std::string_view url, std::string_view body, std::string_view content_type,
                                const Headers &headers, const ResponseChunkHandler &on_response_chunk) const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
