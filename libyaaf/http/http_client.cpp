#include "http_client.h"

#include <cctype>
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

[[nodiscard]] std::string uppercase_ascii(std::string_view value)
{
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return normalized;
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

void append_request_header(std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> &headers, std::string header)
{
    auto *updated = curl_slist_append(headers.get(), header.c_str());
    if (updated == nullptr)
    {
        throw std::runtime_error("failed to build request headers");
    }

    headers.release();
    headers.reset(updated);
}

void configure_method(CURL *handle, const HttpClient::Request &request, std::string_view method)
{
    if (method == "GET")
    {
        throw_if_curl_failed(curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L), "enabling GET");
    }
    else if (method == "POST")
    {
        throw_if_curl_failed(curl_easy_setopt(handle, CURLOPT_POST, 1L), "enabling POST");
    }
    else if (method == "HEAD")
    {
        throw_if_curl_failed(curl_easy_setopt(handle, CURLOPT_NOBODY, 1L), "enabling HEAD");
        throw_if_curl_failed(curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, "HEAD"), "setting request method");
    }
    else
    {
        throw_if_curl_failed(curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.data()), "setting request method");
    }

    if (request.body.has_value() && method != "HEAD")
    {
        throw_if_curl_failed(curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body->data()), "setting request body");
        throw_if_curl_failed(curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE,
                                              static_cast<curl_off_t>(request.body->size())),
                             "setting request body size");
    }
}
} // namespace

class HttpClient::Impl
{
  public:
    explicit Impl(Options options) : options_(std::move(options))
    {
    }

    [[nodiscard]] Response execute(const Request &request) const
    {
        if (request.url.empty())
        {
            throw std::invalid_argument("request URL must not be empty");
        }

        const auto method = uppercase_ascii(request.method.empty() ? std::string_view{"GET"} : request.method);
        if (method.empty())
        {
            throw std::invalid_argument("request method must not be empty");
        }

        global_curl_guard();

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);

        if (!handle)
        {
            throw std::runtime_error("curl_easy_init failed");
        }

        ResponseSink sink;
        sink.chunk_handler = request.on_response_chunk ? &request.on_response_chunk : nullptr;

        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_URL, request.url.c_str()), "setting request URL");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L), "enabling redirects");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &append_response_data),
                             "setting response callback");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &sink), "setting response buffer");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &append_header_data),
                             "setting header callback");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &sink), "setting header buffer");
        throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "yaaf-http-client/1.0"),
                             "setting user agent");

        if (request.timeout.has_value())
        {
            throw_if_curl_failed(curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS,
                                                  static_cast<long>(request.timeout->count())),
                                 "setting request timeout");
        }

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

        configure_method(handle.get(), request, method);

        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, &curl_slist_free_all);

        if (request.content_type.has_value())
        {
            append_request_header(headers, fmt::format("Content-Type: {}", *request.content_type));
        }

        for (const auto &[name, value] : request.headers)
        {
            append_request_header(headers, fmt::format("{}: {}", name, value));
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

  private:
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

HttpClient::Response HttpClient::execute(const Request &request) const
{
    return impl_->execute(request);
}

HttpClient::Response HttpClient::get(std::string_view url) const
{
    Request request;
    request.url = std::string(url);
    return execute(request);
}

HttpClient::Response HttpClient::get(std::string_view url, const Headers &headers) const
{
    Request request;
    request.url = std::string(url);
    request.headers = headers;
    return execute(request);
}

HttpClient::Response HttpClient::post(std::string_view url, std::string_view body, std::string_view content_type) const
{
    Request request;
    request.method = "POST";
    request.url = std::string(url);
    request.body = std::string(body);
    request.content_type = std::string(content_type);
    return execute(request);
}

HttpClient::Response HttpClient::post(std::string_view url, std::string_view body, std::string_view content_type,
                                      const Headers &headers) const
{
    Request request;
    request.method = "POST";
    request.url = std::string(url);
    request.body = std::string(body);
    request.content_type = std::string(content_type);
    request.headers = headers;
    return execute(request);
}

HttpClient::Response HttpClient::post(std::string_view url, std::string_view body, std::string_view content_type,
                                      const ResponseChunkHandler &on_response_chunk) const
{
    Request request;
    request.method = "POST";
    request.url = std::string(url);
    request.body = std::string(body);
    request.content_type = std::string(content_type);
    request.on_response_chunk = on_response_chunk;
    return execute(request);
}

HttpClient::Response HttpClient::post(std::string_view url, std::string_view body, std::string_view content_type,
                                      const Headers &headers, const ResponseChunkHandler &on_response_chunk) const
{
    Request request;
    request.method = "POST";
    request.url = std::string(url);
    request.body = std::string(body);
    request.content_type = std::string(content_type);
    request.headers = headers;
    request.on_response_chunk = on_response_chunk;
    return execute(request);
}
