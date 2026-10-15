/** @file backend/cve_2026_43284/landing/encap_peer.cpp
 *  The ESP-in-UDP receiver. See the header for why it exists.
 */
#include "backend/cve_2026_43284/landing/encap_peer.hpp"

#include <cerrno>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef UDP_ENCAP
#define UDP_ENCAP 100
#endif
#ifndef UDP_ENCAP_ESPINUDP
#define UDP_ENCAP_ESPINUDP 2
#endif

namespace ghostlock::backend::cve_2026_43284::landing {
    EncapPeer::~EncapPeer() noexcept { close(); }

    const char *EncapPeer::status_name(Status status) noexcept {
        switch (status) {
            case Status::Never: return "never";
            case Status::Bound: return "bound";
            case Status::AlreadyHeld: return "already-held";
            case Status::Failed: return "failed";
        }
        return "unknown";
    }

    bool EncapPeer::open(std::uint16_t port) noexcept {
        close();
        if (port == 0U) {
            status_ = Status::Failed;
            error_ = EINVAL;
            return false;
        }
        const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            status_ = Status::Failed;
            error_ = errno;
            return false;
        }
        /* 先声明这是一条 ESP-in-UDP 的接收端：内核据此把落在这个套接字上的报文交给 xfrm。 */
        const int encap = UDP_ENCAP_ESPINUDP;
        if (::setsockopt(fd, IPPROTO_UDP, UDP_ENCAP, &encap, sizeof(encap)) != 0) {
            const int saved = errno;
            static_cast<void>(::close(fd));
            /* 这个平台没有 ESP-in-UDP：不是设备的问题（主机上就是如此），按「用不着」处理，
             *  让调用方照常往下走；别的错误才是要报的。 */
            if (saved == ENOPROTOOPT || saved == ENOMSG || saved == EINVAL) {
                status_ = Status::AlreadyHeld;
                error_ = 0;
                return true;
            }
            status_ = Status::Failed;
            error_ = saved;
            return false;
        }
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(port);
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(fd, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
            /* 已经有人持有：生产环境里就是 App 的会话，这不是错误。 */
            const int saved = errno;
            static_cast<void>(::close(fd));
            error_ = saved;
            status_ = (saved == EADDRINUSE) ? Status::AlreadyHeld : Status::Failed;
            return status_ == Status::AlreadyHeld;
        }
        fd_ = fd;
        status_ = Status::Bound;
        error_ = 0;
        return true;
    }

    void EncapPeer::close() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }
}  // namespace ghostlock::backend::cve_2026_43284::landing
