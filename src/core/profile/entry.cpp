/* Profile entry points: the App path (stdin) and the prebuilt-profile path
 * (file). Both discriminate GLKv3 from v2 and frame the result into the
 * neutral profile::Document (A2-5): owner binding happens later, in the
 * selected backend, never here. */
#include "profile/entry.h"

#include "profile/binary.h"
#include "profile/glkv3_parse.hpp"
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

        /* GLKv3 is map-rooted and has no magic; v2 starts with the
         * little-endian magic byte 0x21. A map root that then fails to decode is
         * rejected whole (fail-closed), never retried as v2. */
        int32_t decode(const std::string &document, profile::Document *out) {
            const std::string_view view(document.data(), document.size());
            if (binary_profile::looks_like_glkv3(view)) {
                return binary_profile::frame_v3(view, out);
            }
            return binary_profile::frame(view, out);
        }
    } // namespace

    ReadResult read_glk1_stdin() {
        ReadResult result;
        std::string document;
        if (read_all(STDIN_FILENO, &document) != 0) {
            result.error = -1;
            return result;
        }
        result.error = decode(document, &result.document);
        return result;
    }

    ReadResult read_glk1_frame_stdin() {
        ReadResult result;
        unsigned char header[4];
        if (read_exact(STDIN_FILENO, header, sizeof(header)) != 0) {
            result.error = -1;
            return result;
        }
        const size_t length = (static_cast<size_t>(header[0]) << 24) |
                              (static_cast<size_t>(header[1]) << 16) |
                              (static_cast<size_t>(header[2]) << 8) |
                              static_cast<size_t>(header[3]);
        if (length == 0 || length > kMaxDocument) {
            errno = EFBIG;
            result.error = -1;
            return result;
        }
        std::string document(length, '\0');
        if (read_exact(STDIN_FILENO, document.data(), length) != 0) {
            result.error = -1;
            return result;
        }
        result.error = decode(document, &result.document);
        return result;
    }

    ReadResult read_glk1_file(const char *path) {
        ReadResult result;
        if (!path) {
            errno = EINVAL;
            result.error = -1;
            return result;
        }
        ghostlock::support::UniqueFd fd(open(path, O_RDONLY | O_CLOEXEC));
        if (!fd.valid()) {
            result.error = -1;
            return result;
        }
        std::string document;
        if (read_all(fd.get(), &document) != 0) {
            result.error = -1;
            return result;
        }
        result.error = decode(document, &result.document);
        return result;
    }
} // namespace ghostlock::profile_entry
