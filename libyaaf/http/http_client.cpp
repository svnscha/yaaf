#include "http_client.h"

#include <curl/curl.h>

namespace
{
struct ResponseSink
{
    std::string body;
    HttpClient::Headers headers;
    const HttpClient::ResponseChunkHandler *chunk_handler = nullptr;
    std::exception_ptr callback_error;
};

class CurlGlobalGuard
{
  public:
    CurlGlobalGuard()
    {
        const auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (result != CURLE_OK)
        {
            throw std::runtime_error(fmt::format("curl_global_init failed: {}", curl_easy_strerror(result)));
        }
    }

    ~CurlGlobalGuard()
    {
        curl_global_cleanup();
    }

    CurlGlobalGuard(const CurlGlobalGuard &) = delete;
    CurlGlobalGuard &operator=(const CurlGlobalGuard &) = delete;
};

CurlGlobalGuard &global_curl_guard()
{
    static CurlGlobalGuard guard;
    return guard;
}

[[nodiscard]] std::size_t append_response_data(const char *data, const std::size_t size, const std::size_t count,
                                               void *user_data)
{
    auto *sink = static_cast<ResponseSink *>(user_data);
    const auto bytes = size * count;

    sink->body.append(data, bytes);

    if (sink->chunk_handler != nullptr)
    {
        try
        {
            (*sink->chunk_handler)(std::string_view(data, bytes));
        }
        catch (...)
        {
            sink->callback_error = std::current_exception();
            return 0;
        }
    }

    return bytes;
}

[[nodiscard]] std::size_t append_header_data(const char *data, const std::size_t size, const std::size_t count,
                                             void *user_data)
{
    auto *sink = static_cast<ResponseSink *>(user_data);
    const auto bytes = size * count;
    std::string_view line{data, bytes};
    if (!line.empty() && line.back() == '\n')
    {
        line.remove_suffix(1);
    }
    if (!line.empty() && line.back() == '\r')
    {
        line.remove_suffix(1);
    }

    const auto separator = line.find(':');
    if (separator != std::string_view::npos)
    {
        auto name = std::string(line.substr(0, separator));
        auto value = std::string(line.substr(separator + 1));
        while (!value.empty() && value.front() == ' ')
        {
            value.erase(value.begin());
        }
        sink->headers.emplace_back(std::move(name), std::move(value));
    }

    return bytes;
}

void throw_if_curl_failed(const CURLcode code, std::string_view action)
{
    if (code == CURLE_OK)
    {
        return;
    }

    throw std::runtime_error(fmt::format("{}: {}", action, curl_easy_strerror(code)));
}
} // namespace

class HttpClient::Impl
{
  public:
    explicit Impl(Options options) : options_(std::move(options))
    {
    }

    [[nodiscard]] Response get(std::string_view url) const
    {
        return perform(url, nullptr, std::nullopt, nullptr, {});
    }

    [[nodiscard]] Response get(std::string_view url, const Headers &headers) const
    {
        return perform(url, nullptr, std::nullopt, nullptr, headers);
    }

    [[nodiscard]] Response post(std::string_view url, std::string_view body, std::string_view content_type) const
    {
        return perform(url, &body, content_type, nullptr, {});
    }

    [[nodiscard]] Response post(std::string_view url, std::string_view body, std::string_view content_type,
                                const Headers &headers) const
    {
        return perform(url, &body, content_type, nullptr, headers);
    }

    [[nodiscard]] Response post(std::string_view url, std::string_view body, std::string_view content_type,
                                const ResponseChunkHandler &on_response_chunk) const
    {
        return perform(url, &body, content_type, &on_response_chunk, {});
    }

    [[nodiscard]] Response post(std::string_view url, std::string_view body, std::string_view content_type,
                                const Headers &headers, const ResponseChunkHandler &on_response_chunk) const
    {
        return perform(url, &body, content_type, &on_response_chunk, headers);
    }

  private:
    [[nodiscard]] Response perform(std::string_view url, const std::string_view *body,
                                   std::optional<std::string_view> content_type,
                                   const ResponseChunkHandler *chunk_handler, const Headers &extra_headers) const
    {
        global_curl_guard();

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);

        if (!handle)
        {
            throw std::runtime_error("curl_easy_init failed");
        }

        ResponseSink sink;
        sink.chunk_handler = chunk_handler;

        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_URL, url.data()), "setting request URL");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L), "enabling redirects");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &append_response_data),
                             "setting response callback");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &sink), "setting response buffer");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &append_header_data),
                             "setting header callback");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &sink), "setting header buffer");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "yaaf-http-client/1.0"),
                             "setting user agent");

        if (options_.proxy.has_value() && !options_.proxy->empty())
        {
            throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_PROXY, options_.proxy->c_str()),
                                 "setting proxy");

            if (options_.allow_invalid_proxy_certificates)
            {
                throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 0L),
                                     "disabling server certificate verification for proxy");
                throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 0L),
                                     "disabling server host verification for proxy");
                throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_PROXY_SSL_VERIFYPEER, 0L),
                                     "disabling proxy certificate verification");
                throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_PROXY_SSL_VERIFYHOST, 0L),
                                     "disabling proxy host verification");
            }
        }

        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, &curl_slist_free_all);

        if (body != nullptr)
        {
            throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_POST, 1L), "enabling POST");
            throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body->data()),
                                 "setting request body");
            throw_if_curl_failed(
                curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body->size())),
                "setting request body size");

            if (content_type.has_value())
            {
                headers.reset(
                    curl_slist_append(headers.release(), fmt::format("Content-Type: {}", *content_type).c_str()));

                if (!headers)
                {
                    throw std::runtime_error("failed to build request headers");
                }
            }
        }

        for (const auto &[name, value] : extra_headers)
        {
            headers.reset(curl_slist_append(headers.release(), fmt::format("{}: {}", name, value).c_str()));

            if (!headers)
            {
                throw std::runtime_error("failed to build request headers");
            }
        }

        if (headers != nullptr)
        {
            throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get()),
                                 "setting request headers");
        }

        const auto perform_result = curl_easy_perform(handle.get());
        if (sink.callback_error != nullptr)
        {
            std::rethrow_exception(sink.callback_error);
        }

        throw_if_curl_failed(perform_result, "performing HTTP request");

        long status_code = 0;
        throw_if_curl_failed(curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status_code),
                             "reading status code");

        char *content_type_value = nullptr;
        throw_if_curl_failed(curl_easy_getinfo(handle.get(), CURLINFO_CONTENT_TYPE, &content_type_value),
                             "reading response content type");

        Response response;
        response.status_code = status_code;
        response.content_type = content_type_value != nullptr ? content_type_value : "";
        response.headers = std::move(sink.headers);
        response.body = std::move(sink.body);
        return response;
    }

    Options options_;
};

HttpClient::HttpClient() : HttpClient(Options{})
{
}

HttpClient::HttpClient(Options options) : impl_(std::make_unique<Impl>(std::move(options)))
{
}

HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient &&other) noexcept = default;

HttpClient &HttpClient::operator=(HttpClient &&other) noexcept = default;

HttpClient::Response HttpClient::get(std::string_view url) const
{
    return impl_->get(url);
}

HttpClient::Response HttpClient::get(std::string_view url, const Headers &headers) const
{
    return impl_->get(url, headers);
}

HttpClient::Response HttpClient::post(std::string_view url, std::string_view body, std::string_view content_type) const
{
    return impl_->post(url, body, content_type);
}

HttpClient::Response HttpClient::post(std::string_view url, std::string_view body, std::string_view content_type,
                                      const Headers &headers) const
{
    return impl_->post(url, body, content_type, headers);
}

HttpClient::Response HttpClient::post(std::string_view url, std::string_view body, std::string_view content_type,
                                      const ResponseChunkHandler &on_response_chunk) const
{
    return impl_->post(url, body, content_type, on_response_chunk);
}

HttpClient::Response HttpClient::post(std::string_view url, std::string_view body, std::string_view content_type,
                                      const Headers &headers, const ResponseChunkHandler &on_response_chunk) const
{
    return impl_->post(url, body, content_type, headers, on_response_chunk);
}
