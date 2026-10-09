/** @file terminal/payload_store.cpp
 *  The filesystem end of the payload path: one writer, atomic, 0600.
 */
#include "terminal/payload_store.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

namespace ghostlock::terminal {
    namespace {
        /* Bounded paths: the terminal never builds a path from document text. */
        constexpr std::size_t kPathBytes = 192U;
        constexpr std::size_t kNameBytes = 96U;

        void copy_text(char *out, std::size_t capacity, const char *text) noexcept {
            std::size_t i = 0U;
            while (text != nullptr && text[i] != '\0' && i + 1U < capacity) {
                out[i] = text[i];
                ++i;
            }
            out[i] = '\0';
        }

        bool join(char *out, std::size_t capacity, const char *directory,
                  const char *name) noexcept {
            const std::size_t directory_length = std::strlen(directory);
            const std::size_t name_length = std::strlen(name);
            if (directory_length + 1U + name_length + 1U > capacity) {
                return false;
            }
            copy_text(out, capacity, directory);
            std::size_t at = directory_length;
            if (at > 0U && out[at - 1U] != '/') {
                out[at] = '/';
                ++at;
            }
            copy_text(out + at, capacity - at, name);
            return true;
        }

        /* Write everything or report failure: a short write is not success. */
        bool write_all(int fd, const std::uint8_t *bytes, std::size_t size) noexcept {
            std::size_t written = 0U;
            while (written < size) {
                const ssize_t step = ::write(fd, bytes + written, size - written);
                if (step <= 0) {
                    if (step < 0 && errno == EINTR) {
                        continue;
                    }
                    return false;
                }
                written += static_cast<std::size_t>(step);
            }
            return true;
        }
    }  // namespace

    StoreResult store_payload(const char *directory, const char *name,
                              const std::uint8_t *bytes, std::size_t size) noexcept {
        StoreResult result{};
        if (directory == nullptr || name == nullptr || bytes == nullptr || size == 0U) {
            return result;
        }
        if (std::strlen(name) > kNameBytes) {
            return result;
        }
        char final_path[kPathBytes];
        if (!join(final_path, sizeof(final_path), directory, name)) {
            return result;
        }
        char temp_name[kNameBytes + 8U];
        copy_text(temp_name, sizeof(temp_name), name);
        copy_text(temp_name + std::strlen(temp_name),
                  sizeof(temp_name) - std::strlen(temp_name), ".pending");
        char temp_path[kPathBytes];
        if (!join(temp_path, sizeof(temp_path), directory, temp_name)) {
            return result;
        }
        const int fd = ::open(temp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) {
            return result;
        }
        bool ok = write_all(fd, bytes, size);
        if (ok && ::fsync(fd) != 0) {
            ok = false;
        }
        if (::close(fd) != 0) {
            ok = false;
        }
        if (ok && ::rename(temp_path, final_path) != 0) {
            ok = false;
        }
        if (!ok) {
            /* Leave nothing half-written behind. */
            (void)::unlink(temp_path);
            return result;
        }
        copy_text(result.path, sizeof(result.path), final_path);
        result.ok = true;
        result.reason = "ok";
        return result;
    }

    bool remove_payload(const char *path) noexcept {
        if (path == nullptr || path[0] == '\0') {
            return false;
        }
        return ::unlink(path) == 0;
    }
}  // namespace ghostlock::terminal
