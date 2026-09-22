/* Legacy profile transport: remote/main-era offsets.json -> kernel_offsets.
 *
 * This unit owns every "old document" rule so the rest of the native core only
 * ever sees a resolved profile: top-level array scanning, release matching,
 * symbols/struct_fields/flat keys (through the shared JSON primitives) and the
 * pre-route-field geometry inference. */
#include "legacy_support/legacy_profile_converter.h"

#include "legacy_support/offsets_json.h"

#include <cerrno>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <string>
#include <string_view>

namespace ghostlock::legacy_support {
    using namespace ghostlock::profile;

    namespace {
        constexpr size_t kMaxDocument = 1U << 20; /* 1 MiB */

        int read_document(const char *path, std::string *out) {
            int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) return -1;
            std::string buffer(kMaxDocument, '\0');
            size_t used = 0;
            while (used < buffer.size()) {
                const ssize_t count =
                        read(fd, buffer.data() + used, buffer.size() - used);
                if (count > 0) {
                    used += static_cast<size_t>(count);
                    continue;
                }
                if (count == 0) break;
                if (errno == EINTR) continue;
                close(fd);
                return -1;
            }
            close(fd);
            if (used == 0 || used == buffer.size()) {
                errno = used == buffer.size() ? EFBIG : EINVAL;
                return -1;
            }
            buffer.resize(used);
            *out = std::move(buffer);
            return 0;
        }

        /* remote/main selected the route from kernel geometry; documents written
 * before the explicit field keep behaving the same way. */
        void infer_route(struct kernel_offsets *out) {
            if (out->route != kRouteAuto) return;
            if (out->kernel_major == 5 && out->mcast_waiter_off > 0) {
                out->route = kRouteMulticastWaiter;
            } else if (out->compact_waiter) {
                out->route = kRouteTcpZerocopy;
            } else {
                out->route = kRouteSelectStack;
            }
        }

        /* remote/main-era configuration environment becomes profile state; nothing
 * downstream reads these variables. */
        void apply_legacy_environment(struct kernel_offsets *out) {
            out->execution.recommended_main_cpu = 0;
            out->execution.recommended_consumer_cpu = 1;

            if (const char *core = getenv("GHOSTLOCK_CORE")) {
                char *end = nullptr;
                const long value = strtol(core, &end, 10);
                if (end != core && value >= 0 && value < 65536) {
                    out->execution.recommended_main_cpu = (uint32_t) value;
                }
            }
            const char *consumer = getenv("GHOSTLOCK_CONSUMER_CORE");
            if (consumer) {
                char *end = nullptr;
                const long value = strtol(consumer, &end, 10);
                if (end != consumer && value >= 0 && value < 65536) {
                    out->execution.recommended_consumer_cpu = (uint32_t) value;
                } else {
                    out->execution.recommended_consumer_cpu =
                            out->execution.recommended_main_cpu + 1;
                }
            } else {
                out->execution.recommended_consumer_cpu =
                        out->execution.recommended_main_cpu + 1;
            }

            if (const char *tcp = getenv("GHOSTLOCK_TCP_ROUTE")) {
                if (strcmp(tcp, "0") == 0) out->route = kRouteSelectStack;
            }
            if (const char *disable = getenv("GHOSTLOCK_DISABLE_MODULES")) {
                if (strcmp(disable, "1") == 0) out->safe_mode = 1;
            }
        }
    } // namespace

    int convert_legacy_offsets(const char *path, const char *release,
                               struct kernel_offsets *out, char *release_buf, size_t release_buf_cap) {
        if (!path || !release || !out || !release_buf || release_buf_cap == 0) {
            errno = EINVAL;
            return -1;
        }
        std::string document;
        if (read_document(path, &document) != 0) return -1;

        std::string_view entry;
        if (profile_json::select_entry(
                std::string_view(document.data(), document.size()), release,
                &entry) != 0) {
            errno = ENOENT;
            return -1;
        }

        memset(out, 0, sizeof(*out));
        /* The caller's release is authoritative; fill_entry sees it for its own
     * 6.1-layout warning and leaves out->uname_r pointing at the buffer. */
        snprintf(release_buf, release_buf_cap, "%s", release);
        profile_json::fill_entry(out, release_buf, entry);
        out->uname_r = release_buf;
        infer_route(out);
        apply_legacy_environment(out);
        return 0;
    }
} // namespace ghostlock::legacy_support
