#include "libyaaf/pch/pch_std.h"
#include "libyaaf/pch/pch_dependencies.h"

#include "mcp_host_stdio.h"

namespace yaaf::mcp
{
namespace
{
constexpr int JSON_PARSE_ERROR = -32700;
constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;
constexpr int INTERNAL_ERROR = -32603;

[[nodiscard]] std::string as_string(const nlohmann::json &value, std::string_view fallback = {})
{
    return value.is_string() ? value.get<std::string>() : std::string(fallback);
}

[[nodiscard]] std::optional<std::int64_t> as_int(const nlohmann::json &value)
{
    if (value.is_number_integer())
    {
        return value.get<std::int64_t>();
    }
    return std::nullopt;
}
} // namespace

StdioHost::StdioHost(Host &host, std::istream &input, std::ostream &output)
    : host_(host), input_(input), output_(output)
{
}

std::optional<HostRequest> StdioHost::read_request()
{
    std::string line;
    if (!std::getline(input_, line))
    {
        return std::nullopt;  // EOF
    }

    try
    {
        const auto json = nlohmann::json::parse(line);

        HostRequest request;
        request.jsonrpc = as_string(json.value("jsonrpc", nlohmann::json{}), "2.0");
        request.method = as_string(json.value("method", nlohmann::json{}));
        request.params = json.value("params", nlohmann::json::object());
        request.id = as_int(json.value("id", nlohmann::json{}));

        if (request.method.empty())
        {
            throw std::runtime_error("method field is required");
        }

        return request;
    }
    catch (const nlohmann::json::exception &e)
    {
        throw std::runtime_error(fmt::format("JSON parse error: {}", e.what()));
    }
}

void StdioHost::send_response(std::optional<std::int64_t> request_id, const nlohmann::json &result)
{
    nlohmann::json response = {{"jsonrpc", "2.0"}};
    if (request_id.has_value())
    {
        response["id"] = request_id.value();
    }
    response["result"] = result;

    output_ << response.dump() << "\n";
    output_.flush();
}

void StdioHost::send_error(std::optional<std::int64_t> request_id, int code, std::string_view message)
{
    nlohmann::json response = {{"jsonrpc", "2.0"}, {"error", {{"code", code}, {"message", std::string(message)}}}};
    if (request_id.has_value())
    {
        response["id"] = request_id.value();
    }

    output_ << response.dump() << "\n";
    output_.flush();
}

bool StdioHost::handle_initialize(const HostRequest &request)
{
    if (initialized_)
    {
        send_error(request.id, INVALID_REQUEST, "server already initialized");
        return false;
    }

    try
    {
        const auto result = host_.initialize(request.params);
        send_response(request.id, result);
        initialized_ = true;
        return true;
    }
    catch (const std::exception &e)
    {
        send_error(request.id, INTERNAL_ERROR, fmt::format("initialize failed: {}", e.what()));
        return false;
    }
}

void StdioHost::dispatch_method(const HostRequest &request)
{
    // Route to appropriate handler
    if (request.method == "tools/list")
    {
        try
        {
            const auto tools = host_.list_tools();
            nlohmann::json result = nlohmann::json::array();
            for (const auto &tool : tools)
            {
                result.push_back(tool);
            }
            send_response(request.id, {{"tools", result}});
        }
        catch (const std::exception &e)
        {
            send_error(request.id, INTERNAL_ERROR, fmt::format("tools/list failed: {}", e.what()));
        }
    }
    else if (request.method == "tools/call")
    {
        try
        {
            const auto name = as_string(request.params.value("name", nlohmann::json{}));
            const auto arguments = request.params.value("arguments", nlohmann::json::object());

            if (name.empty())
            {
                send_error(request.id, INVALID_PARAMS, "tools/call requires 'name' parameter");
                return;
            }

            const auto result = host_.call_tool(name, arguments);
            send_response(request.id, result);
        }
        catch (const std::exception &e)
        {
            send_error(request.id, INTERNAL_ERROR, fmt::format("tools/call failed: {}", e.what()));
        }
    }
    else if (request.method == "prompts/list")
    {
        try
        {
            const auto prompts = host_.list_prompts();
            nlohmann::json result = nlohmann::json::array();
            for (const auto &prompt : prompts)
            {
                result.push_back(prompt);
            }
            send_response(request.id, {{"prompts", result}});
        }
        catch (const std::exception &e)
        {
            send_error(request.id, INTERNAL_ERROR, fmt::format("prompts/list failed: {}", e.what()));
        }
    }
    else if (request.method == "prompts/get")
    {
        try
        {
            const auto name = as_string(request.params.value("name", nlohmann::json{}));
            const auto arguments = request.params.value("arguments", nlohmann::json::object());

            if (name.empty())
            {
                send_error(request.id, INVALID_PARAMS, "prompts/get requires 'name' parameter");
                return;
            }

            const auto messages = host_.get_prompt(name, arguments);
            nlohmann::json result = nlohmann::json::array();
            for (const auto &msg : messages)
            {
                result.push_back(msg);
            }
            send_response(request.id, {{"messages", result}});
        }
        catch (const std::exception &e)
        {
            send_error(request.id, INTERNAL_ERROR, fmt::format("prompts/get failed: {}", e.what()));
        }
    }
    else
    {
        send_error(request.id, METHOD_NOT_FOUND, fmt::format("method '{}' not found", request.method));
    }
}

void StdioHost::run()
{
    while (true)
    {
        std::optional<HostRequest> request;

        try
        {
            request = read_request();
        }
        catch (const std::exception &e)
        {
            // Parse error - send error response if we can extract ID
            send_error(std::nullopt, JSON_PARSE_ERROR, fmt::format("failed to parse request: {}", e.what()));
            continue;
        }

        // EOF - clean exit
        if (!request.has_value())
        {
            break;
        }

        const auto &req = request.value();

        // Handle initialize specially
        if (req.method == "initialize")
        {
            handle_initialize(req);
            continue;
        }

        // Handle notifications/initialized (no-op)
        if (req.method == "notifications/initialized")
        {
            continue;
        }

        // Require initialization before processing other methods
        if (!initialized_)
        {
            send_error(req.id, INVALID_REQUEST, "server not initialized");
            continue;
        }

        // Dispatch method call
        dispatch_method(req);
    }
}

} // namespace yaaf::mcp
