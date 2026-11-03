/* Profile entry points: the App path (stdin) and the prebuilt-profile path
 * (file). Both decode the typed transport directly (v3, with v2 still
 * accepted); no JSON or legacy rules reach this unit. */
#include "profile/entry.h"

#include "profile/binary.h"
#include "support/native_resource.hpp"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <string_view>

namespace ghostlock::profile_entry {
    namespace {
        constexpr size_t kMaxDocument = 1U << 20; /* 1 MiB */

        int32_t read_all(int32_t fd, std::string *out) {
            std::string buffer(kMaxDocument, '\0');
            size_t used = 0;
            while (used < buffer.size()) {
                const ssize_t count = read(fd, buffer.data() + used,
                                           buffer.size() - used);
                if (count > 0) {
                    used += static_cast<size_t>(count);
                    continue;
                }
                if (count == 0) break;
                if (errno == EINTR) continue;
                return -1;
            }
            if (used == 0 || used == buffer.size()) {
                errno = used == buffer.size() ? EFBIG : EINVAL;
                return -1;
            }
            buffer.resize(used);
            *out = std::move(buffer);
            return 0;
        }

        int32_t read_exact(int32_t fd, void *buf, size_t len) {
            auto *bytes = static_cast<unsigned char *>(buf);
            size_t used = 0;
            while (used < len) {
                const ssize_t count = read(fd, bytes + used, len - used);
                if (count > 0) {
                    used += static_cast<size_t>(count);
                    continue;
                }
                if (count == 0) {
                    errno = EIO;
                    return -1;
                }
                if (errno == EINTR) continue;
                return -1;
            }
            return 0;
        }

        int32_t decode(const std::string &document, profile::kernel_offsets *out,
                   char *release_buf, size_t release_buf_cap,
                   binary_profile::component_ids *ids) {
            return ghostlock::binary_profile::parse(
                std::string_view(document.data(), document.size()), out,
                release_buf, release_buf_cap, ids);
        }
    } // namespace

    int32_t read_glk1_stdin(profile::kernel_offsets *out, char *release_buf,
                        size_t release_buf_cap, binary_profile::component_ids *ids) {
        if (!out || !release_buf || release_buf_cap == 0) {
            errno = EINVAL;
            return -1;
        }
        std::string document;
        if (read_all(STDIN_FILENO, &document) != 0) return -1;
        return decode(document, out, release_buf, release_buf_cap, ids);
    }

    int32_t read_glk1_frame_stdin(profile::kernel_offsets *out, char *release_buf,
                        size_t release_buf_cap, binary_profile::component_ids *ids) {
        if (!out || !release_buf || release_buf_cap == 0) {
            errno = EINVAL;
            return -1;
        }
        unsigned char header[4];
        if (read_exact(STDIN_FILENO, header, sizeof(header)) != 0) return -1;
        const size_t length = (static_cast<size_t>(header[0]) << 24) |
                              (static_cast<size_t>(header[1]) << 16) |
                              (static_cast<size_t>(header[2]) << 8) |
                              static_cast<size_t>(header[3]);
        if (length == 0 || length > kMaxDocument) {
            errno = EFBIG;
            return -1;
        }
        std::string document(length, '\0');
        if (read_exact(STDIN_FILENO, document.data(), length) != 0) return -1;
        return decode(document, out, release_buf, release_buf_cap, ids);
    }

    int32_t read_glk1_file(const char *path, profile::kernel_offsets *out,
                       char *release_buf, size_t release_buf_cap,
                       binary_profile::component_ids *ids) {
        if (!path || !out || !release_buf || release_buf_cap == 0) {
            errno = EINVAL;
            return -1;
        }
        ghostlock::support::UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
        if (!fd.valid()) return -1;
        std::string document;
        const int32_t rc = read_all(fd.get(), &document);
        if (rc != 0) return -1;
        return decode(document, out, release_buf, release_buf_cap, ids);
    }
} // namespace ghostlock::profile_entry
