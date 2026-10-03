#ifndef GHOSTLOCK_HOST_PLATFORM_RUNTIME_HPP
#define GHOSTLOCK_HOST_PLATFORM_RUNTIME_HPP

/* Host shadow of platform/runtime.hpp: the side-effecting probes are scripted
 * by attack_stub.cpp so the data-flow test can drive W3. */

#include <cstdint>

namespace ghostlock::platform::runtime {
    void apply_iomem_cache(const char *home_dir, const char *release);

    int32_t check_selinux_off(void);
    int32_t enforce_readable(void);
    int32_t process_has_seccomp(void);
} // namespace ghostlock::platform::runtime

#endif
