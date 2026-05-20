#pragma once

#include "mcp_schema.h"

namespace yaaf::mcp::schema
{
class GeneratedBackendFactory final : public BackendFactory
{
  public:
    [[nodiscard]] std::shared_ptr<const Backend> create(std::string_view version) const override;
    [[nodiscard]] std::shared_ptr<const Backend> create_latest() const override;
    [[nodiscard]] std::shared_ptr<const Registry> create_registry() const override;
};

[[nodiscard]] std::shared_ptr<const BackendFactory> generated_factory();

// One declaration is generated for each versioned schema source in mcp/schema/.
// The generated registry uses these functions to avoid centralizing all schema data in one translation unit.
[[nodiscard]] std::shared_ptr<const Backend> generated_backend_2024_11_05();
[[nodiscard]] std::shared_ptr<const Backend> generated_backend_2025_03_26();
[[nodiscard]] std::shared_ptr<const Backend> generated_backend_2025_06_18();
[[nodiscard]] std::shared_ptr<const Backend> generated_backend_2025_11_25();
} // namespace yaaf::mcp::schema
