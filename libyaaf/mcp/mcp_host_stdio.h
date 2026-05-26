#pragma once

#include "mcp_host.h"

namespace yaaf::mcp
{
/// Wraps Host with stdio JSON-RPC transport.
/**
 * StdioHost handles JSON-RPC framing over stdin/stdout, route method calls
 * to the Host, and return responses in JSON-RPC format with \n delimiters.
 *
 * The main loop (run()) reads requests, dispatches them, and sends responses
 * until EOF is received on input.
 */
class StdioHost
{
  public:
    /// Construct stdio host wrapper.
    /**
     * @param host Host instance to dispatch requests to
     * @param input Input stream to read JSON-RPC requests from (typically stdin)
     * @param output Output stream to write JSON-RPC responses to (typically stdout)
     */
    StdioHost(Host &host, std::istream &input, std::ostream &output);

    /// Run the main request/response loop.
    /**
     * Reads JSON-RPC requests line-by-line from input, dispatches to host,
     * and writes JSON-RPC responses to output. Continues until EOF or error.
     *
     * Handles:
     * - initialize negotiation (first request only)
     * - notifications/initialized (no-op)
     * - tools/list, tools/call, prompts/list, prompts/get
     * - Unknown methods (-32601)
     * - Malformed params (-32602)
     * - Parse errors (-32700)
     * - Internal errors (-32603)
     *
     * @throws std::runtime_error on fatal I/O or parsing errors
     */
    void run();

  private:
    /// Read next JSON-RPC request from input stream.
    /**
     * @return HostRequest if valid JSON-RPC request read; empty optional on EOF
     * @throws std::runtime_error on parse errors
     */
    [[nodiscard]] std::optional<HostRequest> read_request();

    /// Send JSON-RPC response with the given result.
    /**
     * @param request_id Request ID from the incoming request
     * @param result Result object to include in response
     */
    void send_response(std::optional<std::int64_t> request_id, const nlohmann::json &result);

    /// Send JSON-RPC error response.
    /**
     * @param request_id Request ID from the incoming request (optional for errors without ID)
     * @param code JSON-RPC error code
     * @param message Human-readable error description
     */
    void send_error(std::optional<std::int64_t> request_id, int code, std::string_view message);

    /// Handle initialize request specially (must be first).
    /**
     * @param request The initialize request
     * @return True if handled successfully
     */
    [[nodiscard]] bool handle_initialize(const HostRequest &request);

    /// Dispatch a method call to the host.
    /**
     * @param request The request containing method and params
     */
    void dispatch_method(const HostRequest &request);

    Host &host_;
    std::istream &input_;
    std::ostream &output_;
    bool initialized_ = false;
};

} // namespace yaaf::mcp
