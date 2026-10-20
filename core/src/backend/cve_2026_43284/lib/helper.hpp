#pragma once

#include <cstdint>
#include <sys/types.h>

namespace ghostlock::backend::cve_2026_43284::lib {

struct HelperError {
    const char *stage = nullptr;
    int err = 0;
    int status = 0;
};

bool gl_helper_exec(off_t offset, const char *path, int out_fd, bool read_mode,
                    HelperError *error) noexcept;
bool gl_helper_read(const char *path, off_t offset, std::uint8_t out[16],
                    HelperError *error) noexcept;
bool gl_fadvise_drop(const char *path, HelperError *error) noexcept;
bool gl_trigger_orphan(unsigned delay_ms, HelperError *error) noexcept;


}  // namespace ghostlock::backend::cve_2026_43284::lib
