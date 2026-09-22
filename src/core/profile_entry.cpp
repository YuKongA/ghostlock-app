/* GLK1 entry points: the App path (stdin) and the prebuilt-profile path
 * (file). Both decode the typed transport layout directly; no JSON or legacy
 * rules reach this unit. */
#include "profile_entry.h"

#include "profile_binary.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <string_view>

namespace ghostlock::profile_entry {
    using namespace ghostlock::profile;

    namespace {
        constexpr size_t kMaxDocument = 1U << 20; /* 1 MiB */

        int read_all(int fd, std::string *out) {
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

        int decode(const std::string &document, struct kernel_offsets *out,
                   char *release_buf, size_t release_buf_cap) {
            return ghostlock::binary_profile::parse(
                std::string_view(document.data(), document.size()), out,
                release_buf, release_buf_cap);
        }
    } // namespace

    int read_glk1_stdin(struct kernel_offsets *out, char *release_buf,
                        size_t release_buf_cap) {
        if (!out || !release_buf || release_buf_cap == 0) {
            errno = EINVAL;
            return -1;
        }
        std::string document;
        if (read_all(STDIN_FILENO, &document) != 0) return -1;
        return decode(document, out, release_buf, release_buf_cap);
    }

    int read_glk1_file(const char *path, struct kernel_offsets *out,
                       char *release_buf, size_t release_buf_cap) {
        if (!path || !out || !release_buf || release_buf_cap == 0) {
            errno = EINVAL;
            return -1;
        }
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return -1;
        std::string document;
        const int rc = read_all(fd, &document);
        close(fd);
        if (rc != 0) return -1;
        return decode(document, out, release_buf, release_buf_cap);
    }
} // namespace ghostlock::profile_entry
