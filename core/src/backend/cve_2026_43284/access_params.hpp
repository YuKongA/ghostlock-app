#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "contract/kernel_access.hpp"

/** 43284 重置期的最小参数面：只保留 session_object 仍在用的字段。 */
namespace ghostlock::backend::cve_2026_43284 {

struct PathView {
    const char *bytes = nullptr;
    std::size_t length = 0;
};

struct SaMaterial {
    std::uint32_t spi = 0;
    std::uint16_t encap_port = 0;
    std::uint16_t sender_port = 0;
    std::uint8_t icv_len = 0;
    std::uint8_t aes_key[32] = {};
    std::uint8_t hmac_key[32] = {};
    bool set = false;
};

class AccessParams final : public contract::KernelAccess::AccessMiscParameters {
public:
    SaMaterial sa{};
    std::uint64_t offset = 0;
    bool offset_set = false;
    std::uint32_t sequence = 0;
    bool sequence_set = false;
    PathView carrier{};
    PathView patch_target{};
    PathView dev_carrier{};

    [[nodiscard]] std::string_view name() const noexcept override {
        return "cve_2026_43284";
    }
};

}  // namespace ghostlock::backend::cve_2026_43284
