#ifndef GHOSTLOCK_PLATFORM_RUNTIME_HPP
#define GHOSTLOCK_PLATFORM_RUNTIME_HPP

#include "support/native_resource.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace ghostlock::platform::runtime {
    /* Apply a rooted run's cached /proc/iomem (System RAM span) to the direct-map
     * end. `home_dir` locates the cache; `release` scopes it to the uname it was
     * dumped for. Off-thread inputs keep this module free of config/backend deps. */
    void apply_iomem_cache(const char *home_dir, const char *release);
    /* Read /sys/fs/selinux/enforce; 0 when it is permissive or unreadable
     * (untrusted_app while enforcing). */
    inline int32_t check_selinux_off() {
        ghostlock::support::UniqueFd efd(open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC));
        if (!efd.valid()) return 0;
        std::array<char, 4> b{};
        (void) read(efd.get(), b.data(), b.size());
        return b[0] == '0';
    }

    inline int32_t enforce_readable() {
        ghostlock::support::UniqueFd efd(open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC));
        return efd.valid() ? 1 : 0;
    }

    /* True when this process carries a seccomp filter (zygote/app flow). */
    inline int32_t process_has_seccomp() {
        FILE *status = fopen("/proc/self/status", "r");
        if (!status) return 0;
        auto close_status = ghostlock::support::make_scope_exit(
            [status]() noexcept { fclose(status); });
        char line[256];
        int32_t seccomp = 0;
        while (fgets(line, sizeof(line), status)) {
            if (strncmp(line, "Seccomp:", 8) == 0) {
                seccomp = atoi(line + 8);
                break;
            }
        }
        return seccomp != 0;
    }
} // namespace ghostlock::platform::runtime

#endif
