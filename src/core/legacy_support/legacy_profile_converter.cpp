/* Legacy profile transport: remote/main-era offsets.json -> kernel_offsets.
 *
 * This unit owns every "old document" rule so the rest of the native core only
 * ever sees a resolved profile: top-level array scanning, release matching,
 * symbols/struct_fields/flat keys (through the shared JSON primitives) and the
 * pre-route-field geometry inference. */
#include "legacy_support/legacy_profile_converter.h"

#include "offsets_json.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <string_view>

namespace ghostlock::legacy_support {

namespace {

constexpr size_t kMaxDocument = 1U << 20;  /* 1 MiB */

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

}  // namespace

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
    return 0;
}

}  // namespace ghostlock::legacy_support
