/* memory/constants.hpp first: a later <unistd.h> would define PAGE_SIZE. */
#include "memory/constants.hpp"
#include "memory/target.h"

#include "platform/runtime.hpp"
#include "kernelsnitch/utils.h"
#include "support/native_resource.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <unistd.h>

namespace ghostlock::platform::runtime {
    namespace {
        /* Lowest start and highest end of the System RAM banks in a /proc/iomem
         * dump. A nested bank lies inside its parent, so it cannot widen either
         * bound. */
        int32_t iomem_map_span(FILE *f, uint64_t *map_span) {
            unsigned long long base = 0, top = 0;
            char *line = nullptr;
            size_t cap = 0;
            int32_t found = 0;

            while (getline(&line, &cap, f) > 0) {
                size_t len = strlen(line);
                unsigned long long a, b;
                int32_t used = 0;

                while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                    line[--len] = '\0';
                }
                if (sscanf(line, " %llx-%llx : System RAM%n", &a, &b, &used) == 2 &&
                    used == static_cast<int32_t>(len)) {
                    if (!found || a < base) {
                        base = a;
                        found = 1;
                    }
                    if (b + 1 > top) top = b + 1;
                }
            }
            free(line);

            base &= ~((1ULL << 30) - 1);
            if (!found || top <= base) return 0;
            *map_span = top - base;
            return 1;
        }
    } // namespace

    void apply_iomem_cache(const char *home_dir, const char *release) {
        std::array<char, 320> path{};
        std::array<char, 192> stamp{};
        uint64_t span = 0;
        int32_t ok = 0;

        snprintf(path.data(), path.size(), "%s/.ghostlock_iomem", home_dir);
        if (FILE *f = fopen(path.data(), "r")) {
            auto close_iomem = ghostlock::support::make_scope_exit(
                [f]() noexcept { fclose(f); });
            if (fgets(stamp.data(), static_cast<int32_t>(stamp.size()), f)) {
                stamp[strcspn(stamp.data(), "\r\n")] = '\0'; // NOLINT(clang-analyzer-security.ArrayBound)
                const std::string_view stamp_view(stamp.data());
                ok = stamp_view.starts_with("# ") &&
                     stamp_view.substr(2) == release &&
                     iomem_map_span(f, &span);
            }
        }

        long pages = sysconf(_SC_PHYS_PAGES), page_sz = sysconf(_SC_PAGE_SIZE);
        uint64_t dram = (pages > 0 && page_sz > 0)
                            ? static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_sz)
                            : 0;
        if (!ok || !dram || span < (1ULL << 30) || span < dram ||
            span >= memory::DIRECT_MAP_END - memory::DIRECT_MAP_BASE) {
            pr_info("iomem cache: no usable dump, keeping the built-in geometry\n");
            return;
        }

        memory::g_direct_map_end = memory::DIRECT_MAP_BASE + span;
        pr_info("iomem cache: direct_map_end=%016llx\n",
                (unsigned long long) memory::g_direct_map_end);
    }
} // namespace ghostlock::platform::runtime
