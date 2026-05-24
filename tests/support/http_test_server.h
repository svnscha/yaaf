#pragma once

#include "../../libyaaf/pch/pch_dependencies.h"
#include "../../libyaaf/pch/pch_std.h"

#include "../../libyaaf/http/http_client.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <thread>

namespace yaaf::tests::http
{
namespace detail
{
#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

class SocketRuntime
{
  public:
    SocketRuntime()
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            throw std::runtime_error("WSAStartup failed");
        }
    }

    ~SocketRuntime()
    {
        WSACleanup();
    }
};

[[nodiscard]] inline SocketRuntime &socket_runtime()
{
    static SocketRuntime runtime;
    return runtime;
}

inline void close_socket(SocketHandle socket)
{
    if (socket != kInvalidSocket)
    {
        closesocket(socket);
    }
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

inline void socket_runtime()
{
}

inline void close_socket(SocketHandle socket)
{
    if (socket != kInvalidSocket)
    {
        close(socket);
    }
}
#endif
} // namespace detail

struct Request
{
    std::string method;
    std::string target;
    HttpClient::Headers headers;
    std::string body;
};

struct Response
{
    int status_code = 200;
    std::string content_type = "text/plain";
    std::string body;
    HttpClient::Headers headers;
    std::chrono::milliseconds delay{0};
};

class LocalHttpServer
{
  public:
    using Handler = std::function<Response(const Request &request)>;

    explicit LocalHttpServer(Handler handler) : handler_(std::move(handler))
    {
#ifdef _WIN32
        detail::socket_runtime();
#else
        detail::socket_runtime();
#endif
        listen_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket_ == detail::kInvalidSocket)
        {
            throw std::runtime_error("failed to create test HTTP socket");
        }

        int reuse = 1;
#ifdef _WIN32
        setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#else
        setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(listen_socket_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
        {
            detail::close_socket(listen_socket_);
            throw std::runtime_error("failed to bind test HTTP socket");
        }

        if (::listen(listen_socket_, 8) != 0)
        {
            detail::close_socket(listen_socket_);
            throw std::runtime_error("failed to listen on test HTTP socket");
        }

        sockaddr_in bound{};
#ifdef _WIN32
        int bound_size = sizeof(bound);
#else
        socklen_t bound_size = sizeof(bound);
#endif
        if (::getsockname(listen_socket_, reinterpret_cast<sockaddr *>(&bound), &bound_size) != 0)
        {
            detail::close_socket(listen_socket_);
            throw std::runtime_error("failed to read test HTTP socket address");
        }

        port_ = ntohs(bound.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~LocalHttpServer()
    {
        stop_.store(true);
        detail::close_socket(listen_socket_);
        listen_socket_ = detail::kInvalidSocket;
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    [[nodiscard]] std::string base_url() const
    {
        return fmt::format("http://127.0.0.1:{}", port_);
    }

  private:
    static void send_all(detail::SocketHandle socket, const std::string &payload)
    {
        std::size_t sent = 0;
        while (sent < payload.size())
        {
            const auto remaining = payload.size() - sent;
#ifdef _WIN32
            const auto result = ::send(socket, payload.data() + sent, static_cast<int>(remaining), 0);
#else
            const auto result = ::send(socket, payload.data() + sent, remaining, 0);
#endif
            if (result <= 0)
            {
                return;
            }
            sent += static_cast<std::size_t>(result);
        }
    }

    [[nodiscard]] static std::optional<std::size_t> content_length(const HttpClient::Headers &headers)
    {
        for (const auto &[name, value] : headers)
        {
            if (name.size() == std::string_view("Content-Length").size() &&
                std::equal(name.begin(), name.end(), std::string_view("Content-Length").begin(),
                           [](char left, char right) {
                               return std::tolower(static_cast<unsigned char>(left)) ==
                                      std::tolower(static_cast<unsigned char>(right));
                           }))
            {
                return static_cast<std::size_t>(std::stoull(value));
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static Request parse_request(std::string payload)
    {
        const auto header_end = payload.find("\r\n\r\n");
        if (header_end == std::string::npos)
        {
            throw std::runtime_error("incomplete HTTP request in test server");
        }

        Request request;
        std::istringstream lines(payload.substr(0, header_end));
        std::string request_line;
        std::getline(lines, request_line);
        if (!request_line.empty() && request_line.back() == '\r')
        {
            request_line.pop_back();
        }

        std::istringstream request_line_stream(request_line);
        request_line_stream >> request.method >> request.target;
        if (request.method.empty() || request.target.empty())
        {
            throw std::runtime_error("invalid HTTP request line in test server");
        }

        std::string header_line;
        while (std::getline(lines, header_line))
        {
            if (!header_line.empty() && header_line.back() == '\r')
            {
                header_line.pop_back();
            }
            const auto separator = header_line.find(':');
            if (separator == std::string::npos)
            {
                continue;
            }
            auto value = header_line.substr(separator + 1);
            while (!value.empty() && value.front() == ' ')
            {
                value.erase(value.begin());
            }
            request.headers.emplace_back(header_line.substr(0, separator), std::move(value));
        }

        request.body = payload.substr(header_end + 4);
        return request;
    }

    [[nodiscard]] static std::string status_text(int status_code)
    {
        switch (status_code)
        {
        case 200:
            return "OK";
        case 204:
            return "No Content";
        case 404:
            return "Not Found";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
        }
    }

    [[nodiscard]] static std::string format_response(const Response &response)
    {
        std::string payload = fmt::format("HTTP/1.1 {} {}\r\n", response.status_code, status_text(response.status_code));
        if (!response.content_type.empty())
        {
            payload += fmt::format("Content-Type: {}\r\n", response.content_type);
        }
        for (const auto &[name, value] : response.headers)
        {
            payload += fmt::format("{}: {}\r\n", name, value);
        }
        payload += fmt::format("Content-Length: {}\r\nConnection: close\r\n\r\n", response.body.size());
        payload += response.body;
        return payload;
    }

    void handle_client(detail::SocketHandle client_socket)
    {
        std::string payload;
        char buffer[4096];
        std::optional<std::size_t> expected_body_size;
        while (true)
        {
#ifdef _WIN32
            const auto bytes_read = ::recv(client_socket, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
            const auto bytes_read = ::recv(client_socket, buffer, sizeof(buffer), 0);
#endif
            if (bytes_read <= 0)
            {
                return;
            }

            payload.append(buffer, buffer + bytes_read);
            const auto header_end = payload.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                continue;
            }

            if (!expected_body_size.has_value())
            {
                expected_body_size = content_length(parse_request(payload).headers).value_or(0);
            }
            const auto current_body_size = payload.size() - (header_end + 4);
            if (current_body_size >= *expected_body_size)
            {
                break;
            }
        }

        auto request = parse_request(std::move(payload));
        auto response = handler_(request);
        if (response.delay.count() > 0)
        {
            std::this_thread::sleep_for(response.delay);
        }
        send_all(client_socket, format_response(response));
    }

    void serve()
    {
        while (!stop_.load())
        {
            if (listen_socket_ == detail::kInvalidSocket)
            {
                return;
            }

            fd_set sockets;
            FD_ZERO(&sockets);
            FD_SET(listen_socket_, &sockets);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
#ifdef _WIN32
            const auto ready = ::select(0, &sockets, nullptr, nullptr, &timeout);
#else
            const auto ready = ::select(listen_socket_ + 1, &sockets, nullptr, nullptr, &timeout);
#endif
            if (ready <= 0)
            {
                continue;
            }

            sockaddr_in client_address{};
#ifdef _WIN32
            int client_size = sizeof(client_address);
#else
            socklen_t client_size = sizeof(client_address);
#endif
            const auto client_socket =
                ::accept(listen_socket_, reinterpret_cast<sockaddr *>(&client_address), &client_size);
            if (client_socket == detail::kInvalidSocket)
            {
                continue;
            }

            handle_client(client_socket);
            detail::close_socket(client_socket);
        }
    }

    Handler handler_;
    std::atomic<bool> stop_{false};
    detail::SocketHandle listen_socket_ = detail::kInvalidSocket;
    std::uint16_t port_ = 0;
    std::thread thread_;
};
} // namespace yaaf::tests::http
